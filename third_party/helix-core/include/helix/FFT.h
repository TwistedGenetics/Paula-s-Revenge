// Radix-2 complex FFT for table preparation. NOT for the audio thread (it is fine there, but it is never called there).
#pragma once
#include <complex>
#include <vector>

namespace helix {

// In-place FFT; n = data.size() must be a power of two. inverse = true computes the unscaled inverse.
void fft(std::vector<std::complex<double>>& data, bool inverse);

// Magnitude spectrum (n/2+1 bins) of a real signal with a Blackman-Harris window. For tests and analysis.
std::vector<double> magnitudeSpectrum(const float* x, int n);

} // namespace helix
