#include "choppage/TransientDetector.h"
#include <complex>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bc::TransientDetector
{
    namespace
    {
        constexpr int kFftOrder = 10;
        constexpr int kFftSize  = 1 << kFftOrder;   // 1024
        constexpr int kHop      = 256;
        constexpr int kMedianHalf = 8;              // frames either side
        constexpr int kPeakHalf   = 3;
    }

    std::vector<Onset> findOnsets (const SampleData& buf, float sensitivity)
    {
        std::vector<Onset> onsets;
        const int n = buf.numSamples;
        if (n < kFftSize) return onsets;

        const double rate  = buf.sampleRate;

        // ---- 1. mono mix
        std::vector<float> mono ((size_t) n);
        const int chans = buf.channels;
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < chans; ++c) s += buf.data (c)[i];
            mono[(size_t) i] = s / (float) chans;
        }

        // ---- 2. spectral flux
        std::vector<std::complex<float>> cbuf ((size_t) kFftSize);
        std::vector<float> window ((size_t) kFftSize), frame ((size_t) kFftSize * 2), prev ((size_t) kFftSize / 2 + 1, 0.0f);
        for (int i = 0; i < kFftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos ((float) (2.0 * kPi) * (float) i / (float) kFftSize);

        const int numFrames = (n - kFftSize) / kHop + 1;
        const int bins = kFftSize / 2;
        // Two bands, judged separately so a kick under a ringing snare still
        // registers: lows (kicks, below ~250 Hz) and everything above.
        const int lowTop = clampv ((int) std::ceil (250.0 * kFftSize / rate), 2, bins);
        std::vector<float> fluxLow ((size_t) numFrames, 0.0f), fluxHigh ((size_t) numFrames, 0.0f);

        for (int t = 0; t < numFrames; ++t)
        {
            std::fill (frame.begin(), frame.end(), 0.0f);
            for (int i = 0; i < kFftSize; ++i)
                frame[(size_t) i] = mono[(size_t) (t * kHop + i)] * window[(size_t) i];
            for (int i = 0; i < kFftSize; ++i) cbuf[(size_t) i] = { frame[(size_t) i], 0.0f };
            fft (cbuf);
            for (int i = 0; i <= kFftSize / 2; ++i) frame[(size_t) i] = std::abs (cbuf[(size_t) i]);

            float lo = 0.0f, hi = 0.0f;
            for (int k = 1; k <= bins; ++k)
            {
                const float mag  = std::log1p (100.0f * frame[(size_t) k]);
                const float rise = std::max (0.0f, mag - prev[(size_t) k]);
                (k < lowTop ? lo : hi) += rise;
                prev[(size_t) k] = mag;
            }
            if (t > 0) { fluxLow[(size_t) t] = lo; fluxHigh[(size_t) t] = hi; }
        }

        // Normalise each band by its loudest onset, but never by less than a
        // fixed floor (an average rise of 1.5 log-units per bin): audio with
        // no real hits stays near zero instead of being scaled up.
        const float lowPeak  = *std::max_element (fluxLow.begin(), fluxLow.end());
        const float highPeak = *std::max_element (fluxHigh.begin(), fluxHigh.end());
        const float lowRef   = std::max (lowPeak,  1.5f * (float) (lowTop - 1));
        const float highRef  = std::max (highPeak, 1.5f * (float) (bins - lowTop + 1));
        if (std::getenv ("TB_DEBUG") != nullptr)
            fprintf (stderr, "    RAW low peak %.2f (ref %.2f, %d bins)  high peak %.2f (ref %.2f)\n",
                     lowPeak, lowRef, lowTop - 1, highPeak, highRef);

        for (int t = 0; t < numFrames; ++t)
        {
            fluxLow[(size_t) t]  /= lowRef;
            fluxHigh[(size_t) t] /= highRef;
        }

        // ---- 3. adaptive threshold per band: how far each frame rises above
        //         its local median. Hat noise in the highs can't hide a kick.
        const float delta = 0.30f - 0.27f * clampv (sensitivity, 0.0f, 1.0f);
        std::vector<float> score ((size_t) numFrames, 0.0f), win;
        win.reserve (kMedianHalf * 2 + 1);
        auto localMedian = [&] (const std::vector<float>& f, int t)
        {
            win.clear();
            for (int j = std::max (0, t - kMedianHalf); j <= std::min (numFrames - 1, t + kMedianHalf); ++j)
                win.push_back (f[(size_t) j]);
            std::nth_element (win.begin(), win.begin() + (std::ptrdiff_t) win.size() / 2, win.end());
            return win[win.size() / 2];
        };
        for (int t = 1; t < numFrames; ++t)
            score[(size_t) t] = std::max (fluxLow[(size_t) t]  - localMedian (fluxLow, t),
                                            fluxHigh[(size_t) t] - localMedian (fluxHigh, t));

        // Envelope for refinement: |x| with instant attack, ~5 ms release.
        std::vector<float> env ((size_t) n);
        {
            const float rel = std::exp (-1.0f / (float) (rate * 0.005));
            float e = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float a = std::abs (mono[(size_t) i]);
                e = a > e ? a : e * rel;
                env[(size_t) i] = e;
            }
        }
        auto rms = [&] (int from, int to)
        {
            from = clampv (from, 0, n); to = clampv (to, from, n);
            double acc = 0.0;
            for (int i = from; i < to; ++i) acc += (double) mono[(size_t) i] * mono[(size_t) i];
            return to > from ? (float) std::sqrt (acc / (to - from)) : 0.0f;
        };
        const int ms1 = std::max (1, (int) (rate * 0.001));

        // ---- 4. peak picking
        for (int t = 1; t < numFrames; ++t)
        {
            const float v = score[(size_t) t];
            if (v < delta) continue;

            bool isPeak = true;
            for (int j = std::max (0, t - kPeakHalf); j <= std::min (numFrames - 1, t + kPeakHalf) && isPeak; ++j)
                if (j != t && (score[(size_t) j] > v || (j < t && score[(size_t) j] >= v)))
                    isPeak = false;
            if (! isPeak) continue;

            // ---- 5. refine: steepest rise of the envelope near the frame centre
            const int centre = t * kHop + kFftSize / 2;
            const int a = std::max (16, centre - kFftSize);
            const int b = std::min (n - 1, centre + kFftSize / 2);
            float bestRise = 0.0f; int bestAt = centre;
            for (int i = a; i < b; ++i)
            {
                const float rise = env[(size_t) i] - env[(size_t) i - 16];
                if (rise > bestRise) { bestRise = rise; bestAt = i; }
            }
            // Where the attack begins: walk back while the envelope is still rising.
            int attack = bestAt;
            const float base = env[(size_t) std::max (0, bestAt - 16)];
            while (attack > a && env[(size_t) attack - 1] > base + 0.1f * bestRise) --attack;
            attack = std::max (0, attack - 16);

            // Start ~1 ms before the attack, on a zero crossing if there's one
            // in that gap, so the hit is never softened by the start fade.
            int onset = std::max (0, attack - ms1);
            for (int i = attack; i > std::max (1, attack - 2 * ms1); --i)
                if ((mono[(size_t) i] >= 0.0f) != (mono[(size_t) i - 1] >= 0.0f)) { onset = i; break; }

            // Rank by how much louder it gets: kicks / snares outrank quiet ghost hats.
            const float strength = std::max (0.0f, rms (attack, attack + (int) (rate * 0.02))
                                                     - rms (attack - (int) (rate * 0.02), attack)) + 0.05f * v;

            const int minGap = (int) (rate * 0.040);
            if (! onsets.empty() && onset - onsets.back().position < minGap)
            {
                if (strength > onsets.back().strength) onsets.back() = { onset, strength };
                continue;
            }
            onsets.push_back ({ onset, strength });
        }

        return onsets;
    }

    SliceTable detect (const SampleData& buf, float sensitivity, int maxSlices)
    {
        const int n = buf.numSamples;
        maxSlices = clampv (maxSlices, 1, SliceTable::kMax);

        auto onsets = findOnsets (buf, sensitivity);

        // The sample start is always slice 1; an onset within 20 ms of it merges in.
        const int nearStart = (int) (buf.sampleRate * 0.020);
        onsets.erase (std::remove_if (onsets.begin(), onsets.end(),
                                      [&] (const Onset& o) { return o.position < nearStart; }), onsets.end());

        std::sort (onsets.begin(), onsets.end(), [] (const Onset& x, const Onset& y) { return x.strength > y.strength; });
        if ((int) onsets.size() > maxSlices - 1) onsets.resize ((size_t) maxSlices - 1);

        SliceTable t;
        t.starts[0] = 0;
        t.count = 1;
        for (const auto& o : onsets) t.starts[(size_t) t.count++] = o.position;
        t.normalise (n);
        return t;
    }
}
