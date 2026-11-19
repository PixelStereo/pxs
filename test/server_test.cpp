// The controls server on its own, with a stand-in application behind it: its pages, its PIN, its protocol, its limits.
#include <pxs/Server.h>

#include <pxs/ControlsPage.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QtEndian>
#include <cmath>
#include <cstdio>
#include <stdexcept>

using namespace pxs::controls;

static void require(bool ok, const char *why)
{
    if (!ok) throw std::runtime_error(why);
}
static bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

// A plain client: one connection, the replies as they come
struct Client {
    QTcpSocket socket;
    QByteArray in;
    bool connectTo(quint16 port)
    {
        socket.connectToHost(QHostAddress::LocalHost, port);
        return socket.waitForConnected(2000);
    }
    void pump(int ms = 400)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (!socket.isOpen()) break;
            socket.waitForReadyRead(5);
            if (socket.bytesAvailable()) in += socket.readAll();
        }
    }
    template<class Predicate>
    void pumpUntil(Predicate ready, int timeoutMs = 2000)
    {
        QElapsedTimer deadline;
        deadline.start();
        while (!ready() && deadline.elapsed() < timeoutMs) pump(10);
    }
    QByteArray request(const QByteArray &text)
    {
        socket.write(text);
        socket.flush();
        in.clear();
        QElapsedTimer deadline;
        deadline.start();
        while (deadline.elapsed() < 5000) {
            pump(10);
            const int headerEnd = in.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                if (socket.state() == QAbstractSocket::UnconnectedState) break;
                continue;
            }
            // Wait for the HTTP response, rather than assuming the runner
            // delivered it within a fixed sleep. WebSocket upgrades end here.
            if (in.startsWith("HTTP/1.1 101")) break;
            const QByteArray header = in.left(headerEnd).toLower();
            const int lengthAt = header.indexOf("\r\ncontent-length:");
            if (lengthAt >= 0) {
                const int valueAt = lengthAt + int(sizeof("\r\ncontent-length:") - 1);
                const int lineEnd = header.indexOf("\r\n", valueAt);
                bool valid = false;
                const auto size = header.mid(valueAt, lineEnd < 0 ? -1 : lineEnd - valueAt).trimmed().toLongLong(&valid);
                if (valid && size >= 0 && in.size() - headerEnd - 4 >= size) break;
            }
            if (socket.state() == QAbstractSocket::UnconnectedState) break;
        }
        return in;
    }
    QByteArray upgrade(const QByteArray &path, const QByteArray &origin = {}, const QByteArray &host = "127.0.0.1")
    {
        QByteArray r = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n";
        if (!origin.isEmpty()) r += "Origin: " + origin + "\r\n";
        return request(r + "\r\n");
    }
    // A masked text frame, as a browser sends it
    void sendText(const QByteArray &payload)
    {
        const char mask[4] = {0x12, 0x34, 0x56, 0x78};
        QByteArray f;
        f.append(char(0x81));
        if (payload.size() < 126) {
            f.append(char(0x80 | payload.size()));
        } else { // a longer frame: its length in two more bytes, as a browser sends it
            f.append(char(0x80 | 126));
            f.append(char(payload.size() >> 8));
            f.append(char(payload.size() & 0xFF));
        }
        f.append(mask, 4);
        for (int i = 0; i < payload.size(); ++i) f.append(char(payload[i] ^ mask[i & 3]));
        socket.write(f);
        socket.flush();
    }
    // The server's text frames received so far, as JSON
    QList<QJsonObject> messages()
    {
        QList<QJsonObject> out;
        QByteArray rest = in;
        const int header = rest.indexOf("\r\n\r\n");
        if (rest.startsWith("HTTP/1.1 101") && header >= 0) rest.remove(0, header + 4);
        while (rest.size() >= 2) {
            quint64 len = quint8(rest[1]) & 0x7F;
            int pos = 2;
            if (len == 126) {
                if (rest.size() < 4) break;
                len = qFromBigEndian<quint16>(rest.constData() + 2);
                pos = 4;
            } else if (len == 127) {
                if (rest.size() < 10) break;
                len = qFromBigEndian<quint64>(rest.constData() + 2);
                pos = 10;
            }
            if (quint64(rest.size()) < quint64(pos) + len) break;
            out << QJsonDocument::fromJson(rest.mid(pos, int(len))).object();
            rest.remove(0, pos + int(len));
        }
        return out;
    }
};


// The application's side: a few controls it keeps
class Fake : public Backend
{
public:
    std::vector<Control> list;
    quint64 rev = 1;
    std::vector<Control> controls() const override { return list; }
    std::optional<Control> control(quint64 id) const override
    {
        for (const Control &c : list)
            if (c.id == id) return c;
        return std::nullopt;
    }
    quint64 revision() const override { return rev; }
    QString title() const override { return QStringLiteral("Fake"); }
    Control *find(quint64 id)
    {
        for (Control &c : list)
            if (c.id == id) return &c;
        return nullptr;
    }
    bool setValue(quint64 id, double v) override { if (Control *c = find(id)) { c->value = c->clamped(v); return true; } return false; }
    bool setPoint(quint64, double, double) override { return false; }
    bool setPoints(quint64 id, const std::vector<double> &xy) override
    {
        Control *c = find(id);
        if (!c) return false;
        for (size_t i = 0; i < xy.size() && i / 2 < c->points.size(); ++i) c->points[i / 2][i % 2] = std::clamp(xy[i], c->min, c->max);
        return true;
    }
    bool setMulti(quint64, const std::vector<double> &) override { return false; }
    bool setAdsr(quint64, const std::array<double, 4> &) override { return false; }
    bool setColor(quint64, double, double, double) override { return false; }
    bool setLayouts(const QHash<quint64, std::array<int, 4>> &places) override
    {
        bool any = false;
        for (Control &c : list)
            if (places.contains(c.id)) {
                const auto &p = places[c.id];
                c.gx = p[0], c.gy = p[1], c.gw = p[2], c.gh = p[3];
                any = true;
            }
        if (any) ++rev;
        return any;
    }
    bool update(quint64 id, const Control &changed) override { if (Control *c = find(id)) { *c = changed; ++rev; return true; } return false; }
    bool remove(quint64 id) override
    {
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].id == id) { list.erase(list.begin() + long(i)); ++rev; return true; }
        return false;
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        Fake fake;
        Control level;
        level.id = 1;
        level.name = "Level";
        level.key = "level";
        level.max = 10;
        Control crowd;
        crowd.id = 2;
        crowd.name = "Crowd";
        crowd.key = "crowd";
        crowd.kind = Control::Kind::Points;
        crowd.count = 2;
        crowd.points = {{0.2, 0.2}, {0.8, 0.8}};
        fake.list = {level, crowd};
        Server server(&fake);
        server.setPageTitle("Fake remote");
        require(!server.start(0, "ab"), "a code of two letters is refused");
        require(server.start(0, "secret1", "127.0.0.1"), "the server starts on the loopback");
        const quint16 port = server.port();

        { // the page and what it is allowed to run
            Client c;
            require(c.connectTo(port), "connect");
            const QByteArray reply = c.request("GET /control HTTP/1.1\r\nHost: x\r\n\r\n");
            const QByteArray script = "'sha256-" + QCryptographicHash::hash(pxs::controls::page::script(), QCryptographicHash::Sha256).toBase64() + "'";
            require(reply.contains("<title>Fake remote</title>") && reply.contains("Content-Security-Policy:") && reply.contains(script), "the page is served under a policy that hashes its script");
            require(!reply.contains("secret1"), "the code is nowhere in the page");
        }
        { // an unknown page, a wrong code, a foreign origin
            Client c;
            require(c.connectTo(port), "connect");
            require(c.request("GET /nothing HTTP/1.1\r\nHost: x\r\n\r\n").startsWith("HTTP/1.1 404"), "an unknown page is not found");
            Client w;
            require(w.connectTo(port), "connect");
            require(w.upgrade("/ws?pin=wrong").startsWith("HTTP/1.1 403"), "a wrong code is refused");
            Client f;
            require(f.connectTo(port), "connect");
            require(f.upgrade("/ws?pin=secret1", "http://evil.example").startsWith("HTTP/1.1 403"), "a page of another site is refused");
        }
        Client phone;
        require(phone.connectTo(port), "connect");
        require(phone.upgrade("/ws?pin=secret1", "http://127.0.0.1:" + QByteArray::number(port), "127.0.0.1:" + QByteArray::number(port)).startsWith("HTTP/1.1 101"), "the right code opens the WebSocket");
        phone.pumpUntil([&] { return !phone.messages().isEmpty(); });
        const QList<QJsonObject> first = phone.messages();
        require(!first.isEmpty() && first[0].value("t").toString() == "controls" && first[0].value("title").toString() == "Fake" &&
                    first[0].value("list").toArray().size() == 2, "the first picture is the list of controls");
        // Both ways
        phone.sendText("{\"t\":\"set\",\"id\":\"1\",\"v\":99}");
        phone.sendText("{\"t\":\"points\",\"id\":\"2\",\"v\":[0.5,0.5,9,-9]}");
        phone.pumpUntil([&] { return near(fake.find(1)->value, 10); });
        phone.pumpUntil([&] { return near(fake.find(2)->points[0][0], 0.5); });
        require(near(fake.find(1)->value, 10) && near(fake.find(2)->points[1][0], 1) && near(fake.find(2)->points[1][1], 0), "a value and points set from the page, kept in range");
        phone.in.clear();
        fake.find(1)->value = 4;
        phone.pumpUntil([&] { return !phone.messages().isEmpty(); });
        require(!phone.messages().isEmpty() && phone.messages()[0].value("t").toString() == "values" &&
                    near(phone.messages()[0].value("v").toObject().value("1").toDouble(), 4), "a value changed in the application reaches the page");
        phone.in.clear();
        phone.pump(500);
        require(phone.messages().isEmpty(), "nothing changes, nothing is sent");
        // Layout, edit and remove
        phone.sendText("{\"t\":\"layout\",\"items\":{\"1\":{\"x\":2,\"y\":3,\"w\":4,\"h\":2}}}");
        phone.pumpUntil([&] { return fake.find(1)->gw == 4; });
        require(fake.find(1)->gx == 2 && fake.find(1)->gy == 3 && fake.find(1)->gh == 2, "a tile placed from the page");
        phone.sendText("{\"t\":\"edit\",\"id\":\"1\",\"props\":{\"widget\":\"knob\",\"accent\":\"#E5564A\",\"curve\":99,\"name\":\"Gain\"}}");
        phone.pumpUntil([&] { return fake.find(1)->widget == "knob"; });
        require(fake.find(1)->accent == "#e5564a" && near(fake.find(1)->curve, 10) && fake.find(1)->name == "Gain" && fake.find(1)->gw == 4, "a tile's settings set from the page, within bounds, its place kept");
        phone.sendText("{\"t\":\"remove\",\"id\":\"1\"}");
        phone.pumpUntil([&] { return fake.list.size() == 1; });
        require(fake.list.size() == 1, "a control removed from the page");
        phone.sendText("not json");
        phone.sendText("{\"t\":\"set\",\"id\":\"999\",\"v\":1}");
        phone.pump(100);
        require(server.isRunning(), "junk is ignored, nothing falls");
        std::printf("controls server: OK\n");
        return 0;
    } catch (const std::exception &e) {
        std::printf("FAIL controls server: %s\n", e.what());
        return 1;
    }
}
