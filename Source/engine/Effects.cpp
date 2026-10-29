// Paula's Revenge - mixer effects and EQ
#include "Effects.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace prv
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;

    inline float mixDryWet (float dry, float wet, float mix) { return dry + (wet - dry) * mix; }

    // ---- state-variable filter (TPT), one channel
    struct Svf
    {
        double ic1 = 0, ic2 = 0;
        void reset() { ic1 = ic2 = 0; }
        // returns lp, bp, hp
        inline void tick (double x, double g, double k, double& lp, double& bp, double& hp)
        {
            const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double v3 = x - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1; ic2 = 2.0 * v2 - ic2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }
    };

    // ---------------------------------------------------------------- FILTER
    class FilterFx : public Effect
    {
    public:
        void prepare (double s) override { sr = s; reset(); }
        void reset() override { for (auto& f : svf) f.reset(); }
        void process (float* L, float* R, int n, const float* p, double) override
        {
            const int mode = p[0] < 0.34f ? 0 : p[0] < 0.67f ? 1 : 2;
            const double fc = std::min (sr * 0.45, 20.0 * std::pow (1000.0, (double) p[1]));
            const double g = std::tan (kTwoPi * 0.5 * fc / sr);
            const double k = 2.0 - 1.9 * std::clamp ((double) p[2], 0.0, 1.0);
            float* ch[2] = { L, R };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    double lp, bp, hp;
                    svf[c].tick (ch[c][i], g, k, lp, bp, hp);
                    const double y = mode == 0 ? lp : mode == 1 ? bp : hp;
                    ch[c][i] = mixDryWet (ch[c][i], (float) y, p[3]);
                }
        }
    private:
        double sr = 48000;
        Svf svf[2];
    };

    // ---------------------------------------------------------------- DRIVE / CRUSH
    class DriveFx : public Effect
    {
    public:
        void prepare (double s) override { sr = s; reset(); }
        void reset() override { hold[0] = hold[1] = 0; count = 0; lp[0] = lp[1] = 0; }
        void process (float* L, float* R, int n, const float* p, double) override
        {
            const float gain = 1.0f + 39.0f * p[0] * p[0];
            const int bits = 2 + (int) std::lround (14.0 * p[1]);
            const float steps = (float) (1 << (bits - 1));
            const int every = 1 + (int) std::lround (31.0 * p[2]);
            const bool toneOn = p[3] < 0.999f;
            const double a = 1.0 - std::exp (-kTwoPi * 1000.0 * std::pow (20.0, (double) p[3]) / sr);
            const float comp = 1.0f / std::tanh (std::max (1.0f, gain * 0.5f));   // keep the level roughly even
            for (int i = 0; i < n; ++i)
            {
                if (count == 0)
                {
                    float x[2] = { L[i], R[i] };
                    for (int c = 0; c < 2; ++c)
                    {
                        float y = std::tanh (x[c] * gain) * std::min (1.0f, comp);
                        if (bits < 16) y = std::round (y * steps) / steps;
                        hold[c] = y;
                    }
                }
                count = (count + 1) % every;
                float o[2] = { hold[0], hold[1] };
                if (toneOn) for (int c = 0; c < 2; ++c) { lp[c] += a * (o[c] - lp[c]); o[c] = (float) lp[c]; }
                L[i] = mixDryWet (L[i], o[0], p[4]);
                R[i] = mixDryWet (R[i], o[1], p[4]);
            }
        }
    private:
        double sr = 48000;
        float hold[2] {};
        int count = 0;
        double lp[2] {};
    };

    // ---------------------------------------------------------------- COMPRESSOR
    class CompressorFx : public Effect
    {
    public:
        void prepare (double s) override { sr = s; reset(); }
        void reset() override { env = 0; }
        void process (float* L, float* R, int n, const float* p, double) override
        {
            const double thresh = -48.0 + 48.0 * p[0];
            const double ratio = 1.0 + 19.0 * p[1];
            const double att = std::exp (-1.0 / (0.001 * 0.1 * std::pow (1000.0, (double) p[2]) * sr));
            const double rel = std::exp (-1.0 / (0.001 * 10.0 * std::pow (100.0, (double) p[3]) * sr));
            const double makeup = std::pow (10.0, 24.0 * p[4] / 20.0);
            for (int i = 0; i < n; ++i)
            {
                const double peak = std::max (std::abs (L[i]), std::abs (R[i]));
                env = peak > env ? att * env + (1.0 - att) * peak : rel * env + (1.0 - rel) * peak;
                const double db = 20.0 * std::log10 (env + 1e-9);
                const double over = db - thresh;
                const double gainDb = over > 0.0 ? -over * (1.0 - 1.0 / ratio) : 0.0;
                lastReductionDb = (float) gainDb;
                const float g = (float) (std::pow (10.0, gainDb / 20.0) * makeup);
                L[i] = mixDryWet (L[i], L[i] * g, p[5]);
                R[i] = mixDryWet (R[i], R[i] * g, p[5]);
            }
        }
        float lastReductionDb = 0;
    private:
        double sr = 48000, env = 0;
    };

    // ---------------------------------------------------------------- DELAY
    class DelayFx : public Effect
    {
    public:
        void prepare (double s) override
        {
            sr = s;
            size = (int) (sr * 2.2) + 4;
            buf[0].assign ((size_t) size, 0.0f);
            buf[1].assign ((size_t) size, 0.0f);
            reset();
        }
        void reset() override
        {
            for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
            pos = 0; lp[0] = lp[1] = 0; smoothed = -1;
        }
        void process (float* L, float* R, int n, const float* p, double bpm) override
        {
            if (size <= 4) return;
            const double target = std::clamp (delaySeconds (p, bpm) * sr, 1.0, (double) size - 3.0);
            if (smoothed < 0) smoothed = target;
            const float fb = 0.95f * p[2];
            const double a = 1.0 - std::exp (-kTwoPi * 500.0 * std::pow (40.0, (double) p[3]) / sr);
            const bool ping = p[4] > 0.5f;
            for (int i = 0; i < n; ++i)
            {
                smoothed += (target - smoothed) * 0.0005;   // glide when the time changes
                double rp = pos - smoothed;
                if (rp < 0) rp += size;
                const int i0 = (int) rp; const int i1 = (i0 + 1) % size; const float fr = (float) (rp - i0);
                float d[2];
                for (int c = 0; c < 2; ++c) d[c] = buf[c][(size_t) i0] + (buf[c][(size_t) i1] - buf[c][(size_t) i0]) * fr;
                for (int c = 0; c < 2; ++c) { lp[c] += a * (d[c] - lp[c]); d[c] = (float) lp[c]; }
                const float inL = L[i], inR = R[i];
                if (ping)
                {
                    // the input goes in on the left; repeats bounce between the sides
                    buf[0][(size_t) pos] = 0.5f * (inL + inR) + d[1] * fb;
                    buf[1][(size_t) pos] = d[0] * fb;
                }
                else
                {
                    buf[0][(size_t) pos] = inL + d[0] * fb;
                    buf[1][(size_t) pos] = inR + d[1] * fb;
                }
                pos = (pos + 1) % size;
                L[i] = mixDryWet (inL, d[0], p[5]);
                R[i] = mixDryWet (inR, d[1], p[5]);
            }
        }
    private:
        double sr = 48000, smoothed = -1;
        int size = 0, pos = 0;
        std::vector<float> buf[2];
        double lp[2] {};
    };

    // ---------------------------------------------------------------- REVERB (Freeverb-style)
    class ReverbFx : public Effect
    {
    public:
        void prepare (double s) override
        {
            sr = s;
            const double scale = sr / 44100.0;
            static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            static const int apT[4] = { 556, 441, 341, 225 };
            for (int c = 0; c < 2; ++c)
            {
                const int spread = c == 0 ? 0 : 23;
                for (int k = 0; k < 8; ++k) comb[c][k].init ((int) ((combT[k] + spread) * scale));
                for (int k = 0; k < 4; ++k) ap[c][k].init ((int) ((apT[k] + spread) * scale));
            }
            pre.assign ((size_t) (sr * 0.21) + 2, 0.0f);
            reset();
        }
        void reset() override
        {
            for (auto& ch : comb) for (auto& x : ch) x.clear();
            for (auto& ch : ap) for (auto& x : ch) x.clear();
            std::fill (pre.begin(), pre.end(), 0.0f); prePos = 0;
        }
        void process (float* L, float* R, int n, const float* p, double) override
        {
            if (pre.empty()) return;
            const float room = 0.7f + 0.28f * p[0];
            const float damp = 0.4f * p[1];
            const float width = p[2];
            const int preLen = std::clamp ((int) (0.2 * p[3] * sr), 0, (int) pre.size() - 1);
            const float wet1 = 3.0f * (width * 0.5f + 0.5f), wet2 = 3.0f * ((1.0f - width) * 0.5f);
            for (int i = 0; i < n; ++i)
            {
                const float in = (L[i] + R[i]) * 0.015f;
                pre[(size_t) prePos] = in;
                int rp = prePos - preLen; if (rp < 0) rp += (int) pre.size();
                const float x = pre[(size_t) rp];
                prePos = (prePos + 1) % (int) pre.size();
                float o[2] = { 0, 0 };
                for (int c = 0; c < 2; ++c)
                {
                    for (auto& cb : comb[c]) o[c] += cb.tick (x, room, damp);
                    for (auto& a : ap[c]) o[c] = a.tick (o[c]);
                }
                const float wl = o[0] * wet1 + o[1] * wet2, wr = o[1] * wet1 + o[0] * wet2;
                L[i] = mixDryWet (L[i], wl, p[4]);
                R[i] = mixDryWet (R[i], wr, p[4]);
            }
        }
    private:
        struct Comb
        {
            std::vector<float> b; int i = 0; float store = 0;
            void init (int n) { b.assign ((size_t) std::max (1, n), 0.0f); i = 0; store = 0; }
            void clear() { std::fill (b.begin(), b.end(), 0.0f); store = 0; }
            float tick (float x, float fb, float damp)
            {
                const float y = b[(size_t) i];
                store = y * (1.0f - damp) + store * damp;
                b[(size_t) i] = x + store * fb;
                if (++i >= (int) b.size()) i = 0;
                return y;
            }
        };
        struct Allpass
        {
            std::vector<float> b; int i = 0;
            void init (int n) { b.assign ((size_t) std::max (1, n), 0.0f); i = 0; }
            void clear() { std::fill (b.begin(), b.end(), 0.0f); }
            float tick (float x)
            {
                const float y = b[(size_t) i];
                b[(size_t) i] = x + y * 0.5f;
                if (++i >= (int) b.size()) i = 0;
                return y - x;
            }
        };
        double sr = 48000;
        Comb comb[2][8];
        Allpass ap[2][4];
        std::vector<float> pre;
        int prePos = 0;
    };

    // ---------------------------------------------------------------- CHORUS
    class ChorusFx : public Effect
    {
    public:
        void prepare (double s) override
        {
            sr = s;
            size = (int) (sr * 0.05) + 4;
            buf[0].assign ((size_t) size, 0.0f); buf[1].assign ((size_t) size, 0.0f);
            reset();
        }
        void reset() override { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); pos = 0; phase = 0; }
        void process (float* L, float* R, int n, const float* p, double) override
        {
            if (size <= 4) return;
            const double rate = 0.05 * std::pow (100.0, (double) p[0]);
            const double depth = 0.006 * p[1] * sr;
            const double base = (0.005 + 0.020 * p[2]) * sr;
            const float fb = 0.8f * p[3];
            const double inc = rate / sr;
            for (int i = 0; i < n; ++i)
            {
                phase += inc; if (phase >= 1.0) phase -= 1.0;
                float d[2];
                for (int c = 0; c < 2; ++c)
                {
                    // the right side's sweep is a quarter turn behind for width
                    const double lfo = std::sin (kTwoPi * (phase + (c ? 0.25 : 0.0)));
                    double rp = pos - std::clamp (base + depth * (0.5 + 0.5 * lfo), 1.0, (double) size - 3.0);
                    if (rp < 0) rp += size;
                    const int i0 = (int) rp; const int i1 = (i0 + 1) % size; const float fr = (float) (rp - i0);
                    d[c] = buf[c][(size_t) i0] + (buf[c][(size_t) i1] - buf[c][(size_t) i0]) * fr;
                }
                buf[0][(size_t) pos] = L[i] + d[0] * fb;
                buf[1][(size_t) pos] = R[i] + d[1] * fb;
                pos = (pos + 1) % size;
                L[i] = mixDryWet (L[i], d[0], p[4]);
                R[i] = mixDryWet (R[i], d[1], p[4]);
            }
        }
    private:
        double sr = 48000, phase = 0;
        int size = 0, pos = 0;
        std::vector<float> buf[2];
    };
}

std::unique_ptr<Effect> makeEffect (FxType t)
{
    switch (t)
    {
        case FxType::Filter:     return std::make_unique<FilterFx>();
        case FxType::Drive:      return std::make_unique<DriveFx>();
        case FxType::Compressor: return std::make_unique<CompressorFx>();
        case FxType::Delay:      return std::make_unique<DelayFx>();
        case FxType::Reverb:     return std::make_unique<ReverbFx>();
        case FxType::Chorus:     return std::make_unique<ChorusFx>();
        default:                 return nullptr;
    }
}

// ==================================================================== EQ
ChannelEqProcessor::Coeffs ChannelEqProcessor::design (const EqBand& b, double sr)
{
    Coeffs c;
    const double f = std::clamp ((double) b.freq, 10.0, sr * 0.45);
    const double w = kTwoPi * f / sr, cw = std::cos (w), sw = std::sin (w);
    const double A = std::pow (10.0, b.gain / 40.0);
    double b0, b1, b2, a0, a1, a2;
    if (b.type == EqType::HighPass || b.type == EqType::LowPass)
    {
        const double alpha = sw / (2.0 * 0.7071);
        if (b.type == EqType::HighPass) { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
        else                            { b0 = (1 - cw) / 2; b1 = 1 - cw;    b2 = (1 - cw) / 2; }
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    }
    else if (b.type == EqType::LowShelf || b.type == EqType::HighShelf)
    {
        const double alpha = sw / 2.0 * std::sqrt (2.0);   // shelf slope 1
        const double sq = 2.0 * std::sqrt (A) * alpha;
        if (b.type == EqType::LowShelf)
        {
            b0 = A * ((A + 1) - (A - 1) * cw + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sq);
            a0 = (A + 1) + (A - 1) * cw + sq; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sq;
        }
        else
        {
            b0 = A * ((A + 1) + (A - 1) * cw + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sq);
            a0 = (A + 1) - (A - 1) * cw + sq; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sq;
        }
    }
    else
    {
        const double alpha = sw / (2.0 * std::max (0.1, (double) b.q));
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
    }
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

void ChannelEqProcessor::reset()
{
    for (auto& band : s) for (auto& st : band) st = {};
}

void ChannelEqProcessor::process (float* L, float* R, int n, const ChannelEq& eq)
{
    for (int k = 0; k < kEqBands; ++k)
        if (dirty || ! (eq.band[k] == last.band[k]))
        {
            if (eq.band[k].active() && ! (last.band[k].active() && last.band[k].type == eq.band[k].type)) { s[k][0] = {}; s[k][1] = {}; }
            c[k] = design (eq.band[k], sr);
        }
    last = eq; dirty = false;
    float* ch[2] = { L, R };
    for (int k = 0; k < kEqBands; ++k)
    {
        if (! eq.band[k].active()) continue;
        const Coeffs& q = c[k];
        for (int cc = 0; cc < 2; ++cc)
        {
            auto& st = s[k][cc];
            float* d = ch[cc];
            for (int i = 0; i < n; ++i)
            {
                const double x = d[i];
                const double y = q.b0 * x + st.z1;
                st.z1 = q.b1 * x - q.a1 * y + st.z2;
                st.z2 = q.b2 * x - q.a2 * y;
                d[i] = (float) y;
            }
        }
    }
}

double ChannelEqProcessor::responseDb (const ChannelEq& eq, double freq, double sr)
{
    const std::complex<double> z1 = std::polar (1.0, -kTwoPi * freq / sr);
    const std::complex<double> z2 = z1 * z1;
    double db = 0.0;
    for (int k = 0; k < kEqBands; ++k)
    {
        if (! eq.band[k].active()) continue;
        const Coeffs q = design (eq.band[k], sr);
        const auto h = (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
        db += 20.0 * std::log10 (std::max (1e-9, std::abs (h)));
    }
    return db;
}
} // namespace prv
