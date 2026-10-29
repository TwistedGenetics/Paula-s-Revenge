// Paula's Revenge - instrument UTILITY, spectrum tap and analysis (plain C++)
#include "Utility.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace prv
{
namespace { constexpr double kPi = 3.14159265358979323846; }

void StereoUtility::reset()
{
    for (auto& c : lp) for (auto& b : c) b.z1 = b.z2 = 0;
    for (auto& c : hp) for (auto& b : c) b.z1 = b.z2 = 0;
}

void StereoUtility::design (int hz, bool steep)
{
    const double f = std::clamp ((double) hz, 10.0, sr * 0.45);
    const double w = 2.0 * kPi * f / sr, cw = std::cos (w), sw = std::sin (w);
    // LR4 = a 2nd-order Butterworth twice; LR8 = a 4th-order Butterworth (Q 0.5412, 1.3066) twice
    const double qs4[4] = { 0.70710678118654752, 0.70710678118654752, 0, 0 };
    const double qs8[4] = { 0.54119610014619698, 1.30656296487637652, 0.54119610014619698, 1.30656296487637652 };
    const double* qs = steep ? qs8 : qs4;
    for (int st = 0; st < (steep ? 4 : 2); ++st)
    {
        const double alpha = sw / (2.0 * qs[st]), a0 = 1 + alpha;
        for (int c = 0; c < 2; ++c)
        {
            auto& l = lp[c][st];
            l.b0 = (1 - cw) / 2 / a0; l.b1 = (1 - cw) / a0; l.b2 = (1 - cw) / 2 / a0; l.a1 = -2 * cw / a0; l.a2 = (1 - alpha) / a0;
            auto& h = hp[c][st];
            h.b0 = (1 + cw) / 2 / a0; h.b1 = -(1 + cw) / a0; h.b2 = (1 + cw) / 2 / a0; h.a1 = -2 * cw / a0; h.a2 = (1 - alpha) / a0;
        }
    }
    lastHz = hz; lastSteep = steep;
}

void StereoUtility::process (float* L, float* R, int n, float width, int monoHz, bool steep)
{
    width = std::clamp (width, 0.0f, 2.0f);
    if (monoHz <= 0)
    {
        if (lastHz != -1) { reset(); lastHz = -1; }
        if (width == 1.0f) return;
        for (int i = 0; i < n; ++i)
        {
            const float m = (L[i] + R[i]) * 0.5f, s = (L[i] - R[i]) * 0.5f * width;
            L[i] = m + s; R[i] = m - s;
        }
        return;
    }
    if (monoHz != lastHz || steep != lastSteep) { if (lastHz == -1 || steep != lastSteep) reset(); design (monoHz, steep); }
    const int stages = steep ? 4 : 2;
    for (int i = 0; i < n; ++i)
    {
        // the two halves are in phase (Linkwitz-Riley), so low + high is flat (an all-pass)
        double lowL = L[i], lowR = R[i], hiL = L[i], hiR = R[i];
        for (int st = 0; st < stages; ++st)
        {
            lowL = lp[0][st].run (lowL); lowR = lp[1][st].run (lowR);
            hiL = hp[0][st].run (hiL);  hiR = hp[1][st].run (hiR);
        }
        const double lm = (lowL + lowR) * 0.5;
        const double hm = (hiL + hiR) * 0.5, hs = (hiL - hiR) * 0.5 * width;
        L[i] = (float) (lm + hm + hs);
        R[i] = (float) (lm + hm - hs);
    }
}

// ------------------------------------------------------------------ tap
void AudioTap::push (const float* l, const float* r, int n)
{
    uint32_t w = written.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i, ++w) ring[w % kSize] = (l[i] + r[i]) * 0.5f;
    written.store (w, std::memory_order_release);
}

int AudioTap::read (float* out, int n) const
{
    const uint32_t w = written.load (std::memory_order_acquire);
    n = (int) std::min<uint32_t> ((uint32_t) std::min (n, kSize), w);
    for (int i = 0; i < n; ++i) out[i] = ring[(w - (uint32_t) n + (uint32_t) i) % kSize];
    return n;
}

void AudioTap::clear()
{
    std::fill (ring, ring + kSize, 0.0f);
    written.store (0, std::memory_order_release);
}

// ------------------------------------------------------------------ FFT
void spectrumDb (const float* x, int n, std::vector<float>& out)
{
    std::vector<std::complex<double>> a ((size_t) n);
    double wsum = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / (n - 1));
        wsum += w;
        a[(size_t) i] = x[i] * w;
    }
    for (int i = 1, j = 0; i < n; ++i)   // bit reversal
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[(size_t) i], a[(size_t) j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const std::complex<double> wl = std::polar (1.0, -2.0 * kPi / len);
        for (int i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0);
            for (int k = 0; k < len / 2; ++k, w *= wl)
            {
                const auto u = a[(size_t) (i + k)], v = a[(size_t) (i + k + len / 2)] * w;
                a[(size_t) (i + k)] = u + v; a[(size_t) (i + k + len / 2)] = u - v;
            }
        }
    }
    out.resize ((size_t) (n / 2 + 1));
    for (int k = 0; k <= n / 2; ++k)
        out[(size_t) k] = (float) (20.0 * std::log10 (std::max (1e-9, 2.0 * std::abs (a[(size_t) k]) / wsum)));
}
} // namespace prv
