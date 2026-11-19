// A preset by address: values of parameters, then actions; a locked parameter keeps its value; what the tree lacks is counted.

#include <pxs/Preset.h>

#include <QJsonDocument>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

using namespace pxs;

int main()
{
    Tree tree;
    double level = 0.5;
    bool playing = true;
    QString mode = "loop";
    double x = 0, y = 0, scratch = 0;
    auto add = [&](const QString &path, Parameter::Type type, auto get, auto set, auto configure) {
        auto p = std::make_unique<Parameter>(path, path, type);
        configure(*p);
        p->bind(get, set);
        tree.add(path, std::move(p));
    };
    add("/layer/1/level", Parameter::Type::Float, [&] { return QVariant(level); }, [&](const QVariant &v) { level = v.toDouble(); }, [](Parameter &p) { p.range(0, 1); });
    add("/layer/1/play", Parameter::Type::Bool, [&] { return QVariant(playing); }, [&](const QVariant &v) { playing = v.toBool(); }, [](Parameter &) {});
    add("/layer/1/mode", Parameter::Type::Choice, [&] { return QVariant(mode); }, [&](const QVariant &v) { mode = v.toString(); },
        [](Parameter &p) { p.choices({"loop", "oneshot", "pingpong"}); });
    add("/scratch", Parameter::Type::Float, [&] { return QVariant(scratch); }, [&](const QVariant &v) { scratch = v.toDouble(); },
        [](Parameter &p) { p.range(0, 1).transient(); }); // a state of the show, not of a cue: never held
    pxs::Node &pos = tree.add("/layer/1/position"); // filled in by hand: two numbers
    pos.type = "ff";
    pos.access = 3;
    pos.get = [&] { return QVariantList{x, y}; };
    pos.set = [&](const QVariantList &a) { x = a.value(0).toDouble(); y = a.value(1).toDouble(); return true; };
    // An action: it sees the values already written when it is done
    QList<double> levelWhenRewound;
    int rewound = 0, cues = -1;
    tree.addAction("/layer/1/rewind", "N", [&](const QVariantList &) { ++rewound; levelWhenRewound << level; return true; });
    tree.addAction("/show/go", "i", [&](const QVariantList &a) { cues = a.value(0).toInt(); return true; });

    // What a preset holds by default: every parameter that is read and written and saved, not the transient one, not an action
    level = 0.8; playing = false; mode = "pingpong"; x = 0.25; y = 0.75;
    Preset p = capture(tree, {}, {{"/layer/1/rewind", {}}, {"/show/go", QJsonArray{7}}});
    CHECK(p.values.size() == 4 && p.values.contains("/layer/1/level") && p.values.contains("/layer/1/position") && !p.values.contains("/scratch"));
    CHECK(p.values["/layer/1/mode"].toArray().at(0) == "pingpong" && p.values["/layer/1/play"].toArray().at(0) == false);
    CHECK(p.values["/layer/1/position"].toArray().size() == 2 && p.actions.size() == 2);

    // Recalled later: the values come back, then the actions are done, with their arguments, in order
    level = 0.1; playing = true; mode = "loop"; x = y = 0; scratch = 0.9;
    RecallResult r = recall(tree, p);
    CHECK(level == 0.8 && !playing && mode == "pingpong" && x == 0.25 && y == 0.75 && scratch == 0.9);
    CHECK(r.applied == 4 && r.actions == 2 && r.locked == 0 && r.missing == 0);
    CHECK(rewound == 1 && levelWhenRewound.value(0) == 0.8 && cues == 7); // rewound after the values were put back

    // A locked parameter keeps its value; a locked action is not done
    level = 0.2; tree.setLocked("/layer/1/level", true); tree.setLocked("/show/go", true); cues = -1;
    r = recall(tree, p);
    CHECK(level == 0.2 && r.locked == 2 && r.applied == 3 && cues == -1 && rewound == 2);
    tree.setLocked("/layer/1/level", false);
    tree.setLocked("/show/go", false);

    // An address the tree no longer has is counted, not an error; a selection holds only what it names
    tree.remove("/layer/1/mode");
    r = recall(tree, p);
    CHECK(r.missing == 1 && r.applied == 3);
    const Preset some = capture(tree, {"/layer/1/level", "/nothing", "/show/go"});
    CHECK(some.values.size() == 1 && some.values.contains("/layer/1/level") && some.actions.isEmpty()); // an action is not a value

    // In a file and back
    const Preset back = Preset::fromJson(QJsonDocument::fromJson(QJsonDocument(p.toJson()).toJson()).object());
    CHECK(back.values == p.values && back.actions.size() == 2 && back.actions[1].address == "/show/go" && back.actions[1].args.at(0).toInt() == 7);
    Preset named = p;
    named.name = "Verse";
    CHECK(Preset::fromJson(named.toJson()).name == "Verse" && Preset().isEmpty() && !p.isEmpty());
    return failures ? 1 : 0;
}
