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
    const QJsonObject all = tree.json();
    const QJsonObject opacityNode = all["CONTENTS"].toObject()["layer"].toObject()["CONTENTS"].toObject()["1"].toObject()["CONTENTS"].toObject()["opacity"].toObject();
    CHECK(opacityNode["TYPE"] == "f" && opacityNode["ACCESS"] == 3);
    CHECK(opacityNode["RANGE"].toArray()[0].toObject()["MAX"] == 1);
    CHECK(qAbs(opacityNode["VALUE"].toArray()[0].toDouble() - 0.5) < 1e-9); // what it holds now
    CHECK(opacityNode["DESCRIPTION"] == "How opaque the layer is");
    const QJsonObject mode_ = tree.json("/layer/1/mode");
    CHECK(mode_["RANGE"].toArray()[0].toObject()["VALS"].toArray().size() == 2);
    CHECK(tree.json("/layer/1/fps")["ACCESS"] == 1);
    CHECK(pxs::Tree::hostInfo("App", 9000)["OSC_PORT"] == 9000);

    // A node filled in by hand, for an application that already has its own way to read and write: two arguments, its
    // own attribute, a container with a description
    {
        QVariantList position{0.25, 0.75};
        pxs::Node &xy = tree.add("/layer/1/position");
        xy.type = "ff";
        xy.access = 3;
        xy.description = "Position";
        xy.range = QJsonArray{QJsonObject{{"MIN", 0}, {"MAX", 1}}, QJsonObject{{"MIN", 0}, {"MAX", 1}}};
        xy.clip = "both";
        xy.get = [&] { return position; };
        xy.set = [&](const QVariantList &v) { if (v.size() != 2) return false; position = v; return true; };
        xy.attributes["APP_DEFAULT"] = [] { return QJsonValue(QJsonArray{0.5, 0.5}); };
        CHECK(tree.apply({"/layer/1/position", "ff", {0.1, 0.2}}) == 1 && qAbs(position[1].toDouble() - 0.2) < 1e-9);
        CHECK(tree.apply({"/layer/1/position", "f", {0.1}}) == 0); // refused
        const QJsonObject j = tree.json("/layer/1/position");
        CHECK(j["TYPE"] == "ff" && j["VALUE"].toArray().size() == 2 && j["CLIPMODE"].toArray().size() == 2);
        CHECK(j["APP_DEFAULT"].toArray().size() == 2);
        QJsonObject only;
        CHECK(tree.attribute("/layer/1/position", "value", only) && only.contains("VALUE") && only.size() == 1);
        QJsonObject none;
        CHECK(tree.attribute("/layer/1/fps", "clipmode", none) && none.isEmpty()); // known node, no value
        CHECK(!tree.attribute("/nothing", "value", none));
        tree.add("/layer/1").description = "Layer 1";
        CHECK(tree.json("/layer/1")["DESCRIPTION"] == "Layer 1" && tree.json("/layer/1").contains("CONTENTS"));
        // A method without argument is not a container
        pxs::Node &go = tree.add("/go");
        go.access = 2;
        go.set = [](const QVariantList &) { return true; };
        CHECK(!tree.json("/go").contains("CONTENTS") && tree.apply({"/go", "", {}}) == 1);
        // A parameter without a range says none
        auto raw = make("/raw", Parameter::Type::Float);
        raw->unbounded().bind([] { return QVariant(1.0); }, [](const QVariant &) {});
        tree.add("/raw", std::move(raw));
        CHECK(!tree.json("/raw").contains("RANGE"));
    }
    // The three kinds: parameter (state), message (received, no state), signal (emitted, no state)
    {
        pxs::Tree kinds;
        int received = 0;
        kinds.addMessage("/show/go", "i", [&](const QVariantList &a) { received += a.value(0).toInt(); return true; }, "Go to a cue");
        kinds.addMessage("/show/stop", "", [&](const QVariantList &) { ++received; return true; });
        kinds.addSignal("/detect/peak", "f", "A peak was heard");
        auto level = make("/level", Parameter::Type::Float);
        double stored = 0;
        level->range(0, 1).bind([&] { return QVariant(stored); }, [&](const QVariant &v) { stored = v.toDouble(); });
        kinds.add("/level", std::move(level));
        CHECK(kinds.find("/show/go")->kind() == pxs::Kind::Message && kinds.find("/detect/peak")->kind() == pxs::Kind::Signal);
        CHECK(kinds.find("/level")->kind() == pxs::Kind::Parameter && kinds.find("/show")->kind() == pxs::Kind::Container);
        CHECK(kinds.apply({"/show/go", "i", {3}}) == 1 && received == 3);
        CHECK(kinds.apply({"/show/stop", "", {}}) == 1 && received == 4);
        CHECK(kinds.apply({"/detect/peak", "f", {1.0}}) == 0); // nobody writes a signal
        const QJsonObject go = kinds.json("/show/go");
        CHECK(go["ACCESS"] == 2 && go["TYPE"] == "i" && go["PXS_KIND"] == "message" && !go.contains("VALUE"));
        CHECK(kinds.json("/show/stop")["TYPE"] == "N");
        const QJsonObject peak = kinds.json("/detect/peak");
        CHECK(peak["ACCESS"] == 1 && peak["PXS_KIND"] == "signal" && !peak.contains("VALUE") && !peak.contains("CONTENTS"));
        CHECK(!kinds.json("/level").contains("PXS_KIND"));
        // Everything the application says goes to one output
        std::vector<osc::Message> out;
        kinds.setOutput([&](const osc::Message &m) { out.push_back(m); });
        CHECK(kinds.fire("/detect/peak", {0.8}) && out.size() == 1 && out[0].address == "/detect/peak" && out[0].types == "f");
        CHECK(!kinds.fire("/level") && !kinds.fire("/show/go") && !kinds.fire("/nothing"));
        stored = 0.25;
        CHECK(kinds.changed("/level") && out.size() == 2 && out[1].args.value(0).toDouble() == 0.25);
        CHECK(!kinds.changed("/detect/peak"));
    }
    CHECK(tree.remove("/layer/2") && !tree.find("/layer/2/opacity"));
    return failures ? 1 : 0;
}
