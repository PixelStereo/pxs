// The explorer's model against a device: open it, add a control for one of its parameters, and the control follows the device and drives it.
#include "Explorer.h"

#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static void pump(const std::function<bool()> &until, int ms = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!until() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pxs::Tree tree;
    double level = 0.25;
    bool on = false;
    auto p = std::make_unique<Parameter>("level", "Level", Parameter::Type::Float);
    p->range(0, 1).bind([&] { return QVariant(level); }, [&](const QVariant &v) { level = v.toDouble(); });
    tree.add("/mix/level", std::move(p));
    auto q = std::make_unique<Parameter>("on", "On", Parameter::Type::Bool);
    q->bind([&] { return QVariant(on); }, [&](const QVariant &v) { on = v.toBool(); });
    tree.add("/mix/on", std::move(q));
    pxs::OscService service(&tree, "Device");
    CHECK(service.start(0, 0, false));

    Explorer ex;
    const quint64 dev = ex.addDevice("Device", "127.0.0.1", service.httpPort());
    CHECK(ex.addDevice("Device", "127.0.0.1", service.httpPort()) == dev); // the same one
    CHECK(ex.addControl(dev, "/mix/level") == 0); // not open yet
    bool ready = false;
    QObject::connect(&ex, &Explorer::deviceReady, [&](quint64) { ready = true; });
    ex.openDevice(dev);
    pump([&] { return ready; });
    CHECK(ready);

    const quint64 rev = ex.revision();
    const quint64 id = ex.addControl(dev, "/mix/level");
    CHECK(id != 0 && ex.revision() > rev);
    CHECK(ex.addControl(dev, "/nowhere") == 0);
    const quint64 toggle = ex.addControl(dev, "/mix/on");
    CHECK(toggle != 0);
    CHECK(ex.controls().size() == 2);
    auto c = ex.control(id);
    CHECK(c && c->name == "Level" && c->key == "level" && c->value == 0.25 && c->max == 1);

    level = 0.9; // the device moves: the control follows
    pump([&] { return ex.control(id)->value == 0.9; });
    CHECK(ex.control(id)->value == 0.9);

    CHECK(ex.setValue(id, 0.4)); // the control drives the device
    pump([&] { return std::abs(level - 0.4) < 1e-6; });
    CHECK(std::abs(level - 0.4) < 1e-6); // over OSC: a float
    CHECK(ex.setValue(toggle, 1));
    pump([&] { return on; });
    CHECK(on);

    pxs::controls::Control renamed = *ex.control(id);
    renamed.name = "Master";
    CHECK(ex.update(id, renamed) && ex.control(id)->key == "master" && ex.control(id)->boundParam == "/mix/level");
    CHECK(ex.remove(id) && !ex.control(id) && ex.controls().size() == 1);
    CHECK(!ex.remove(id));

    // The page is served on the loopback
    CHECK(ex.serve(false, QString()) && ex.pageUrl().startsWith("http://127.0.0.1:"));

    if (!failures) std::printf("explorer: ok\n");
    return failures ? 1 : 0;
}
