// Paula's Revenge - an instrument's UTILITY (stereo width, mono below a frequency) and the
// taps and analysis behind the EQ's spectrum display (plain C++)
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace prv
{
// WIDTH 0 (mono) .. 2 (200%) as mid / side, and MONO BELOW: a Linkwitz-Riley (24 dB/oct, or 48 when steep)
// crossover, the lows made mono, the highs given the width. The two halves add back flat.
class StereoUtility
{
public:
    void prepare (double sampleRate) { sr = sampleRate; lastHz = -1; reset(); }
    void reset();
    void process (float* left, float* right, int numSamples, float width, int monoHz, bool steep = false);

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
                    double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    double sr = 48000.0;
    int lastHz = -1;
    bool lastSteep = false;
    Biquad lp[2][4], hp[2][4];   // [channel][stage]: Butterworths in a row = Linkwitz-Riley (2 stages LR4, 4 stages LR8)
    void design (int hz, bool steep);
};

// Where the spectrum display gets its audio: the audio thread pushes the stereo signal of the one
// source the display wants (an instrument, or a mixer channel) into a ring; the UI reads the
// latest samples. Nothing is pushed while no display wants it (source -1).
struct AudioTap
{
    static constexpr int kSize = 8192;
    std::atomic<int> source { -1 };
    void push (const float* l, const float* r, int n);
    int  read (float* out, int n) const;   // the latest n (or fewer) samples, oldest first, mono
    uint32_t count() const { return written.load (std::memory_order_acquire); }
    void clear();
private:
    float ring[kSize] {};
    std::atomic<uint32_t> written { 0 };
};

// The spectrum of n samples (n a power of 2), Hann-windowed: dB per bin (bin k = k * rate / n),
// 0 dB = a full-scale sine. n / 2 + 1 values.
void spectrumDb (const float* samples, int n, std::vector<float>& outDb);
} // namespace prv
