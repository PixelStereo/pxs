// AnimTrack evaluation (keys, curves, oscillators), from NyxMap's timelines: what an interaction's Curve filter reads.
#include "Anim.h"

#include <cmath>
#include <limits>

static constexpr double kPi = 3.14159265358979323846;

// The easing of normalized time 0..1: 0 linear, 1 ease in, 2 ease out, 3 ease in-out (smoothstep), 4 ease in cubic, 5 ease out cubic
double easeCurve(double t, int curve)
{
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    switch (curve < 0 ? 0 : curve > 5 ? 5 : curve) {
    case 0: return t;
    case 1: return t * t;
    case 2: return t * (2 - t);
    case 3: return t * t * (3 - 2 * t);
    case 4: return t * t * t;
    default: return 1 - (1 - t) * (1 - t) * (1 - t);
    }
}

// ---------------------------------------------------------------------------
// Values

// A random number in -1..1 for (seed, n): the same every time
static double hashUnit(quint32 seed, long long n)
{
    quint64 x = (quint64(seed) << 32) ^ quint64(n) ^ 0x9E3779B97F4A7C15ull;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return double(x >> 11) / double(1ull << 53) * 2.0 - 1.0;
}

// The value of a cubic Bézier (one coordinate) at s in 0..1
static double cubic(double a, double b, double c, double d, double s)
{
    const double u = 1 - s;
    return u * u * u * a + 3 * u * u * s * b + 3 * u * s * s * c + s * s * s * d;
}

void AnimTrack::bezierHandles(size_t k, double *t1, double *v1, double *t2, double *v2) const
{
    const AnimKey &a = keys[k], &b = keys[k + 1];
    const double va = keyValue(k), vb = keyValue(k + 1), span = std::max(0.0, b.t - a.t);
    // Automatic: flat, a third of the way; a handle never reaches beyond the other key (the curve stays a function of time)
    *t1 = a.t + (a.hasOut() ? std::clamp(a.outDt, 0.0, span) : span / 3);
    *v1 = va + (a.hasOut() ? a.outDv : 0.0);
    *t2 = b.t + (b.hasIn() ? std::clamp(b.inDt, -span, 0.0) : -span / 3);
    *v2 = vb + (b.hasIn() ? b.inDv : 0.0);
}

double AnimTrack::valueAt(double position, double played) const
{
    if (oscillator) {
        const double per = std::max(0.01, period);
        const double total = played / per + phase; // periods since the start
        double p = total - std::floor(total);      // 0..1 within the period
        double w = 0;
        switch (wave) {
        case AnimWave::Sine: w = std::sin(2 * kPi * p); break;
        case AnimWave::Triangle: w = p < 0.25 ? 4 * p : p < 0.75 ? 2 - 4 * p : 4 * p - 4; break; // starts at 0, rises
        case AnimWave::Saw: w = 2 * p - 1; break;
        case AnimWave::Square: w = p < 0.5 ? 1 : -1; break;
        case AnimWave::Random: w = hashUnit(seed, (long long)std::floor(total)); break;
        case AnimWave::SmoothRandom: {
            const long long n = (long long)std::floor(total);
            const double e = (1 - std::cos(kPi * p)) / 2; // eased from one value to the next
            w = hashUnit(seed, n) + (hashUnit(seed, n + 1) - hashUnit(seed, n)) * e;
            break;
        }
        }
        return center + amplitude * w;
    }
    if (keys.empty()) return std::numeric_limits<double>::quiet_NaN();
    if (position <= keys.front().t) return keyValue(0);
    if (position >= keys.back().t) return keyValue(keys.size() - 1);
    for (size_t k = 0; k + 1 < keys.size(); ++k) {
        const AnimKey &a = keys[k], &b = keys[k + 1];
        if (position >= b.t) continue;
        const double va = keyValue(k), vb = keyValue(k + 1);
        if (a.curve == kAnimHold || b.t - a.t <= 1e-9) return va;
        if (a.curve == kAnimBezier) {
            double t1, v1, t2, v2;
            bezierHandles(k, &t1, &v1, &t2, &v2);
            // The curve's time grows with s (the handles stay within the segment): s found by halving
            double lo = 0, hi = 1;
            for (int i = 0; i < 40; ++i) {
                const double mid = (lo + hi) / 2;
                (cubic(a.t, t1, t2, b.t, mid) < position ? lo : hi) = mid;
            }
            return cubic(va, v1, v2, vb, (lo + hi) / 2);
        }
        return va + (vb - va) * easeCurve((position - a.t) / (b.t - a.t), a.curve);
    }
    return keyValue(keys.size() - 1);
}

double AnimTrack::keyValue(size_t k) const
{
    if (k == 0 && !keys.empty() && keys[0].isCurrentValue && std::isfinite(captured)) return captured;
    return k < keys.size() ? keys[k].v : std::numeric_limits<double>::quiet_NaN();
}

std::array<double, 3> AnimTrack::keyColor(size_t k) const
{
    if (k == 0 && !keys.empty() && keys.front().isCurrentValue &&
        std::all_of(capturedRgb.begin(), capturedRgb.end(), [](double v) { return std::isfinite(v); })) return capturedRgb;
    return k < keys.size() ? keys[k].rgb : AnimTrack().capturedRgb;
}

std::array<double, 3> AnimTrack::colorAt(double position) const
{
    if (keys.empty()) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan, nan};
    }
    if (position <= keys.front().t) return keyColor(0);
    for (size_t k = 0; k + 1 < keys.size(); ++k) {
        const AnimKey &a = keys[k], &b = keys[k + 1];
        if (position >= b.t) continue;
        const auto from = keyColor(k), to = keyColor(k + 1);
        if (a.curve == kAnimHold || b.t - a.t <= 1e-9) return from;
        const double u = easeCurve((position - a.t) / (b.t - a.t), a.curve);
        std::array<double, 3> rgb;
        for (int c = 0; c < 3; ++c) rgb[c] = from[c] + (to[c] - from[c]) * u;
        return rgb;
    }
    return keyColor(keys.size() - 1);
}
