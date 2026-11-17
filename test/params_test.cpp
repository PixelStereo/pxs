#include "ChangeFeed.h"
#include "Parameter.h"

#include <QCoreApplication>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

struct Recorder : ChangeFeed::Observer {
    QStringList written;
    void parameterWritten(quint64, const QString &path) override { written << path; }
    void layerChanged(quint64) override {}
    void addressChanged(const QString &) override {}
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    double stored = 0.5;
    // A parameter is declared where it is made: its path, its label, its kind, then what it accepts and where it lives
    Parameter opacity("opacity", "Opacity", Parameter::Type::Float);
    opacity.range(0, 1).byDefault(0.5).animatable().bind([&] { return QVariant(stored); }, [&](const QVariant &v) { stored = v.toDouble(); });

    Recorder recorder;
    ChangeFeed::observe(&recorder);
    CHECK(opacity.setValue(0.25));
    CHECK(qAbs(stored - 0.25) < 1e-9);
    CHECK(recorder.written == QStringList{"opacity"}); // the others are told, with its address
    opacity.setValue(7); // kept within its limits
    CHECK(qAbs(stored - 1.0) < 1e-9);
    CHECK(opacity.reset() && qAbs(stored - 0.5) < 1e-9);
    ChangeFeed::ignore(&recorder);

    const ParamInfo info = opacity.info();
    CHECK(info.path == "opacity" && info.isNumber() && info.animatable && info.min == 0 && info.max == 1);

    // Saved by address, restored by address
    std::vector<Parameter *> list{&opacity};
    const QJsonObject saved = parametersToJson(list);
    CHECK(saved.contains("opacity"));
    stored = 0.9;
    parametersFromJson(list, saved);
    CHECK(qAbs(stored - 0.5) < 1e-9);
    return failures ? 1 : 0;
}
