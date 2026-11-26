#include "helix/FFT.h"
#include <cmath>

namespace helix {

void fft(std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * M_PI / (double)len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

std::vector<double> magnitudeSpectrum(const float* x, int n)
{
    std::vector<std::complex<double>> buf((size_t)n);
    for (int i = 0; i < n; ++i)
    {
        const double t = 2.0 * M_PI * i / (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        buf[(size_t)i] = std::complex<double>(x[i] * w, 0.0);
    }
    fft(buf, false);
    std::vector<double> mag((size_t)n / 2 + 1);
    for (size_t i = 0; i < mag.size(); ++i) mag[i] = std::abs(buf[i]);
    return mag;
}

} // namespace helix
