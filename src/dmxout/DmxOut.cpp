#include "pxs/DmxOut.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QUdpSocket>
#include <algorithm>
#include <chrono>
#include <limits>

namespace pxs::dmx {

namespace {
bool ipv4(const QString &text) { return QHostAddress(text).protocol() == QAbstractSocket::IPv4Protocol; }
const char *protocolKey(Protocol p) { return p == Protocol::ArtNet ? "artnet" : "sacn"; }
} // namespace

// --- Output -------------------------------------------------------------------------------------------------------------------

Output Output::defaults(Protocol protocol)
{
    Output o;
    o.protocol = protocol;
    o.firstUniverse = protocol == Protocol::ArtNet ? 0 : 1; // Art-Net counts from 0, sACN from 1
    return o;
}

QString Output::validate() const
{
    if (rate < 1 || rate > maxRate()) return QStringLiteral("The rate is 1 to %1 frames per second").arg(maxRate());
    if (priority < 0 || priority > 200) return QStringLiteral("The sACN priority is 0 to 200");
    if (port < 0 || port > 65535) return QStringLiteral("The port is 0 to 65535");
    if (!interfaceAddress.isEmpty() && !ipv4(interfaceAddress)) return QStringLiteral("The network card is an IPv4 address");
    if (delivery == Delivery::Unicast) {
        if (destinations.isEmpty()) return QStringLiteral("Unicast needs at least one IPv4 address");
        for (const QString &d : destinations)
            if (!ipv4(d)) return QStringLiteral("Not an IPv4 address: %1").arg(d);
    }
    return {};
}

QString Output::validateMapping() const
{
    const int low = protocol == Protocol::ArtNet ? 0 : 1;
    if (firstUniverse < low || firstUniverse > highestUniverse())
        return QStringLiteral("The first network universe is %1 to %2").arg(low).arg(highestUniverse());
    if (firstSourceUniverse < 0 || universeCount < 0) return QStringLiteral("The universe range is not valid");
    if (universeCount && firstUniverse + universeCount - 1 > highestUniverse())
        return QStringLiteral("The universes go past the protocol's last one (%1)").arg(highestUniverse());
    return {};
}

bool Output::operator==(const Output &o) const
{
    return id == o.id && name == o.name && enabled == o.enabled && protocol == o.protocol && delivery == o.delivery &&
           destinations == o.destinations && interfaceAddress == o.interfaceAddress && port == o.port && rate == o.rate &&
           priority == o.priority && sourceName == o.sourceName && transmission == o.transmission &&
           firstSourceUniverse == o.firstSourceUniverse && firstUniverse == o.firstUniverse && universeCount == o.universeCount;
}

QJsonObject Output::toJson() const
{
    return QJsonObject{{"id", QString::number(id)},
                       {"name", name},
                       {"enabled", enabled},
                       {"protocol", protocolKey(protocol)},
                       {"delivery", delivery == Delivery::Unicast ? "unicast" : "group"},
                       {"destinations", QJsonArray::fromStringList(destinations)},
                       {"interface", interfaceAddress},
                       {"port", port},
                       {"rate", rate},
                       {"priority", priority},
                       {"sourceName", sourceName},
                       {"transmission", transmission == Transmission::Partial ? "partial" : "full"},
                       {"firstSourceUniverse", firstSourceUniverse},
                       {"firstUniverse", firstUniverse},
                       {"universeCount", universeCount}};
}

Output Output::fromJson(const QJsonObject &j)
{
    Output o = defaults(j.value("protocol").toString() == QLatin1String("sacn") ? Protocol::Sacn : Protocol::ArtNet);
    o.id = j.value("id").toString().toULongLong();
    o.name = j.value("name").toString(o.name);
    o.enabled = j.value("enabled").toBool(o.enabled);
    o.delivery = j.value("delivery").toString() == QLatin1String("unicast") ? Delivery::Unicast : Delivery::Group;
    for (const QJsonValue &v : j.value("destinations").toArray()) o.destinations << v.toString();
    o.interfaceAddress = j.value("interface").toString();
    o.port = j.value("port").toInt(0);
    o.rate = std::clamp(j.value("rate").toDouble(o.rate), 1.0, o.maxRate());
    o.priority = std::clamp(j.value("priority").toInt(o.priority), 0, 200);
    o.sourceName = j.value("sourceName").toString(o.sourceName);
    o.transmission = j.value("transmission").toString() == QLatin1String("partial") ? Transmission::Partial : Transmission::Full;
    o.firstSourceUniverse = j.value("firstSourceUniverse").toInt(o.firstSourceUniverse);
    o.firstUniverse = j.value("firstUniverse").toInt(o.firstUniverse);
    o.universeCount = j.value("universeCount").toInt(0);
    return o;
}

Universes route(const std::map<int, QByteArray> &sources, const std::vector<Output> &outputs)
{
    Universes out;
    for (const Output &o : outputs) {
        if (!o.enabled) continue;
        for (const auto &[universe, levels] : sources) {
            const int offset = universe - o.firstSourceUniverse;
            if (offset < 0 || (o.universeCount && offset >= o.universeCount)) continue;
            const int wire = o.firstUniverse + offset;
            if (wire > o.highestUniverse()) continue;
            out[{o.id, wire}] = levels;
        }
    }
    return out;
}

// --- Sender -------------------------------------------------------------------------------------------------------------------

struct Sender::Link {
    Output settings;
    std::unique_ptr<QUdpSocket> socket;
    std::vector<QHostAddress> destinations; // unicast, or Art-Net's broadcast (none: sACN multicast, by universe)
    std::map<int, quint8> sequence;
    std::chrono::steady_clock::time_point next{};
    std::chrono::steady_clock::time_point nextPriority{}; // sACN: the per-channel priorities sent again
    std::shared_ptr<const Universes> ownedSent;           // those last sent
    QString error;
    quint64 packets = 0;
};

Sender::Sender() : m_cid(QUuid::createUuid()) {}
Sender::~Sender() { stop(); }

QUuid Sender::cid() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_cid;
}

void Sender::setCid(const QUuid &cid)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!cid.isNull()) m_cid = cid;
}

void Sender::ensureThread()
{
    if (m_thread.joinable()) return;
    m_quit = false;
    m_thread = std::thread([this] { run(); });
}

void Sender::follow(const std::vector<Output> &outputs)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (outputs == m_outputs && (m_thread.joinable() || outputs.empty())) return;
    m_outputs = outputs;
    m_changed = true;
    if (!outputs.empty()) ensureThread();
    m_cv.notify_all();
}

void Sender::submit(Universes frame, Universes owned)
{
    auto f = std::make_shared<const Universes>(std::move(frame));
    auto o = std::make_shared<const Universes>(std::move(owned));
    std::lock_guard<std::mutex> lk(m_mutex);
    m_frame = std::move(f);
    m_owned = std::move(o);
}

void Sender::stop()
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_quit = true;
        m_cv.notify_all();
    }
    if (m_thread.joinable()) m_thread.join();
    std::lock_guard<std::mutex> lk(m_mutex);
    m_outputs.clear();
    m_frame.reset();
    m_owned.reset();
    m_states.clear();
}

OutputState Sender::state(quint64 output) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const auto it = m_states.find(output);
    return it != m_states.end() ? it->second : OutputState{};
}

void Sender::run()
{
    using clock = std::chrono::steady_clock;
    std::map<quint64, Link> links; // only this thread's: the sockets live and die here
    const QByteArray cid = m_cid.toRfc4122();

    auto send = [&](Link &l, int universe, const QByteArray &data, bool terminated, quint8 startCode = 0) {
        const Output &o = l.settings;
        const bool sacn = o.protocol == Protocol::Sacn;
        Frame f;
        f.sacn = sacn;
        f.universe = universe;
        f.priority = o.priority;
        f.cid = cid;
        f.name = o.sourceName;
        f.terminated = terminated;
        f.startCode = startCode;
        f.values = data;
        f.length = o.transmission == Transmission::Partial && startCode == 0 && !terminated ? int(data.size()) : channelCount;
        quint8 &seq = l.sequence[universe];
        seq = sacn ? quint8(seq + 1) : quint8(seq % 255 + 1); // Art-Net's 1..255: 0 turns reordering off
        f.sequence = seq;
        const QByteArray packet = encode(f);
        if (packet.isEmpty()) return QStringLiteral("Universe %1 is outside the protocol's range").arg(universe);
        std::vector<QHostAddress> to = l.destinations;
        if (sacn && to.empty()) to.emplace_back(sacnGroup(universe));
        QString error;
        for (const QHostAddress &a : to)
            if (l.socket->writeDatagram(packet, a, quint16(o.effectivePort())) != packet.size()) error = l.socket->errorString();
        return error;
    };
    auto terminate = [&](Link &l) {
        if (!l.socket || l.settings.protocol != Protocol::Sacn) return;
        const QByteArray zero(channelCount, '\0');
        for (const auto &[universe, seq] : l.sequence)
            for (int k = 0; k < 3; ++k) send(l, universe, zero, true);
    };
    auto open = [&](const Output &o) {
        Link l;
        l.settings = o;
        const QString invalid = o.validate();
        if (!invalid.isEmpty()) {
            l.error = invalid;
            return l;
        }
        l.socket = std::make_unique<QUdpSocket>();
        const QHostAddress local = o.interfaceAddress.isEmpty() ? QHostAddress(QHostAddress::AnyIPv4) : QHostAddress(o.interfaceAddress);
        if (!l.socket->bind(local, 0)) {
            l.error = QStringLiteral("Cannot send from %1").arg(o.interfaceAddress.isEmpty() ? QStringLiteral("this machine") : o.interfaceAddress);
            l.socket.reset();
            return l;
        }
        QNetworkInterface card; // the one holding the local address, if one is chosen
        QHostAddress broadcast(QHostAddress::Broadcast);
        if (!o.interfaceAddress.isEmpty())
            for (const QNetworkInterface &i : QNetworkInterface::allInterfaces())
                for (const QNetworkAddressEntry &e : i.addressEntries())
                    if (e.ip() == local) {
                        card = i;
                        if (!e.broadcast().isNull()) broadcast = e.broadcast();
                    }
        if (o.delivery == Delivery::Unicast) {
            for (const QString &ip : o.destinations) l.destinations.emplace_back(ip);
        } else if (o.protocol == Protocol::ArtNet) {
            l.destinations = {broadcast};
        } else {
            l.socket->setMulticastInterface(card);
            l.socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 4);
        }
        return l;
    };

    std::unique_lock<std::mutex> lk(m_mutex);
    while (!m_quit) {
        if (m_changed) {
            m_changed = false;
            const std::vector<Output> outputs = m_outputs;
            lk.unlock();
            for (auto it = links.begin(); it != links.end();) {
                const auto o = std::find_if(outputs.begin(), outputs.end(), [&](const Output &x) { return x.id == it->first; });
                if (o != outputs.end() && o->enabled && *o == it->second.settings) {
                    ++it;
                    continue;
                }
                terminate(it->second);
                it = links.erase(it);
            }
            for (const Output &o : outputs)
                if (o.enabled && !links.count(o.id)) links.emplace(o.id, open(o));
            lk.lock();
            m_states.clear();
            for (const Output &o : outputs) {
                OutputState &s = m_states[o.id];
                const auto l = links.find(o.id);
                if (l == links.end()) continue; // off
                s.level = l->second.error.isEmpty() ? OutputState::Ok : OutputState::Error;
                s.text = l->second.error;
            }
            continue;
        }
        const std::shared_ptr<const Universes> frame = m_frame;
        const std::shared_ptr<const Universes> owned = m_owned;
        lk.unlock();
        const auto now = clock::now();
        auto wake = now + std::chrono::milliseconds(100);
        for (auto &[id, l] : links) {
            if (!l.socket) continue;
            if (now >= l.next) {
                l.next = now + std::chrono::microseconds(qint64(1e6 / l.settings.rate));
                l.error.clear();
                if (frame)
                    for (auto it = frame->lower_bound({id, std::numeric_limits<int>::min()}); it != frame->end() && it->first.first == id; ++it) {
                        const QString error = send(l, it->first.second, it->second, false);
                        if (error.isEmpty()) ++l.packets;
                        else l.error = error;
                    }
            }
            if (l.settings.protocol == Protocol::Sacn && owned && (now >= l.nextPriority || owned != l.ownedSent)) {
                l.nextPriority = now + std::chrono::seconds(1);
                l.ownedSent = owned;
                for (auto it = owned->lower_bound({id, std::numeric_limits<int>::min()}); it != owned->end() && it->first.first == id; ++it) {
                    if (!it->second.contains('\0')) continue; // every channel ours: the universe's priority says it
                    QByteArray priorities(channelCount, '\0');
                    for (int k = 0; k < channelCount && k < it->second.size(); ++k)
                        if (it->second[k]) priorities[k] = char(std::clamp(l.settings.priority, 1, 200));
                    const QString error = send(l, it->first.second, priorities, false, 0xDD);
                    if (error.isEmpty()) ++l.packets;
                    else l.error = error;
                }
            }
            wake = std::min(wake, std::min(l.next, l.settings.protocol == Protocol::Sacn ? l.nextPriority : l.next));
        }
        lk.lock();
        for (auto &[id, l] : links) {
            OutputState &s = m_states[id];
            s.packets = l.packets;
            s.level = l.error.isEmpty() ? OutputState::Ok : OutputState::Error;
            s.text = l.error;
        }
        if (!m_quit && !m_changed) m_cv.wait_until(lk, wake);
    }
    lk.unlock();
    for (auto &[id, l] : links) terminate(l);
}

} // namespace pxs::dmx
