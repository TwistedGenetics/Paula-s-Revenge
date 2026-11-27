#include "choppage/TransientDetector.h"
#include <complex>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bc::TransientDetector
{

    // 0.15: rebuilt and tuned on a bench of 60 synthetic breaks with known hit starts (kick / snare / ghost / hat /
    // open hat, humanised, flams, room, noise, half of them "sampled": low-passed, 12-bit). Against the old
    // detector: F-measure 0.90 -> 0.94 and the start of each hit 0.98 ms -> 0.07 ms off (median), 51% -> 97%
    // within 1 ms.
    namespace
    {
        // A radix-2 FFT of N real samples (re in, im zero) to magnitudes, with its tables made once per size.
        struct FastFft
        {
            int n = 0;
            std::vector<int> rev;
            std::vector<float> cosT, sinT, re, im;
            explicit FastFft (int size) : n (size), rev ((size_t) size), cosT ((size_t) size / 2), sinT ((size_t) size / 2), re ((size_t) size), im ((size_t) size)
            {
                int bitsN = 0; while ((1 << bitsN) < n) ++bitsN;
                for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bitsN; ++b) if (i & (1 << b)) r |= 1 << (bitsN - 1 - b); rev[(size_t) i] = r; }
                for (int k = 0; k < n / 2; ++k) { cosT[(size_t) k] = (float) std::cos (2.0 * kPi * k / n); sinT[(size_t) k] = (float) -std::sin (2.0 * kPi * k / n); }
            }
            // in: n samples; out: magnitudes of bins 0 .. n/2
            void magnitudes (const float* in, float* mag)
            {
                for (int i = 0; i < n; ++i) { re[(size_t) rev[(size_t) i]] = in[i]; im[(size_t) rev[(size_t) i]] = 0.0f; }
                for (int len = 2; len <= n; len <<= 1)
                {
                    const int half = len / 2, step = n / len;
                    for (int i = 0; i < n; i += len)
                        for (int k = 0; k < half; ++k)
                        {
                            const float c = cosT[(size_t) (k * step)], s = sinT[(size_t) (k * step)];
                            const int p = i + k, q = p + half;
                            const float vr = re[(size_t) q] * c - im[(size_t) q] * s, vi = re[(size_t) q] * s + im[(size_t) q] * c;
                            re[(size_t) q] = re[(size_t) p] - vr; im[(size_t) q] = im[(size_t) p] - vi;
                            re[(size_t) p] += vr; im[(size_t) p] += vi;
                        }
                }
                for (int k = 0; k <= n / 2; ++k) mag[k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
            }
        };
    }

    std::vector<Onset> findOnsets (const SampleData& buf, float sensitivity)
    {
        std::vector<Onset> out;
        const int n = buf.numSamples;
        const double rate = buf.sampleRate;
        constexpr int N = 1024, H = 128, kLag = 2, kPreMax = 3, kPostMax = 3, kPreAvg = 24, kPostAvg = 3, kBands = 24;
        if (n < N || rate <= 0) return out;

        // ---- 1. mono, and a high-passed copy (two 1-pole high-passes at 900 Hz) for snares, ghosts and hats
        std::vector<float> x ((size_t) n), hp ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < buf.channels; ++c) s += buf.data (c)[i];
            x[(size_t) i] = s / (float) std::max (1, buf.channels);
        }
        {
            const float a = std::exp (-2.0f * (float) kPi * 900.0f / (float) rate);
            float y1 = 0, x1 = 0, y2 = 0, z1 = 0;
            for (int i = 0; i < n; ++i)
            {
                y1 = a * (y1 + x[(size_t) i] - x1); x1 = x[(size_t) i];
                y2 = a * (y2 + y1 - z1); z1 = y1;
                hp[(size_t) i] = y2;
            }
        }

        // ---- 2. a 24-band log-spaced spectrogram (40 Hz - 16 kHz), frames centred every 128 samples (2.9 ms)
        const int bins = N / 2;
        const int frames = (n + H - 1) / H + 1;
        std::array<int, kBands + 1> edge {};
        for (int k = 0; k <= kBands; ++k)
            edge[(size_t) k] = clampv ((int) std::lround (40.0 * std::pow (400.0, (double) k / kBands) * N / rate), 1, bins);
        for (int k = 1; k <= kBands; ++k) if (edge[(size_t) k] <= edge[(size_t) k - 1]) edge[(size_t) k] = std::min (bins, edge[(size_t) k - 1] + 1);
        std::vector<float> win ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * (float) i / (float) N);
        std::vector<float> S ((size_t) frames * kBands, 0.0f);
        FastFft fftN (N);
        std::vector<float> frame ((size_t) N), mag ((size_t) N / 2 + 1);
        for (int t = 0; t < frames; ++t)
        {
            const int s0 = t * H - N / 2;
            for (int i = 0; i < N; ++i)
            {
                const int j = s0 + i;
                frame[(size_t) i] = (j >= 0 && j < n ? x[(size_t) j] : 0.0f) * win[(size_t) i];
            }
            fftN.magnitudes (frame.data(), mag.data());
            for (int k = 0; k < kBands; ++k)
            {
                float e = 0.0f;
                for (int q = edge[(size_t) k]; q < edge[(size_t) k + 1]; ++q) e += mag[(size_t) q];
                S[(size_t) (t * kBands + k)] = std::log1p (10.0f * e / (float) std::max (1, edge[(size_t) k + 1] - edge[(size_t) k]));
            }
        }

        // ---- 3. SuperFlux in three groups (lows < 200 Hz: kicks; mids < 3 kHz: snare bodies; highs: snares, hats):
        //         how much each band rose against the louder of it and its neighbours two frames back (so a
        //         ringing or sliding sound is not a new hit)
        int g1 = 0, g2 = 0;
        for (int k = 0; k < kBands; ++k)
        {
            const double f = 40.0 * std::pow (400.0, (double) k / kBands);
            if (f < 200.0) g1 = k + 1;
            if (f < 3000.0) g2 = k + 1;
        }
        std::vector<float> odf[3];
        for (auto& o : odf) o.assign ((size_t) frames, 0.0f);
        for (int t = kLag; t < frames; ++t)
            for (int k = 0; k < kBands; ++k)
            {
                const float* p = &S[(size_t) ((t - kLag) * kBands)];
                float ref = p[k];
                if (k > 0) ref = std::max (ref, p[k - 1]);
                if (k + 1 < kBands) ref = std::max (ref, p[k + 1]);
                const float d = std::max (0.0f, S[(size_t) (t * kBands + k)] - ref);
                odf[k < g1 ? 0 : k < g2 ? 1 : 2][(size_t) t] += d;
            }
        // ... plus a short-window (6 ms) energy rise of the high-passed signal: abrupt noise bursts (ghost notes,
        // hats) that 23 ms frames smear into the tail before them
        std::vector<float> burst ((size_t) frames, 0.0f);
        {
            std::vector<double> L ((size_t) frames);
            for (int t = 0; t < frames; ++t)
            {
                double e = 0.0;
                for (int i = t * H; i < t * H + 256; ++i) if (i < n) e += (double) hp[(size_t) i] * hp[(size_t) i];
                L[(size_t) t] = std::log10 (e / 256.0 + 1.0e-8);
            }
            for (int t = 2; t < frames; ++t)
                burst[(size_t) t] = (float) std::max (0.0, L[(size_t) t] - std::max (L[(size_t) t - 1], L[(size_t) t - 2]));
        }
        // one curve: each group normalised by its big hits (98th percentile, with a floor so silence stays quiet),
        // weighted (lows 0.3, mids 0.3, highs 0.5, bursts 0.4) and summed; `which` = the group that rose most
        std::vector<float> curve ((size_t) frames, 0.0f), top ((size_t) frames, 0.0f);
        std::vector<int> which ((size_t) frames, 0);
        for (int t = 0; t < frames; ++t) { curve[(size_t) t] = 0.4f * burst[(size_t) t]; top[(size_t) t] = curve[(size_t) t]; which[(size_t) t] = 2; }
        const int groupBands[3] = { g1, g2 - g1, kBands - g2 };
        const float weight[3] = { 0.3f, 0.3f, 0.5f };
        for (int g = 0; g < 3; ++g)
        {
            std::vector<float> v = odf[g];
            std::sort (v.begin(), v.end());
            const float ref = std::max (v[(size_t) ((double) v.size() * 0.98)], 0.25f * (float) std::max (1, groupBands[g]));
            for (int t = 0; t < frames; ++t)
            {
                const float y = weight[g] * odf[g][(size_t) t] / ref;
                curve[(size_t) t] += y;
                if (y > top[(size_t) t]) { top[(size_t) t] = y; which[(size_t) t] = g; }
            }
        }

        // ---- 4. peaks: the highest within +-9 ms, above the local average (70 ms before, 9 ms after) by delta
        const float delta = std::max (0.02f, 0.22f - 0.25f * clampv (sensitivity, 0.0f, 1.0f));
        std::vector<int> peaks;
        for (int t = 1; t < frames; ++t)
        {
            const float v = curve[(size_t) t];
            if (v < delta) continue;
            bool ok = true;
            for (int j = std::max (0, t - kPreMax); j <= std::min (frames - 1, t + kPostMax) && ok; ++j)
                if (j != t && (curve[(size_t) j] > v || (j < t && curve[(size_t) j] == v))) ok = false;
            if (! ok) continue;
            double m = 0.0; int cnt = 0;
            for (int j = std::max (0, t - kPreAvg); j <= std::min (frames - 1, t + kPostAvg); ++j) { m += curve[(size_t) j]; ++cnt; }
            if (v < m / cnt + delta) continue;
            peaks.push_back (t);
        }

        // ---- 5. each peak to the sample its hit starts on
        std::vector<double> P ((size_t) n + 1, 0.0), Q ((size_t) n + 1, 0.0);
        for (int i = 0; i < n; ++i)
        {
            P[(size_t) i + 1] = P[(size_t) i] + (double) x[(size_t) i] * x[(size_t) i];
            Q[(size_t) i + 1] = Q[(size_t) i] + (double) hp[(size_t) i] * hp[(size_t) i];
        }
        const int W = std::max (16, (int) (rate * 0.0015));
        const int w8 = (int) (rate * 0.008);
        for (int t : peaks)
        {
            const bool high = which[(size_t) t] > 0;              // snare / hat: judged on the high-passed signal
            const auto& E = high ? Q : P;
            const auto& sig = high ? hp : x;
            auto e = [&] (const std::vector<double>& EE, int from, int to)
            {
                from = clampv (from, 0, n); to = clampv (to, from, n);
                return EE[(size_t) to] - EE[(size_t) from];
            };
            // where 1.5 ms after most outweighs 1.5 ms before, from 6 ms before the frame to 12 ms after
            const int centre = t * H;
            const int a = std::max (3 * W, centre - 256), b = std::min (n - W, centre + 512);
            int best = -1; double bestR = 0.0;
            for (int i = a; i < b; i += 4)
            {
                const double after = e (E, i, i + W), before = e (E, i - W, i);
                const double r = after / (before + 1.0e-7 * W) * std::sqrt (after);
                if (r > bestR) { bestR = r; best = i; }
            }
            if (best < 0) continue;
            // the first sample that stands out from what was there before it (noise, the last hit's tail)
            double floorPeak = 0.0;
            for (int i = std::max (0, best - 3 * W); i < best - W / 2; ++i) floorPeak = std::max (floorPeak, (double) std::fabs (sig[(size_t) i]));
            int start = best;
            for (int i = std::max (0, best - W); i < std::min (n, best + W); ++i)
                if (std::fabs (sig[(size_t) i]) > 2.0 * floorPeak + 1.0e-4) { start = i; break; }
            // a real hit: clearly louder just after it than just before, in its band or the other
            const double ra = e (E, start, start + w8) / (e (E, start - w8, start) + 1.0e-9);
            const auto& E2 = high ? P : Q;
            const double rb = e (E2, start, start + w8) / (e (E2, start - w8, start) + 1.0e-9);
            if (std::max (ra, rb) < 1.5) continue;
            // a clean, tight start: the zero crossing just before it (within 0.3 ms), else 4 samples before
            int onset = std::max (0, start - 4);
            const int back = std::max (2, (int) (rate * 0.0003));
            for (int i = start; i > std::max (1, start - back); --i)
                if ((x[(size_t) i] >= 0.0f) != (x[(size_t) i - 1] >= 0.0f)) { onset = i - 1; break; }
            // ranked by how loud the hit is (kicks and snares before ghost hats when slices run short)
            const float strength = (float) std::sqrt (e (E, start, start + (int) (rate * 0.02)) / (rate * 0.02)) + 0.05f * curve[(size_t) t];
            out.push_back ({ onset, strength });
        }

        // ---- 6. one per hit: in time order, anything within 40 ms of a stronger one goes (flams are one hit)
        std::sort (out.begin(), out.end(), [] (const Onset& p, const Onset& q) { return p.position < q.position; });
        std::vector<Onset> kept;
        const int gap = (int) (rate * 0.040);
        for (const auto& o : out)
        {
            if (! kept.empty() && o.position - kept.back().position < gap)
            {
                if (o.strength > kept.back().strength) kept.back() = o;
                continue;
            }
            kept.push_back (o);
        }
        return kept;
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
