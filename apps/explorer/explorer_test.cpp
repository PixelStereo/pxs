// The explorer's model against a device: open it, add a control for one of its parameters, and the control follows the device and drives it.
#include "Explorer.h"

#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
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

    // Two parameters make a pad, 0..1 on each, mapped to each one's range
    auto r = std::make_unique<Parameter>("rate", "Rate", Parameter::Type::Float);
    double rate = 10;
    r->range(0, 20).bind([&] { return QVariant(rate); }, [&](const QVariant &v) { rate = v.toDouble(); });
    tree.add("/mix/rate", std::move(r));
    ready = false;
    ex.openDevice(dev);
    ex.device(dev)->client->refresh();
    pump([&] { return ex.device(dev)->client->tree().find("/mix/rate") != nullptr; });
    level = 0.5;
    const quint64 pad = ex.addPad(dev, "/mix/level", "/mix/rate");
    CHECK(pad != 0);
    pump([&] { return std::abs(ex.control(pad)->value - 0.5) < 1e-6; }); // asked for when it was added
    auto pc = ex.control(pad);
    CHECK(pc && pc->kind == pxs::controls::Control::Kind::Pad && std::abs(pc->value - 0.5) < 1e-6);
    pump([&] { return std::abs(ex.control(pad)->value2 - 0.5) < 1e-6; });
    CHECK(std::abs(ex.control(pad)->value2 - 0.5) < 1e-6 && pc->boundLabel == "Device:/mix/level, /mix/rate");
    CHECK(ex.setPoint(pad, 0.25, 0.75));
    pump([&] { return std::abs(rate - 15) < 1e-4; });
    CHECK(std::abs(level - 0.25) < 1e-4 && std::abs(rate - 15) < 1e-4);
    CHECK(ex.addControl(dev, "/mix/level", "knob") != 0);
    CHECK(ex.controls().back().widget == "knob" && ex.controls().back().boundLabel == "Device:/mix/level");
    const quint64 plain = ex.addControl(dev, "/mix/level", "hold");
    CHECK(plain != 0 && ex.control(plain)->widget.isEmpty()); // a widget of another kind is ignored

    // The interface is kept and found again
    const QJsonObject saved = ex.toJson();
    CHECK(saved.value("devices").toArray().size() == 1);
    const size_t count = ex.controls().size();
    Explorer other;
    CHECK(!other.fromJson(QJsonObject{{"format", "nope"}}));
    CHECK(other.fromJson(saved));
    CHECK(other.controls().size() == count);
    CHECK(other.devices().size() == 1 && other.devices()[0]->name == "Device");
    bool back = false;
    QObject::connect(&other, &Explorer::deviceReady, [&](quint64) { back = true; });
    pump([&] { return back; });
    CHECK(back);
    quint64 padId = 0;
    for (const auto &c : other.controls()) if (c.boundParam2 == "/mix/rate") padId = c.id;
    CHECK(padId && other.control(padId)->boundScope == other.devices()[0]->id);
    pump([&] { return std::abs(other.control(padId)->value - 0.25) < 1e-3; });
    CHECK(std::abs(other.control(padId)->value - 0.25) < 1e-3);

    // The page is served on the loopback
    CHECK(ex.serve() && ex.pageUrl().startsWith("http://127.0.0.1:"));

    if (!failures) std::printf("explorer: ok\n");
    return failures ? 1 : 0;
}
