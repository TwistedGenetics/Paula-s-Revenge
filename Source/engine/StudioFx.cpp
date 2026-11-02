// Paula's Revenge - the studio effects (see StudioFx.h)
#include "StudioFx.h"
#include "FxKit.h"
#include <array>

namespace prv
{
namespace
{
using namespace fxkit;

inline void equalPower (float mix, float& dry, float& wet)
{
    dry = std::cos ((float) (mix * kPi * 0.5));
    wet = std::sin ((float) (mix * kPi * 0.5));
}

inline void widen (float& l, float& r, float width)
{
    const float m = 0.5f * (l + r), s = 0.5f * (l - r) * width;
    l = m + s; r = m - s;
}

inline double lowCutHz (float v)  { return v < 0.005f ? 0.0 : 20.0 * std::pow (100.0, (double) v); }
inline double highCutHz (float v) { return v > 0.995f ? 0.0 : 500.0 * std::pow (40.0, (double) v); }

// ================================================================ VAST
class VastFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        maxLen = (int) (sr * 4.0);
        for (int i = 0; i < N; ++i) { line[i].setMax (maxLen + 64); ap[i].setMax ((int) (sr * 0.06)); }
        for (auto& c : bloom) for (auto& a : c) a.setMax ((int) (sr * 0.08));
        for (auto* sm : { &sMix, &sFb, &sWarp, &sDens, &sDepth, &sWidth }) sm->setTime (0.05, sr);
        sD.setTime (0.25, sr);
        reset();
    }
    void reset() override
    {
        for (int i = 0; i < N; ++i) { line[i].clear(); ap[i].clear(); lp[i].reset(); hp[i].reset(); }
        for (auto& c : bloom) for (auto& a : c) a.clear();
        lfo.phase = 0;
        for (auto* sm : { &sMix, &sD, &sFb, &sWarp, &sDens, &sDepth, &sWidth }) sm->primed = false;
    }
    void process (float* L, float* R, int n, const float* p, double bpm) override
    {
        struct Mode { float spread, diff, fbBoost, darker; bool bloom, drift; };
        static const Mode modes[4] = { { 1.0f, 1.0f, 0.0f, 1.0f, false, false },    // NEBULA
                                       { 2.2f, 1.15f, 0.4f, 0.6f, false, false },   // ABYSS: longer, darker, longer tail
                                       { 1.0f, 1.25f, 0.1f, 0.9f, true, false },    // BLOOM: swells in
                                       { 1.3f, 0.8f, 0.0f, 1.0f, false, true } };  // DRIFT: wandering lines
        const Mode& m = modes[fxChoice (p[10], 4)];
        const double D = std::clamp (delaySeconds (p[1], p[2] > 0.5f, bpm) * sr, 8.0, maxLen / (1.0 + m.spread));
        sMix.set (p[0]); sD.set ((float) D);
        const float fb = std::clamp (p[3], 0.0f, 1.0f);
        sFb.set (std::min (1.0f, fb + (1.0f - fb) * fb * m.fbBoost));
        sWarp.set (p[4]); sDens.set (p[5]); sDepth.set (p[7]); sWidth.set (p[11]);
        const double rate = vastModRateHz (p[6]);
        lfo.setRate (rate, sr);
        for (auto& w : wander) w.setRate (rate * 2.0, sr);
        const double depthMax = std::min (0.012, 0.025 / (kTwoPi * rate)) * sr;   // keeps the pitch wobble gentle
        const double lc = lowCutHz (p[8]), hc = highCutHz (p[9]);
        for (int i = 0; i < N; ++i)
        {
            hp[i].setLowpass (lc > 0 ? lc : 1.0, sr);
            lp[i].setLowpass (hc > 0 ? hc * m.darker : sr * 0.49 * (m.darker < 1 ? 0.5 : 1.0), sr);
        }
        for (int k = 0; k < n; ++k)
        {
            const float mix = sMix.next(), d = sD.next(), g = sFb.next(), warp = sWarp.next(), dens = sDens.next();
            const float depth = sDepth.next(), width = sWidth.next();
            float inL = L[k], inR = R[k];
            if (m.bloom)
            {
                static const float bl[4] = { 0.0131f, 0.0197f, 0.0293f, 0.0411f };
                for (int j = 0; j < 4; ++j)
                {
                    inL = bloom[0][j].process (inL, bl[j] * sr, 0.65f);
                    inR = bloom[1][j].process (inR, bl[j] * 1.07 * sr, 0.65f);
                }
            }
            const float apG = std::min (0.82f, 0.72f * warp * m.diff);
            const double modAmt = depth * depth * depthMax;
            float y[N], sum = 0;
            for (int i = 0; i < N; ++i)
            {
                const double len = d * (1.0 + dens * kSpread[i] * m.spread);
                const double apLen = std::min (0.3 * len, (0.004 + 0.03 * warp) * sr * kApRatio[i]);
                const double mod = m.drift ? wander[i].next() : lfo.sineAt ((double) i / N);
                const double readAt = std::max (2.0, len - apLen + modAmt * (1.0 + mod));
                float v = line[i].read (readAt);
                v = ap[i].process (v, std::max (1.0, apLen), apG);
                if (lc > 0) v = hp[i].hp (v);
                v = lp[i].lp (v);
                v = softLimit (v);
                y[i] = v; sum += v;
            }
            lfo.advance();
            float wl = 0, wr = 0;
            const float h = 2.0f / N * sum;
            for (int i = 0; i < N; ++i)
            {
                if (i & 1) wr += y[i] * kSign[i]; else wl += y[i] * kSign[i];
                const float x = (i & 1 ? inR : inL) * 0.5f * kSign[i];
                line[i].write (flushDenormal (x + g * (y[i] - h)));
            }
            wl *= 0.5f; wr *= 0.5f;
            widen (wl, wr, width);
            float dg, wg; equalPower (mix, dg, wg);
            L[k] = L[k] * dg + wl * wg;
            R[k] = R[k] * dg + wr * wg;
        }
    }
private:
    static constexpr int N = 8;
    // how far each line's length spreads with DENSITY (line 0 keeps the DELAY time), the in-loop
    // allpasses' relative lengths, and the signs (each channel's sum to 0: clean repeats when
    // DENSITY is 0)
    static constexpr float kSpread[N] = { 0.0f, 0.113f, 0.241f, 0.367f, 0.509f, 0.631f, 0.787f, 0.919f };
    static constexpr float kApRatio[N] = { 1.0f, 0.83f, 0.71f, 0.93f, 0.61f, 0.77f, 0.89f, 0.67f };
    static constexpr float kSign[N] = { 1, 1, -1, 1, 1, -1, -1, -1 };
    double sr = 48000;
    int maxLen = 1;
    Delay line[N];
    Allpass ap[N];
    Allpass bloom[2][4];
    OnePole lp[N], hp[N];
    Lfo lfo;
    Wander wander[N];
    Smooth sMix, sD, sFb, sWarp, sDens, sDepth, sWidth;
};

// ================================================================ SPACE
class SpaceFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        const double k = sr / 29761.0 * kMaxSize;
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < 4; ++j) inAp[c][j].setMax ((int) (kIn[j] * k * 1.1) + 8);
        for (int t = 0; t < 2; ++t)
        {
            tAp1[t].setMax ((int) ((kAp1[t] + 40) * k) + 8);
            tDel1[t].setMax ((int) (kDel1[t] * k) + 8);
            tAp2[t].setMax ((int) (kAp2[t] * k) + 8);
            tDel2[t].setMax ((int) (kDel2[t] * k) + 8);
        }
        pre.setMax ((int) (sr * 0.55));
        shift.setMax ((int) (sr * 0.25));
        for (auto* sm : { &sMix, &sDecay, &sDamp, &sPreFb, &sShim, &sFreeze, &sWidth, &sMod, &sPre }) sm->setTime (0.05, sr);
        sSize.setTime (0.4, sr);
        reset();
    }
    void reset() override
    {
        for (auto& c : inAp) for (auto& a : c) a.clear();
        for (int t = 0; t < 2; ++t) { tAp1[t].clear(); tDel1[t].clear(); tAp2[t].clear(); tDel2[t].clear(); damp[t].reset(); end[t] = 0; }
        pre.clear(); shift.clear(); bw.reset(); lowCut.reset(); shLp.reset(); shHp.reset();
        shPhase = 0; lfo.phase = 0;
        for (auto* sm : { &sMix, &sSize, &sDecay, &sDamp, &sPreFb, &sShim, &sFreeze, &sWidth, &sMod, &sPre }) sm->primed = false;
    }
    void process (float* L, float* R, int n, const float* p, double) override
    {
        struct Character { float d1, d2, dd1, dd2, bwHz, mod, size; };
        static const Character chars[3] = { { 0.70f, 0.55f, 0.60f, 0.45f, 7000.0f, 0.5f, 0.75f },     // GROUNDED
                                            { 0.75f, 0.625f, 0.70f, 0.50f, 0.0f, 1.0f, 1.0f },     // AIRY
                                            { 0.78f, 0.66f, 0.76f, 0.56f, 12000.0f, 2.2f, 1.25f } }; // COSMIC
        const Character& c = chars[fxChoice (p[8], 3)];
        const bool freeze = p[10] > 0.5f;
        sMix.set (p[0]);
        sSize.set ((0.3f + 1.3f * p[1]) * c.size);
        sPre.set ((float) std::max (1.0, 0.5 * p[3] * sr));
        sPreFb.set (0.9f * p[4]);
        sDamp.set (freeze ? 0.0f : 0.85f * p[5]);
        sMod.set (p[7] * c.mod);
        sShim.set (p[9]);
        sFreeze.set (freeze ? 1.0f : 0.0f);
        sWidth.set (p[11]);
        const double t60 = spaceDecaySeconds (p[2]);
        bw.setLowpass (c.bwHz > 0 ? c.bwHz : sr * 0.49, sr);
        const double lc = lowCutHz (p[6]);
        lowCut.setLowpass (lc > 0 ? lc : 1.0, sr);
        shLp.setLowpass (6000.0, sr); shHp.setLowpass (300.0, sr);
        lfo.setRate (0.6 + 0.6 * c.mod, sr);
        const double base = sr / 29761.0;
        const double shWin = 0.08 * sr;
        for (int k = 0; k < n; ++k)
        {
            const float mix = sMix.next(), size = sSize.next(), damping = sDamp.next(), preFb = sPreFb.next();
            const float shim = sShim.next(), frz = sFreeze.next(), width = sWidth.next(), modA = sMod.next(), pd = sPre.next();
            const double sc = base * size;
            // the decay (twice in each half of the tank), from the time it takes to go round it
            const double halfSec = (kAp1[0] + kDel1[0] + kAp2[0] + kDel2[0]) * sc / sr;
            const float decay = frz > 0.5f ? 1.0f : (float) std::min (0.993, std::pow (10.0, -1.5 * halfSec / t60));
            // pre-delay (which can echo into the tank)
            const float in = 0.5f * (L[k] + R[k]);
            const float fbScale = (float) std::min (1.0, pd / (0.01 * sr));
            const float pdOut = pre.read (pd);
            pre.write (flushDenormal (in + preFb * fbScale * pdOut));
            float x = pdOut * (1.0f - frz);
            // shimmer: the tail an octave up, back into the tank
            {
                const double d1 = shWin * (1.0 - shPhase) + 2.0;
                double ph2 = shPhase + 0.5; if (ph2 >= 1.0) ph2 -= 1.0;
                const double d2 = shWin * (1.0 - ph2) + 2.0;
                const float g1 = (float) std::sin (kPi * shPhase), g2 = (float) std::sin (kPi * ph2);
                const float up = shift.read (d1) * g1 * g1 + shift.read (d2) * g2 * g2;
                shPhase += 1.0 / shWin; if (shPhase >= 1.0) shPhase -= 1.0;
                x += shim * 0.55f * shHp.hp (shLp.lp (up));
            }
            if (lc > 0) x = lowCut.hp (x);
            x = c.bwHz > 0 ? bw.lp (x) : x;
            for (int j = 0; j < 4; ++j) x = inAp[0][j].process (x, kIn[j] * sc, j < 2 ? c.d1 : c.d2);
            // the tank: two halves, each feeding the other
            const double mod = modA * 16.0 * sc;
            float o[2];
            for (int t = 0; t < 2; ++t)
            {
                float a = x + end[1 - t];
                const double m = t == 0 ? lfo.sineAt (0) : lfo.sineAt (0.25);
                a = tAp1[t].process (a, kAp1[t] * sc + mod * m, c.dd1);
                float b = tDel1[t].read (kDel1[t] * sc);
                tDel1[t].write (flushDenormal (a));
                damp[t].a = damping;
                b = damp[t].lp (b) * decay;
                b = tAp2[t].process (b, kAp2[t] * sc, c.dd2);
                o[t] = tDel2[t].read (kDel2[t] * sc);
                tDel2[t].write (flushDenormal (b));
                o[t] = softLimit (o[t] * decay);
            }
            end[0] = o[0]; end[1] = o[1];
            lfo.advance();
            // the output taps (left from the right half mostly, and the other way round)
            float wl = 0.6f * (tDel1[1].read (266 * sc) + tDel1[1].read (2974 * sc) - tAp2[1].tap (1913 * sc) + tDel2[1].read (1996 * sc)
                               - tDel1[0].read (1990 * sc) - tAp2[0].tap (187 * sc) - tDel2[0].read (1066 * sc));
            float wr = 0.6f * (tDel1[0].read (353 * sc) + tDel1[0].read (3627 * sc) - tAp2[0].tap (1228 * sc) + tDel2[0].read (2673 * sc)
                               - tDel1[1].read (2111 * sc) - tAp2[1].tap (335 * sc) - tDel2[1].read (121 * sc));
            shift.write (flushDenormal (0.5f * (wl + wr)));
            widen (wl, wr, width);
            float dg, wg; equalPower (mix, dg, wg);
            L[k] = L[k] * dg + wl * wg;
            R[k] = R[k] * dg + wr * wg;
        }
    }
private:
    // the tank's lengths at 29761 HZ (a figure-of-eight plate after Dattorro, 1997)
    static constexpr float kMaxSize = 1.6f * 1.25f + 0.05f;
    static constexpr float kIn[4] = { 142, 107, 379, 277 };
    static constexpr float kAp1[2] = { 672, 908 }, kDel1[2] = { 4453, 4217 }, kAp2[2] = { 1800, 2656 }, kDel2[2] = { 3720, 3163 };
    double sr = 48000;
    Allpass inAp[2][4];
    Allpass tAp1[2], tAp2[2];
    Delay tDel1[2], tDel2[2], pre, shift;
    OnePole damp[2], bw, lowCut, shLp, shHp;
    float end[2] {};
    double shPhase = 0;
    Lfo lfo;
    Smooth sMix, sSize, sDecay, sDamp, sPreFb, sShim, sFreeze, sWidth, sMod, sPre;
};

// ================================================================ TAPE ECHO
class TapeEchoFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        tape.setMax ((int) (sr * 6.3));
        for (auto* sm : { &sFb, &sWow, &sFlut, &sSat, &sWear, &sSpread, &sMix }) sm->setTime (0.05, sr);
        sTime.setTime (0.35, sr);
        for (auto& g : headGain) g.setTime (0.02, sr);
        wow.setRate (0.7, sr);
        flutNoise.setRate (14.0, sr);
        flut.setRate (6.8, sr);
        dropRate.setRate (3.0, sr);
        hp.setLowpass (70.0, sr);
        wowSpan = 0.004 * sr / (kTwoPi * 0.7);
        flutSpan = 0.003 * sr / (kTwoPi * 6.8);
        reset();
    }
    void reset() override
    {
        tape.clear(); hp.reset(); tone1.reset(); tone2.reset(); os.reset();
        fbSample = 0; drop = 1;
        for (auto* sm : { &sTime, &sFb, &sWow, &sFlut, &sSat, &sWear, &sSpread, &sMix }) sm->primed = false;
        for (auto& g : headGain) g.primed = false;
    }
    void process (float* L, float* R, int n, const float* p, double bpm) override
    {
        static const int masks[7] = { 1, 2, 4, 3, 5, 6, 7 };
        const int mask = masks[fxChoice (p[3], 7)];
        const double t = std::clamp (delaySeconds (p[0], p[1] > 0.5f, bpm) * sr, 16.0, sr * 2.0);
        sTime.set ((float) t);
        sFb.set (1.15f * p[2]); sWow.set (p[4]); sFlut.set (p[5]); sSat.set (p[6]); sWear.set (p[8]);
        sSpread.set (p[9]); sMix.set (p[10]);
        int active = 0;
        for (int h = 0; h < 3; ++h) { const bool on = (mask >> h) & 1; headGain[h].set (on ? 1.0f : 0.0f); active += on; }
        // where the playing heads sit across the stereo field
        float pan[3] = { 0, 0, 0 };
        {
            int k = 0;
            for (int h = 0; h < 3; ++h)
                if ((mask >> h) & 1) { pan[h] = active == 1 ? 0.0f : -1.0f + 2.0f * (float) k / (float) (active - 1); ++k; }
        }
        const double toneHz = tapeToneHz (p[7]) * (1.0 - 0.45 * p[8]);
        tone1.setLowpass (toneHz, sr); tone2.setLowpass (toneHz * 1.4, sr);
        const float norm = 1.0f / std::sqrt ((float) std::max (1, active));
        for (int k = 0; k < n; ++k)
        {
            const float time = sTime.next(), fb = sFb.next(), wowA = sWow.next(), flutA = sFlut.next(), sat = sSat.next();
            const float wear = sWear.next(), spread = sSpread.next(), mix = sMix.next();
            // the tape's speed wanders: slow wow (up to 0.4% in pitch), fast flutter (up to 0.3%)
            const double dev = wowA * wowA * wowSpan * wow.next() + flutA * flutA * flutSpan * (0.65 * flut.sine() + 0.35 * flutNoise.next());
            float sumHeads = 0, wl = 0, wr = 0, gsum = 0;
            for (int h = 0; h < 3; ++h)
            {
                const float g = headGain[h].next();
                if (g <= 0.0001f) continue;
                const float v = tape.read (time * (h + 1) + dev - Oversampler2x::kLatency) * g;
                sumHeads += v; gsum += g;
                const float pp = 0.5f * (1.0f + pan[h] * spread);
                wl += v * std::cos ((float) (pp * kPi * 0.5)); wr += v * std::sin ((float) (pp * kPi * 0.5));
            }
            // worn tape: hiss and dropouts
            if (dropRate.next() > 0.92f - 0.25f * wear && wear > 0.05f) dropTarget = 1.0f - 0.7f * wear; else dropTarget = 1.0f;
            drop += (dropTarget - drop) * 0.002f;
            wl *= drop * norm * 1.4142f; wr *= drop * norm * 1.4142f;
            const float headMix = gsum > 0 ? sumHeads / std::max (1.0f, gsum) : 0.0f;
            // record: the input and the heads' feedback, filtered and saturated (twice the rate)
            float x = 0.5f * (L[k] + R[k]) + fb * headMix;
            x = hp.hp (x);
            x = tone2.lp (tone1.lp (x));
            x = os.process (x, [sat] (float v) { return tapeSat (v, sat); });
            x += noise.white() * wear * wear * 0.0025f;
            tape.write (flushDenormal (x));
            float dg, wg; equalPower (mix, dg, wg);
            L[k] = L[k] * dg + wl * wg;
            R[k] = R[k] * dg + wr * wg;
        }
    }
private:
    double sr = 48000;
    Delay tape;
    OnePole hp, tone1, tone2;
    Oversampler2x os;
    Wander wow, flutNoise, dropRate;
    Lfo flut;
    Noise noise;
    float fbSample = 0, drop = 1, dropTarget = 1;
    double wowSpan = 0, flutSpan = 0;   // samples the heads' reading moves by at full WOW / FLUTTER
    Smooth headGain[3];
    Smooth sTime, sFb, sWow, sFlut, sSat, sWear, sSpread, sMix;
};

// ================================================================ ENSEMBLE
class EnsembleFx : public Effect
{
public:
    void prepare (double s) override
    {
        sr = s;
        for (auto& d : line) d.setMax ((int) (sr * 0.03));
        for (auto* sm : { &sRate, &sDepth, &sWidth, &sNoise, &sMix }) sm->setTime (0.05, sr);
        sCentre.setTime (0.1, sr); sSwing.setTime (0.1, sr);
        reset();
    }
    void reset() override
    {
        for (auto& d : line) d.clear();
        for (auto& f : pre) f.reset();
        for (auto& f : post) f.reset();
        lfo.phase = 0; vib.phase = 0;
        for (auto* sm : { &sRate, &sDepth, &sWidth, &sNoise, &sMix, &sCentre, &sSwing }) sm->primed = false;
    }
    void process (float* L, float* R, int n, const float* p, double) override
    {
        struct Mode { double rate, centreMs, swingMs; bool strings; };
        static const Mode modes[4] = { { 0.513, 3.5, 1.85, false },    // I
                                       { 0.863, 3.5, 1.85, false },    // II
                                       { 9.75, 3.3, 0.30, false },     // I+II
                                       { 0.62, 5.5, 2.40, true } };    // STRINGS
        const Mode& m = modes[fxChoice (p[0], 4)];
        const double rate = m.rate * ensembleRateScale (p[1]);
        lfo.setRate (rate, sr);
        vib.setRate (6.2, sr);
        const double swing = std::min (m.centreMs - 0.4, m.swingMs * 2.0 * p[2]);
        sCentre.set ((float) (m.centreMs * 0.001 * sr)); sSwing.set ((float) (swing * 0.001 * sr));
        sWidth.set (p[3]); sNoise.set (p[5]); sMix.set (p[6]);
        const double toneHz = 2000.0 * std::pow (8.0, (double) p[4]);
        for (auto& f : pre) f.setLowpass (9500.0, sr);
        for (auto& f : post) f.setLowpass (toneHz, sr);
        for (int k = 0; k < n; ++k)
        {
            const float centre = sCentre.next(), sw = sSwing.next(), width = sWidth.next(), nz = sNoise.next(), mix = sMix.next();
            const float inL = pre[0].lp (L[k]), inR = pre[1].lp (R[k]);
            line[0].write (inL); line[1].write (inR);
            float wl, wr;
            if (m.strings)
            {
                // three voices a third of a cycle apart, and a touch of vibrato
                const double v = 0.08 * sr * 0.001 * vib.sine();
                wl = wr = 0;
                for (int j = 0; j < 3; ++j)
                {
                    wl += line[0].read (centre + sw * lfo.sineAt (j / 3.0) + v);
                    wr += line[1].read (centre + sw * lfo.sineAt (j / 3.0 + 1.0 / 6.0) - v);
                }
                wl *= 0.5f; wr *= 0.5f;
            }
            else
            {
                const double tri = lfo.triAt (0);
                wl = line[0].read (centre + sw * tri);
                wr = line[1].read (centre - sw * tri);   // the other side swings the other way
            }
            lfo.advance();
            const float hiss = nz * nz * 0.004f;
            wl = post[0].lp (std::tanh (wl * 1.1f) / 1.1f + hiss * noise.white());
            wr = post[1].lp (std::tanh (wr * 1.1f) / 1.1f + hiss * noise.white());
            widen (wl, wr, width);
            float dg, wg; equalPower (mix, dg, wg);
            L[k] = L[k] * dg + wl * wg;
            R[k] = R[k] * dg + wr * wg;
        }
    }
private:
    double sr = 48000;
    Delay line[2];
    OnePole pre[2], post[2];
    Lfo lfo, vib;
    Noise noise;
    Smooth sRate, sDepth, sWidth, sNoise, sMix, sCentre, sSwing;
};
} // namespace

std::unique_ptr<Effect> makeStudioEffect (FxType t)
{
    switch (t)
    {
        case FxType::Vast:     return std::make_unique<VastFx>();
        case FxType::Space:    return std::make_unique<SpaceFx>();
        case FxType::TapeEcho: return std::make_unique<TapeEchoFx>();
        case FxType::Ensemble: return std::make_unique<EnsembleFx>();
        default:               return makeStudioEffect2 (t);
    }
}
} // namespace prv
