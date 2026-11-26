#include "helix/ModSystem.h"
#include <vector>

namespace helix {

static std::vector<int> buildDestMap()
{
    std::vector<int> m(kNumSpecialDests, -1);
    for (int i = 0; i < kNumParams; ++i) if (paramDef(i).modulatable) m.push_back(i);
    return m;
}
static const std::vector<int>& destMap() { static const std::vector<int> m = buildDestMap(); return m; }
int modDestParam(int dest) { const auto& m = destMap(); return dest >= 0 && dest < (int)m.size() ? m[(size_t)dest] : -1; }
int numModDests() { return (int)destMap().size(); }

double syncDivBeats(int div)
{
    static const double b[] = {32, 16, 8, 4, 2, 4.0 / 3.0, 1.5, 1, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25,
                               1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625};
    return b[clamp(div, 0, (int)(sizeof b / sizeof b[0]) - 1)];
}

void LfoShapes::resetToDefaults()
{
    for (int l = 0; l < kNumLfos; ++l)
    {
        static const float pattern[kMaxSteps] = {1.0f, -0.5f, 0.5f, -1.0f, 0.75f, 0.0f, -0.75f, 0.25f,
                                                 1.0f, -0.25f, 0.5f, -0.5f, 0.0f, 0.75f, -1.0f, 0.5f};
        for (int s = 0; s < kMaxSteps; ++s) steps[l][s].store(pattern[s], std::memory_order_relaxed);
        numPoints[l].store(3, std::memory_order_relaxed);
        const float x[3] = {0.0f, 0.5f, 1.0f}, y[3] = {-1.0f, 1.0f, -1.0f};
        for (int k = 0; k < kLfoMaxPoints; ++k)
        {
            px[l][k].store(k < 3 ? x[k] : 1.0f, std::memory_order_relaxed);
            py[l][k].store(k < 3 ? y[k] : -1.0f, std::memory_order_relaxed);
        }
    }
}

float LfoShapes::evalBreakpoint(int l, float ph) const
{
    const int n = clamp(numPoints[l].load(std::memory_order_relaxed), 2, kLfoMaxPoints);
    float x0 = px[l][0].load(std::memory_order_relaxed), y0 = py[l][0].load(std::memory_order_relaxed);
    if (ph <= x0) return y0;
    for (int k = 1; k < n; ++k)
    {
        const float x1 = px[l][k].load(std::memory_order_relaxed), y1 = py[l][k].load(std::memory_order_relaxed);
        if (ph <= x1) return x1 > x0 ? y0 + (y1 - y0) * (ph - x0) / (x1 - x0) : y1;
        x0 = x1; y0 = y1;
    }
    return y0;
}

float Lfo::shapeAt(float ph, LfoShape s, int l, const LfoShapes& shapes, int numSteps) const
{
    switch (s)
    {
        case LfoShape::Sine: return std::sin(kTwoPi * ph);
        case LfoShape::Triangle: return ph < 0.25f ? 4 * ph : (ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4);
        case LfoShape::SawUp: return 2.0f * ph - 1.0f;
        case LfoShape::SawDown: return 1.0f - 2.0f * ph;
        case LfoShape::Square: return ph < 0.5f ? 1.0f : -1.0f;
        case LfoShape::SampleHold: return held_;
        case LfoShape::SmoothRandom: { const float t = 0.5f - 0.5f * std::cos(kPi * ph); return held_ + (next_ - held_) * t; }
        case LfoShape::Breakpoint: return shapes.evalBreakpoint(l, ph);
        case LfoShape::Steps:
        {
            const int n = clamp(numSteps, 1, kMaxSteps);
            const int k = std::min(n - 1, (int)(ph * (float)n));
            return shapes.steps[l][k].load(std::memory_order_relaxed);
        }
    }
    return 0.0f;
}

float Lfo::advance(int n, float sr, float rateHz, LfoShape shape, LfoMode mode, float riseMs, float smooth,
                   float freePhase, int l, const LfoShapes& shapes, int numSteps)
{
    const float dPhase = rateHz * (float)n / sr;
    if (freePhase >= 0.0f)
    {
        if (freePhase < phase_ && !first_) { held_ = next_; next_ = rng_.bi(); }
        phase_ = freePhase;
    }
    else if (!done_)
    {
        float np = phase_ + dPhase;
        if (np >= 1.0f)
        {
            if (mode == LfoMode::OneShot) { np = 0.99999f; done_ = true; }
            else { held_ = next_; next_ = rng_.bi(); np -= std::floor(np); }
        }
        phase_ = np;
    }
    first_ = false;
    float v = shapeAt(phase_, shape, l, shapes, numSteps);
    age_ += (float)n / sr * 1000.0f;
    if (riseMs > 0.0f && age_ < riseMs) v *= age_ / riseMs;
    if (smooth > 0.0f)
    {
        // smooth 0..1 -> time constant 0..250 ms
        const float tau = smooth * smooth * 0.25f * sr;
        const float a = 1.0f - std::exp(-(float)n / std::max(tau, 1.0f));
        smooth_ += (v - smooth_) * a;
        v = smooth_;
    }
    else smooth_ = v;
    return v;
}

float perfCurve(int c, float t)
{
    auto sinePi = [](float x) { return std::sin(kPi * x); };
    auto fr = [](float x) { return x - std::floor(x); };
    switch (c)
    {
        case 0:  return 1.0f;                                                   // Hold
        case 1:  return t;                                                      // Ramp Up
        case 2:  return 1.0f - t;                                               // Ramp Down
        case 3:  return 0.5f - 0.5f * std::cos(kPi * t);                        // Smooth (shape of the glide)
        case 4:  return 1.0f - std::fabs(2.0f * t - 1.0f);                      // Triangle
        case 5:  return (1.0f - t) * (1.0f - t) * (1.0f - t);                   // Exp decay (pluck)
        case 6:  return sinePi(t);                                              // Sine bump
        case 7:  return t * t * t;                                              // Exp Up
        case 8:  { const float u = 1.0f - t; return 1.0f - u * u * u; }         // Log Up
        case 9:  return 1.0f - t * t * t;                                       // Log Down (hold then fall)
        case 10: return t * t * (3.0f - 2.0f * t);                              // S-Curve Up
        case 11: { const float u = 1.0f - t; return u * u * (3.0f - 2.0f * u); } // S-Curve Down
        case 12: return t < 0.5f ? 1.0f : 0.0f;                                 // Gate 1/2
        case 13: return t < 0.25f ? 1.0f : 0.0f;                                // Gate 1/4
        case 14: return t < 0.75f ? 1.0f : 0.0f;                                // Gate 3/4
        case 15: return fr(2.0f * t);                                           // Saw Up x2
        case 16: return 1.0f - fr(2.0f * t);                                    // Saw Down x2
        case 17: return 1.0f - std::fabs(2.0f * fr(2.0f * t) - 1.0f);           // Triangle x2
        case 18: return std::fabs(sinePi(2.0f * t));                            // Sine x2
        case 19: return fr(2.0f * t) < 0.5f ? 1.0f : 0.0f;                      // Pulse x2
        case 20: return std::floor(t * 4.0f) / 3.0f;                            // Stairs Up
        case 21: return 1.0f - std::floor(t * 4.0f) / 3.0f;                     // Stairs Down
        case 22: return std::fabs(sinePi(3.0f * t)) * (1.0f - t);               // Bounce
        case 23: return 0.0f;                                                   // Off
        case 24: case 25: case 26: case 27:                                     // Roll x2 / x3 / x4 / x8: repeated hits
        {
            static const float k[] = {2.0f, 3.0f, 4.0f, 8.0f};
            const float u = 1.0f - fr(k[c - 24] * t);
            return u * u;
        }
        default: return 1.0f;
    }
}

// Swing: each pair of steps keeps its length, the first is stretched and the second squeezed (swing 1 = 75/25).
static double swingPos(double pos, float swing)
{
    if (swing <= 0.0f) return pos;
    const double sw = 0.5 * clamp(swing, 0.0f, 1.0f);
    const double pair = std::floor(pos * 0.5), u = pos - 2.0 * pair;
    return 2.0 * pair + (u < 1.0 + sw ? u / (1.0 + sw) : 1.0 + (u - 1.0 - sw) / (1.0 - sw));
}

double performerSteps(const float* p, int pi, double beats)
{
    const int base = pi == 0 ? P_P1_DIV : P_P2_DIV;
    static const double feel[] = {1.0, 0.5, 2.0, 1.5};
    const int f = clamp((int)p[pi == 0 ? P_P1_FEEL : P_P2_FEEL], 0, 3);
    return beats / syncDivBeats((int)p[base]) * feel[f];
}

float lfoJumpFactor(const float* p, int l)
{
    const int j = (int)p[P_L1_JUMP + l];
    if (j <= 0 || p[P_MACRO1 + clamp((int)p[P_LJ_MACRO], 0, 7)] < 0.5f) return 1.0f;
    static const float f[] = {1.0f, 2.0f, 3.0f, 4.0f, 8.0f, 0.5f};
    return f[clamp(j, 0, 5)];
}

// Loop (0.5.4): Off plays steps 1..Length; the other modes play between Loop Start and Loop End (kept inside the
// pattern): Forward, Backward, Ping-Pong (ends not repeated), Random (a fixed shuffle per pass, the same every
// time you play it) and Intro+Loop (steps 1..End once, then Start..End round and round).
struct LoopShape { int mode, len, s, e, L; };
static LoopShape loopShape(const float* p, int pi)
{
    const int base = pi == 0 ? P_P1_DIV : P_P2_DIV;
    LoopShape ls;
    ls.len = clamp((int)p[base + 1], 1, kMaxSteps);
    ls.mode = clamp((int)p[pi == 0 ? P_P1_LOOP : P_P2_LOOP], 0, 5);
    int a = clamp((int)p[pi == 0 ? P_P1_LSTART : P_P2_LSTART], 1, ls.len) - 1;
    int b = clamp((int)p[pi == 0 ? P_P1_LEND : P_P2_LEND], 1, ls.len) - 1;
    if (a > b) std::swap(a, b);
    ls.s = a; ls.e = b; ls.L = b - a + 1;
    return ls;
}

static uint32_t stepHash(long long n, int pi)
{
    uint32_t x = (uint32_t)(n * 2654435761u) ^ (uint32_t)(pi * 0x9E3779B9u) ^ 0x5bd1e995u;
    x ^= x >> 15; x *= 0x2c1b3c6du; x ^= x >> 12; x *= 0x297a2d39u; x ^= x >> 15;
    return x;
}

int performerStepAt(const float* p, int pi, long long n)
{
    const LoopShape ls = loopShape(p, pi);
    if (n < 0) n = 0;
    switch (ls.mode)
    {
        case 1: return ls.s + (int)(n % ls.L);
        case 2: return ls.e - (int)(n % ls.L);
        case 3:
        {
            if (ls.L < 2) return ls.s;
            const long long P = 2LL * ls.L - 2, m = n % P;
            return ls.s + (int)(m < ls.L ? m : P - m);
        }
        case 4: return ls.s + (int)(stepHash(n, pi) % (uint32_t)ls.L);
        case 5: return n <= ls.e ? (int)n : ls.s + (int)((n - ls.e - 1) % ls.L);
        default: return (int)(n % ls.len);
    }
}

float performerPatternValue(const float* p, int pi, double pos)
{
    const int base = pi == 0 ? P_P1_DIV : P_P2_DIV;
    const int len = clamp((int)p[base + 1], 1, kMaxSteps);
    pos = swingPos(std::max(pos, 0.0), p[pi == 0 ? P_P1_SWING : P_P2_SWING]);
    pos -= std::floor(pos / (double)len) * (double)len;
    const int k = std::min(len - 1, (int)pos);
    const float t = (float)(pos - k);
    const float v = p[base + 3 + k];
    const int c = (int)p[base + 19 + k];
    if (c == 3) return v + (p[base + 3 + (k + 1) % len] - v) * perfCurve(3, t);
    return v * perfCurve(c, t);
}

float performerValue(const float* p, int pi, double pos)
{
    const int base = pi == 0 ? P_P1_DIV : P_P2_DIV;
    const LoopShape ls = loopShape(p, pi);
    pos = swingPos(std::max(pos, 0.0), p[pi == 0 ? P_P1_SWING : P_P2_SWING]);
    const long long n = (long long)std::floor(pos);
    const float t = (float)(pos - (double)n);
    const int k = performerStepAt(p, pi, n);
    const float v = p[base + 3 + k];
    const int c = (int)p[base + 19 + k];
    // Fill: on the last pass of every 2 or 4, the last quarter of the pass rolls (x4)
    const int fill = (int)p[pi == 0 ? P_P1_FILL : P_P2_FILL];
    if (fill > 0 && c != 23)
    {
        const int every = fill == 1 ? 2 : 4;
        long long q = n;
        int cyc = ls.len;
        if (ls.mode == 5) { q = n - (ls.e + 1); cyc = ls.L; }              // passes start after the intro
        else if (ls.mode == 3) cyc = std::max(1, 2 * ls.L - 2);
        else if (ls.mode != 0) cyc = ls.L;
        if (q >= 0)
        {
            const long long pass = q / cyc, inPass = q % cyc;
            if ((int)(pass % every) == every - 1 && inPass >= cyc - std::max(1, cyc / 4)) return v * perfCurve(26, t);
        }
    }
    if (c == 3)                                                                 // Smooth: glide to the next step played
    {
        const float vNext = p[base + 3 + performerStepAt(p, pi, n + 1)];
        return v + (vNext - v) * perfCurve(3, t);
    }
    return v * perfCurve(c, t);
}
} // namespace helix
