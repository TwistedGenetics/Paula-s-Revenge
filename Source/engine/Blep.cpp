// Paula's Revenge - the band-limited step table
#include "Blep.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace prv
{
namespace
{
    using cd = std::complex<double>;
    constexpr double kPiD = 3.14159265358979323846;

    void fft (std::vector<cd>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * kPiD / (double) len * (inverse ? 1.0 : -1.0);
            const cd wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                cd w (1.0, 0.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const cd u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse) for (auto& x : a) x /= (double) n;
    }

    double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum) break;
        }
        return sum;
    }
}

void BlepTable::build (double sampleRate)
{
    if (sampleRate <= 0) sampleRate = 48000.0;
    builtRate = sampleRate;
    const int m = kTaps * kPhase;                       // filter length at the table's rate
    const double beta = 8.0;                            // Kaiser window: about -80 dB stopband
    // the transition band this length and window can do, and a cutoff that keeps it below Nyquist
    const double transition = (beta * 9.0) / (2.285 * (m - 1)) / (2.0 * kPiD) * kPhase * sampleRate;   // Hz
    fc = std::min (20000.0, 0.5 * sampleRate - 0.5 * transition);
    const double wc = fc / (sampleRate * kPhase);       // cycles per table point

    // windowed sinc low-pass
    std::vector<double> h ((size_t) m);
    const double mid = 0.5 * (m - 1), i0b = besselI0 (beta);
    double sum = 0;
    for (int n = 0; n < m; ++n)
    {
        const double x = n - mid;
        const double sinc = x == 0.0 ? 2.0 * wc : std::sin (2.0 * kPiD * wc * x) / (kPiD * x);
        const double r = x / mid;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        h[(size_t) n] = sinc * w;
        sum += h[(size_t) n];
    }
    for (auto& v : h) v /= sum;

    // minimum phase (real cepstrum): the step starts at once, no added delay
    const size_t big = (size_t) m * 4;
    std::vector<cd> a (big, cd (0, 0));
    for (int n = 0; n < m; ++n) a[(size_t) n] = h[(size_t) n];
    fft (a, false);
    for (auto& x : a) x = cd (std::log (std::max (std::abs (x), 1e-9)), 0.0);
    fft (a, true);
    for (size_t n = 1; n < big / 2; ++n) a[n] *= 2.0;
    for (size_t n = big / 2 + 1; n < big; ++n) a[n] = 0.0;
    fft (a, false);
    for (auto& x : a) x = std::exp (x);
    fft (a, true);

    // integrate the impulse into a step, ending exactly at 1
    step.assign ((size_t) m + 1, 0.0f);
    double acc = 0;
    std::vector<double> s ((size_t) m + 1, 0.0);
    for (int n = 0; n < m; ++n) { acc += a[(size_t) n].real(); s[(size_t) n + 1] = acc; }
    for (int n = 0; n <= m; ++n) step[(size_t) n] = (float) (s[(size_t) n] / acc);
    step[(size_t) m] = 1.0f;

    // how long until it has settled (|step - 1| below -100 dB from then on): no work after that
    len = kTaps;
    for (int n = m; n >= 0; --n)
        if (std::abs (step[(size_t) n] - 1.0f) > 1e-5f) { len = std::min (kTaps, n / kPhase + 2); break; }

    // per phase: what the step still lacks at each output instant
    res.assign ((size_t) (kPhase + 1) * (size_t) kTaps, 0.0f);
    for (int q = 0; q <= kPhase; ++q)
        for (int k = 0; k < kTaps; ++k)
            res[(size_t) q * (size_t) kTaps + (size_t) k] = k < len ? at (k + (double) q / kPhase) - 1.0f : 0.0f;
}
} // namespace prv
