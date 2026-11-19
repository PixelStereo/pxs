#include "pxs/Client.h"

#include <QHostInfo>
#include <cmath>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QUrl>

#ifdef PXS_HAS_WEBSOCKETS
#include <QWebSocket>
#endif

namespace pxs::oscquery {

namespace {
constexpr int kMaxListened = 256;
constexpr int kMaxReply = 8 << 20;
} // namespace

Client::Client(QObject *parent) : QObject(parent)
{
    m_poll.setInterval(200);
    connect(&m_poll, &QTimer::timeout, this, &Client::poll);
}

Client::~Client() { close(); }

void Client::close()
{
    ++m_generation;
    m_poll.stop();
#ifdef PXS_HAS_WEBSOCKETS
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
#endif
    m_udp.close();
    m_listening.clear();
    m_tree.clear();
    m_pushes = false;
    m_state = State::Idle;
}

void Client::fail(const QString &reason)
{
    if (m_state == State::Failed) return;
    const QString host = m_host;
    close();
    m_host = host;
    m_state = State::Failed;
    emit failed(reason);
}

void Client::get(const QString &target, std::function<void(const QJsonDocument &)> done, std::function<void()> missing)
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_httpPort);
    url.setPath(target.section(QLatin1Char('?'), 0, 0));
    if (target.contains(QLatin1Char('?'))) url.setQuery(target.section(QLatin1Char('?'), 1));
    QNetworkRequest request(url);
    request.setTransferTimeout(5000);
    QNetworkReply *reply = m_net.get(request);
    reply->setReadBufferSize(kMaxReply);
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [=]() {
        reply->deleteLater();
        if (generation != m_generation) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && status != 204) {
            if (missing && status) missing();
            else fail(reply->errorString());
            return;
        }
        if (status == 204) {
            if (missing) missing();
            return;
        }
        const QByteArray body = reply->read(kMaxReply);
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError) {
            fail(QStringLiteral("Not OSCQuery: %1").arg(error.errorString()));
            return;
        }
        done(doc);
    });
}

void Client::open(const QString &host, quint16 httpPort)
{
    close();
    m_host = host;
    m_httpPort = httpPort;
    m_state = State::Connecting;
    m_name.clear();
    m_oscPort = 0;
    get(QStringLiteral("/?HOST_INFO"), [this](const QJsonDocument &doc) {
        const QJsonObject info = doc.object();
        m_name = info.value("NAME").toString();
        m_oscPort = quint16(info.value("OSC_PORT").toInt());
        const bool listen = info.value("EXTENSIONS").toObject().value("LISTEN").toBool();
        const int wsPort = info.contains("WS_PORT") ? info.value("WS_PORT").toInt() : m_httpPort;
        QHostInfo::lookupHost(m_host, this, [this, listen, wsPort, generation = m_generation](const QHostInfo &found) {
            if (generation != m_generation) return;
            if (found.addresses().isEmpty()) return fail(QStringLiteral("Unknown host %1").arg(m_host));
            m_address = found.addresses().first();
            for (const QHostAddress &a : found.addresses())
                if (a.protocol() == QAbstractSocket::IPv4Protocol) { m_address = a; break; }
            if (listen) openSocket(quint16(wsPort));
            get(QStringLiteral("/"), [this](const QJsonDocument &tree) {
                if (!m_tree.load(tree.object())) return fail(QStringLiteral("Not an OSCQuery tree"));
                m_state = State::Ready;
                if (!m_pushes) m_poll.start();
                emit ready();
            });
        });
    });
}

void Client::openSocket(quint16 port)
{
#ifdef PXS_HAS_WEBSOCKETS
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_pushes = true;
        m_poll.stop();
        for (const QString &path : std::as_const(m_listening))
            m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"COMMAND", "LISTEN"}, {"DATA", path}}).toJson(QJsonDocument::Compact)));
    });
    connect(m_socket, &QWebSocket::binaryMessageReceived, this, &Client::onPacket);
    connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        const QJsonObject o = QJsonDocument::fromJson(text.toUtf8()).object();
        const QString command = o.value("COMMAND").toString();
        if (command == QLatin1String("PATH_ADDED") || command == QLatin1String("PATH_REMOVED") || command == QLatin1String("PATH_RENAMED")
            || command == QLatin1String("PATH_CHANGED"))
            refresh();
    });
    connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        m_pushes = false;
        if (m_state == State::Ready) m_poll.start();
    });
    m_socket->open(QUrl(QStringLiteral("ws://%1:%2/").arg(m_address.toString()).arg(port)));
#else
    Q_UNUSED(port);
#endif
}

void Client::refresh()
{
    if (m_state != State::Ready) return;
    get(QStringLiteral("/"), [this](const QJsonDocument &doc) {
        if (!m_tree.load(doc.object())) return;
        emit treeChanged();
    });
}

void Client::listen(const QString &path)
{
    if (m_listening.size() >= kMaxListened || m_listening.contains(path)) return;
    m_listening.insert(path);
#ifdef PXS_HAS_WEBSOCKETS
    if (m_socket && m_pushes)
        m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"COMMAND", "LISTEN"}, {"DATA", path}}).toJson(QJsonDocument::Compact)));
#endif
    // Its value now (a listener only hears the changes)
    get(path + QStringLiteral("?VALUE"), [this, path](const QJsonDocument &doc) {
        const QJsonArray value = doc.object().value("VALUE").toArray();
        if (!value.isEmpty() && m_tree.setValue(path, value)) emit valueChanged(path, value);
    }, [] {});
}

void Client::ignore(const QString &path)
{
    if (!m_listening.remove(path)) return;
#ifdef PXS_HAS_WEBSOCKETS
    if (m_socket && m_pushes)
        m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"COMMAND", "IGNORE"}, {"DATA", path}}).toJson(QJsonDocument::Compact)));
#endif
}

void Client::poll()
{
    for (const QString &path : std::as_const(m_listening)) {
        get(path + QStringLiteral("?VALUE"), [this, path](const QJsonDocument &doc) {
            const QJsonArray value = doc.object().value("VALUE").toArray();
            const RemoteNode *n = m_tree.find(path);
            if (value.isEmpty() || !n || n->value == value) return;
            m_tree.setValue(path, value);
            emit valueChanged(path, value);
        }, [] {});
    }
}

QJsonArray Client::toJson(const osc::Message &m)
{
    QJsonArray out;
    int k = 0;
    for (const QChar tag : m.types) {
        if (tag == QLatin1Char('T')) out.append(true);
        else if (tag == QLatin1Char('F')) out.append(false);
        else if (tag == QLatin1Char('N') || tag == QLatin1Char('I')) out.append(QJsonValue::Null);
        else if (k < m.args.size()) {
            const QVariant &v = m.args[k++];
            if (tag == QLatin1Char('s')) out.append(v.toString());
            else if (tag == QLatin1Char('i') || tag == QLatin1Char('h')) out.append(v.toLongLong());
            else out.append(v.toDouble());
        }
    }
    return out;
}

osc::Message Client::toMessage(const RemoteNode &node, const QJsonArray &values)
{
    osc::Message m;
    m.address = node.path;
    m.types = node.type;
    if (node.type == QLatin1String("N")) return m;
    for (int k = 0; k < node.type.size(); ++k) {
        const QChar tag = node.type[k];
        const QJsonValue v = k < values.size() ? values[k] : QJsonValue();
        if (tag == QLatin1Char('T') || tag == QLatin1Char('F')) {
            const bool on = v.isBool() ? v.toBool() : v.toDouble() != 0;
            m.types[k] = on ? QLatin1Char('T') : QLatin1Char('F');
            m.args << on;
        } else if (tag == QLatin1Char('s')) {
            m.args << v.toString();
        } else if (tag == QLatin1Char('i') || tag == QLatin1Char('h')) {
            m.args << int(std::lround(v.toDouble()));
        } else {
            m.args << v.toDouble();
        }
    }
    return m;
}

bool Client::write(const QString &path, const QJsonArray &values)
{
    const RemoteNode *n = m_tree.find(path);
    if (m_state != State::Ready || !n || n->type.isEmpty() || !(n->access & 2) || !m_oscPort) return false;
    if (n->type != QLatin1String("N") && values.size() != n->type.size()) return false;
    if (m_udp.state() == QAbstractSocket::UnconnectedState) m_udp.bind(QHostAddress::AnyIPv4, 0);
    const bool sent = m_udp.writeDatagram(osc::encode(toMessage(*n, values)), m_address, m_oscPort) >= 0;
    if (sent && (n->access & 1)) { // what it holds now, as far as this client knows (a device that pushes will confirm)
        m_tree.setValue(path, values);
    }
    return sent;
}

void Client::onPacket(const QByteArray &packet)
{
    std::vector<osc::Message> messages;
    if (!osc::decode(packet, messages)) return;
    for (const osc::Message &m : messages) {
        const QJsonArray value = toJson(m);
        if (m_tree.setValue(m.address, value)) emit valueChanged(m.address, value);
    }
}

} // namespace pxs::oscquery
