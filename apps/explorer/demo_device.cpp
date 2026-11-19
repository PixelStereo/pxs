// A small OSCQuery device to try the explorer on: a few parameters that drift, announced on the network. Run it, then the explorer.
#include <pxs/OscService.h>

#include <QCoreApplication>
#include <QTimer>
#include <cmath>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pxs::Tree tree;
    double level = 0.5, rate = 2;
    int steps = 4;
    bool on = true;
    QString mode = "sine";
    auto add = [&](const char *path, const char *label, Parameter::Type type, auto setup) {
        auto p = std::make_unique<Parameter>(QString(path).section('/', -1), label, type);
        setup(*p);
        tree.add(path, std::move(p));
    };
    add("/mix/level", "Level", Parameter::Type::Float, [&](Parameter &p) {
        p.range(0, 1).bind([&] { return QVariant(level); }, [&](const QVariant &v) { level = v.toDouble(); });
    });
    add("/mix/rate", "Rate (Hz)", Parameter::Type::Float, [&](Parameter &p) {
        p.range(0.1, 20).bind([&] { return QVariant(rate); }, [&](const QVariant &v) { rate = v.toDouble(); });
    });
    add("/mix/steps", "Steps", Parameter::Type::Int, [&](Parameter &p) {
        p.range(1, 16).bind([&] { return QVariant(steps); }, [&](const QVariant &v) { steps = v.toInt(); });
    });
    add("/mix/on", "On", Parameter::Type::Bool, [&](Parameter &p) {
        p.bind([&] { return QVariant(on); }, [&](const QVariant &v) { on = v.toBool(); });
    });
    add("/osc/mode", "Mode", Parameter::Type::Choice, [&](Parameter &p) {
        p.choices({"sine", "saw", "square"}).bind([&] { return QVariant(mode); }, [&](const QVariant &v) { mode = v.toString(); });
    });
    tree.addMessage("/osc/reset", "N", [&](const QVariantList &) { level = 0.5; std::printf("reset\n"); return true; }, "Back to 0.5");

    pxs::OscService service(&tree, QStringLiteral("Demo device"));
    if (!service.start(0, 0, true)) return 1;
    std::printf("Demo device: OSCQuery http://127.0.0.1:%u, OSC udp %u\n", service.httpPort(), service.oscPort());
    std::fflush(stdout);

    QTimer drift; // the level moves by itself: what a view shows follows
    double t = 0;
    QObject::connect(&drift, &QTimer::timeout, [&] {
        t += 0.05;
        if (on) level = 0.5 + 0.4 * std::sin(t * rate);
    });
    drift.start(50);
    return app.exec();
}
