#include <pxs/Tree.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pxs::Tree tree;
    double opacity = 0.5, speed = 1;
    bool on = false;
    QString mode = "add";
    auto make = [](const QString &path, Parameter::Type type) { return std::make_unique<Parameter>(path, path, type); };

    auto p = make("/layer/1/opacity", Parameter::Type::Float);
    p->range(0, 1).bind([&] { return QVariant(opacity); }, [&](const QVariant &v) { opacity = v.toDouble(); });
    tree.add("/layer/1/opacity", std::move(p)).description = "How opaque the layer is";
    auto q = make("/layer/2/opacity", Parameter::Type::Float);
    q->range(0, 1).bind([&] { return QVariant(speed); }, [&](const QVariant &v) { speed = v.toDouble(); });
    tree.add("/layer/2/opacity", std::move(q));
    auto s = make("/layer/1/enable", Parameter::Type::Bool);
    s->bind([&] { return QVariant(on); }, [&](const QVariant &v) { on = v.toBool(); });
    tree.add("/layer/1/enable", std::move(s));
    auto c = make("/layer/1/mode", Parameter::Type::Choice);
    c->choices({"add", "mix"}).bind([&] { return QVariant(mode); }, [&](const QVariant &v) { mode = v.toString(); });
    tree.add("/layer/1/mode", std::move(c));
    auto ro = make("/layer/1/fps", Parameter::Type::Float);
    ro->readOnly().bind([] { return QVariant(30.0); }, [](const QVariant &) {});
    tree.add("/layer/1/fps", std::move(ro));

    // Nodes are found by address, and their address is their path
    CHECK(tree.find("/layer/1/opacity") && tree.find("/layer/1/opacity")->path() == "/layer/1/opacity");
    CHECK(tree.find("/layer") && !tree.find("/layer")->parameter());
    CHECK(!tree.find("/nothing"));

    // A message is applied as the parameter allows: within its range, by its type, to everything the pattern names
    CHECK(tree.apply({"/layer/1/opacity", "f", {0.25}}) == 1 && qAbs(opacity - 0.25) < 1e-9);
    tree.apply({"/layer/1/opacity", "f", {7.0}});
    CHECK(qAbs(opacity - 1.0) < 1e-9); // kept within its limits
    CHECK(tree.apply({"/layer/*/opacity", "f", {0.5}}) == 2 && qAbs(speed - 0.5) < 1e-9);
    CHECK(tree.apply({"/layer/1/enable", "T", {true}}) == 1 && on);
    CHECK(tree.apply({"/layer/1/mode", "s", {QString("mix")}}) == 1 && mode == "mix");
    CHECK(tree.apply({"/layer/1/mode", "s", {QString("nope")}}) == 0 && mode == "mix"); // not a key
    CHECK(tree.apply({"/layer/1/fps", "f", {99.0}}) == 0);                              // read only

    // The feedback message says what a parameter holds
    const osc::Message m = pxs::Tree::message(*tree.find("/layer/1/enable"));
    CHECK(m.address == "/layer/1/enable" && m.types == "T");

    // OSCQuery is derived from the declarations
    const QJsonObject all = tree.query();
    const QJsonObject opacityNode = all["CONTENTS"].toObject()["layer"].toObject()["CONTENTS"].toObject()["1"].toObject()["CONTENTS"].toObject()["opacity"].toObject();
    CHECK(opacityNode["TYPE"] == "f" && opacityNode["ACCESS"] == 3);
    CHECK(opacityNode["RANGE"].toArray()[0].toObject()["MAX"] == 1);
    CHECK(qAbs(opacityNode["VALUE"].toArray()[0].toDouble() - 0.5) < 1e-9); // what it holds now
    CHECK(opacityNode["DESCRIPTION"] == "How opaque the layer is");
    const QJsonObject mode_ = tree.query("/layer/1/mode");
    CHECK(mode_["RANGE"].toArray()[0].toObject()["VALS"].toArray().size() == 2);
    CHECK(tree.query("/layer/1/fps")["ACCESS"] == 1);
    CHECK(pxs::Tree::hostInfo("App", 9000)["OSC_PORT"] == 9000);

    CHECK(tree.remove("/layer/2") && !tree.find("/layer/2/opacity"));
    return failures ? 1 : 0;
}
