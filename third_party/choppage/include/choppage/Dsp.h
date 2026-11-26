#pragma once
/*
    Dsp — the small building blocks the engine needs, with no JUCE:
    clamp / dB helpers, a deterministic RNG, a linear smoother, RBJ biquads,
    a TPT state-variable filter, a radix-2 FFT and a Freeverb-style reverb.
    Nothing here allocates after prepare().
*/
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace bc
{
    constexpr double kPi = 3.14159265358979323846;

    template <typename T> inline T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
    inline float gainToDb (float g, float floorDb = -100.0f) { return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb; }

    struct Range
    {
        int start = 0, end = 0;
        int length() const noexcept { return end - start; }
        bool contains (int v) const noexcept { return v >= start && v < end; }
    };

    /** xorshift64*: fast, deterministic for a given seed on every platform. */
    class Rng
    {
    public:
        explicit Rng (int64_t seed = 1) { setSeed (seed); }
        void setSeed (int64_t seed) { s = (uint64_t) seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull; if (s == 0) s = 1; next(); }
        uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
        float nextFloat() { return (float) ((next() >> 40) * (1.0 / 16777216.0)); }       // [0, 1)
        int nextInt (int n) { return n <= 0 ? 0 : (int) ((next() >> 33) % (uint64_t) n); }
        bool nextBool() { return (next() >> 63) != 0; }
        int64_t nextInt64() { return (int64_t) next(); }
    private:
        uint64_t s = 1;
    };

    /** Linear ramp to a target over a fixed time. */
    class Smoothed
    {
    public:
        void reset (double sampleRate, double seconds) { steps = std::max (1, (int) (sampleRate * seconds)); left = 0; cur = target; }
        void setCurrentAndTarget (float v) { cur = target = v; left = 0; }
        void setTarget (float v)
        {
            if (v == target) return;
            target = v; left = steps; inc = (target - cur) / (float) steps;
        }
        float next() { if (left > 0) { cur += inc; if (--left == 0) cur = target; } return cur; }
        bool isSmoothing() const { return left > 0; }
        float current() const { return cur; }
    private:
        float cur = 0, target = 0, inc = 0;
        int steps = 1, left = 0;
    };

    /** RBJ biquad, direct form I, one channel. */
    class Biquad
    {
    public:
        void lowPass (double sr, double f, double q = 0.70710678) { design (sr, f, q, false); }
        void highPass (double sr, double f, double q = 0.70710678) { design (sr, f, q, true); }
        void reset() { x1 = x2 = y1 = y2 = 0; }
        float process (float x)
        {
            const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return (float) y;
        }
    private:
        void design (double sr, double f, double q, bool hp)
        {
            f = clampv (f, 10.0, sr * 0.49);
            const double w = 2.0 * kPi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
            const double a0 = 1.0 + al;
            if (hp) { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; }
            else    { b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0;  b2 = b0; }
            a1 = -2 * c / a0; a2 = (1 - al) / a0;
        }
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };

    /** Zavalishin TPT state-variable filter, stereo, LP or HP output. */
    class Svf
    {
    public:
        enum class Type { lowPass, highPass };
        void prepare (double sampleRate) { sr = sampleRate; reset(); update(); }
        void reset() { s1 = { 0, 0 }; s2 = { 0, 0 }; }
        void setType (Type t) { type = t; }
        void setCutoff (float hz) { if (hz != fc) { fc = hz; update(); } }
        void setResonance (float r) { if (r != res) { res = r; update(); } }   // Q, 0.5 .. ~10
        float process (int ch, float x)
        {
            const double hp = (x - (2.0 * R + g) * s1[(size_t) ch] - s2[(size_t) ch]) * h;
            const double bp = g * hp + s1[(size_t) ch];
            s1[(size_t) ch] = g * hp + bp;
            const double lp = g * bp + s2[(size_t) ch];
            s2[(size_t) ch] = g * bp + lp;
            return (float) (type == Type::lowPass ? lp : hp);
        }
    private:
        void update()
        {
            g = std::tan (kPi * clampv ((double) fc, 10.0, sr * 0.49) / sr);
            R = 1.0 / (2.0 * std::max (0.1, (double) res));
            h = 1.0 / (1.0 + 2.0 * R * g + g * g);
        }
        double sr = 44100, g = 0.1, R = 0.7, h = 1;
        float fc = 1000.0f, res = 0.7071f;
        Type type = Type::lowPass;
        std::array<double, 2> s1 { 0, 0 }, s2 { 0, 0 };
    };

    /** In-place radix-2 complex FFT (size a power of two). */
    inline void fft (std::vector<std::complex<float>>& a)
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
            const double ang = -2.0 * kPi / (double) len;
            const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    /** Freeverb (Jezar's public-domain design): 8 combs + 4 allpasses per channel. */
    class Reverb
    {
    public:
        struct Params { float roomSize = 0.5f, damping = 0.5f, width = 1.0f, wet = 0.33f, dry = 0.4f; };

        void prepare (double sr)
        {
            static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static const int apT[4] = { 556, 441, 341, 225 };
            const double scale = sr / 44100.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const int spread = ch == 0 ? 0 : 23;
                for (int i = 0; i < 8; ++i) comb[ch][i].init ((int) ((combT[i] + spread) * scale));
                for (int i = 0; i < 4; ++i) ap[ch][i].init ((int) ((apT[i] + spread) * scale));
            }
            setParams (p);
        }
        void reset() { for (auto& c : comb) for (auto& x : c) x.clear(); for (auto& c : ap) for (auto& x : c) x.clear(); }
        void setParams (const Params& np)
        {
            p = np;
            const float fb = p.roomSize * 0.28f + 0.7f, damp = p.damping * 0.4f;
            for (auto& c : comb) for (auto& x : c) { x.feedback = fb; x.damp = damp; }
            wet1 = p.wet * 3.0f * (p.width / 2.0f + 0.5f);
            wet2 = p.wet * 3.0f * ((1.0f - p.width) / 2.0f);
        }
        void processStereo (float* l, float* r, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                const float in = (l[i] + r[i]) * 0.015f;
                float outL = 0, outR = 0;
                for (int k = 0; k < 8; ++k) { outL += comb[0][k].process (in); outR += comb[1][k].process (in); }
                for (int k = 0; k < 4; ++k) { outL = ap[0][k].process (outL); outR = ap[1][k].process (outR); }
                const float dl = l[i], dr = r[i];
                l[i] = outL * wet1 + outR * wet2 + dl * p.dry;
                r[i] = outR * wet1 + outL * wet2 + dr * p.dry;
            }
        }
    private:
        struct Comb
        {
            std::vector<float> buf; int pos = 0; float store = 0, feedback = 0.84f, damp = 0.2f;
            void init (int n) { buf.assign ((size_t) std::max (1, n), 0.0f); pos = 0; store = 0; }
            void clear() { std::fill (buf.begin(), buf.end(), 0.0f); store = 0; }
            float process (float x)
            {
                const float out = buf[(size_t) pos];
                store = out * (1.0f - damp) + store * damp;
                buf[(size_t) pos] = x + store * feedback;
                if (++pos >= (int) buf.size()) pos = 0;
                return out;
            }
        };
        struct Allpass
        {
            std::vector<float> buf; int pos = 0;
            void init (int n) { buf.assign ((size_t) std::max (1, n), 0.0f); pos = 0; }
            void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
            float process (float x)
            {
                const float b = buf[(size_t) pos];
                buf[(size_t) pos] = x + b * 0.5f;
                if (++pos >= (int) buf.size()) pos = 0;
                return b - x;
            }
        };
        Comb comb[2][8];
        Allpass ap[2][4];
        Params p;
        float wet1 = 1, wet2 = 0;
    };
}
