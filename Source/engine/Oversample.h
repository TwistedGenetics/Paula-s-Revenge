// Paula's Revenge - 4x OVERSAMPLING (v0.88) for the synths' nonlinear stages - drive, fold, clip, a screaming filter.
// Run at 48 kHz they make harmonics above what 48 kHz can hold, and those fold back down as tones that aren't in the
// note: a whistle riding on a bass. Run at 4x and filtered back down, the harmonics above 20 kHz are taken off first.
//
// Decimator: takes kOs samples at the high rate, gives one at the output rate (a 96-tap Kaiser low-pass at the output
// rate's half). Oversampler: one sample in, the stage run on 4 (the same filter as the interpolator), one sample out.
// About half a millisecond of delay (23 samples), the same every time (nothing to line up: these stages sit in a synth's one signal path).
#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace prv
{
constexpr int kOs = 4;

struct OsFilter
{
    static constexpr int kTaps = 96;
    static const std::array<float, kTaps>& taps()
    {
        static const std::array<float, kTaps> h = []
        {
            std::array<float, kTaps> t {};
            const double fc = 0.5 / kOs;   // (the output rate's half, at the high rate)
            const double beta = 8.0, mid = (kTaps - 1) * 0.5;
            auto i0 = [] (double x) { double s = 1, term = 1; for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); s += term; } return s; };
            double sum = 0;
            for (int k = 0; k < kTaps; ++k)
            {
                const double n = k - mid;
                const double sinc = n == 0 ? 2.0 * fc : std::sin (2.0 * 3.14159265358979323846 * fc * n) / (3.14159265358979323846 * n);
                const double r = n / mid;
                const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
                t[(size_t) k] = (float) (sinc * w);
                sum += sinc * w;
            }
            for (auto& x : t) x = (float) (x / sum);
            return t;
        }();
        return h;
    }
};

class Decimator
{
public:
    void reset() { buf.fill (0.0f); pos = 0; }
    // kOs new samples at the high rate in; one out
    float push (const float* hi)
    {
        for (int k = 0; k < kOs; ++k) { buf[(size_t) pos] = buf[(size_t) (pos + OsFilter::kTaps)] = hi[k]; pos = (pos + 1) % OsFilter::kTaps; }
        const auto& h = OsFilter::taps();
        // (the newest is at pos - 1: h[0] goes with it)
        const float* x = buf.data() + pos;   // x[0..kTaps-1], oldest first
        float y = 0.0f;
        for (int k = 0; k < OsFilter::kTaps; ++k) y += h[(size_t) (OsFilter::kTaps - 1 - k)] * x[k];
        return y;
    }

private:
    std::array<float, OsFilter::kTaps * 2> buf {};
    int pos = 0;
};

class Oversampler
{
public:
    void reset() { in.fill (0.0f); inPos = 0; down.reset(); }
    // x at the output rate in; stage (a float -> float) run at 4x; back at the output rate
    template <typename Stage>
    float process (float x, Stage&& stage)
    {
        constexpr int per = OsFilter::kTaps / kOs;   // (input samples each phase reaches)
        in[(size_t) inPos] = in[(size_t) (inPos + per)] = x;
        inPos = (inPos + 1) % per;
        const float* hist = in.data() + inPos;   // hist[per - 1] newest
        const auto& h = OsFilter::taps();
        float hi[kOs];
        for (int ph = 0; ph < kOs; ++ph)
        {
            float y = 0.0f;
            for (int j = 0; j < per; ++j) y += h[(size_t) (ph + j * kOs)] * hist[per - 1 - j];
            hi[ph] = stage (y * (float) kOs);
        }
        return down.push (hi);
    }

private:
    std::array<float, (OsFilter::kTaps / kOs) * 2> in {};
    int inPos = 0;
    Decimator down;
};
} // namespace prv
