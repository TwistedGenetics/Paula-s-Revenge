// Paula's Revenge - old-sampler cyclic time stretch / pitch shift
//
//  * Output is made of cycles of a fixed length (CYCLE, converted from 44.1 kHz
//    samples to the material's own rate).
//  * Each cycle starts reading the source where the output "is" in source time
//    (output position / time factor), and plays at the pitch ratio.
//  * CLASSIC (measured from real hardware recordings): no line-up; each cycle
//    fades in linearly over 2/3 of a cycle while the previous one carries on.
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

// ---------------------------------------------------------------- AKAIZER
int akaizerRound (double x)
{
    const double f = std::floor (x), d = x - f;
    if (d > 0.5) return (int) f + 1;
    if (d < 0.5) return (int) f;
    return std::fmod (f, 2.0) == 0.0 ? (int) f : (int) f + 1;
}

int akaizerLinkedFreq (int cycleLength) { return akaizerRound (41000.0 / std::max (1, cycleLength)); }

std::vector<int16_t> akaizerStretch (const std::vector<int16_t>& input, int channels, double sampleRate,
                                     double stretchPercent, int cycleLength, int cycleFreq)
{
    constexpr double kPi = 3.141592654;   // the literal the original uses
    const int nIn = (int) input.size();
    const double factor = stretchPercent / 100.0;
    const int nOut = akaizerRound ((double) nIn * factor);
    const int L = cycleLength * 2 * channels;                                  // grain length (words)
    const int C = akaizerRound ((double) channels * sampleRate / std::max (1, cycleFreq));   // output hop
    std::vector<int16_t> out ((size_t) std::max (nOut, 0), 0);
    if (nOut <= 0 || C <= 0 || L <= 0) return out;
    const int S = akaizerRound ((double) nIn / ((double) nOut / C));            // input hop
    const int kMax = akaizerRound ((double) nOut / C - 1.0);
    const int ubIn = nIn - 1, ubOut = nOut - 1;
    for (int k = 0; k <= kMax; ++k)
    {
        const long long ib = (long long) S * k, ob = (long long) C * k;
        for (int j = 0; j <= L - 1; ++j)
        {
            if (ib + j > ubIn) break;
            if (ob + j > ubOut) break;
            const double g = std::sin (((double) (j + 1) * kPi) / (double) L);
            int v = akaizerRound ((double) input[(size_t) (ib + j)] * g + (double) out[(size_t) (ob + j)]);
            v = std::clamp (v, -32768, 32767);
            out[(size_t) (ob + j)] = (int16_t) v;
        }
    }
    return out;
}

namespace
{
    // AKAIZER for the editor: optional pitch first (a plain resample, the jungle
    // "pitch up then stretch back" trick), then the grain stretch to the length asked for
    std::vector<std::vector<float>> akaizerChannels (const std::vector<std::vector<float>>& ch, const StretchSettings& s)
    {
        const double ratio = std::pow (2.0, (s.semitones + s.cents / 100.0) / 12.0);
        std::vector<std::vector<int16_t>> src;
        for (const auto& c : ch)
        {
            std::vector<int16_t> v;
            if (std::abs (ratio - 1.0) < 1e-9)
            {
                v.resize (c.size());
                for (size_t i = 0; i < c.size(); ++i) v[i] = (int16_t) std::clamp ((int) std::lround (c[i] * 32768.0f), -32768, 32767);
            }
            else
            {
                const size_t n = (size_t) std::max (1.0, std::floor ((double) c.size() / ratio));
                v.resize (n);
                for (size_t i = 0; i < n; ++i)
                {
                    const double pos = (double) i * ratio;
                    const size_t a = (size_t) pos;
                    const double f = pos - (double) a;
                    const double x = a + 1 < c.size() ? c[a] + (c[a + 1] - c[a]) * f : (a < c.size() ? c[a] : 0.0);
                    v[i] = (int16_t) std::clamp ((int) std::lround (x * 32768.0), -32768, 32767);
                }
            }
            src.push_back (std::move (v));
        }
        // the length asked for is relative to the original, so allow for the pitch change
        const double pct = std::clamp (s.timeFactor, 0.05, 40.0) * ratio * 100.0;
        const int cycle = std::clamp (s.cycleLength, 2, 1 << 15);
        const int freq = s.cycleFreq > 0 ? s.cycleFreq : akaizerLinkedFreq (cycle);
        const int rate = (int) std::lround (s.sampleRate);
        std::vector<std::vector<float>> out (ch.size());
        auto toFloat = [] (const std::vector<int16_t>& v, size_t start, size_t step, size_t n)
        {
            std::vector<float> f (n);
            for (size_t i = 0; i < n; ++i) f[i] = v[start + i * step] / 32768.0f;
            return f;
        };
        if (src.size() == 2 && s.stereoQuirk)
        {
            std::vector<int16_t> inter (src[0].size() * 2);
            for (size_t i = 0; i < src[0].size(); ++i) { inter[i * 2] = src[0][i]; inter[i * 2 + 1] = i < src[1].size() ? src[1][i] : 0; }
            const auto r = akaizerStretch (inter, 2, rate, pct, cycle, freq);
            const size_t frames = r.size() / 2;
            out[0] = toFloat (r, 0, 2, frames);
            out[1] = toFloat (r, 1, 2, frames);
        }
        else
        {
            for (size_t c = 0; c < src.size(); ++c)
            {
                const auto r = akaizerStretch (src[c], 1, rate, pct, cycle, freq);
                out[c] = toFloat (r, 0, 1, r.size());
            }
            const size_t n = out[0].size();
            for (auto& o : out) o.resize (n, 0.0f);
        }
        return out;
    }
}

std::vector<std::vector<float>> cyclicStretch (const std::vector<std::vector<float>>& ch, const StretchSettings& s)
{
    std::vector<std::vector<float>> out (ch.size());
    if (ch.empty() || ch[0].empty()) return out;
    if (s.mode == StretchSettings::Mode::Akaizer) return akaizerChannels (ch, s);
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
        case StretchSettings::Mode::Classic: fade = std::max (1, (int) std::lround (C * 2.0 / 3.0)); break;
        case StretchSettings::Mode::Revised: searchRange = std::max (1, C / 20); fade = std::max (2, (int) std::lround (C * 0.30)); break;
        case StretchSettings::Mode::Hard:    break;
        case StretchSettings::Mode::Akaizer: break;   // (handled above)
    }

    double prevNext = -1.0;   // where the previous cycle would have continued
    for (size_t o = 0; o < outLen; o += (size_t) C)
    {
        const double ideal = (double) o / T;
        double start = ideal;
        if (prevNext >= 0.0 && searchRange > 0)   // REVISED: line the join up by waveform shape
        {
            {
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
