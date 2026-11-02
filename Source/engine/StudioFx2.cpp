// Paula's Revenge - the studio effects, part 2 (v0.35): ROOMS, SPRING, DUB DELAY, BBD DELAY,
// GRAIN DELAY, MULTI-TAP, FLANGER, PHASER, ROTARY, TREMOLO, FREQ SHIFTER, AUTO-FILTER, DIMENSION.
// Our own designs; parameters glide (no zipper noise), saturation runs at twice the rate.
#include "StudioFx.h"
#include "FxKit.h"
#include <array>

namespace prv
{
namespace
{
using namespace fxkit;

inline void equalPower2 (float mix, float& dry, float& wet)
{
    dry = std::cos ((float) (mix * kPi * 0.5));
    wet = std::sin ((float) (mix * kPi * 0.5));
}
inline void widen2 (float& l, float& r, float width)
{
    const float m = 0.5f * (l + r), s = 0.5f * (l - r) * width;
    l = m + s; r = m - s;
}
inline double lowCut2 (float v)  { return v < 0.005f ? 0.0 : 20.0 * std::pow (100.0, (double) v); }
inline double highCut2 (float v) { return v > 0.995f ? 0.0 : 500.0 * std::pow (40.0, (double) v); }
inline float bipolar (float v) { return 2.0f * v - 1.0f; }
// a soft saturation with unity gain for small signals, its ceiling 1 / k
inline float satK (float x, float k) { return std::tanh (k * x) / k; }

// small helper: a few smoothed values set together
template <int N> struct Smooths
{
    Smooth s[N];
    void setTime (double t, double sr) { for (auto& x : s) x.setTime (t, sr); }
    void unprime() { for (auto& x : s) x.primed = false; }
    Smooth& operator[] (int i) { return s[i]; }
};

// ================================================================ ROOMS
class RoomsFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        for (auto& d : pre) d.setMax ((int) (sr * (0.25 + 0.09 * kMaxScale)) + 8);
        for (auto& d : line) d.setMax ((int) (sr * 0.13 * kMaxScale) + 64);
        for (auto& a : diff) a.setMax ((int) (sr * 0.02 * kMaxScale) + 8);
        sm.setTime (0.05, sr);
        sScale.setTime (0.3, sr);
        reset();
    }
    void reset() override
    {
        for (auto& d : pre) d.clear();
        for (auto& d : line) d.clear();
        for (auto& a : diff) a.clear();
        for (auto& f : damp) f.reset();
        for (auto& f : outLp) f.reset();
        for (auto& f : outHp) f.reset();
        lfo.phase = 0;
        sm.unprime(); sScale.primed = false;
    }
    void process (float* L, float* R, int n, const float* p, double) override
    {
        struct Type { float e0, e1, l0, l1, inDiff, early; };
        static const Type types[4] = { { 3, 35, 22, 48, 0.55f, 1.0f },      // ROOM
                                       { 6, 55, 33, 71, 0.60f, 1.0f },      // CHAMBER
                                       { 10, 85, 55, 125, 0.65f, 1.0f },    // HALL
                                       { 1, 12, 13, 41, 0.75f, 0.35f } };   // PLATE (dense from the start)
        const Type& ty = types[fxChoice (p[1], 4)];
        sm[0].set (p[0]);
        sScale.set (0.4f + 1.2f * p[2]);
        sm[1].set ((float) (0.25 * p[4] * sr));
        sm[2].set (p[5] * ty.early);
        sm[3].set (0.1f + 0.8f * p[6]);
        sm[4].set (p[9]);
        sm[5].set (p[10]);
        const double t60 = spaceDecaySeconds (p[3]);
        const double lc = lowCut2 (p[7]), hc = highCut2 (p[8]);
        for (auto& f : outHp) f.setLowpass (lc > 0 ? lc : 1.0, sr);
        for (auto& f : outLp) f.setLowpass (hc > 0 ? hc : sr * 0.49, sr);
        lfo.setRate (0.7, sr);
        const double ms = sr * 0.001;
        float gain[N];
        for (int i = 0; i < N; ++i)
            gain[i] = (float) std::pow (10.0, -3.0 * (ty.l0 + (ty.l1 - ty.l0) * kLine[i]) * 0.001 * sScale.target / t60);
        for (int k = 0; k < n; ++k)
        {
            const float mix = sm[0].next(), pd = sm[1].next(), early = sm[2].next(), dmp = sm[3].next(), mod = sm[4].next(), width = sm[5].next();
            const float sc = sScale.next();
            pre[0].write (L[k]); pre[1].write (R[k]);
            // early reflections: a dozen taps on each side
            float er[2] = { 0, 0 };
            for (int c = 0; c < 2; ++c)
                for (int j = 0; j < kTaps; ++j)
                {
                    const float f = kTapPos[c][j];
                    const double at = pd + (ty.e0 + (ty.e1 - ty.e0) * f) * ms * sc + 1.0;
                    er[c] += pre[c].read (at) * (1.0f - 0.8f * f) * ((j & 1) ? -0.22f : 0.22f);
                }
            // the tail: diffused, then 8 lines feeding each other
            float x = 0.5f * (pre[0].read (pd + 1.0) + pre[1].read (pd + 1.0)) + 0.4f * (er[0] + er[1]);
            for (int j = 0; j < 4; ++j) x = diff[j].process (x, kDiff[j] * ms * sc, ty.inDiff);
            float y[N], sum = 0;
            for (int i = 0; i < N; ++i)
            {
                const double len = (ty.l0 + (ty.l1 - ty.l0) * kLine[i]) * ms * sc;
                const double m = i < 4 ? mod * 0.0008 * sr * lfo.sineAt (i * 0.25) : 0.0;
                float v = line[i].read (len + m);
                damp[i].a = dmp;
                v = damp[i].lp (v) * gain[i];
                y[i] = softLimit (v); sum += y[i];
            }
            lfo.advance();
            const float h = 2.0f / N * sum;
            float wl = 0, wr = 0;
            for (int i = 0; i < N; ++i)
            {
                line[i].write (flushDenormal (x * kSign[i] * 0.35f + y[i] - h));
                if (i & 1) wr += y[i] * kSign[i]; else wl += y[i] * kSign[i];
            }
            const float late = 1.0f - 0.5f * early;
            wl = late * 0.5f * wl + early * 1.3f * er[0];
            wr = late * 0.5f * wr + early * 1.3f * er[1];
            if (lc > 0) { wl = outHp[0].hp (wl); wr = outHp[1].hp (wr); }
            wl = outLp[0].lp (wl); wr = outLp[1].lp (wr);
            widen2 (wl, wr, width);
            float dg, wg; equalPower2 (mix, dg, wg);
            L[k] = L[k] * dg + wl * wg;
            R[k] = R[k] * dg + wr * wg;
        }
    }
private:
    static constexpr int N = 8, kTaps = 12;
    static constexpr float kMaxScale = 1.65f;
    static constexpr float kTapPos[2][kTaps] = { { 0.0f, 0.07f, 0.13f, 0.22f, 0.29f, 0.38f, 0.45f, 0.55f, 0.63f, 0.74f, 0.86f, 1.0f },
                                                 { 0.03f, 0.10f, 0.17f, 0.25f, 0.34f, 0.41f, 0.51f, 0.59f, 0.69f, 0.79f, 0.91f, 0.97f } };
    static constexpr float kLine[N] = { 0.0f, 0.57f, 0.21f, 0.86f, 0.43f, 1.0f, 0.12f, 0.71f };
    static constexpr float kDiff[4] = { 4.7f, 3.6f, 12.7f, 9.3f };   // ms
    static constexpr float kSign[N] = { 1, 1, -1, 1, 1, -1, -1, -1 };
    double sr = 48000;
    Delay pre[2], line[N];
    Allpass diff[4];
    OnePole damp[N], outLp[2], outHp[2];
    Lfo lfo;
    Smooths<6> sm;
    Smooth sScale;
};

// ================================================================ SPRING
class SpringFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        for (auto& sp : spring) sp.d.setMax ((int) (sr * 0.08));
        sm.setTime (0.05, sr);
        lfo.setRate (5.3, sr);
        reset();
    }
    void reset() override
    {
        for (auto& sp : spring) { sp.d.clear(); for (auto& a : sp.ap) a.reset(); sp.lp.reset(); sp.fb = 0; }
        inHp.reset(); emph.reset(); os.reset();
        sm.unprime();
    }
    void process (float* L, float* R, int n, const float* p, double) override
    {
        const int springs = 1 + fxChoice (p[3], 3);
        sm[0].set (p[0]); sm[1].set (p[4]); sm[2].set (p[5]); sm[3].set (p[7]);
        for (int j = 0; j < 3; ++j) sm[4 + j].set (j < springs ? 1.0f : 0.0f);
        const double t60 = 0.5 + 4.5 * p[1];
        const double tone = 1500.0 * std::pow (6.0, (double) p[2]);
        for (auto& sp : spring) sp.lp.setLowpass (tone, sr);
        const double lc = lowCut2 (p[6]);
        inHp.setLowpass (lc > 0 ? lc : 1.0, sr);
        emph.setLowpass (2500.0, sr);
        for (int k = 0; k < n; ++k)
        {
            const float mix = sm[0].next(), drip = sm[1].next(), drive = sm[2].next(), width = sm[3].next();
            float on[3]; for (int j = 0; j < 3; ++j) on[j] = sm[4 + j].next();
            float x = 0.5f * (L[k] + R[k]);
            if (lc > 0) x = inHp.hp (x);
            x += drip * 1.5f * emph.hp (x);   // the "drip": the attack pushed into the springs
            const float kd = 1.0f + 5.0f * drive;
            x = os.process (x, [kd] (float v) { return satK (v, kd); });
            const float a = -(0.38f + 0.45f * drip);
            const double wob = 0.0004 * sr * lfo.sine();
            float out[3];
            for (int j = 0; j < 3; ++j)
            {
                auto& sp = spring[j];
                const double len = kLen[j] * 0.001 * sr;
                const float g = (float) std::pow (10.0, -3.0 * (kLen[j] * 0.001) / t60);
                float v = 0.5f * x + sp.fb * g;
                for (auto& ap : sp.ap) v = ap.process (v, a);
                sp.d.write (flushDenormal (v));
                float o = sp.d.read (len + wob * (j + 1) * 0.3);
                o = softLimit (sp.lp.lp (o));
                sp.fb = o;
                out[j] = o * on[j];
            }
            // spring 1 left, 2 right, 3 in the middle
            float wl = out[0] + 0.7f * out[2];
            float wr = out[1] + 0.7f * out[2];
            if (springs == 1) wr = wl;
            widen2 (wl, wr, width);
            float dg, wg; equalPower2 (mix, dg, wg);
            L[k] = L[k] * dg + wl * wg * 1.4f;
            R[k] = R[k] * dg + wr * wg * 1.4f;
        }
    }
private:
    static constexpr float kLen[3] = { 37.3f, 43.1f, 51.7f };   // ms each spring takes
    struct Spring { Delay d; Allpass1 ap[12]; OnePole lp; float fb = 0; };
    double sr = 48000;
    Spring spring[3];
    OnePole inHp, emph;
    Oversampler2x os;
    Lfo lfo;
    Smooths<7> sm;
};

// ================================================================ DUB DELAY
class DubDelayFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        for (auto& d : line) d.setMax ((int) (sr * 2.1));
        sm.setTime (0.05, sr);
        sTime.setTime (0.3, sr);
        sHold.setTime (0.03, sr);
        sCut.setTime (0.08, sr);
        lfo.setRate (0.35, sr);
        reset();
    }
    void reset() override
    {
        for (auto& d : line) d.clear();
        for (auto& f : svf) f.reset();
        for (auto& o : os) o.reset();
        last[0] = last[1] = 0;
        sm.unprime(); sTime.primed = sHold.primed = sCut.primed = false;
    }
    void process (float* L, float* R, int n, const float* p, double bpm) override
    {
        sTime.set ((float) std::clamp (delaySeconds (p[0], p[1] > 0.5f, bpm) * sr, 16.0, sr * 2.0));
        sm[0].set (1.1f * p[2]); sm[1].set (p[4]); sm[2].set (p[5]); sm[3].set (p[8]); sm[4].set (p[9]);
        sCut.set ((float) (200.0 * std::pow (25.0, (double) p[3])));
        sHold.set (p[6] > 0.5f ? 1.0f : 0.0f);
        const bool ping = p[7] > 0.5f;
        const double wobSpan = 0.003 * sr / (kTwoPi * 0.35);
        for (int k = 0; k < n; ++k)
        {
            const float t = sTime.next(), fb0 = sm[0].next(), reso = sm[1].next(), drive = sm[2].next(), wob = sm[3].next(), mix = sm[4].next();
            const float hold = sHold.next(), cut = sCut.next();
            const double m = wob * wob * wobSpan * lfo.sine();
            float y[2] = { line[0].read (t + m), line[1].read (t - m) };
            const float g = Svf::gFor (cut, sr), kq = 1.4f - 1.25f * reso, kd = 1.0f + 3.0f * drive;
            float f[2];
            for (int c = 0; c < 2; ++c)
            {
                float lp, bp, hp;
                svf[c].tick (y[c], g, kq, lp, bp, hp);
                const float filtered = os[c].process (bp * kq * 1.05f, [kd] (float v) { return satK (v, kd); });
                // HOLD: the loop goes round untouched (and nothing new comes in)
                f[c] = filtered * (1.0f - hold) + y[c] * hold;
            }
            const float fb = fb0 * (1.0f - hold) + hold;
            const float in = 1.0f - hold;
            if (ping)
            {
                line[0].write (flushDenormal (0.5f * (L[k] + R[k]) * in + fb * f[1]));
                line[1].write (flushDenormal (fb * f[0]));
            }
            else
            {
                line[0].write (flushDenormal (L[k] * in + fb * f[0]));
                line[1].write (flushDenormal (R[k] * in + fb * f[1]));
            }
            float dg, wg; equalPower2 (mix, dg, wg);
            L[k] = L[k] * dg + f[0] * wg;
            R[k] = R[k] * dg + f[1] * wg;
        }
    }
private:
    double sr = 48000;
    Delay line[2];
    Svf svf[2];
    Oversampler2x os[2];
    Lfo lfo;
    float last[2] {};
    Smooths<5> sm;
    Smooth sTime, sHold, sCut;
};

// ================================================================ BBD DELAY
class BbdDelayFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        for (auto& d : line) d.setMax ((int) (sr * 1.1));
        sm.setTime (0.05, sr);
        sTime.setTime (0.3, sr);
        reset();
    }
    void reset() override
    {
        for (auto& d : line) d.clear();
        for (auto& f : pre) f.reset();
        for (auto& f : post) f.reset();
        lfo.phase = 0;
        sm.unprime(); sTime.primed = false;
    }
    void process (float* L, float* R, int n, const float* p, double bpm) override
    {
        const double secs = std::clamp (delaySeconds (p[0], p[1] > 0.5f, bpm), 0.02, 1.0);
        sTime.set ((float) (secs * sr));
        sm[0].set (1.05f * p[2]); sm[1].set (p[4]); sm[2].set (p[5]); sm[3].set (p[6]); sm[4].set (p[7]); sm[5].set (p[8]);
        const double rate = lfoHz (p[3]);
        lfo.setRate (rate, sr);
        // a bucket brigade gets darker the longer it's clocked to be
        const double bw = std::clamp (3500.0 * std::pow (0.25 / secs, 0.6), 1800.0, 12000.0) * (1.0 - 0.4 * p[5]);
        for (auto& f : pre) f.setLowpass (bw, sr);
        for (auto& f : post) f.setLowpass (bw * 1.2, sr);
        const double depthMax = 0.02 * sr / (kTwoPi * rate);
        for (int k = 0; k < n; ++k)
        {
            const float t = sTime.next(), fb = sm[0].next(), depth = sm[1].next(), age = sm[2].next(), drive = sm[3].next();
            const float width = sm[4].next(), mix = sm[5].next();
            const double s1 = lfo.sine();
            const double a = std::min ({ depth * depth * depthMax, 0.45 * t, 0.012 * sr });
            float w[2];
            w[0] = line[0].read (t + a * s1);
            w[1] = line[1].read (t * (1.0 + 0.07 * width) - a * s1 * width + a * s1 * (1.0f - width));
            const float kd = 1.0f + 3.0f * drive;
            for (int c = 0; c < 2; ++c)
            {
                w[c] = post[2 * c + 1].lp (post[2 * c].lp (w[c]));
                const float in = c == 0 ? L[k] : R[k];
                const float x = pre[2 * c + 1].lp (pre[2 * c].lp (in)) + fb * satK (w[c], kd);
                line[c].write (flushDenormal (x + noise.white() * age * age * 0.0015f));
            }
            float dg, wg; equalPower2 (mix, dg, wg);
            L[k] = L[k] * dg + w[0] * wg;
            R[k] = R[k] * dg + w[1] * wg;
        }
    }
private:
    double sr = 48000;
    Delay line[2];
    OnePole pre[4], post[4];
    Lfo lfo;
    Noise noise;
    Smooths<6> sm;
    Smooth sTime;
};

// ================================================================ GRAIN DELAY (REVERSE / GRAINS)
class GrainFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        for (auto& d : buf) d.setMax ((int) (sr * 6.5));
        sm.setTime (0.05, sr);
        sT.setTime (0.2, sr);
        reset();
    }
    void reset() override
    {
        for (auto& d : buf) d.clear();
        for (auto& v : voice) v.on = false;
        ph = 0; untilNext = 0; fbv[0] = fbv[1] = 0;
        sm.unprime(); sT.primed = false;
    }
    void process (float* L, float* R, int n, const float* p, double bpm) override
    {
        const bool grains = fxChoice (p[0], 2) == 1;
        sT.set ((float) (std::clamp (delaySeconds (p[1], p[2] > 0.5f, bpm), 0.05, 2.0) * sr));
        sm[0].set (0.95f * p[3]); sm[1].set (p[5]); sm[2].set (p[6]); sm[3].set (p[7]); sm[4].set (p[8]);
        const double semis = grainPitch (p[4]);
        const double r = std::pow (2.0, semis / 12.0);
        for (int k = 0; k < n; ++k)
        {
            const float T = sT.next(), fb = sm[0].next(), spray = sm[1].next(), dens = sm[2].next(), width = sm[3].next(), mix = sm[4].next();
            float wl = 0, wr = 0;
            if (! grains)
            {
                // two readers running backwards through the last stretch, half a stretch apart
                for (int j = 0; j < 2; ++j)
                {
                    double q = ph + 0.5 * j; if (q >= 1.0) q -= 1.0;
                    const double d = 2.0 + (1.0 + r) * q * T;
                    const float w = (float) std::sin (kPi * q);
                    wl += w * w * buf[0].read (d);
                    wr += w * w * buf[1].read (d);
                }
                ph += 1.0 / T; if (ph >= 1.0) ph -= 1.0;
            }
            else
            {
                const double G = std::clamp (0.5 * T, 0.02 * sr, sr * 1.0);
                if (--untilNext <= 0)
                {
                    untilNext = (int) std::max (8.0, G / (1.0 + 7.0 * dens));
                    for (auto& v : voice)
                        if (! v.on)
                        {
                            v.on = true; v.t = 0; v.len = G;
                            v.rate = r * std::pow (2.0, spray * 0.15 * noise.white() / 12.0);
                            v.d0 = T * (0.5 + spray * 0.5 * (1.0 + noise.white())) + std::max (0.0, v.rate - 1.0) * G + 4.0;
                            v.pan = width * noise.white();
                            break;
                        }
                }
                const float norm = 1.2f / std::sqrt (1.0f + 7.0f * dens);
                for (auto& v : voice)
                {
                    if (! v.on) continue;
                    const double d = v.d0 + (1.0 - v.rate) * v.t;
                    const float w = (float) std::sin (kPi * v.t / v.len);
                    const float x = 0.5f * (buf[0].read (d) + buf[1].read (d)) * w * w * norm;
                    wl += x * (1.0f - v.pan) * 0.7071f; wr += x * (1.0f + v.pan) * 0.7071f;
                    if (++v.t >= v.len) v.on = false;
                }
            }
            if (! grains) widen2 (wl, wr, width);
            buf[0].write (flushDenormal (L[k] + fb * softLimit (wl)));
            buf[1].write (flushDenormal (R[k] + fb * softLimit (wr)));
            float dg, wg; equalPower2 (mix, dg, wg);
            L[k] = L[k] * dg + wl * wg;
            R[k] = R[k] * dg + wr * wg;
        }
    }
private:
    struct Voice { bool on = false; double t = 0, len = 1, d0 = 0, rate = 1; float pan = 0; };
    double sr = 48000;
    Delay buf[2];
    Voice voice[16];
    Noise noise;
    double ph = 0;
    int untilNext = 0;
    float fbv[2] {};
    Smooths<5> sm;
    Smooth sT;
};

// ================================================================ MULTI-TAP
class MultiTapFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        buf.setMax ((int) (sr * 8.2));
        sm.setTime (0.05, sr);
        sStep.setTime (0.25, sr);
        for (auto& g : tapOn) g.setTime (0.02, sr);
        reset();
    }
    void reset() override
    {
        buf.clear(); tone[0].reset(); tone[1].reset();
        sm.unprime(); sStep.primed = false;
        for (auto& g : tapOn) g.primed = false;
    }
    void process (float* L, float* R, int n, const float* p, double bpm) override
    {
        static const float pos[7][8] = { { 1, 2, 3, 4, 5, 6, 7, 8 },                                   // STRAIGHT
                                         { 1.5f, 3, 4.5f, 6, 7.5f, 9, 10.5f, 12 },                     // DOTTED
                                         { 0.667f, 1.333f, 2, 2.667f, 3.333f, 4, 4.667f, 5.333f },     // TRIPLET
                                         { 1.333f, 2, 3.333f, 4, 5.333f, 6, 7.333f, 8 },               // SWING
                                         { 2, 3.6f, 4.8f, 5.7f, 6.3f, 6.75f, 7.05f, 7.25f },           // SPEED UP (gaps shrink)
                                         { 0.5f, 1.2f, 2.1f, 3.2f, 4.5f, 6, 7.7f, 9.6f },              // SLOW DOWN
                                         { 0.7f, 1.9f, 2.4f, 3.6f, 4.1f, 5.5f, 6.2f, 7.3f } };         // SCATTER
        const int pattern = fxChoice (p[2], 7), taps = 1 + fxChoice (p[3], 8);
        sStep.set ((float) (delaySeconds (p[0], p[1] > 0.5f, bpm) * sr));
        sm[0].set (p[4]); sm[1].set (0.9f * p[5]); sm[2].set (p[6]); sm[3].set (p[8]);
        for (int j = 0; j < 8; ++j) tapOn[j].set (j < taps ? 1.0f : 0.0f);
        for (auto& t : tone) t.setLowpass (tapeToneHz (p[7]), sr);
        const double maxD = sr * 8.0;
        for (int k = 0; k < n; ++k)
        {
            const float step = sStep.next(), decay = sm[0].next(), fb = sm[1].next(), spread = sm[2].next(), mix = sm[3].next();
            float wl = 0, wr = 0, lastTap = 0, lvl = 1.0f;
            for (int j = 0; j < 8; ++j)
            {
                const float on = tapOn[j].next();
                if (on > 0.0001f)
                {
                    const float v = buf.read (std::min ((double) step * pos[pattern][j], maxD)) * lvl * on;
                    const float pan = taps == 1 ? 0.0f : spread * ((j & 1) ? 1.0f : -1.0f);
                    const float a = (float) ((pan + 1.0f) * kPi * 0.25);
                    wl += v * std::cos (a) * 1.4142f; wr += v * std::sin (a) * 1.4142f;
                    if (j == taps - 1) lastTap = v / std::max (0.05f, lvl);
                }
                lvl *= 1.0f - 0.85f * decay;
            }
            buf.write (flushDenormal (0.5f * (L[k] + R[k]) + fb * softLimit (lastTap)));
            wl = tone[0].lp (wl); wr = tone[1].lp (wr);
            float dg, wg; equalPower2 (mix, dg, wg);
            L[k] = L[k] * dg + wl * wg;
            R[k] = R[k] * dg + wr * wg;
        }
    }
private:
    double sr = 48000;
    Delay buf;
    OnePole tone[2];
    Smooths<4> sm;
    Smooth sStep, tapOn[8];
};

// ================================================================ FLANGER
class FlangerFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        for (auto& d : line) d.setMax ((int) (sr * 0.02));
        for (auto& d : dry) d.setMax ((int) (sr * 0.02));
        sm.setTime (0.05, sr);
        reset();
    }
    void reset() override
    {
        for (auto& d : line) d.clear();
        for (auto& d : dry) d.clear();
        lfo.phase = 0; wet[0] = wet[1] = 0;
        sm.unprime();
    }
    void process (float* L, float* R, int n, const float* p, double) override
    {
        lfo.setRate (lfoHz (p[0]), sr);
        sm[0].set (p[1]); sm[1].set ((float) ((0.3 + 7.7 * p[2] * p[2]) * 0.001 * sr)); sm[2].set (0.95f * bipolar (p[3]));
        sm[3].set (p[4] > 0.5f ? 1.0f : 0.0f); sm[4].set (p[5]); sm[5].set (p[6]);
        for (int k = 0; k < n; ++k)
        {
            const float depth = sm[0].next(), d0 = sm[1].next(), fb = sm[2].next(), tz = sm[3].next(), stereo = sm[4].next(), mix = sm[5].next();
            float* io[2] = { L + k, R + k };
            for (int c = 0; c < 2; ++c)
            {
                const double s = lfo.triAt (c * 0.5 * stereo);
                // through-zero: the dry is held back by the centre delay, the wet sweeps both sides of it
                const double span = depth * (tz * 1.0 + (1.0f - tz) * 0.92);
                const double d = std::max (1.0, d0 * (1.0 + span * s) + 1.0);
                const float x = *io[c];
                dry[c].write (x);
                const float w = line[c].read (d);
                line[c].write (flushDenormal (softLimit (x + fb * w)));
                const float ref = x * (1.0f - tz) + dry[c].read (d0 + 1.0) * tz;
                *io[c] = ref * (1.0f - mix) + w * mix;
            }
            lfo.advance();
        }
    }
private:
    double sr = 48000;
    Delay line[2], dry[2];
    Lfo lfo;
    float wet[2] {};
    Smooths<6> sm;
};

// ================================================================ PHASER
class PhaserFx : public Effect
{
public:
    void prepare (double s) override { sr = s; sm.setTime (0.05, sr); reset(); }
    void reset() override
    {
        for (auto& c : ap) for (auto& a : c) a.reset();
        last[0] = last[1] = 0; lfo.phase = 0;
        sm.unprime();
    }
    void process (float* L, float* R, int n, const float* p, double) override
    {
        const int stages = 4 + 4 * fxChoice (p[0], 3);
        lfo.setRate (lfoHz (p[1]), sr);
        sm[0].set (p[2]); sm[1].set ((float) (200.0 * std::pow (10.0, (double) p[3]))); sm[2].set (0.9f * bipolar (p[4]));
        sm[3].set (p[5]); sm[4].set (p[6]);
        for (int k = 0; k < n; ++k)
        {
            const float depth = sm[0].next(), centre = sm[1].next(), fb = sm[2].next(), stereo = sm[3].next(), mix = sm[4].next();
            float* io[2] = { L + k, R + k };
            for (int c = 0; c < 2; ++c)
            {
                const double s = lfo.sineAt (c * 0.5 * stereo);
                const float a = Allpass1::coefFor (centre * std::pow (2.0, 3.0 * depth * s), sr);
                float v = *io[c] + fb * last[c];
                for (int j = 0; j < stages; ++j) v = ap[c][j].process (v, a);
                last[c] = flushDenormal (softLimit (v));
                *io[c] = *io[c] * (1.0f - mix) + v * mix;
            }
            lfo.advance();
        }
    }
private:
    double sr = 48000;
    Allpass1 ap[2][12];
    float last[2] {};
    Lfo lfo;
    Smooths<5> sm;
};

// ================================================================ ROTARY (a rotating speaker)
class RotaryFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        horn.setMax ((int) (sr * 0.006)); drum.setMax ((int) (sr * 0.006));
        xo1.setLowpass (800.0, sr); xo2.setLowpass (800.0, sr);
        sm.setTime (0.05, sr);
        reset();
    }
    void reset() override
    {
        horn.clear(); drum.clear(); xo1.reset(); xo2.reset(); os.reset();
        hornHz = 0.8; drumHz = 0.67; hornPh = drumPh = 0;
        sm.unprime();
    }
    void process (float* L, float* R, int n, const float* p, double) override
    {
        const bool fast = p[0] > 0.5f;
        const double accel = 0.3 + 3.7 * p[1];
        const double hornTarget = fast ? 6.7 : 0.8, drumTarget = fast ? 5.8 : 0.67;
        const double hc = std::exp (-1.0 / (accel * 0.6 * sr)), dc = std::exp (-1.0 / (accel * 1.6 * sr));
        sm[0].set (p[2]); sm[1].set (p[3]); sm[2].set (p[4]); sm[3].set (p[5]);
        const double hornSwing = 0.00045 * sr, drumSwing = 0.00015 * sr;
        for (int k = 0; k < n; ++k)
        {
            const float bal = sm[0].next(), drive = sm[1].next(), width = sm[2].next(), mix = sm[3].next();
            hornHz = hornTarget + (hornHz - hornTarget) * hc;
            drumHz = drumTarget + (drumHz - drumTarget) * dc;
            hornPh += hornHz / sr; if (hornPh >= 1) hornPh -= 1;
            drumPh += drumHz / sr; if (drumPh >= 1) drumPh -= 1;
            const float kd = 1.0f + 5.0f * drive;
            float x = 0.5f * (L[k] + R[k]);
            x = os.process (x, [kd] (float v) { return satK (v, kd); }) * (1.0f + 0.6f * drive);
            const float lo = xo2.lp (xo1.lp (x)), hi = x - lo;
            horn.write (hi); drum.write (lo);
            const float hg = std::min (1.0f, 2.0f * bal), dg2 = std::min (1.0f, 2.0f * (1.0f - bal));
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                const double mic = (c == 0 ? 0.25 : -0.25) * width;   // the two microphones, either side
                const double th = kTwoPi * (hornPh + mic), ph = kTwoPi * (drumPh + mic);
                const float h = horn.read (0.0015 * sr + hornSwing * std::sin (th)) * (float) (1.0 - 0.4 * (0.5 + 0.5 * std::cos (th)));
                const float d = drum.read (0.0010 * sr + drumSwing * std::sin (ph)) * (float) (1.0 - 0.25 * (0.5 + 0.5 * std::cos (ph)));
                out[c] = 1.2f * (hg * h + dg2 * d);
            }
            float dg, wg; equalPower2 (mix, dg, wg);
            L[k] = L[k] * dg + out[0] * wg;
            R[k] = R[k] * dg + out[1] * wg;
        }
    }
private:
    double sr = 48000;
    Delay horn, drum;
    OnePole xo1, xo2;
    Oversampler2x os;
    double hornHz = 0.8, drumHz = 0.67, hornPh = 0, drumPh = 0;
    Smooths<4> sm;
};

// ================================================================ TREMOLO / AUTO-PAN / HARMONIC
class TremoloFx : public Effect
{
public:
    void prepare (double s) override { sr = s; sm.setTime (0.03, sr); split[0].setLowpass (600.0, sr); split[1].setLowpass (600.0, sr); reset(); }
    void reset() override { lfo.phase = 0; split[0].reset(); split[1].reset(); sm.unprime(); }
    void process (float* L, float* R, int n, const float* p, double bpm) override
    {
        const int mode = fxChoice (p[0], 3);
        lfo.setRate (p[2] > 0.5f ? syncedHz (p[1], bpm) : tremoloHz (p[1]), sr);
        sm[0].set (p[3]); sm[1].set (p[4]); sm[2].set (p[5]);
        for (int k = 0; k < n; ++k)
        {
            const float depth = sm[0].next(), shape = sm[1].next(), stereo = sm[2].next();
            const float sk = 1.0f + 20.0f * shape;
            const auto wave = [&] (double off) { return std::tanh (sk * (float) lfo.sineAt (off)) / std::tanh (sk); };
            const float a = wave (0), b = wave (0.5 * stereo);
            if (mode == 0)
            {
                L[k] *= 1.0f - depth * 0.5f * (1.0f - a);
                R[k] *= 1.0f - depth * 0.5f * (1.0f - b);
            }
            else if (mode == 1)
            {
                const float pan = depth * a, ang = (float) ((pan + 1.0f) * kPi * 0.25);
                L[k] *= std::cos (ang) * 1.4142f;
                R[k] *= std::sin (ang) * 1.4142f;
            }
            else
            {
                // the low and the high half pulse in turn
                float* io[2] = { L + k, R + k };
                const float w[2] = { a, b };
                for (int c = 0; c < 2; ++c)
                {
                    const float lo = split[c].lp (*io[c]), hi = *io[c] - lo;
                    *io[c] = lo * (1.0f - depth * 0.5f * (1.0f - w[c])) + hi * (1.0f - depth * 0.5f * (1.0f + w[c]));
                }
            }
            lfo.advance();
        }
    }
private:
    double sr = 48000;
    Lfo lfo;
    OnePole split[2];
    Smooths<3> sm;
};

// ================================================================ FREQ SHIFTER / RING MOD
class FreqShiftFx : public Effect
{
public:
    void prepare (double s) override { sr = s; sm.setTime (0.05, sr); reset(); }
    void reset() override { for (auto& h : hil) h.reset(); ph[0] = ph[1] = 0; last[0] = last[1] = 0; sm.unprime(); }
    void process (float* L, float* R, int n, const float* p, double) override
    {
        const bool ring = fxChoice (p[0], 2) == 1;
        const double fine = 20.0 * p[2] - 10.0;
        sm[0].set ((float) ((ring ? ringHz (p[1]) : freqShiftHz (p[1])) + fine));
        sm[1].set (0.9f * p[3]); sm[2].set (p[4]); sm[3].set (p[5]);
        for (int k = 0; k < n; ++k)
        {
            const float f = sm[0].next(), fb = sm[1].next(), stereo = sm[2].next(), mix = sm[3].next();
            float* io[2] = { L + k, R + k };
            for (int c = 0; c < 2; ++c)
            {
                const double fc = c == 0 ? f : (ring ? f : f * (1.0 - 2.0 * stereo));
                double& q = ph[c];
                const double phase = ring && c == 1 ? q + 0.25 * stereo : q;
                const float x = *io[c] + fb * last[c];
                float y;
                if (ring) y = x * (float) std::sin (kTwoPi * phase);
                else
                {
                    float re, im;
                    hil[c].process (x, re, im);
                    y = re * (float) std::cos (kTwoPi * q) + im * (float) std::sin (kTwoPi * q);
                }
                q += fc / sr; q -= std::floor (q);
                last[c] = flushDenormal (softLimit (y));
                float dg, wg; equalPower2 (mix, dg, wg);
                *io[c] = *io[c] * dg + y * wg;
            }
        }
    }
private:
    double sr = 48000;
    Hilbert hil[2];
    double ph[2] {};
    float last[2] {};
    Smooths<4> sm;
};

// ================================================================ AUTO-FILTER (LFO and envelope)
class AutoFilterFx : public Effect
{
public:
    void prepare (double s) override { sr = s; sm.setTime (0.03, sr); reset(); }
    void reset() override { svf[0].reset(); svf[1].reset(); env = 0; lfo.phase = 0; sm.unprime(); }
    void process (float* L, float* R, int n, const float* p, double bpm) override
    {
        const int mode = fxChoice (p[0], 3);
        lfo.setRate (p[4] > 0.5f ? syncedHz (p[3], bpm) : lfoHz (p[3]), sr);
        sm[0].set (p[1]); sm[1].set (p[2]); sm[2].set (4.0f * p[5]); sm[3].set (8.0f * p[6] - 4.0f); sm[4].set (p[8]); sm[5].set (p[9]);
        const double att = (1.0 + 49.0 * p[7]) * 0.001, rel = (20.0 + 980.0 * p[7]) * 0.001;
        const float ac = (float) std::exp (-1.0 / (att * sr)), rc = (float) std::exp (-1.0 / (rel * sr));
        for (int k = 0; k < n; ++k)
        {
            const float cut = sm[0].next(), reso = sm[1].next(), lfoOct = sm[2].next(), envOct = sm[3].next(), drive = sm[4].next(), mix = sm[5].next();
            const float level = std::max (std::abs (L[k]), std::abs (R[k]));
            env = level > env ? level + (env - level) * ac : level + (env - level) * rc;
            const double oct = lfoOct * lfo.sine() + envOct * std::min (1.0f, env * 3.0f);
            const double hz = 20.0 * std::pow (1000.0, (double) cut) * std::pow (2.0, oct);
            const float g = Svf::gFor (hz, sr), kq = 2.0f - 1.9f * reso, kd = 1.0f + 4.0f * drive;
            float* io[2] = { L + k, R + k };
            for (int c = 0; c < 2; ++c)
            {
                float lp, bp, hp;
                svf[c].tick (satK (*io[c], kd), g, kq, lp, bp, hp);
                const float y = mode == 0 ? lp : mode == 1 ? bp * kq : hp;
                *io[c] = *io[c] * (1.0f - mix) + y * mix;
            }
        }
    }
private:
    double sr = 48000;
    Svf svf[2];
    float env = 0;
    Lfo lfo;
    Smooths<6> sm;
};

// ================================================================ DIMENSION
class DimensionFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        for (auto& d : line) d.setMax ((int) (sr * 0.02));
        pre.setLowpass (9000.0, sr);
        sm.setTime (0.1, sr);
        reset();
    }
    void reset() override { for (auto& d : line) d.clear(); pre.reset(); lfo.phase = 0; sm.unprime(); }
    void process (float* L, float* R, int n, const float* p, double) override
    {
        static const float rate[4] = { 0.25f, 0.25f, 0.3f, 0.45f }, depth[4] = { 0.35f, 0.55f, 0.75f, 1.0f };
        const int mode = fxChoice (p[0], 4);
        lfo.setRate (rate[mode], sr);
        sm[0].set (depth[mode] * 0.0015f * (float) sr); sm[1].set (p[1]); sm[2].set (p[2]);
        const double centre = 0.003 * sr;
        for (int k = 0; k < n; ++k)
        {
            const float sw = sm[0].next(), width = sm[1].next(), mix = sm[2].next();
            const float x = pre.lp (0.5f * (L[k] + R[k]));
            line[0].write (x); line[1].write (x);
            const double t = lfo.tri();
            const float b1 = line[0].read (centre + sw * t), b2 = line[1].read (centre - sw * t);
            // each side hears its line and a little of the other one, upside down: wide, yet sums to mono cleanly
            const float wl = (b1 - 0.7f * b2) * width, wr = (b2 - 0.7f * b1) * width;
            L[k] += 0.6f * mix * wl;
            R[k] += 0.6f * mix * wr;
        }
    }
private:
    double sr = 48000;
    Delay line[2];
    OnePole pre;
    Lfo lfo;
    Smooths<3> sm;
};
} // namespace

std::unique_ptr<Effect> makeStudioEffect2 (FxType t)
{
    switch (t)
    {
        case FxType::Rooms:      return std::make_unique<RoomsFx>();
        case FxType::Spring:     return std::make_unique<SpringFx>();
        case FxType::DubDelay:   return std::make_unique<DubDelayFx>();
        case FxType::BbdDelay:   return std::make_unique<BbdDelayFx>();
        case FxType::Grain:      return std::make_unique<GrainFx>();
        case FxType::MultiTap:   return std::make_unique<MultiTapFx>();
        case FxType::Flanger:    return std::make_unique<FlangerFx>();
        case FxType::Phaser:     return std::make_unique<PhaserFx>();
        case FxType::Rotary:     return std::make_unique<RotaryFx>();
        case FxType::Tremolo:    return std::make_unique<TremoloFx>();
        case FxType::FreqShift:  return std::make_unique<FreqShiftFx>();
        case FxType::AutoFilter: return std::make_unique<AutoFilterFx>();
        case FxType::Dimension:  return std::make_unique<DimensionFx>();
        default:                 return nullptr;
    }
}
} // namespace prv
