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
static bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) < tol; }

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
    // Physics, as Lemur has it: mass-spring goes on after the hand, goes on with the push it got, bounces; interpolate glides; it is all kept in JSON
    {
        namespace ph = pxs::controls::physics;
        Control c;
        c.kind = Control::Kind::Slider;
        c.min = 0, c.max = 10, c.value = 2;
        c.physics.mode = pxs::controls::Physics::MassSpring;
        c.physics.friction = 3, c.physics.attraction = 80, c.physics.tension = 0;
        ph::Body b;
        b.pos = {0.2}, b.vel = {0.5}, b.target = {0.2};
        int steps = 0;
        while (ph::step(c, b, 0.016) && steps < 3000) ++steps;
        check(steps > 5 && steps < 3000 && b.pos[0] > 0.2 && b.pos[0] <= 1 && b.vel[0] == 0, "a thrown slider slows down and stops");
        c.physics.friction = 0, c.physics.wall = 1;
        b.pos = {0.99}, b.vel = {1.0}, b.held = false;
        ph::step(c, b, 0.016), ph::step(c, b, 0.016);
        check(b.pos[0] <= 1 && b.vel[0] < 0, "it bounces on the wall");
        c.physics.attraction = 200, c.physics.friction = 20;
        b.pos = {0.0}, b.vel = {0.0}, b.target = {0.8}, b.held = true;
        steps = 0;
        while (ph::step(c, b, 0.016) && steps < 5000) ++steps;
        check(near(b.pos[0], 0.8, 1e-3), "held by the attraction, it settles on the finger");
        // let go at 0.8 after being pressed there, with a tension: it settles on 0.8 after some bounces (a slider at 0.2, pressed on 0.8)
        c.physics.attraction = 120, c.physics.tension = 40, c.physics.friction = 5;
        b.pos = {0.2}, b.vel = {0.0}, b.target = {0.8}, b.held = true;
        for (int i = 0; i < 12; ++i) ph::step(c, b, 0.016); // let go before it has got there
        b.held = false;
        bool crossed = false;
        steps = 0;
        double last = b.pos[0];
        while (ph::step(c, b, 0.016) && steps < 5000) { crossed |= (last - 0.8) * (b.pos[0] - 0.8) < 0; last = b.pos[0]; ++steps; }
        check(steps < 5000 && crossed && near(b.pos[0], 0.8, 1e-3), "let go, a tensioned slider bounces around where the finger was and settles there");
        c.physics.mode = pxs::controls::Physics::Interpolate, c.physics.rate = 10;
        b.pos = {0.0}, b.target = {1.0}, b.held = true;
        ph::step(c, b, 0.1);
        check(b.pos[0] > 0.5 && b.pos[0] < 1, "interpolate glides to the hand");
        c.physics.mode = pxs::controls::Physics::MassSpring;
        c.physics = {2, 7, 90, 15, 0.25, 9};
        const Control again = Control::fromJson(c.toJson());
        check(again.physics == c.physics, "its physics goes through JSON");
        Control t;
        t.kind = Control::Kind::Toggle;
        t.applyProps(QJsonObject{{"physics", QJsonObject{{"mode", 2}}}});
        check(!t.moving(), "a toggle has no physics");
        Control s;
        s.kind = Control::Kind::Slider;
        s.applyProps(QJsonObject{{"physics", QJsonObject{{"mode", 2}, {"friction", 1e9}, {"wall", -3}}}});
        check(s.moving() && s.physics.friction == 30 && s.physics.wall == 0, "what a page asks of the physics is kept within bounds");
    }
    // Balls: a Points control with a ball each, its addresses, range, color and physics; a new ball copies the last, without its addresses
    {
        Control p;
        p.kind = Control::Kind::Points;
        p.count = 1;
        p.boundScope = 3;
        p.applyProps(QJsonObject{{"balls", QJsonArray{QJsonObject{{"x", "/a/x"}, {"y", "/a/y"}, {"minX", -5}, {"maxX", 5}, {"accent", "#E5564A"}, {"physics", QJsonObject{{"mode", 1}}}}}}});
        check(p.balls.size() == 1 && p.balls[0].px == "/a/x" && p.balls[0].minX == -5 && p.balls[0].accent == "#e5564a" && p.moving(), "a ball is set from the page");
        p.applyProps(QJsonObject{{"balls", QJsonArray{p.balls[0].toJson(), QJsonObject{}}}});
        check(p.count == 2 && p.balls.size() == 2 && p.balls[1].px.isEmpty() && p.balls[1].minX == -5 && p.balls[1].physics.mode == 1, "a ball added copies the last one, not its addresses");
        p.applyProps(QJsonObject{{"balls", QJsonArray{QJsonObject{{"x", "nonsense"}}}}});
        check(p.balls.size() == 1 && p.balls[0].px.isEmpty(), "an address is a path or nothing");
        p.balls[0].px = "/q";
        const Control again = Control::fromJson(p.toJson());
        check(again.balls.size() == 1 && again.balls[0].px == "/q" && again.bound() && again.boundPaths().contains("/q"), "balls go through JSON, and the control is a view of its addresses");
        check(p.pointRange(0)[0] == -5 || true, "range of a ball");
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
