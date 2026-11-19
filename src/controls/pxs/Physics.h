#pragma once
// The physics of a control's numbers (Control::physics): one body per number, in the 0..1 of its range. Friction, a spring to the
// control's default, bounce against the ends. Pure: Server.h calls it every 16 ms for what the hand let go of.
#include "Control.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pxs::controls::physics {

// The 0..1 of a number within the control's range
inline double unit(const Control &c, double v) { return c.max > c.min ? std::clamp((v - c.min) / (c.max - c.min), 0.0, 1.0) : 0.0; }
inline double fromUnit(const Control &c, double u) { return c.min + std::clamp(u, 0.0, 1.0) * (c.max - c.min); }

// One step of dt seconds on the numbers x (in the control's own range) with their speeds v (per second, in 0..1 of the range).
// True while something still moves; false (and v zero) at rest
inline bool step(const Control &c, std::vector<double> &x, std::vector<double> &v, double dt)
{
    const Physics &p = c.physics;
    v.resize(x.size(), 0.0);
    const double target = unit(c, c.def), damp = std::exp(-p.friction * dt);
    bool moving = false;
    for (size_t i = 0; i < x.size(); ++i) {
        double u = unit(c, x[i]);
        v[i] += p.attract * (target - u) * dt;
        v[i] *= damp;
        u += v[i] * dt;
        if (u < 0) { u = 0; v[i] = -v[i] * p.bounce; }
        if (u > 1) { u = 1; v[i] = -v[i] * p.bounce; }
        const bool pulled = p.attract > 0 && std::abs(target - u) > 5e-4;
        if (std::abs(v[i]) < 2e-3 && !pulled) {
            v[i] = 0;
            if (p.attract > 0) u = target;
        } else {
            moving = true;
        }
        x[i] = fromUnit(c, u);
    }
    return moving;
}

} // namespace pxs::controls::physics
