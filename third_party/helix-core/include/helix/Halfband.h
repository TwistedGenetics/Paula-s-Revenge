// 2x decimator: linear-phase half-band FIR (47 taps, Kaiser beta 8). About 80 dB of stopband rejection above
// 0.61 x the output rate; passband flat to ~0.39 x the output rate (17 kHz at 44.1 kHz).
#pragma once
#include <algorithm>
#include <cmath>

namespace helix {

struct HalfbandCoefs
{
    static constexpr int kHalf = 23;            // taps run -23..23; odd offsets carry the non-zero coefficients
    float c[(kHalf + 1) / 2];                   // c[k] for offset 2k+1, k = 0..11
    HalfbandCoefs()
    {
        auto bessel0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        const double beta = 8.0, i0b = bessel0(beta);
        for (int k = 0; k < (kHalf + 1) / 2; ++k)
        {
            const int n = 2 * k + 1;
            const double sinc = std::sin(M_PI * n / 2.0) / (M_PI * n / 2.0);
            const double r = (double)n / kHalf;
            const double w = bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            c[k] = (float)(0.5 * sinc * w);
        }
    }
    static const HalfbandCoefs& get() { static const HalfbandCoefs h; return h; }
};

// Takes two input samples (at 2x rate), returns one output sample.
class HalfbandDecimator
{
public:
    void reset() { for (float& v : x_) v = 0.0f; pos_ = 0; }
    inline float process(float a, float b)
    {
        // history ring of 48 samples, duplicated to avoid wrap checks
        push(a); push(b);
        const HalfbandCoefs& h = HalfbandCoefs::get();
        const float* x = x_ + pos_;          // x[0] = newest ... x[46] = oldest
        float y = 0.5f * x[23];
        for (int k = 0; k < 12; ++k) y += h.c[k] * (x[23 - (2 * k + 1)] + x[23 + (2 * k + 1)]);
        return y;
    }
private:
    inline void push(float v)
    {
        pos_ = pos_ == 0 ? kLen - 1 : pos_ - 1;
        x_[pos_] = v;
        x_[pos_ + kLen] = v;
    }
    static constexpr int kLen = 48;
    float x_[2 * kLen] = {};
    int pos_ = 0;
};

} // namespace helix

namespace helix {

// 1 -> 2 interpolator with the same half-band kernel (gain 2 so the level is unchanged).
class HalfbandInterpolator
{
public:
    void reset() { for (float& v : x_) v = 0.0f; pos_ = 0; }
    inline void process(float in, float& outA, float& outB)
    {
        push(in);
        outA = filter();
        push(0.0f);
        outB = filter();
    }
private:
    inline float filter() const
    {
        const HalfbandCoefs& h = HalfbandCoefs::get();
        const float* x = x_ + pos_;
        float y = x[23];                        // centre tap 0.5 x gain 2
        for (int k = 0; k < 12; ++k) y += 2.0f * h.c[k] * (x[23 - (2 * k + 1)] + x[23 + (2 * k + 1)]);
        return y;
    }
    inline void push(float v)
    {
        pos_ = pos_ == 0 ? kLen - 1 : pos_ - 1;
        x_[pos_] = v;
        x_[pos_ + kLen] = v;
    }
    static constexpr int kLen = 48;
    float x_[2 * kLen] = {};
    int pos_ = 0;
};

} // namespace helix
