// Paula's Revenge - old-sampler cyclic time stretch / pitch shift
//
//  * Output is made of cycles of a fixed length (CYCLE, converted from 44.1 kHz
//    samples to the material's own rate).
//  * Each cycle starts reading the source where the output "is" in source time
//    (output position / time factor), and plays at the pitch ratio.
//  * CLASSIC (matches the S3000XL recording): the start is moved by up to 10% of
//    a cycle to where the source's level and slope best continue what the
//    previous cycle was about to play, then a 3%-of-a-cycle blend.
//  * REVISED: start lined up by waveform correlation (within 5%) and a longer
//    30% blend: smoother, the joins nearly vanish.
//  * HARD: no line-up and no blend; every join can click.
#include "TimeStretch.h"
#include <algorithm>
#include <cmath>

namespace prv
{
int cycleSamples (const StretchSettings& s)
{
    const double c = std::clamp (s.cycleLength, 4, 1 << 16) * std::max (1000.0, s.sampleRate) / 44100.0;
    return std::max (4, (int) std::lround (c));
}

std::vector<std::vector<float>> cyclicStretch (const std::vector<std::vector<float>>& ch, const StretchSettings& s)
{
    std::vector<std::vector<float>> out (ch.size());
    if (ch.empty() || ch[0].empty()) return out;
    const size_t inLen = ch[0].size();
    const double T = std::clamp (s.timeFactor, 0.05, 40.0);
    const double ratio = std::pow (2.0, (s.semitones + s.cents / 100.0) / 12.0);
    const int C = cycleSamples (s);
    const size_t outLen = (size_t) std::max (1.0, std::round ((double) inLen * T));
    for (auto& o : out) o.assign (outLen, 0.0f);

    auto readCh = [&] (const std::vector<float>& d, double pos) -> float
    {
        if (pos < 0.0) return 0.0f;
        const size_t a = (size_t) pos;
        if (a + 1 >= d.size()) return a < d.size() ? d[a] : 0.0f;
        const float f = (float) (pos - (double) a);
        return d[a] + (d[a + 1] - d[a]) * f;
    };
    // mono "guide" for lining joins up
    auto guide = [&] (double pos) -> float
    {
        float v = 0.0f;
        for (const auto& d : ch) v += readCh (d, pos);
        return v / (float) ch.size();
    };

    int searchRange = 0, fade = 0;
    switch (s.mode)
    {
        case StretchSettings::Mode::Classic: searchRange = std::max (1, C / 10); fade = std::max (1, (int) std::lround (C * 0.03)); break;
        case StretchSettings::Mode::Revised: searchRange = std::max (1, C / 20); fade = std::max (2, (int) std::lround (C * 0.30)); break;
        case StretchSettings::Mode::Hard:    break;
    }

    double prevNext = -1.0;   // where the previous cycle would have continued
    for (size_t o = 0; o < outLen; o += (size_t) C)
    {
        const double ideal = (double) o / T;
        double start = ideal;
        if (prevNext >= 0.0 && searchRange > 0)
        {
            if (s.mode == StretchSettings::Mode::Classic)
            {
                // level + slope match at the splice point
                const float v0 = guide (prevNext), s0 = guide (prevNext + ratio) - v0;
                double best = 1e30;
                for (int d = -searchRange; d <= searchRange; ++d)
                {
                    const double p = ideal + d;
                    if (p < 0) continue;
                    const float v1 = guide (p), s1 = guide (p + ratio) - v1;
                    const double cost = std::abs (v1 - v0) + 2.0 * std::abs (s1 - s0);
                    if (cost < best) { best = cost; start = p; }
                }
            }
            else
            {
                // waveform correlation over the blend region
                const int W = std::max (32, fade);
                double best = -1e30;
                for (int d = -searchRange; d <= searchRange; ++d)
                {
                    const double p = ideal + d;
                    if (p < 0) continue;
                    double xy = 0, xx = 0, yy = 0;
                    for (int j = 0; j < W; j += 2)
                    {
                        const double a = guide (prevNext + j * ratio), b = guide (p + j * ratio);
                        xy += a * b; xx += a * a; yy += b * b;
                    }
                    const double c = xy / (std::sqrt (xx * yy) + 1e-12);
                    if (c > best) { best = c; start = p; }
                }
            }
        }
        const size_t n = std::min ((size_t) C, outLen - o);
        for (size_t c = 0; c < ch.size(); ++c)
        {
            for (size_t j = 0; j < n; ++j)
            {
                float v = readCh (ch[c], start + (double) j * ratio);
                if (prevNext >= 0.0 && (int) j < fade)
                {
                    const float w = (float) (j + 1) / (float) (fade + 1);
                    v = v * w + readCh (ch[c], prevNext + (double) j * ratio) * (1.0f - w);
                }
                out[c][o + j] = v;
            }
        }
        prevNext = start + (double) n * ratio;
    }
    return out;
}
} // namespace prv
