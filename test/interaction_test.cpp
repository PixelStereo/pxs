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

    // Hold: a fall waits, a rise goes through (a switch stays on a moment after it went off)
    Interaction hd = Interaction::made(InteractionSource::Osc);
    hd.filters = {InteractionStage::made(InteractionFilter::Hold)};
    hd.filters[0].a = 0.55;
    InteractionProcessor hp;
    hp.input(hd, 1);
    CHECK(near(hp.step(hd, 0.1).value_or(-1), 1));
    hp.input(hd, 0);
    CHECK(near(hp.step(hd, 0.1).value_or(-1), 1)); // still on
    for (int k = 0; k < 3; ++k) hp.step(hd, 0.1);
    CHECK(near(hp.step(hd, 0.1).value_or(-1), 1)); // 0.5 s in the wait: still
    CHECK(near(hp.step(hd, 0.1).value_or(-1), 0)); // over
    hp.input(hd, 1);
    CHECK(near(hp.step(hd, 0.1).value_or(-1), 1)); // a rise: at once

    // Debounce: a change is followed once it lasts
    Interaction db = Interaction::made(InteractionSource::Osc);
    db.filters = {InteractionStage::made(InteractionFilter::Debounce)};
    db.filters[0].a = 0.25;
    InteractionProcessor dp;
    dp.input(db, 0);
    CHECK(near(dp.step(db, 0.1).value_or(-1), 0));
    dp.input(db, 1);
    CHECK(near(dp.step(db, 0.1).value_or(-1), 0)); // new: not yet
    dp.input(db, 0); // flickers back
    CHECK(near(dp.step(db, 0.1).value_or(-1), 0));
    dp.input(db, 1);
    dp.step(db, 0.1);
    for (int k = 0; k < 2; ++k) dp.step(db, 0.1);
    CHECK(near(dp.step(db, 0.1).value_or(-1), 1)); // held 0.3 s: followed
    dp.input(db, 1);
    CHECK(near(dp.step(db, 0.1).value_or(-1), 1));

    // Auto Return: away from its rest for a time, then back (until the value is at rest again)
    Interaction ar = Interaction::made(InteractionSource::Osc);
    ar.filters = {InteractionStage::made(InteractionFilter::AutoReturn)};
    ar.filters[0].a = 0.55;
    InteractionProcessor ap;
    ap.input(ar, 0);
    CHECK(near(ap.step(ar, 0.1).value_or(-1), 0));
    ap.input(ar, 1);
    CHECK(near(ap.step(ar, 0.1).value_or(-1), 1));
    for (int k = 0; k < 3; ++k) ap.step(ar, 0.1);
    CHECK(near(ap.step(ar, 0.1).value_or(-1), 1)); // 0.5 s
    CHECK(near(ap.step(ar, 0.1).value_or(-1), 0)); // back, though the value stays 1
    CHECK(near(ap.step(ar, 0.1).value_or(-1), 0));
    ap.input(ar, 0);
    CHECK(near(ap.step(ar, 0.1).value_or(-1), 0)); // at rest: armed again
    ap.input(ar, 1);
    CHECK(near(ap.step(ar, 0.1).value_or(-1), 1));

    // Their keys are saved and read back
    for (auto kind : {InteractionFilter::Hold, InteractionFilter::Debounce, InteractionFilter::AutoReturn}) {
        bool ok = false;
        CHECK(interactionFilterFromKey(interactionFilterKey(kind), &ok) == kind && ok);
        CHECK(InteractionStage::fromJson(InteractionStage::made(kind).toJson()).kind == kind);
    }

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
