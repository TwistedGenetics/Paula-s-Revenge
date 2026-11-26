// Topology-preserving-transform state variable filter (Zavalishin / Simper). Stable under fast modulation.
#pragma once
#include "helix/Math.h"

namespace helix {

struct SvfCoefs
{
    float g = 0, k = 2, a1 = 0, a2 = 0, a3 = 0;
    // g = tan(pi fc / fs), res 0..1 -> k = 2 - 1.98 * res (self-oscillation edge at res = 1)
    void set(float gIn, float res)
    {
        g = gIn;
        k = 2.0f - 1.98f * clamp(res, 0.0f, 1.0f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

struct SvfState
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    // Outputs: lp, bp, hp (notch = lp + hp)
    inline void tick(const SvfCoefs& c, float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - c.k * v1 - v2;
    }
};

} // namespace helix
