#include "pxs/Server.h"
#include "pxs/ControlsPage.h"
#include "WebSocketFrame.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>

namespace pxs::controls {
namespace {
constexpr int kMaxClients = 16;
constexpr int kMaxHeader = 16384;
constexpr int kMaxFailures = 8;
constexpr qint64 kLockMs = 60'000;

QByteArray hashOf(const QByteArray &text)
{
    return "'sha256-" + QCryptographicHash::hash(text, QCryptographicHash::Sha256).toBase64() + "'";
}

// Same time whatever the digits: a wrong PIN says nothing of how close it was
bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    int diff = a.size() ^ b.size();
    for (int i = 0; i < std::max(a.size(), b.size()); ++i) diff |= (i < a.size() ? a[i] : 0) ^ (i < b.size() ? b[i] : 0);
    return diff == 0;
}

QJsonValue jsonOf(const std::vector<double> &v)
{
    if (v.size() > 1) {
        QJsonArray a;
        for (double x : v) a.append(x);
        return a;
    }
    return v.empty() ? 0.0 : v.front();
}
} // namespace

QByteArray Server::response(int status, const QByteArray &body, const QByteArray &type, const QByteArray &extra, bool frameable)
{
    static const QHash<int, QByteArray> text = {{200, "OK"}, {302, "Found"}, {400, "Bad Request"}, {403, "Forbidden"}, {404, "Not Found"}, {429, "Too Many Requests"}};
    QByteArray r = "HTTP/1.1 " + QByteArray::number(status) + ' ' + text.value(status, "Error") + "\r\n";
    r += "Content-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
    r += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nX-Frame-Options: DENY\r\n";
    if (frameable) r.replace("X-Frame-Options: DENY", "X-Frame-Options: SAMEORIGIN"); // framed by a page of the same site only
    r += extra + "Connection: close\r\n\r\n";
    return r + body;
}

QByteArray Server::policy(const QByteArray &script, const QByteArray &style, const QByteArray &extra, bool frameable)
{
    return "default-src 'none'; script-src " + hashOf(script) + "; style-src " + hashOf(style) +
           "; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors " + QByteArray(frameable ? "'self'" : "'none'") + (extra.isEmpty() ? QByteArray() : "; " + extra);
}

QByteArray Server::page(const QString &title)
{
    return QByteArray("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
                      "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\"><title>") +
           title.toHtmlEscaped().toUtf8() + "</title><style>" + page::style() + "</style></head><body>" + page::kBody + "<script>" + page::script() + "</script></body></html>";
}

Server::Server(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend)
{
    m_timer.setInterval(50);
    connect(&m_timer, &QTimer::timeout, this, &Server::push);
}

Server::~Server() { stop(); }

QString Server::makePin(int digits)
{
    QString pin;
    for (int i = 0; i < std::clamp(digits, 5, 12); ++i) pin += QChar('0' + QRandomGenerator::system()->bounded(10));
    return pin;
}

bool Server::pinValid(const QString &pin)
{
    if (pin.size() < 4 || pin.size() > 32) return false;
    for (const QChar c : pin)
        if (c.unicode() > 127 || !c.isLetterOrNumber()) return false; // letters and digits: it travels in an address
    return true;
}

bool Server::start(quint16 port, const QString &pin, const QString &listen)
{
    stop();
    m_csp = policy(page::script(), page::style(), {}, m_frameable);
    if (!pin.isEmpty() && !pinValid(pin)) {
        m_status = m_name + QStringLiteral(": the code has to be 4 to 32 letters or digits");
        return false;
    }
    m_pin = pin;
    m_server = new QTcpServer(this);
    const QHostAddress address = listen.isEmpty() ? QHostAddress(QHostAddress::Any) : QHostAddress(listen);
    if (!m_server->listen(address, port)) {
        m_status = m_name + QStringLiteral(": port %1 is not available (%2)").arg(port).arg(m_server->errorString());
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QTcpServer::newConnection, this, &Server::onConnection);
    m_timer.start();
    const QStringList list = urls();
    m_status = m_name + QStringLiteral(" on port %1").arg(m_server->serverPort()) + (m_pin.isEmpty() ? QStringLiteral(" (no code)") : QString()) +
               (list.isEmpty() ? QString() : QStringLiteral(": ") + list.join(QStringLiteral("  ")));
    return true;
}

void Server::stop()
{
    m_timer.stop();
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it)
        if (it->socket) {
            it->socket->disconnect(this);
            it->socket->abort();
            it->socket->deleteLater();
        }
    m_clients.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_attempts.clear();
    emit clientsChanged(0);
}

bool Server::isRunning() const { return m_server && m_server->isListening(); }
quint16 Server::port() const { return m_server ? m_server->serverPort() : 0; }

int Server::clientCount() const
{
    int n = 0;
    for (const Client &c : m_clients) n += c.websocket;
    return n;
}

QStringList Server::urls() const
{
    QStringList out;
    if (!m_server) return out;
    for (const QNetworkInterface &nif : QNetworkInterface::allInterfaces()) {
        if (!(nif.flags() & QNetworkInterface::IsUp) || (nif.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry &e : nif.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol)
                out << QStringLiteral("http://%1:%2/control").arg(e.ip().toString()).arg(m_server->serverPort());
    }
    return out;
}

bool Server::authorized(const QByteArray &presented) const { return m_pin.isEmpty() || sameBytes(presented, m_pin.toUtf8()); }

bool Server::servePage(const QString &, QByteArray *, QByteArray *) { return false; }
bool Server::message(QTcpSocket *, const QString &, const QJsonObject &) { return false; }
void Server::tick() {}
void Server::clientGone(QTcpSocket *) {}

void Server::sendText(QTcpSocket *client, const QByteArray &json) const { send(client, websocket::encode(websocket::Text, json)); }

QList<QTcpSocket *> Server::websockets() const
{
    QList<QTcpSocket *> out;
    for (const Client &c : m_clients)
        if (c.websocket) out << c.socket;
    return out;
}

void Server::leaveControls(QTcpSocket *client)
{
    const auto it = m_clients.find(client);
    if (it != m_clients.end()) it->ownPages = true;
}

void Server::onConnection()
{
    while (QTcpSocket *s = m_server->nextPendingConnection()) {
        if (m_clients.size() >= kMaxClients * 2) { // a flood: no more room
            s->abort();
            s->deleteLater();
            continue;
        }
        Client c;
        c.socket = s;
        m_clients.insert(s, c);
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            const bool was = m_clients.value(s).websocket;
            clientGone(s);
            m_clients.remove(s);
            s->deleteLater();
            if (was) emit clientsChanged(clientCount());
        });
    }
}

void Server::send(QTcpSocket *s, const QByteArray &frame) const
{
    if (s->bytesToWrite() > (1 << 20)) { // a client that does not read: dropped, not buffered without end
        s->abort();
        return;
    }
    s->write(frame);
}

QByteArray Server::controlsMessage(const std::vector<Control> &controls) const
{
    QJsonArray list;
    for (const Control &c : controls) list.append(c.wireJson());
    const QString title = m_backend->title();
    return QJsonDocument(QJsonObject{{"t", "controls"}, {"title", title.isEmpty() ? m_name : title}, {"show", hasShowPage()}, {"list", list}}).toJson(QJsonDocument::Compact);
}

void Server::onData(QTcpSocket *s)
{
    auto it = m_clients.find(s);
    if (it == m_clients.end()) return;
    Client &c = *it;
    c.buffer += s->readAll();
    if (!c.websocket) {
        const int end = c.buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (c.buffer.size() > kMaxHeader) s->abort();
            return;
        }
        const QList<QByteArray> lines = c.buffer.left(end).split('\n');
        c.buffer.remove(0, end + 4);
        const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
        QHash<QByteArray, QByteArray> headers;
        for (int k = 1; k < lines.size(); ++k) {
            const int colon = lines[k].indexOf(':');
            if (colon > 0) headers.insert(lines[k].left(colon).trimmed().toLower(), lines[k].mid(colon + 1).trimmed());
        }
        if (request.value(0) != "GET" || request.size() < 2) {
            send(s, response(400, {}, "text/plain"));
            s->disconnectFromHost();
            return;
        }
        const QUrl url = QUrl::fromEncoded(request.value(1));
        const QString path = url.path();
        if (headers.value("upgrade").toLower() == "websocket") {
            const QString who = s->peerAddress().toString();
            Attempts &a = m_attempts[who];
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (a.until > now) {
                send(s, response(429, {}, "text/plain", "Retry-After: 60\r\n"));
                s->disconnectFromHost();
                return;
            }
            // Only the page this server served opens it: a page of another site must not reach the application from a phone's browser
            const QByteArray origin = headers.value("origin");
            const bool sameOrigin = origin.isEmpty() || QUrl(QString::fromUtf8(origin)).authority() == QString::fromUtf8(headers.value("host"));
            const QByteArray pin = QUrl::fromPercentEncoding(QUrlQuery(url).queryItemValue(QStringLiteral("pin"), QUrl::FullyEncoded).toUtf8()).toUtf8();
            int clients = 0;
            for (const Client &other : std::as_const(m_clients)) clients += other.websocket;
            if (path != QLatin1String("/ws") || !sameOrigin || !authorized(pin)) {
                if (path == QLatin1String("/ws") && sameOrigin && ++a.failures >= kMaxFailures) {
                    a.failures = 0;
                    a.until = now + kLockMs;
                }
                send(s, response(403, {}, "text/plain"));
                s->disconnectFromHost();
                return;
            }
            if (clients >= kMaxClients) {
                send(s, response(429, {}, "text/plain"));
                s->disconnectFromHost();
                return;
            }
            a.failures = 0;
            const QByteArray accept = QCryptographicHash::hash(headers.value("sec-websocket-key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                                               QCryptographicHash::Sha1).toBase64();
            s->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n");
            c.websocket = true;
            emit clientsChanged(clientCount());
            push(); // its first picture of the controls
        } else if (path == QLatin1String("/info")) { // whether it asks a code: nothing else
            send(s, response(200, m_pin.isEmpty() ? "{\"pin\":false}" : "{\"pin\":true}", "application/json"));
            s->disconnectFromHost();
            return;
        } else if (path == QLatin1String("/")) {
            if (QByteArray html, csp; servePage(path, &html, &csp)) { // an application's own front page
                send(s, response(200, html, "text/html; charset=utf-8", "Content-Security-Policy: " + csp + "\r\n"));
                s->disconnectFromHost();
                return;
            }
            send(s, response(302, {}, "text/plain", "Location: /control\r\n"));
            s->disconnectFromHost();
            return;
        } else if (path == QLatin1String("/control")) {
            send(s, response(200, page(m_pageTitle), "text/html; charset=utf-8", "Content-Security-Policy: " + m_csp + "\r\n", m_frameable));
            s->disconnectFromHost();
            return;
        } else if (QByteArray html, csp; servePage(path, &html, &csp)) {
            send(s, response(200, html, "text/html; charset=utf-8", "Content-Security-Policy: " + csp + "\r\n"));
            s->disconnectFromHost();
            return;
        } else {
            send(s, response(404, {}, "text/plain"));
            s->disconnectFromHost();
            return;
        }
    }
    while (c.websocket) {
        int opcode = 0;
        QByteArray payload;
        const int got = websocket::decode(c.buffer, &opcode, &payload, m_maxFrame);
        if (got == 0) return;
        if (got < 0) {
            s->abort();
            return;
        }
        onFrame(c, opcode, payload);
        if (!m_clients.contains(s)) return;
    }
}

void Server::onFrame(Client &c, int opcode, const QByteArray &payload)
{
    switch (opcode) {
    case websocket::Text: {
        if (--c.budget < 0) return; // faster than any hand: dropped
        const QJsonObject o = QJsonDocument::fromJson(payload).object();
        const QString type = o.value("t").toString();
        if (type == QLatin1String("layout")) { // the page laid out again: where each control is, for every device
            QHash<quint64, std::array<int, 4>> places;
            const QJsonObject items = o.value("items").toObject();
            for (auto it = items.begin(); it != items.end() && places.size() < 512; ++it) {
                const QJsonObject p = it.value().toObject();
                places.insert(it.key().toULongLong(), {p.value("x").toInt(), p.value("y").toInt(), p.value("w").toInt(1), p.value("h").toInt(1)});
            }
            if (m_backend->setLayouts(places)) emit layoutEdited();
            return;
        }
        if (type == QLatin1String("edit") || type == QLatin1String("remove")) { // a tile's settings changed on the page, or the control removed
            const quint64 target = o.value("id").toString().toULongLong();
            if (!target) return;
            if (type == QLatin1String("remove")) {
                if (m_backend->remove(target)) emit layoutEdited();
                return;
            }
            std::optional<Control> found = m_backend->control(target);
            if (!found) return;
            found->applyProps(o.value("props").toObject());
            if (m_backend->update(target, *found)) emit layoutEdited();
            return;
        }
        const bool setting = type == QLatin1String("set") || type == QLatin1String("color") || type == QLatin1String("point") || type == QLatin1String("adsr") ||
                             type == QLatin1String("multi") || type == QLatin1String("points");
        if (!setting) {
            message(c.socket, type, o);
            return;
        }
        const quint64 id = o.value("id").toString().toULongLong();
        if (!id) return;
        bool moved = false;
        if (type == QLatin1String("set") && o.value("v").isDouble()) {
            moved = m_backend->setValue(id, o.value("v").toDouble());
        } else if (type == QLatin1String("point")) {
            const QJsonArray xy = o.value("xy").toArray();
            moved = xy.size() == 2 && m_backend->setPoint(id, xy[0].toDouble(), xy[1].toDouble());
        } else if (type == QLatin1String("points")) {
            const QJsonArray v = o.value("v").toArray();
            std::vector<double> values;
            for (int i = 0; i < v.size() && i < Control::kPointsMax * 2; ++i) values.push_back(v[i].toDouble());
            moved = !values.empty() && m_backend->setPoints(id, values);
        } else if (type == QLatin1String("multi")) {
            const QJsonArray v = o.value("v").toArray();
            std::vector<double> values;
            for (int i = 0; i < v.size() && i < Control::kMultiMax; ++i) values.push_back(v[i].toDouble());
            moved = !values.empty() && m_backend->setMulti(id, values);
        } else if (type == QLatin1String("adsr")) {
            const QJsonArray v = o.value("v").toArray();
            moved = v.size() == 4 && m_backend->setAdsr(id, {v[0].toDouble(), v[1].toDouble(), v[2].toDouble(), v[3].toDouble()});
        } else if (type == QLatin1String("color")) {
            const QJsonArray rgb = o.value("rgb").toArray();
            moved = rgb.size() == 3 && m_backend->setColor(id, rgb[0].toDouble(), rgb[1].toDouble(), rgb[2].toDouble());
        }
        if (moved) emit controlMoved();
        break;
    }
    case websocket::Close:
        send(c.socket, websocket::encode(websocket::Close, {}));
        c.socket->disconnectFromHost();
        break;
    case websocket::Ping: send(c.socket, websocket::encode(websocket::Pong, payload.left(125))); break;
    default: break;
    }
}

// Every 50 ms: the list to a client that has not got its latest form, then the values that changed
void Server::push()
{
    bool any = false;
    for (const Client &c : std::as_const(m_clients)) any |= c.websocket;
    if (!any) return;
    tick();
    const std::vector<Control> controls = m_backend->controls();
    const quint64 revision = m_backend->revision();
    QByteArray list;
    for (Client &c : m_clients) {
        if (!c.websocket) continue;
        c.budget = std::min(300.0, c.budget + 15); // 300 a second
        if (c.ownPages) continue;
        if (c.controlsSeen != revision) {
            if (list.isEmpty()) list = websocket::encode(websocket::Text, controlsMessage(controls));
            send(c.socket, list);
            c.controlsSeen = revision;
            c.sent.clear();
            for (const Control &control : controls) c.sent.insert(control.id, control.numbers());
            continue;
        }
        QJsonObject changed;
        for (const Control &control : controls) {
            const std::vector<double> now = control.numbers();
            auto found = c.sent.find(control.id);
            if (found != c.sent.end() && found.value() == now) continue;
            c.sent.insert(control.id, now);
            changed[QString::number(control.id)] = jsonOf(now);
        }
        if (!changed.isEmpty())
            send(c.socket, websocket::encode(websocket::Text, QJsonDocument(QJsonObject{{"t", "values"}, {"v", changed}}).toJson(QJsonDocument::Compact)));
    }
}

} // namespace pxs::controls
