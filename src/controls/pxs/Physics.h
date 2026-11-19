#pragma once
// The physics of a control's numbers (Control::physics, a Points control's balls each have theirs), as Lemur has it: one body per number, in
// the 0..1 of its range, that goes toward where the hand is (Interpolate) or is held to it by a spring and goes on by itself when the hand
// lets go (Mass-Spring: attracted to the finger, then thrown or pulled back to where it was: friction, tension, the bounce on the walls). Pure: Server.h steps it every 16 ms.
#include "Control.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <vector>

namespace pxs::controls::physics {

struct Body {
    std::vector<double> pos, vel, target; // 0..1 of each number's range (vel per second)
    bool held = false;                    // the hand is on the control
    std::vector<char> hand;               // which numbers it holds (empty: all of them); a pad's other balls go on by themselves
    bool holds(size_t i) const { return held && (hand.empty() || (i < hand.size() && hand[i])); }
    bool moving = false;
    qint64 movedAt = 0;
};

// Number i of a control: its range, its physics, its rest place
inline void rangeOf(const Control &c, size_t i, double *lo, double *hi)
{
    if (c.kind == Control::Kind::Points) {
        const auto r = c.pointRange(i / 2);
        *lo = i % 2 == 0 ? r[0] : r[2];
        *hi = i % 2 == 0 ? r[1] : r[3];
    } else {
        *lo = c.min;
        *hi = c.max;
    }
}
inline const Physics &of(const Control &c, size_t i) { return c.kind == Control::Kind::Points ? c.physicsOf(i / 2) : c.physics; }
inline std::vector<double> toUnits(const Control &c, const std::vector<double> &numbers)
{
    std::vector<double> out(numbers.size());
    for (size_t i = 0; i < numbers.size(); ++i) {
        double lo, hi;
        rangeOf(c, i, &lo, &hi);
        out[i] = hi > lo ? std::clamp((numbers[i] - lo) / (hi - lo), 0.0, 1.0) : 0.0;
    }
    return out;
}
inline std::vector<double> fromUnits(const Control &c, const std::vector<double> &units)
{
    std::vector<double> out(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        double lo, hi;
        rangeOf(c, i, &lo, &hi);
        out[i] = lo + std::clamp(units[i], 0.0, 1.0) * (hi - lo);
    }
    return out;
}

// One step of dt seconds. True while something still moves (or is held by the hand and not there yet)
inline bool step(const Control &c, Body &b, double dt)
{
    const size_t n = b.pos.size();
    b.vel.resize(n, 0.0);
    b.target.resize(n, 0.0);
    bool moving = false;
    for (size_t i = 0; i < n; ++i) {
        const Physics &p = of(c, i);
        double &u = b.pos[i], &v = b.vel[i];
        const double t = b.target[i];
        if (p.mode == Physics::None) {
            u = t;
            v = 0;
        } else if (p.mode == Physics::Interpolate) {
            u += (t - u) * (1 - std::exp(-p.rate * dt));
            v = 0;
            if (std::abs(t - u) < 5e-4) u = t;
            else moving = true;
        } else {
            v += (b.holds(i) ? p.attraction : p.tension) * (t - u) * dt; // attracted to the finger; let go, pulled to where it was (tension 0: thrown)
            v *= std::exp(-p.friction * dt);
            u += v * dt;
            if (u < 0) { u = 0; v = -v * p.wall; }
            if (u > 1) { u = 1; v = -v * p.wall; }
            if (std::abs(v) > 2e-3 || ((b.holds(i) ? p.attraction : p.tension) > 0 && std::abs(t - u) > 5e-4)) moving = true;
            else v = 0;
        }
    }
    return moving;
}

} // namespace pxs::controls::physics
