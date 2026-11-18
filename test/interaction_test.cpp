#include "Interactions.h"

#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static bool near(double a, double b) { return std::abs(a - b) < 1e-6; }

int main()
{
    // Remap: a range of values to another, held within the output range at its ends
    Interaction it = Interaction::made(InteractionSource::Osc);
    it.filters = {InteractionStage::remap(0, 1, 10, 20)};
    InteractionProcessor p;
    CHECK(!p.step(it, 0.016)); // nothing came in yet
    p.input(it, 0.5);
    auto out = p.step(it, 0.016);
    CHECK(out && near(*out, 15));
    p.input(it, 3);
    out = p.step(it, 0.016);
    CHECK(out && near(*out, 20));

    // The master level scales the result
    it.master = 0.5;
    p.input(it, 1);
    out = p.step(it, 0.016);
    CHECK(out && near(*out, 10));

    // Threshold: 0 or 1
    Interaction th = Interaction::made(InteractionSource::Osc);
    th.filters = {InteractionStage::made(InteractionFilter::Threshold)};
    InteractionProcessor q;
    q.input(th, 0.9);
    CHECK(q.step(th, 0.016).value_or(-1) == 1);
    q.input(th, 0.0);
    CHECK(q.step(th, 0.016).value_or(-1) == 0);

    // Action mode: conditions on the value
    Interaction action = Interaction::made(InteractionSource::Osc);
    action.mode = InteractionMode::Action;
    action.conditions = {InteractionCondition{ConditionOp::Greater, 0.5, 1}};
    CHECK(action.conditionsMet(0.7) && !action.conditionsMet(0.2));

    // What is saved comes back, filters and source included
    it.oscAddress = "/fader/1";
    it.layer = 7;
    it.param = "opacity";
    const Interaction back = Interaction::fromJson(it.toJson());
    CHECK(back.oscAddress == "/fader/1" && back.layer == 7 && back.param == "opacity" && back.filters.size() == 1);
    CHECK(back.filters[0].kind == InteractionFilter::Remap && near(back.master, 0.5));
    return failures ? 1 : 0;
}
