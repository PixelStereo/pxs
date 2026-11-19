#include <pxs/OscService.h>

namespace pxs {

OscService::OscService(Tree *tree, const QString &name, QObject *parent)
    : QObject(parent), m_tree(tree), m_name(name), m_http(tree, this), m_mdns(this)
{
    m_http.setName(name);
    connect(&m_udp, &QUdpSocket::readyRead, this, &OscService::onReadyRead);
    connect(&m_http, &QueryServer::logMessage, this, &OscService::logMessage);
    connect(&m_mdns, &MdnsResponder::logMessage, this, &OscService::logMessage);
}

OscService::~OscService() { stop(); }

bool OscService::start(quint16 oscPort, quint16 httpPort, bool announce)
{
    stop();
    if (!m_udp.bind(QHostAddress::Any, oscPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit logMessage(QStringLiteral("OSC: cannot listen on UDP port %1: %2").arg(oscPort).arg(m_udp.errorString()));
        return false;
    }
    m_http.setOscPort(m_udp.localPort());
    if (!m_http.start(httpPort)) {
        m_udp.close();
        return false;
    }
    m_tree->setOutput([this](const osc::Message &m) { send(m); });
    if (announce) {
        const QString type = QStringLiteral("_oscjson._tcp");
        m_mdns.start(m_name, {{type, m_http.port()}, {QStringLiteral("_osc._udp"), m_udp.localPort()}});
    }
    emit logMessage(QStringLiteral("OSC on UDP %1, OSCQuery on HTTP %2").arg(m_udp.localPort()).arg(m_http.port()));
    return true;
}

void OscService::stop()
{
    m_mdns.stop();
    m_http.stop();
    m_udp.close();
    m_tree->setOutput(nullptr);
}

void OscService::addTarget(const QHostAddress &address, quint16 port)
{
    for (const auto &t : m_targets)
        if (t.first == address && t.second == port) return;
    m_targets.append({address, port});
}

void OscService::clearTargets() { m_targets.clear(); }

void OscService::onReadyRead()
{
    while (m_udp.hasPendingDatagrams()) {
        QHostAddress from;
        quint16 port = 0;
        QByteArray packet(int(m_udp.pendingDatagramSize()), Qt::Uninitialized);
        m_udp.readDatagram(packet.data(), packet.size(), &from, &port);
        std::vector<osc::Message> messages;
        if (!osc::decode(packet, messages)) continue;
        if (m_learn && !m_learned.contains({from, port})) m_learned.append({from, port}); // hears the feedback
        for (const osc::Message &m : messages) emit messageReceived(m, m_tree->apply(m));
    }
}

void OscService::send(const osc::Message &message)
{
    if (m_udp.state() != QAbstractSocket::BoundState) return;
    const QByteArray packet = osc::encode(message);
    for (const auto &t : m_targets) m_udp.writeDatagram(packet, t.first, t.second);
    const Node *node = m_tree->find(message.address);
    if (node && node->kind() == Kind::Signal) return; // a stream goes to the targets only
    for (const auto &t : m_learned)
        if (!m_targets.contains(t)) m_udp.writeDatagram(packet, t.first, t.second);
}

} // namespace pxs
