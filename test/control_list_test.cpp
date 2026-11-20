// The list of controls and what is done to it, and the views of a parameter, against a fake ParameterSource.
#include <pxs/ControlList.h>
#include <QJsonValue>
#include <QMap>
#include <cstdio>

using namespace pxs::controls;

static int failures = 0;
static void check(bool ok, const char *what)
{
    if (ok) return;
    std::printf("FAIL control_list: %s\n", what);
    ++failures;
}

static QString keyOf(const QString &name, const QStringList &taken, int index)
{
    QString k = name.toLower().replace(' ', '_');
    if (k.isEmpty()) k = QStringLiteral("p%1").arg(index);
    QString u = k;
    for (int n = 2; taken.contains(u); ++n) u = QStringLiteral("%1_%2").arg(k).arg(n);
    return u;
}

struct Fake : ParameterSource {
    QMap<QString, ParamInfo> infos;
    QMap<QString, QJsonValue> values;
    QStringList touchedPaths;
    bool info(quint64, const QString &path, ParamInfo *out) const override
    {
        const auto it = infos.constFind(path);
        if (it == infos.cend()) return false;
        *out = it.value();
        return true;
    }
    QJsonValue value(quint64, const QString &path) const override { return values.value(path, QJsonValue::Undefined); }
    bool setNumber(quint64, const QString &path, double v) override { if (!values.contains(path)) return false; values[path] = v; return true; }
    bool setJson(quint64, const QString &path, const QJsonValue &v) override { if (!values.contains(path)) return false; values[path] = v; return true; }
    void touched(quint64, const QString &path) override { touchedPaths << path; }
};

int main()
{
    std::vector<Control> list;
    quint64 next = 1;
    Control s;
    s.name = "Gain";
    s.min = 0;
    s.max = 10;
    s.value = 50;
    const quint64 a = add(list, next, s, keyOf);
    const quint64 b = add(list, next, s, keyOf);
    check(a == 1 && b == 2, "ids are given in order");
    check(list[0].key == "gain" && list[1].key == "gain_2", "keys are unique");
    check(list[0].value == 10, "a new value is clamped");

    Control g;
    g.kind = Control::Kind::Group;
    g.name = "Page";
    const quint64 gid = add(list, next, g, keyOf);
    Control child = list[0];
    child.group = gid;
    check(update(list, a, child, keyOf).found && list[0].group == gid, "a control goes in a group");
    Control cycle = list[2];
    cycle.group = a; // a group inside a non-group: refused
    update(list, gid, cycle, keyOf);
    check(list[2].group == 0, "a group's parent must be a group");

    // Renaming changes the key and says so
    Control renamed = list[0];
    renamed.name = "Level";
    const Updated u = update(list, a, renamed, keyOf);
    check(u.keyChanged && u.oldKey == "gain" && u.newKey == "level" && list[0].key == "level", "renaming changes the key");

    // Moving and removing
    check(move(list, gid, 0) && list[0].id == gid, "move to the front");
    check(remove(list, gid) && find(list, a)->group == 0, "what a group held goes up");
    check(!remove(list, 999), "removing nothing");

    // Layouts are kept on the grid
    QHash<quint64, std::array<int, 4>> places;
    places.insert(a, {50, -3, 99, 0});
    check(setLayouts(list, places), "a layout changes");
    const Control *p = find(list, a);
    check(p->gw == Control::kGridColumns && p->gx == 0 && p->gh == 1 && p->gy == 0, "a layout stays on the grid");
    check(!setLayouts(list, places), "the same layout again changes nothing");

    // Values, and what they feed
    QStringList fed;
    const Feed feed = [&](const QString &addr, double v) { fed << addr + "=" + QString::number(v); };
    check(setValue(list, a, 3, feed) && fed == QStringList{"=3"}, "a value is fed");
    check(setValue(list, a, 3, feed) && fed.size() == 1, "the same value is not fed twice");
    check(!setValue(list, a, NAN, feed), "a NaN is refused");
    Control pad;
    pad.kind = Control::Kind::Pad;
    pad.min = 0;
    pad.max = 1;
    const quint64 pid = add(list, next, pad, keyOf);
    fed.clear();
    check(setPoint(list, pid, 0.25, 2, feed) && fed == QStringList{"x=0.25", "y=1"}, "a pad point: x, y, clamped");
    Control multi;
    multi.kind = Control::Kind::Multi;
    multi.count = 3;
    const quint64 mid = add(list, next, multi, keyOf);
    fed.clear();
    check(setMulti(list, mid, {0.5, 0.5}, feed) && fed.size() == 2 && fed[0].startsWith("1="), "multi bars are fed by number");
    Control pts;
    pts.kind = Control::Kind::Points;
    pts.count = 2;
    const quint64 qid = add(list, next, pts, keyOf);
    fed.clear();
    check(setPoints(list, qid, {0.1, 0.2, 0.3, 0.4}, feed) && fed.contains("p2/y=0.4"), "points are fed by point");
    Control adsr;
    adsr.kind = Control::Kind::Adsr;
    const quint64 sid = add(list, next, adsr, keyOf);
    fed.clear();
    check(setAdsr(list, sid, {0.1, 0.2, 5, 0.4}, feed) && fed.contains("sustain=1"), "adsr sustain is at most 1");
    check(!setValue(list, sid + 100, 1, feed), "unknown id");

    // Views of a parameter
    Fake fake;
    ParamInfo gain;
    gain.path = "gain";
    gain.label = "Audio › Gain (dB)";
    gain.min = -60;
    gain.max = 6;
    gain.defaultValue = 0;
    fake.infos["gain"] = gain;
    fake.values["gain"] = -12.0;
    ParamInfo on;
    on.path = "on";
    on.type = ParamInfo::Type::Bool;
    fake.infos["on"] = on;
    fake.values["on"] = true;
    ParamInfo mode;
    mode.path = "mode";
    mode.type = ParamInfo::Type::Choice;
    mode.choices = {"a", "b", "c"};
    fake.infos["mode"] = mode;
    fake.values["mode"] = "b";
    ParamInfo ro;
    ro.path = "ro";
    ro.access = ParamInfo::Read;
    fake.infos["ro"] = ro;
    ParamInfo rgb;
    rgb.path = "tint/r";
    rgb.label = "Look › Tint R";
    rgb.colorComponent = 0;
    fake.infos["tint/r"] = rgb;
    for (const char *k : {"tint/r", "tint/g", "tint/b"}) fake.values[k] = 0.5;

    const auto slider = forParameter(fake, 7, "gain", 0);
    check(slider && slider->kind == Control::Kind::Slider && slider->boundScope == 7 && slider->boundParam == "gain", "a slider for a number");
    check(slider && slider->min == -60 && slider->value == -12 && slider->unit == " dB", "its range, value and unit");
    check(forParameter(fake, 0, "on", 0)->kind == Control::Kind::Toggle, "a toggle for a bool");
    const auto choice = forParameter(fake, 0, "mode", 0);
    check(choice && choice->kind == Control::Kind::Choice && choice->value == 1, "a choice");
    {
        const auto ro = forParameter(fake, 0, "ro", 0);
        check(ro && ro->widget == "meter" && ro->boundParam == "ro", "a read-only number is a view: a meter");
    }
    check(!forParameter(fake, 0, "nope", 0), "nor an unknown one");
    const auto color = forParameter(fake, 0, "tint/r", 0);
    check(color && color->kind == Control::Kind::Color && color->boundParam == "tint", "a color's channel gives the color");

    Control view = *slider;
    fake.values["gain"] = 3.0;
    fillBound(view, fake);
    check(view.value == 3, "a view shows the parameter now");
    check(setBound(view, fake, 4.5) && fake.values["gain"].toDouble() == 4.5 && fake.touchedPaths == QStringList{"gain"}, "a view sets the parameter");
    Control cview = *choice;
    check(setBound(cview, fake, 2) && fake.values["mode"].toString() == "c", "a choice view sets by index");
    check(setBound(cview, fake, 99) && fake.values["mode"].toString() == "c", "an index past the end is the last");
    check(setBoundColor(*color, fake, 0.1, 2, 0.3) && fake.values["tint/g"].toDouble() == 1, "a color view sets its channels, clamped");
    Control rview = *slider;
    rview.boundParam = "ro";
    check(!setBound(rview, fake, 1), "a read-only parameter is not set");

    { // a slider on a physics setting of another control
        Control target;
        target.id = 5, target.name = "Ball pad", target.kind = Control::Kind::Points, target.count = 2;
        target.balls = {Ball{}, Ball{}};
        target.balls[1].physics.friction = 7;
        target.points = {{0.5, 0.5}, {0.5, 0.5}};
        check(!forPhysics(target, "nonsense") && !forPhysics(target, "3.friction"), "a setting that is not there has no widget");
        const auto view = forPhysics(target, "2.friction");
        check(view && view->physTarget == 5 && view->max == 30 && view->value == 7 && view->name.contains("ball 2"), "a slider for a ball's friction");
        std::vector<Control> list{target, *view};
        list[1].id = 6;
        fillPhysics(list);
        check(list[1].value == 7, "its value is the setting");
        check(setPhysics(list, 6, 12.5) && list[0].balls[1].physics.friction == 12.5 && list[0].balls[0].physics.friction == 5, "setting it sets that ball's physics only");
        check(setPhysics(list, 6, 1e9) && list[0].balls[1].physics.friction == 30, "within the setting's range");
        const Control again = Control::fromJson(list[1].toJson());
        check(again.physTarget == 5 && again.physKey == "2.friction", "it goes through JSON");
    }
    { // A macro: one value sent to several parameters, each in its own range, with its own curve
        Fake f;
        const auto add = [&](const QString &path, ParamInfo::Type type, double lo, double hi) {
            ParamInfo i;
            i.type = type;
            i.min = lo;
            i.max = hi;
            f.infos[path] = i;
            f.values[path] = 0.0;
        };
        add("/a", ParamInfo::Type::Float, 0, 100);
        add("/b", ParamInfo::Type::Int, 0, 255);
        add("/c", ParamInfo::Type::Bool, 0, 1);
        add("/d", ParamInfo::Type::Float, 0, 1);
        Control m;
        m.kind = Control::Kind::Slider;
        m.min = 0;
        m.max = 1;
        m.targets = {{1, "/a", 0, 100, 1, 0, {}}, {1, "/b", 255, 0, 1, 0, {}}, {1, "/c", 0, 1, 1, 0, {}}, {1, "/d", 0, 1, 2, 0, {}}, {1, "/gone", 0, 1, 1, 0, {}}};
        check(m.macro() && !m.bound(), "a macro is not a view");
        check(setTargets(m, f, 0.5), "a value goes to the targets there are");
        check(std::abs(f.values["/a"].toDouble() - 50) < 1e-9, "a range");
        check(std::abs(f.values["/b"].toDouble() - 127.5) < 1e-9, "an inverted range");
        check(f.values["/c"].toBool(), "a switch from 0.5");
        check(std::abs(f.values["/d"].toDouble() - 0.25) < 1e-9, "a curve of its own");
        check(f.touchedPaths.contains("/a") && !f.touchedPaths.contains("/gone"), "what took is told");
        const Control back = Control::fromJson(m.toJson());
        check(back.targets.size() == 5 && back.targets[1].outMin == 255 && back.targets[3].curve == 2, "targets go through JSON");
        Control e = m; // edited from the page: by position, one left out goes
        e.applyProps(QJsonObject{{"targets", QJsonArray{QJsonObject{{"i", 3}, {"min", 0.2}, {"curve", 99}}, QJsonObject{{"i", 0}}, QJsonObject{{"i", 17}}}}});
        check(e.targets.size() == 2 && e.targets[0].path == "/d" && e.targets[0].outMin == 0.2 && e.targets[0].curve == 10 && e.targets[1].path == "/a", "targets edited from the page");
        Control scene;
        scene.kind = Control::Kind::Button;
        scene.targets = {{1, "/a", 20, 80, 1, 0, {}}};
        f.values["/a"] = 0.0;
        check(setTargets(scene, f, 1, 0, false) && f.values["/a"].toDouble() == 80, "a button sends its high value when pressed");
        check(!setTargets(scene, f, 0, 0, false) && f.values["/a"].toDouble() == 80, "a scene button sends nothing when let go");
        check(setTargets(scene, f, 0, 0, true) && f.values["/a"].toDouble() == 20, "a hold button sends its low value when let go");
        Control pad;
        pad.kind = Control::Kind::Pad;
        pad.targets = {{1, "/a", 0, 100, 1, 0, {}}, {1, "/d", 0, 1, 1, 1, {}}};
        f.values["/a"] = 0.0;
        f.values["/d"] = 0.0;
        setTargets(pad, f, 0.4, 0);
        check(std::abs(f.values["/a"].toDouble() - 40) < 1e-9 && f.values["/d"].toDouble() == 0.0, "a pad's X goes to the X targets only");
    }
    if (!failures) std::printf("control_list: ok\n");
    return failures ? 1 : 0;
}
