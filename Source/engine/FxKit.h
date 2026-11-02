// Paula's Revenge - building blocks for the high-end effects (plain C++): smoothed parameters,
// fractional delay lines read with 4-point Hermite interpolation, Schroeder allpasses, one-pole
// filters, LFOs, a 2x oversampler for the saturation (so it doesn't alias) and noise.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace prv::fxkit
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// ---- a parameter that glides to its target (no zipper noise)
struct Smooth
{
    float value = 0, target = 0, coef = 0.999f;
    bool primed = false;
    void setTime (double seconds, double sr) { coef = (float) std::exp (-1.0 / std::max (1.0, seconds * sr)); }
    void set (float t) { target = t; if (! primed) { value = t; primed = true; } }
    void jump (float t) { target = value = t; primed = true; }
    inline float next() { value = target + (value - target) * coef; return value; }
};

// ---- a delay line: write one sample, read anywhere behind it (fractional, Hermite)
class Delay
{
public:
    void setMax (int samples)
    {
        int n = 1; while (n < samples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; pos = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    int  size() const { return mask + 1; }
    inline void write (float x) { buf[(size_t) pos] = x; pos = (pos + 1) & mask; }
    // the sample `d` samples ago (d >= 1)
    inline float read (double d) const
    {
        d = std::clamp (d, 1.0, (double) (mask - 3));
        const double rp = (double) pos - d;
        const int i = (int) std::floor (rp);
        const float t = (float) (rp - i);
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
    inline float readInt (int d) const { return at (pos - std::clamp (d, 1, mask)); }
private:
    std::vector<float> buf;
    int mask = 0, pos = 0;
    inline float at (int i) const { return buf[(size_t) (i & mask)]; }
};

// ---- a Schroeder allpass (diffusion), its length modulated if wanted
class Allpass
{
public:
    void setMax (int samples) { d.setMax (samples + 64); }
    void clear() { d.clear(); }
    inline float process (float x, double len, float g)
    {
        const float delayed = d.read (len);
        const float v = x + g * delayed;
        d.write (v);
        return delayed - g * v;
    }
    inline float tap (double len) const { return d.read (len); }   // inside it (for reverb output taps)
private:
    Delay d;
};

// ---- one-pole filters
struct OnePole
{
    float z = 0, a = 0;
    void setLowpass (double hz, double sr) { a = (float) std::exp (-kTwoPi * std::clamp (hz, 1.0, sr * 0.49) / sr); }
    inline float lp (float x) { z = x + (z - x) * a; return z; }
    inline float hp (float x) { return x - lp (x); }
    void reset() { z = 0; }
};

// ---- an LFO (phase 0..1)
struct Lfo
{
    double phase = 0, inc = 0;
    void setRate (double hz, double sr) { inc = hz / sr; }
    inline double sine() { const double v = std::sin (kTwoPi * phase); advance(); return v; }
    inline double tri() { const double v = phase < 0.5 ? 4 * phase - 1 : 3 - 4 * phase; advance(); return v; }
    inline double sineAt (double off) const { return std::sin (kTwoPi * (phase + off)); }
    inline double triAt (double off) const { double p = phase + off; p -= std::floor (p); return p < 0.5 ? 4 * p - 1 : 3 - 4 * p; }
    inline void advance() { phase += inc; if (phase >= 1.0) phase -= 1.0; }
};

// ---- a state-variable filter (TPT: stable when swept fast); g = tan (pi fc / sr), k = 1 / Q
struct Svf
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    static inline float gFor (double hz, double sr) { return (float) std::tan (kPi * std::clamp (hz, 5.0, sr * 0.45) / sr); }
    inline void tick (float x, float g, float k, float& lp, float& bp, float& hp)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

// ---- first-order allpass (phasers, dispersion)
struct Allpass1
{
    float x1 = 0, y1 = 0;
    void reset() { x1 = y1 = 0; }
    inline float process (float x, float a) { const float y = a * x + x1 - a * y1; x1 = x; y1 = y; return y; }
    static inline float coefFor (double hz, double sr) { const double t = std::tan (kPi * std::clamp (hz, 5.0, sr * 0.45) / sr); return (float) ((t - 1.0) / (t + 1.0)); }
};

// ---- a Hilbert pair: two allpass chains whose outputs are 90 degrees apart over ~20 HZ .. 20 KHZ
// (Olli Niemitalo's coefficients); for frequency shifting
class Hilbert
{
public:
    void reset() { for (auto& c : st) c = {}; }
    inline void process (float x, float& re, float& im)
    {
        static constexpr double a[4] = { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
        static constexpr double b[4] = { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
        double u = x, v = x;
        for (int i = 0; i < 4; ++i) { u = section (st[i], u, a[i] * a[i]); v = section (st[4 + i], v, b[i] * b[i]); }
        re = (float) delayed; delayed = u;   // (the first chain one sample late)
        im = (float) v;
    }
private:
    struct S { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    S st[8];
    double delayed = 0;
    static inline double section (S& s, double x, double c)
    {
        const double y = c * (x + s.y2) - s.x2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }
};

// ---- noise (xorshift)
struct Noise
{
    uint32_t s = 0x9E3779B9u;
    inline float white() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) ((int32_t) s) * (1.0f / 2147483648.0f); }
};

// ---- a slow random wander (tape wow, drift): smoothed noise, -1..1
struct Wander
{
    Noise n; float v = 0, target = 0; int count = 0, every = 1000;
    void setRate (double hz, double sr) { every = std::max (1, (int) (sr / std::max (0.01, hz))); }
    inline float next()
    {
        if (--count <= 0) { count = every; target = n.white(); }
        v += (target - v) * (2.0f / (float) every);
        return v;
    }
};

// ---- 2x oversampling around a nonlinearity (half-band FIR, 31 taps)
class Oversampler2x
{
public:
    Oversampler2x()
    {
        // windowed-sinc half-band low-pass at a quarter of the doubled rate
        const int N = kTaps;
        for (int i = 0; i < N; ++i)
        {
            const double m = i - (N - 1) / 2.0;
            const double sinc = m == 0 ? 0.5 : std::sin (kPi * 0.5 * m) / (kPi * m);
            const double w = 0.42 - 0.5 * std::cos (kTwoPi * i / (N - 1)) + 0.08 * std::cos (2 * kTwoPi * i / (N - 1));
            h[i] = (float) (sinc * w);
        }
        float s = 0; for (float v : h) s += v;
        for (float& v : h) v /= s;
    }
    static constexpr int kLatency = 15;   // samples (at the base rate) its filters delay the sound by
    void reset() { std::fill (std::begin (up), std::end (up), 0.0f); std::fill (std::begin (dn), std::end (dn), 0.0f); }
    // one sample in, two through f, one out
    template <typename F> inline float process (float x, F&& f)
    {
        float y[2];
        for (int k = 0; k < 2; ++k)
        {
            push (up, k == 0 ? x * 2.0f : 0.0f);
            y[k] = f (conv (up));
        }
        push (dn, y[0]); push (dn, y[1]);
        return conv (dn);
    }
private:
    static constexpr int kTaps = 31;
    float h[kTaps] {}, up[kTaps] {}, dn[kTaps] {};
    static inline void push (float* b, float x) { for (int i = kTaps - 1; i > 0; --i) b[i] = b[i - 1]; b[0] = x; }
    inline float conv (const float* b) const { float s = 0; for (int i = 0; i < kTaps; ++i) s += h[i] * b[i]; return s; }
};

// a gentle, slightly asymmetric tape-like saturation: unity gain for small signals, a ceiling
// that comes down as the drive goes up (3.3 at 0, 0.33 at 1)
inline float tapeSat (float x, float drive)
{
    const float k = 0.3f + 2.7f * drive, b = 0.15f * drive;
    return (std::tanh (k * x + b) - std::tanh (b)) / (k * (1.0f - std::tanh (b) * std::tanh (b)));
}

// a soft limiter for feedback loops (transparent until it nears full scale)
inline float softLimit (float x) { const float a = std::abs (x); return a < 0.8f ? x : std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f) * 1.5f, x); }

inline float flushDenormal (float x) { return std::abs (x) < 1.0e-20f ? 0.0f : x; }
} // namespace prv::fxkit
