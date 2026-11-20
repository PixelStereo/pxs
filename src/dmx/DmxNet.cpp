#include "pxs/DmxNet.h"

#include <QNetworkDatagram>
#include <QTimer>
#include <QVariant>
#include <QUuid>

namespace pxs::dmx {

// --- Transmitter --------------------------------------------------------------------------------------------------------

Transmitter::Transmitter(QObject *parent) : QObject(parent), m_cid(QUuid::createUuid().toRfc4122()) {}

bool Transmitter::start(const QNetworkInterface &network, QString *error)
{
    if (!network.isValid() || !network.flags().testFlag(QNetworkInterface::IsUp)) {
        if (error) *error = QStringLiteral("The network interface is unavailable");
        return false;
    }
    m_network = network;
    QHostAddress local;
    for (const auto &entry : network.addressEntries())
        if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
            local = entry.ip();
            break;
        }
    if (m_socket.state() == QAbstractSocket::BoundState) return true;
    if (local.isNull() || !m_socket.bind(local, 0)) {
        if (error) *error = QStringLiteral("Could not bind output to the selected interface");
        return false;
    }
    m_socket.setMulticastInterface(network);
    m_socket.setSocketOption(QAbstractSocket::MulticastTtlOption, 1);
    m_socket.setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);
    return true;
}

void Transmitter::stop() { m_socket.close(); }

bool Transmitter::send(Frame frame, const QHostAddress &destination, QString *error)
{
    if (!active()) {
        if (error) *error = QStringLiteral("The output is not started");
        return false;
    }
    frame.cid = m_cid;
    // Art-Net's sequence runs 1..255 (0 means "not used"); sACN's wraps
    m_sequence = frame.sacn ? quint8(m_sequence + 1) : (m_sequence == 255 ? 1 : quint8(m_sequence + 1));
    frame.sequence = m_sequence;
    const QByteArray packet = encode(frame);
    if (packet.isEmpty()) {
        if (error) *error = QStringLiteral("Universe or priority out of range");
        return false;
    }
    const QHostAddress target = frame.sacn ? QHostAddress(sacnGroup(frame.universe)) : destination;
    const quint16 port = frame.sacn ? m_sacnPort : m_artPort;
    if (m_socket.writeDatagram(packet, target, port) < 0) {
        if (error) *error = m_socket.errorString();
        return false;
    }
    m_lastTarget = target;
    m_lastPort = port;
    return true;
}

bool Transmitter::end(Frame frame, QString *error)
{
    frame.sacn = true;
    frame.terminated = true;
    bool ok = true;
    for (int i = 0; i < 3; ++i) ok = send(frame, {}, error) && ok;
    return ok;
}

// --- Monitor ------------------------------------------------------------------------------------------------------------

Monitor::Monitor(QObject *parent) : QObject(parent)
{
    m_clock.start();
    connect(&m_art, &QUdpSocket::readyRead, this, [this] { drain(&m_art, false); });
    connect(&m_artUnicast, &QUdpSocket::readyRead, this, [this] { drain(&m_artUnicast, false); });
    connect(&m_sacn, &QUdpSocket::readyRead, this, [this] { drain(&m_sacn, true); });
}

bool Monitor::start(const QNetworkInterface &network, QString *error)
{
    if (!network.isValid() || !network.flags().testFlag(QNetworkInterface::IsUp)) {
        if (error) *error = QStringLiteral("The network interface is unavailable");
        return false;
    }
    m_network = network;
    const auto flags = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    const bool loopback = network.flags().testFlag(QNetworkInterface::IsLoopBack);
    // A specific loopback listener avoids losing local unicast to a more specific listener owned by another lighting application
    const QHostAddress artAddress = loopback ? QHostAddress(QHostAddress::LocalHost) : QHostAddress(QHostAddress::AnyIPv4);
    if (!m_art.bind(artAddress, m_artPort, flags) || !m_sacn.bind(QHostAddress::AnyIPv4, m_sacnPort, flags)) {
        const QString reason = m_art.state() != QAbstractSocket::BoundState ? m_art.errorString() : m_sacn.errorString();
        stop();
        if (error) *error = reason;
        return false;
    }
    if (!loopback)
        for (const auto &entry : network.addressEntries())
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                if (!m_artUnicast.bind(entry.ip(), m_artPort, flags))
                    emit problem(QStringLiteral("Local unicast listener unavailable: ") + m_artUnicast.errorString());
                break;
            }
    m_art.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1024 * 1024);
    m_sacn.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 1024 * 1024);
    m_stats = {};
    m_streams.clear();
    return true;
}

void Monitor::stop()
{
    m_art.close();
    m_artUnicast.close();
    m_sacn.close();
    m_groups.clear();
}

bool Monitor::listenTo(int universe, QString *error)
{
    if (m_sacn.state() != QAbstractSocket::BoundState) return true;
    for (int old : std::as_const(m_groups)) m_sacn.leaveMulticastGroup(QHostAddress(sacnGroup(old)), m_network);
    m_groups.clear();
    if (universe < 1) return true;
    if (m_sacn.joinMulticastGroup(QHostAddress(sacnGroup(universe)), m_network)) {
        m_groups.insert(universe);
        return true;
    }
    if (error) *error = QStringLiteral("Could not join multicast group: ") + m_sacn.errorString();
    return false;
}

void Monitor::prune()
{
    const qint64 now = elapsed();
    for (auto i = m_streams.begin(); i != m_streams.end();)
        i = now - i->seen > forgetMs ? m_streams.erase(i) : std::next(i);
}

void Monitor::drain(QUdpSocket *socket, bool sacn)
{
    int budget = 256;
    while (socket->hasPendingDatagrams() && budget--) {
        const QNetworkDatagram d = socket->receiveDatagram();
        if (!d.isValid()) break;
        ingest(d.data(), d.senderAddress(), quint16(d.senderPort()), sacn, d.interfaceIndex());
    }
    // Drain in bounded batches, yielding to the interface and the timers between batches
    if (socket->hasPendingDatagrams() && !m_draining.contains(socket)) {
        m_draining.insert(socket);
        QTimer::singleShot(0, this, [this, socket, sacn] {
            m_draining.remove(socket);
            drain(socket, sacn);
        });
    }
}

bool Monitor::ingest(const QByteArray &data, const QHostAddress &from, quint16 port, bool isSacn, uint interfaceIndex)
{
    ++m_stats.datagrams;
    if (interfaceIndex != 0 && interfaceIndex != uint(m_network.index())) {
        bool local = false;
        for (const auto &entry : m_network.addressEntries())
            if (entry.ip() == from) local = true;
        // Same-machine traffic may be delivered through the loopback interface
        if (from.isLoopback()) local = true;
        if (!local) {
            ++m_stats.otherInterface;
            return false;
        }
    }
    Frame f;
    if (!decode(data, isSacn, f)) {
        ++m_stats.invalid;
        emit event(QStringLiteral("RX unsupported · %1:%2 · %3 bytes").arg(from.toString()).arg(port).arg(data.size()));
        return false;
    }
    const QString key = QStringLiteral("%1/%2/%3/%4/%5")
                            .arg(isSacn ? "sACN" : "Art-Net")
                            .arg(f.universe)
                            .arg(isSacn ? QString() : from.toString(), QString::fromLatin1(f.cid.toHex()))
                            .arg(isSacn ? 0 : port);
    if (f.terminated) {
        emit event(QStringLiteral("RX · sACN · U%1 · %2 · CID %3 · stream terminated")
                       .arg(f.universe)
                       .arg(from.toString(), QString::fromLatin1(f.cid.toHex())));
        m_streams.remove(key);
        return false;
    }
    if (!m_streams.contains(key) && m_streams.size() >= maxStreams) return false;
    Stream &s = m_streams[key];
    if (s.packets && elapsed() - s.seen < staleMs) {
        if (isSacn) {
            const int diff = int(f.sequence) - int(s.frame.sequence);
            if (diff <= 0 && diff > -20) {
                ++m_stats.reordered;
                return false;
            }
        } else if (f.sequence && s.frame.sequence) {
            const int diff = (int(f.sequence) - int(s.frame.sequence) + 255) % 255;
            if (diff == 0 || diff > 127) {
                ++m_stats.reordered;
                return false;
            }
        }
    }
    QStringList changes;
    int changedCount = 0;
    ++m_arrival;
    for (int ch = 0; ch < channelCount; ++ch) {
        if (!s.packets || s.frame.values[ch] != f.values[ch]) {
            s.changed[size_t(ch)] = m_arrival;
            ++changedCount;
            if (changes.size() < 8) changes << QStringLiteral("%1=%2").arg(ch + 1).arg(quint8(f.values[ch]));
        }
    }
    s.frame = f;
    s.ip = from.toString() + (isSacn ? QString() : QStringLiteral(":%1").arg(port));
    if (m_transmitter && !s.own) {
        s.own = isSacn ? f.cid == m_transmitter->cid()
                       : port == m_transmitter->localPort() && m_transmitter->localPort() != 0 && from == m_transmitter->localAddress();
    }
    if (s.own) s.frame.name = m_ownLabel;
    emit event(QStringLiteral("RX · %1 · U%2 · %3%4 · seq %5%6 · %7")
                   .arg(isSacn ? "sACN" : "Art-Net")
                   .arg(f.universe)
                   .arg(s.ip, s.frame.name.isEmpty() ? QString() : QStringLiteral(" · ") + s.frame.name)
                   .arg(f.sequence)
                   .arg(isSacn ? QStringLiteral(" · priority %1 · CID %2").arg(f.priority).arg(QString::fromLatin1(f.cid.toHex())) : QString())
                   .arg(changes.isEmpty() ? QStringLiteral("unchanged")
                                          : changes.join(QStringLiteral(", ")) +
                                                (changedCount > 8 ? QStringLiteral(" … (%1 changed)").arg(changedCount) : QString())));
    s.seen = elapsed();
    ++s.packets;
    ++m_stats.received;
    return true;
}

QByteArray Monitor::merged(bool sacn, int universe, MergeMode mode, bool externalOnly, bool *liveOut) const
{
    if (liveOut) *liveOut = false;
    QList<const Stream *> candidates;
    int highest = -1;
    for (const Stream &s : m_streams) {
        if (s.frame.sacn != sacn || s.frame.universe != universe || !live(s) || (externalOnly && s.own)) continue;
        if (s.frame.sacn && s.frame.priority < highest) continue;
        if (s.frame.sacn && s.frame.priority > highest) {
            candidates.clear();
            highest = s.frame.priority;
        }
        candidates << &s;
    }
    if (candidates.isEmpty()) return {};
    if (liveOut) *liveOut = true;
    QByteArray result(channelCount, 0);
    std::array<quint64, channelCount> latest{};
    for (const Stream *s : candidates)
        for (int ch = 0; ch < channelCount; ++ch)
            if (mode == MergeMode::HTP) {
                if (quint8(s->frame.values[ch]) > quint8(result[ch])) result[ch] = s->frame.values[ch];
            } else if (s->changed[size_t(ch)] >= latest[size_t(ch)]) {
                result[ch] = s->frame.values[ch];
                latest[size_t(ch)] = s->changed[size_t(ch)];
            }
    return result;
}

} // namespace pxs::dmx
