#include "choppage/Akaizer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace akaizer
{
    long long roundHalfEven (double x) noexcept
    {
        const double f = std::floor (x);
        const double d = x - f;
        auto fi = (long long) f;
        if (d > 0.5) return fi + 1;
        if (d < 0.5) return fi;
        return (fi % 2 == 0) ? fi : fi + 1;
    }

    int linkedFrequency (int cycleLength) noexcept
    {
        if (cycleLength <= 0) return 0;
        return (int) roundHalfEven (41000.0 / (double) cycleLength);
    }

    Result stretch (const int16_t* in, size_t numWords, int numChannels,
                    double sampleRate, const Settings& s)
    {
        Result r;
        if (in == nullptr || numWords == 0 || numChannels <= 0 || sampleRate <= 0.0
            || s.cycleLength <= 0 || s.cycleFrequency <= 0 || s.stretchPercent <= 0.0)
            return r;

        // The literal typed into the VB source; kept for bit-faithfulness.
        constexpr double piVB = 3.141592654;

        const auto nIn  = (long long) numWords;
        const double factor = s.stretchPercent / 100.0;
        const long long nOut = roundHalfEven ((double) nIn * factor);
        const long long L = (long long) s.cycleLength * 2 * numChannels;
        const long long C = roundHalfEven ((double) numChannels * sampleRate / (double) s.cycleFrequency);

        r.grainWords = L;
        r.outHopWords = C;
        if (nOut <= 0 || C <= 0)
            return r;

        r.samples.assign ((size_t) nOut, 0);
        const long long S = roundHalfEven ((double) nIn / ((double) nOut / (double) C));
        r.inHopWords = S;

        const long long kMax = roundHalfEven ((double) nOut / (double) C - 1.0);
        const long long ubIn = nIn - 1, ubOut = nOut - 1;
        int16_t* out = r.samples.data();

        // Same values the original computes per sample, worked out once.
        std::vector<double> window ((size_t) L);
        for (long long j = 0; j < L; ++j)
            window[(size_t) j] = std::sin ((double) (j + 1) * piVB / (double) L);

        for (long long k = 0; k <= kMax; ++k)
        {
            const long long ib = S * k, ob = C * k;
            for (long long j = 0; j <= L - 1; ++j)
            {
                if (ib + j > ubIn)  break;
                if (ob + j > ubOut) break;
                const double g = window[(size_t) j];
                long long v = roundHalfEven ((double) in[ib + j] * g + (double) out[ob + j]);
                if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
                out[ob + j] = (int16_t) v;
            }
        }
        return r;
    }

    std::vector<int16_t> repitch (const int16_t* in, size_t numWords, int numChannels, double semitones)
    {
        if (in == nullptr || numWords == 0 || numChannels <= 0)
            return {};
        if (std::abs (semitones) < 1.0e-9)
            return std::vector<int16_t> (in, in + numWords);

        const double ratio = std::pow (2.0, semitones / 12.0);
        const auto frames = (long long) (numWords / (size_t) numChannels);
        const auto outFrames = std::max<long long> (1, (long long) std::llround ((double) frames / ratio));
        std::vector<int16_t> out ((size_t) (outFrames * numChannels));

        auto at = [&] (long long f, int c) -> double
        {
            f = std::clamp<long long> (f, 0, frames - 1);
            return (double) in[f * numChannels + c];
        };

        for (long long i = 0; i < outFrames; ++i)
        {
            const double pos = (double) i * ratio;
            const auto i0 = (long long) std::floor (pos);
            const double t = pos - (double) i0;
            for (int c = 0; c < numChannels; ++c)
            {
                // 4-point, 3rd-order Hermite
                const double xm1 = at (i0 - 1, c), x0 = at (i0, c), x1 = at (i0 + 1, c), x2 = at (i0 + 2, c);
                const double c1 = 0.5 * (x1 - xm1);
                const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
                const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
                double v = ((c3 * t + c2) * t + c1) * t + x0;
                v = std::clamp (std::round (v), -32768.0, 32767.0);
                out[(size_t) (i * numChannels + c)] = (int16_t) v;
            }
        }
        return out;
    }

    Result render (const int16_t* in, size_t numWords, int numChannels,
                   double sampleRate, const Settings& s, double semitones)
    {
        if (std::abs (semitones) < 1.0e-9)
            return stretch (in, numWords, numChannels, sampleRate, s);

        const auto pitched = repitch (in, numWords, numChannels, semitones);
        Settings adjusted = s;
        // Keep the final length = original length x stretch%.
        adjusted.stretchPercent = s.stretchPercent * ((double) numWords / (double) pitched.size());
        return stretch (pitched.data(), pitched.size(), numChannels, sampleRate, adjusted);
    }

    std::vector<uint8_t> makeWav (const std::vector<int16_t>& pcm, int numChannels, int sampleRate)
    {
        const auto dataBytes = (uint32_t) (pcm.size() * 2);
        std::vector<uint8_t> b (44 + (size_t) dataBytes);
        auto put4 = [&] (size_t o, const char* s) { std::memcpy (b.data() + o, s, 4); };
        auto u32 = [&] (size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) b[o + (size_t) i] = (uint8_t) (v >> (8 * i)); };
        auto u16 = [&] (size_t o, uint16_t v) { b[o] = (uint8_t) v; b[o + 1] = (uint8_t) (v >> 8); };
        put4 (0, "RIFF"); u32 (4, dataBytes + 36); put4 (8, "WAVE");
        put4 (12, "fmt "); u32 (16, 16); u16 (20, 1); u16 (22, (uint16_t) numChannels);
        u32 (24, (uint32_t) sampleRate); u32 (28, (uint32_t) (sampleRate * numChannels * 2));
        u16 (32, (uint16_t) (numChannels * 2)); u16 (34, 16);
        put4 (36, "data"); u32 (40, dataBytes);
        for (size_t i = 0; i < pcm.size(); ++i)
            u16 (44 + i * 2, (uint16_t) pcm[i]);
        return b;
    }
}
