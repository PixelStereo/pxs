#include <pxs/OscService.h>

#include <QJsonDocument>
#include <QThread>

namespace pxs {

OscService::OscService(Tree *tree, const QString &name, QObject *parent)
    : QObject(parent), m_tree(tree), m_name(name), m_http(tree, this), m_mdns(this)
{
    m_http.setName(name);
    m_pathTimer.setSingleShot(true);
    m_pathTimer.setInterval(50);
    connect(&m_pathTimer, &QTimer::timeout, this, &OscService::onPathTimer);
    connect(&m_http, &QueryServer::packetReceived, this, [this](const QByteArray &packet) { apply(packet, nullptr, 0); });
    connect(&m_http, &QueryServer::webSocketsChanged, this, [this](int count) { m_webSockets = count; });
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
    QJsonObject extensions = Tree::defaultExtensions();
    extensions["LISTEN"] = true;       // the WebSocket of the same port (IGNORE comes with it)
    extensions["PATH_CHANGED"] = true;
    m_http.setExtensions(extensions);
    m_http.setHostInfoMore(QJsonObject{{"WS_PORT", int(m_http.port())}});
    m_tree->setOutput([this](const osc::Message &m) { send(m); });
    m_observer = m_tree->addObserver([this](const Node &n, const QVariantList &args) { onChanged(n, args); });
    m_tree->setStructureObserver([this](const QString &path) { onStructure(path); });
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
    if (m_observer) m_tree->removeObserver(m_observer);
    m_observer = 0;
    m_tree->setStructureObserver(nullptr);
    m_pathTimer.stop();
    m_pathChanged.clear();
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
        apply(packet, &from, port);
    }
}

void OscService::apply(const QByteArray &packet, const QHostAddress *from, quint16 port)
{
    std::vector<osc::Message> messages;
    if (!osc::decode(packet, messages)) return;
    if (from && m_learn && !m_learned.contains({*from, port})) m_learned.append({*from, port}); // hears the feedback
    for (const osc::Message &m : messages) emit messageReceived(m, m_tree->apply(m));
}

// What a client that listens to an address is sent: the message a parameter says it holds, or the signal as it was fired
void OscService::onChanged(const Node &node, const QVariantList &args)
{
    if (m_webSockets.load() == 0 || node.isContainer()) return;
    osc::Message m;
    if (node.kind() == Kind::Parameter) {
        m = Tree::message(node);
    } else if (node.kind() == Kind::Signal) {
        m.address = node.path();
        if (node.type != QLatin1String("N")) {
            m.types = node.type;
            m.args = args;
        }
    } else {
        return; // an action is received, it does not say anything
    }
    const auto stream = [this, m] { m_http.stream(m.address, osc::encode(m)); };
    if (QThread::currentThread() == thread()) stream();
    else QMetaObject::invokeMethod(this, stream, Qt::QueuedConnection); // a socket is written by its own thread
}

static QString commonPath(const QString &a, const QString &b)
{
    if (a.isEmpty()) return b;
    const QStringList x = a.split('/', Qt::SkipEmptyParts), y = b.split('/', Qt::SkipEmptyParts);
    QString out;
    for (int k = 0; k < x.size() && k < y.size() && x[k] == y[k]; ++k) out += '/' + x[k];
    return out.isEmpty() ? QStringLiteral("/") : out;
}

void OscService::onStructure(const QString &path)
{
    if (m_webSockets.load() == 0) return;
    const auto note = [this, path] {
        m_pathChanged = commonPath(m_pathChanged, path);
        if (!m_pathTimer.isActive()) m_pathTimer.start(); // a layer made of dozens of nodes is one message
    };
    if (QThread::currentThread() == thread()) note();
    else QMetaObject::invokeMethod(this, note, Qt::QueuedConnection);
}

void OscService::onPathTimer()
{
    if (m_pathChanged.isEmpty()) return;
    m_http.broadcast(QJsonDocument(QJsonObject{{"COMMAND", "PATH_CHANGED"}, {"DATA", m_pathChanged}}).toJson(QJsonDocument::Compact));
    m_pathChanged.clear();
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
