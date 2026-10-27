// Paula's Revenge - old-sampler cyclic time stretch / pitch shift
//
// How it works (our reconstruction; the original firmware isn't public):
//  * The output is built from cycles of a fixed length (in output samples).
//  * Each cycle starts reading the source where the output "is" in source
//    time (output position / time factor) and plays forward at the pitch
//    ratio, with linear interpolation.
//  * CLASSIC: cycles are butted together with no crossfade. Pitch is exact;
//    timing jitters by up to a cycle, and every join can click: that's the
//    raw Akai sound.
//  * REVISED: each cycle's start is nudged to the nearest rising zero
//    crossing (within 1/8 of a cycle) and the joins get a very short fade,
//    which keeps timing tight and the sound a little cleaner, at the cost of
//    slight pitch wander.
#include "TimeStretch.h"
#include <algorithm>
#include <cmath>

namespace prv
{
std::vector<std::vector<float>> cyclicStretch (const std::vector<std::vector<float>>& ch, const StretchSettings& s)
{
    std::vector<std::vector<float>> out (ch.size());
    if (ch.empty() || ch[0].empty()) return out;
    const size_t inLen = ch[0].size();
    const double T = std::clamp (s.timeFactor, 0.05, 40.0);
    const double ratio = std::pow (2.0, (s.semitones + s.cents / 100.0) / 12.0);
    const int C = std::clamp (s.cycleLength, 8, 1 << 16);
    const size_t outLen = (size_t) std::max (1.0, std::round ((double) inLen * T));
    for (auto& o : out) o.assign (outLen, 0.0f);

    auto read = [&] (const std::vector<float>& d, double pos) -> float
    {
        if (pos < 0.0) return 0.0f;
        const size_t a = (size_t) pos;
        if (a >= d.size()) return 0.0f;
        const size_t b = std::min (d.size() - 1, a + 1);
        const float f = (float) (pos - (double) a);
        return d[a] + (d[b] - d[a]) * f;
    };
    // mono "guide" signal for zero-crossing search
    auto guide = [&] (size_t i) -> float
    {
        float v = 0.0f;
        for (const auto& d : ch) v += i < d.size() ? d[i] : 0.0f;
        return v;
    };

    const bool revised = s.mode == StretchSettings::Mode::Revised;
    const int fade = revised ? std::max (2, std::min (64, C / 16)) : 0;
    std::vector<double> prevEnd (ch.size(), 0.0);   // where the previous cycle would have continued
    bool havePrev = false;

    for (size_t o = 0; o < outLen; o += (size_t) C)
    {
        double start = (double) o / T;
        if (revised && start > 1.0)
        {
            // snap to the nearest rising zero crossing near the ideal start
            const int win = std::max (1, C / 8);
            const long s0 = (long) start;
            long best = -1; long bestDist = win + 1;
            for (long d = -win; d <= win; ++d)
            {
                const long i = s0 + d;
                if (i < 1 || (size_t) i >= inLen) continue;
                if (guide ((size_t) i - 1) <= 0.0f && guide ((size_t) i) > 0.0f && std::abs (d) < bestDist) { best = i; bestDist = std::abs (d); }
            }
            if (best >= 0) start = (double) best;
        }
        const size_t n = std::min ((size_t) C, outLen - o);
        for (size_t c = 0; c < ch.size(); ++c)
        {
            for (size_t j = 0; j < n; ++j)
            {
                float v = read (ch[c], start + (double) j * ratio);
                if (revised && havePrev && (int) j < fade)
                {
                    // short fade from where the last cycle was heading into this one
                    const float w = (float) (j + 1) / (float) (fade + 1);
                    v = v * w + read (ch[c], prevEnd[c] + (double) j * ratio) * (1.0f - w);
                }
                out[c][o + j] = v;
            }
            prevEnd[c] = start + (double) n * ratio;
        }
        havePrev = true;
    }
    return out;
}
} // namespace prv
