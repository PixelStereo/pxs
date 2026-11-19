// The model of a control and its page: kinds, JSON, bounds, what a remote page may ask for.
#include <pxs/Control.h>
#include <pxs/ControlsPage.h>
#include <pxs/Physics.h>
#include <QCryptographicHash>
#include <cmath>
#include <cstdio>

using pxs::controls::Control;

static int failures = 0;
static void check(bool ok, const char *what)
{
    if (ok) return;
    std::printf("FAIL controls: %s\n", what);
    ++failures;
}
static bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

int main()
{
    // Every kind round-trips through its JSON
    for (int k = 0; k < Control::kKindCount; ++k) {
        Control c;
        c.id = 7;
        c.name = "Thing";
        c.key = "thing";
        c.kind = Control::Kind(k);
        c.choices = {"A", "B"};
        c.count = 5;
        c.points = {{0.1, 0.2}, {0.3, 0.4}};
        c.multi = {0.1, 0.2, 0.3};
        const Control again = Control::fromJson(c.toJson());
        check(again.kind == c.kind && again.name == "Thing" && again.id == 7, "a control keeps its kind, name and id through JSON");
    }
    // A slider's range is kept sane, a view of a parameter remembers where it is
    {
        QJsonObject o{{"kind", "slider"}, {"min", 5}, {"max", 1}, {"step", 1e9}, {"value", 99}};
        const Control c = Control::fromJson(o);
        check(c.max > c.min && c.step <= c.max - c.min && c.value >= c.min && c.value <= c.max, "a slider's range, step and value are made sane");
        Control v;
        v.boundScope = 9;
        v.boundParam = "opacity";
        const Control again = Control::fromJson(v.toJson());
        check(again.boundScope == 9 && again.boundParam == "opacity", "a view of a parameter keeps its scope and address");
    }
    // Its place on the page stays inside the grid; what a page asks for is a word and a color, nothing else
    {
        QJsonObject o{{"kind", "slider"}, {"layout", QJsonObject{{"x", 99}, {"y", -4}, {"w", 40}, {"h", 3}}}};
        const Control c = Control::fromJson(o);
        check(c.gw == 12 && c.gx == 0 && c.gy == 0 && c.gh == 3, "a place beyond the grid is brought back into it");
        check(Control::cleanWidget("knob") == "knob" && Control::cleanWidget("<script>").isEmpty() && Control::cleanWidget("averyveryverylongword").isEmpty(), "a widget is a short word");
        check(Control::cleanAccent("#E5564A") == "#e5564a" && Control::cleanAccent("red").isEmpty() && Control::cleanAccent("#12345").isEmpty(), "an accent is #rrggbb");
    }
    // The numbers a parameter can be driven from
    {
        Control p;
        p.kind = Control::Kind::Points;
        p.count = 3;
        Control s;
        s.kind = Control::Kind::Slider;
        Control t;
        t.kind = Control::Kind::Toggle;
        check(p.numberSources().size() == 6 && s.numberSources().size() == 1 && t.numberSources().empty(), "a control says which numbers it offers");
    }
    // Physics: a thrown slider slows down and stops, bounces on the end, a spring brings it back to its default; and it is kept in JSON
    {
        Control c;
        c.kind = Control::Kind::Slider;
        c.min = 0, c.max = 10, c.def = 5, c.value = 2;
        c.physics = {true, 3, 0, 0};
        std::vector<double> x{2}, v{0.5};
        int steps = 0;
        while (pxs::controls::physics::step(c, x, v, 0.016) && steps < 2000) ++steps;
        check(steps > 5 && steps < 2000 && x[0] > 2 && x[0] <= 10 && v[0] == 0, "a thrown slider slows down and stops");
        c.physics = {true, 0, 0, 1};
        x = {9.9}, v = {1.0};
        pxs::controls::physics::step(c, x, v, 0.016);
        pxs::controls::physics::step(c, x, v, 0.016);
        check(x[0] <= 10 && v[0] < 0, "it bounces on the end of its range");
        c.physics = {true, 6, 40, 0};
        x = {0}, v = {0};
        steps = 0;
        while (pxs::controls::physics::step(c, x, v, 0.016) && steps < 5000) ++steps;
        check(steps < 5000 && near(x[0], 5), "a spring brings it back to its default");
        c.physics = {true, 7, 12, 0.25};
        const Control again = Control::fromJson(c.toJson());
        check(again.physics == c.physics, "its physics goes through JSON");
        Control t;
        t.kind = Control::Kind::Toggle;
        t.applyProps(QJsonObject{{"physics", QJsonObject{{"on", true}}}});
        check(!t.physics.on, "a toggle has no physics");
        Control s;
        s.kind = Control::Kind::Slider;
        s.applyProps(QJsonObject{{"physics", QJsonObject{{"on", true}, {"friction", 1e9}, {"bounce", -3}}}});
        check(s.physics.on && s.physics.friction == 30 && s.physics.bounce == 0, "what a page asks of the physics is kept within bounds");
    }
    // The page: the pieces stay under a compiler's limit and the script is whole
    {
        const QByteArray script = pxs::controls::page::script(), style = pxs::controls::page::style();
        check(script.contains("press(") && script.contains("fetch('/info')") && script.endsWith("})();\n"), "the page's script is whole");
        check(sizeof(pxs::controls::page::kScriptA) < 16000 && sizeof(pxs::controls::page::kScriptB) < 16000 && sizeof(pxs::controls::page::kScriptC) < 16000 &&
                  sizeof(pxs::controls::page::kScriptD) < 16000 && style.size() > 1000,
              "each piece of the page is under 16 KB");
    }
    if (!failures) std::printf("controls: OK\n");
    return failures;
}
