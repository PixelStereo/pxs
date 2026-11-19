// The WebSocket of the OSCQuery port, tried with Qt's own client (a second implementation of the handshake and the frames):
// LISTEN / IGNORE stream binary OSC, a binary frame is applied, PATH_CHANGED follows the address space, and a page of another
// origin is refused.

#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWebSocket>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

using namespace pxs;

static void pump(QCoreApplication &app, const std::function<bool()> &until, int ms = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!until() && t.elapsed() < ms) app.processEvents(QEventLoop::AllEvents, 20);
}

static QByteArray command(const char *what, const char *path)
{
    return QJsonDocument(QJsonObject{{"COMMAND", what}, {"DATA", path}}).toJson(QJsonDocument::Compact);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Tree tree;
    int cue = -1;
    tree.addAction("/show/go", "i", [&](const QVariantList &a) { cue = a.value(0).toInt(); return true; });
    tree.addSignal("/detect/peak", "f");
    double level = 0.5;
    auto p = std::make_unique<Parameter>("level", "Level", Parameter::Type::Float);
    p->range(0, 1).bind([&] { return QVariant(level); }, [&](const QVariant &v) { level = v.toDouble(); });
    tree.add("/level", std::move(p));
    OscService service(&tree, "WsTest");
    CHECK(service.start(0, 0, false));
    const QUrl url(QStringLiteral("ws://127.0.0.1:%1/").arg(service.httpPort()));

    // HOST_INFO says what the server does
    const QJsonObject info = QJsonDocument::fromJson(service.query().answer("GET", "/?HOST_INFO").body).object();
    CHECK(info["EXTENSIONS"].toObject()["LISTEN"] == true && info["EXTENSIONS"].toObject()["PATH_CHANGED"] == true);
    CHECK(info["WS_PORT"].toInt() == int(service.httpPort()));

    QWebSocket ws;
    std::vector<osc::Message> heard;
    QStringList texts;
    QObject::connect(&ws, &QWebSocket::binaryMessageReceived, [&](const QByteArray &d) { osc::decode(d, heard); });
    QObject::connect(&ws, &QWebSocket::textMessageReceived, [&](const QString &t) { texts << t; });
    ws.open(url);
    pump(app, [&] { return ws.state() == QAbstractSocket::ConnectedState; });
    CHECK(ws.state() == QAbstractSocket::ConnectedState);

    // Nothing is sent before LISTEN; then the values of that address, whoever writes them
    tree.apply({"/level", "f", {0.1}});
    pump(app, [&] { return false; }, 150);
    CHECK(heard.empty());
    ws.sendTextMessage(QString::fromUtf8(command("LISTEN", "/level")));
    pump(app, [&] { return false; }, 150);
    tree.apply({"/level", "f", {0.25}});
    pump(app, [&] { return !heard.empty(); });
    CHECK(heard.size() == 1 && heard[0].address == "/level" && heard[0].types == "f" && std::abs(heard[0].args.value(0).toDouble() - 0.25) < 1e-6);
    // Only that address: a signal nobody listens to is not sent
    tree.fire("/detect/peak", {0.9});
    pump(app, [&] { return false; }, 150);
    CHECK(heard.size() == 1);
    ws.sendTextMessage(QString::fromUtf8(command("LISTEN", "/detect/peak")));
    pump(app, [&] { return false; }, 150);
    tree.fire("/detect/peak", {0.9});
    pump(app, [&] { return heard.size() >= 2; });
    CHECK(heard.size() == 2 && heard[1].address == "/detect/peak");
    // IGNORE stops it
    ws.sendTextMessage(QString::fromUtf8(command("IGNORE", "/level")));
    pump(app, [&] { return false; }, 150);
    tree.apply({"/level", "f", {0.4}});
    pump(app, [&] { return false; }, 150);
    CHECK(heard.size() == 2);

    // A binary frame is an OSC packet for the application
    ws.sendBinaryMessage(osc::encode({"/show/go", "i", {5}}));
    pump(app, [&] { return cue == 5; });
    CHECK(cue == 5);
    ws.sendBinaryMessage(osc::encode({"/level", "f", {0.9}}));
    pump(app, [&] { return level > 0.85; });
    CHECK(std::abs(level - 0.9) < 1e-6);

    // The address space changes: PATH_CHANGED, once for what was made together
    tree.add("/layer/1/opacity");
    tree.add("/layer/1/mode");
    pump(app, [&] { return !texts.isEmpty(); });
    pump(app, [&] { return false; }, 150);
    CHECK(texts.size() == 1);
    const QJsonObject changed = QJsonDocument::fromJson(texts.value(0).toUtf8()).object();
    CHECK(changed["COMMAND"] == "PATH_CHANGED" && changed["DATA"] == "/layer");
    texts.clear();
    tree.remove("/layer/1/mode");
    pump(app, [&] { return !texts.isEmpty(); });
    CHECK(QJsonDocument::fromJson(texts.value(0).toUtf8()).object()["DATA"] == "/layer/1");

    // A page of another site is refused (a WebSocket writes); one of this machine is let in
    QWebSocket evil(QStringLiteral("http://evil.example"));
    bool refused = false;
    QObject::connect(&evil, &QWebSocket::errorOccurred, [&] { refused = true; });
    evil.open(url);
    pump(app, [&] { return refused || evil.state() == QAbstractSocket::ConnectedState; });
    CHECK(refused && evil.state() != QAbstractSocket::ConnectedState);
    QWebSocket local(QStringLiteral("http://localhost:8080"));
    local.open(url);
    pump(app, [&] { return local.state() == QAbstractSocket::ConnectedState; });
    CHECK(local.state() == QAbstractSocket::ConnectedState);
    CHECK(service.query().webSocketCount() == 2);
    local.close();
    ws.close();
    pump(app, [&] { return service.query().webSocketCount() == 0; });
    CHECK(service.query().webSocketCount() == 0);

    service.stop();
    return failures ? 1 : 0;
}
