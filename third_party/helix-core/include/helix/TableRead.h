// Inner-loop wavetable reads: frame morph x linear interpolation x mip-level crossfade.
#pragma once
#include "helix/Wavetable.h"
#include "helix/Math.h"
#include <cstdint>

namespace helix {

// Which two mip levels to read for a phase increment (cycles per sample, already scaled by any warp speed-up),
// and how much of the higher (duller) one to mix in. Both levels are alias free: level `lo` keeps harmonics up to
// Nyquist at this pitch; `hi` keeps one octave fewer. Crossfading by the fractional part makes the switch smooth.
struct MipSel
{
    int lo = 0, hi = 0;
    float mix = 0.0f;   // 0 = all lo, 1 = all hi
};

inline MipSel selectMip(float inc, bool crossfade)
{
    MipSel s;
    inc = std::fabs(inc);
    // Need (1024 >> L) * inc <= 0.5  <=>  L >= log2(2048 * inc)
    const float x = std::log2(std::max(inc * 2048.0f, 1e-9f));
    if (x <= 0.0f) { s.lo = s.hi = 0; s.mix = 0.0f; return s; }
    const float c = std::ceil(x);
    s.lo = std::min((int)c, kNumMipLevels - 1);
    s.hi = std::min(s.lo + 1, kNumMipLevels - 1);
    s.mix = crossfade ? (x - (c - 1.0f)) : 0.0f;    // 0 just after a level switch, -> 1 just before the next
    if (!crossfade) s.hi = s.lo;
    return s;
}

// One read of a single mip level with frame morph. phase in [0,1), frames f0/f1 with morph t.
inline float readLevel(const Wavetable& wt, int level, int f0, int f1, float t, float phase)
{
    const int n = wt.levelSize(level);
    const float idx = phase * (float)n;
    int i = (int)idx;
    const float fr = idx - (float)i;
    i &= (n - 1);
    const float* a = wt.frame(level, f0);
    const float* b = wt.frame(level, f1);
    const float sa = a[i] + (a[i + 1] - a[i]) * fr;
    const float sb = b[i] + (b[i + 1] - b[i]) * fr;
    return sa + (sb - sa) * t;
}

inline float readTable(const Wavetable& wt, const MipSel& m, int f0, int f1, float t, float phase)
{
    const float lo = readLevel(wt, m.lo, f0, f1, t, phase);
    if (m.mix <= 0.0f || m.hi == m.lo) return lo;
    const float hi = readLevel(wt, m.hi, f0, f1, t, phase);
    return lo + (hi - lo) * m.mix;
}

// Position 0..1 -> two frames and a morph amount.
inline void framePos(const Wavetable& wt, float pos, int& f0, int& f1, float& t)
{
    const float x = clamp(pos, 0.0f, 1.0f) * (float)(wt.numFrames() - 1);
    f0 = (int)x;
    if (f0 >= wt.numFrames() - 1) { f0 = f1 = wt.numFrames() - 1; t = 0.0f; return; }
    f1 = f0 + 1;
    t = x - (float)f0;
}

} // namespace helix
