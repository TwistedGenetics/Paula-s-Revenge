#include "helix/Oscillator.h"
#include "helix/Simd.h"
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

namespace helix {

// --------------------------------------------------------------------------------------------- warps

float WtOscillator::warpSpeed(Warp w, float a)
{
    switch (w)
    {
        case Warp::BendPlus: case Warp::BendMinus: return 1.0f + 4.0f * a;
        case Warp::Asym: return 1.0f + 5.0f * a;
        case Warp::Pwm: return 0.5f / (0.5f - 0.48f * a);
        case Warp::Mirror: return 1.0f + a;
        case Warp::Sync: return 1.0f + 15.0f * a;
        case Warp::Formant: return 1.0f + 7.0f * a;
        case Warp::FmOther: case Warp::FmSub: case Warp::FmNoise: return 1.0f + 6.0f * a * a;
        default: return 1.0f;
    }
}

float WtOscillator::maxWarpAmount(Warp w, float otherSpeed, float inc)
{
    // Sync and formant windows spread energy around the squeezed cycle, so they get extra headroom.
    const float ceiling = w == Warp::Formant ? 0.15f : (w == Warp::Sync ? 0.3f : 0.45f);
    const float limit = ceiling / std::max(inc * otherSpeed, 1e-9f);
    if (warpSpeed(w, 1.0f) <= limit) return 1.0f;
    if (warpSpeed(w, 0.0f) > limit) return 0.0f;
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 10; ++i) { const float m = 0.5f * (lo + hi); (warpSpeed(w, m) <= limit ? lo : hi) = m; }
    return lo;
}

float WtOscillator::warpPhase(Warp w, float p, float a, float m)
{
    switch (w)
    {
        case Warp::BendPlus:
        {
            const float k = 4.0f * a, x = 2.0f * p - 1.0f;
            const float y = x * (1.0f + k) / (1.0f + k * std::fabs(x));
            return 0.5f * (y + 1.0f);
        }
        case Warp::BendMinus:
        {
            const float k = 4.0f * a, x = 2.0f * p - 1.0f;
            const float y = x / (1.0f + k - k * std::fabs(x));
            return 0.5f * (y + 1.0f);
        }
        case Warp::Asym:
        {
            // p^e with e = 1 + 5a, via exp2(e * log2 p); p in (0,1]
            if (p <= 0.0f) return 0.0f;
            const float e = 1.0f + 5.0f * a;
            return fastExp2(e * std::log2(p));
        }
        case Warp::Pwm:
        {
            const float wdt = 0.5f - 0.48f * a;
            return p < wdt ? 0.5f * p / wdt : 0.5f + 0.5f * (p - wdt) / (1.0f - wdt);
        }
        case Warp::Mirror:
        {
            const float tri = p < 0.5f ? 2.0f * p : 2.0f - 2.0f * p;
            return p + (tri - p) * a;
        }
        case Warp::Sync: { const float q = p * (1.0f + 15.0f * a); return q - (float)(int)q; }
        case Warp::Formant: { const float q = p * (1.0f + 7.0f * a); return q < 1.0f ? q : 0.0f; }
        case Warp::FmOther: case Warp::FmSub: case Warp::FmNoise:
        {
            const float q = p + 3.0f * a * a * m;
            return q - std::floor(q);
        }
        case Warp::Quantize:
        {
            const float steps = fastExp2(11.0f - 9.0f * a);
            return std::floor(p * steps) / steps;
        }
        default: return p;
    }
}

// Output-side part of a warp. p: the lane's unwarped phase, s: the sample read.
static inline float warpOutput(Warp w, float s, float p, float a, float other)
{
    switch (w)
    {
        case Warp::Sync:
        {
            const float win = 0.5f - 0.5f * std::cos(kTwoPi * p);
            return s * (1.0f + (win - 1.0f) * std::min(1.0f, 4.0f * a));
        }
        case Warp::Formant:
        {
            const float q = p * (1.0f + 7.0f * a);
            if (q >= 1.0f) return s * (1.0f - std::min(1.0f, 8.0f * a));   // silence after the squeezed cycle
            const float win = 0.5f - 0.5f * std::cos(kTwoPi * q);
            return s * (1.0f + (win - 1.0f) * std::min(1.0f, 4.0f * a));
        }
        case Warp::Am: return s * (1.0f - a + a * (0.5f + 0.5f * other));
        case Warp::Rm: return s * (1.0f + (other - 1.0f) * a);
        case Warp::BitRed:
        {
            const float levels = fastExp2(15.0f - 14.0f * a);
            return std::round(s * levels) / levels;
        }
        default: return s;
    }
}

static inline bool isOutputWarp(Warp w) { return w == Warp::Sync || w == Warp::Formant || w == Warp::Am || w == Warp::Rm || w == Warp::BitRed; }
static inline bool isPhaseWarp(Warp w) { return w != Warp::Off && w != Warp::Am && w != Warp::Rm && w != Warp::BitRed; }

// --------------------------------------------------------------------------------------------- unison

void WtOscillator::configureLanes(const OscBlock& b)
{
    const int n = clamp(b.unison, 1, kMaxUnison);
    lanes_ = n;
    float w[kMaxUnison], sumSq = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float o = n > 1 ? 2.0f * (float)i / (float)(n - 1) - 1.0f : 0.0f;   // -1 .. 1
        const float cents = o * 50.0f * b.detune * std::sqrt(b.detune);
        ratio_[i] = fastExp2(cents * (1.0f / 1200.0f));
        const bool centre = n == 1 || std::fabs(o) < 1.0f / (float)n;          // middle lane(s)
        w[i] = centre ? 1.0f : b.blend;
        sumSq += w[i] * w[i];
        const float pan = clamp(b.pan + o * b.spread, -1.0f, 1.0f);
        const float ang = (pan + 1.0f) * 0.25f * kPi;
        gainL_[i] = std::cos(ang) * 1.41421356f;
        gainR_[i] = std::sin(ang) * 1.41421356f;
    }
    const float norm = sumSq > 0.0f ? 1.0f / std::sqrt(sumSq) : 0.0f;
    for (int i = 0; i < n; ++i)
    {
        gainM_[i] = w[i] * norm;
        gainL_[i] *= gainM_[i];
        gainR_[i] *= gainM_[i];
    }
}

void WtOscillator::render(const Wavetable* wt, const OscBlock& b, const OscModInputs& mod, float* l, float* r,
                          float* mono, int n)
{
    if (!wt || n <= 0) { for (int i = 0; i < n; ++i) mono[i] = 0.0f; return; }
    configureLanes(b);

    const int os = b.oversample ? 2 : 1;
    const Warp w0 = b.warp[0], w1 = b.warp[1];
    const float baseInc = b.freqHz * invSr_;
    float maxRatio = 0.0f;
    for (int k = 0; k < lanes_; ++k) maxRatio = std::max(maxRatio, ratio_[k]);
    // Effective increment of the fastest lane decides the warp limits and the (shared) mip levels.
    const float fastInc = baseInc * maxRatio;
    float a0max = std::max(b.warpAmt0[0], b.warpAmt1[0]), a1max = std::max(b.warpAmt0[1], b.warpAmt1[1]);
    const float lim0 = maxWarpAmount(w0, warpSpeed(w1, a1max), fastInc);
    a0max = std::min(a0max, lim0);
    const float lim1 = maxWarpAmount(w1, warpSpeed(w0, a0max), fastInc);
    a1max = std::min(a1max, lim1);
    // Sync / formant windows widen every harmonic by about +-2x the squeeze ratio: read one octave duller.
    const bool windowed = w0 == Warp::Sync || w0 == Warp::Formant || w1 == Warp::Sync || w1 == Warp::Formant;
    const float speed = warpSpeed(w0, a0max) * warpSpeed(w1, a1max) * (windowed ? 2.0f : 1.0f);
    const bool phase0 = isPhaseWarp(w0), phase1 = isPhaseWarp(w1);
    const bool out0 = isOutputWarp(w0), out1 = isOutputWarp(w1);
    auto modFor = [&](Warp w) -> const float* {
        switch (w)
        {
            case Warp::FmOther: case Warp::Am: case Warp::Rm: return mod.other;
            case Warp::FmSub: return mod.sub;
            case Warp::FmNoise: return mod.noise;
            default: return nullptr;
        }
    };
    const float* m0 = modFor(w0);
    const float* m1 = modFor(w1);

    // One mip choice for every lane: lanes differ by at most +-50 cents, and the fastest lane's choice is
    // alias-free for all of them. Band-limited to the OUTPUT Nyquist even when oversampling (the 2x path is for
    // the partials the warps create). A crossfade that is nearly all one level reads one level only.
    MipSel mip = selectMip(fastInc * speed, b.crossfade);
    if (mip.mix < 0.02f) mip.hi = mip.lo;
    else if (mip.mix > 0.98f) { mip.lo = mip.hi; mip.mix = 0.0f; }
    const bool two = mip.hi != mip.lo;
    const int nLo = wt->levelSize(mip.lo), nHi = wt->levelSize(mip.hi);
    const float fnLo = (float)nLo, fnHi = (float)nHi;
    const float mixHi = mip.mix;

    float inc[kMaxUnison];
    for (int k = 0; k < lanes_; ++k) inc[k] = baseInc * ratio_[k] / (float)os;

    const float invN = 1.0f / (float)n;
    const float dPos = (b.pos1 - b.pos0) * invN, dLev = (b.level1 - b.level0) * invN;
    const float dA0 = (b.warpAmt1[0] - b.warpAmt0[0]) * invN, dA1 = (b.warpAmt1[1] - b.warpAmt0[1]) * invN;
    float pos = b.pos0, lev = b.level0, a0 = b.warpAmt0[0], a1 = b.warpAmt0[1];

    // Fast path: no warps, no oversampling -> four lanes at a time (NEON / SSE), scalar gathers.
    const bool simd = w0 == Warp::Off && w1 == Warp::Off && os == 1;
    alignas(16) float phs[8] = {0}, incs[8] = {0}, gl[8] = {0}, gr[8] = {0}, gm[8] = {0};
    const int groups = (lanes_ + 3) / 4;
    if (simd)
        for (int k = 0; k < lanes_; ++k) { phs[k] = phase_[k]; incs[k] = inc[k]; gl[k] = gainL_[k]; gr[k] = gainR_[k]; gm[k] = gainM_[k]; }

    float4 GL[2], GR[2], GM[2], INC[2];
    for (int g = 0; g < groups; ++g)
    {
        GL[g] = float4::load(gl + 4 * g); GR[g] = float4::load(gr + 4 * g);
        GM[g] = float4::load(gm + 4 * g); INC[g] = float4::load(incs + 4 * g);
    }
    const float* pmIn = mod.pm && mod.pmAmt > 0.0f ? mod.pm : nullptr;
    const float* posIn = mod.posMod && mod.posAmt > 0.0f ? mod.posMod : nullptr;
    const float posK = 0.5f * mod.posAmt;
    const bool moving = dPos != 0.0f || posIn;
    int f0 = 0, f1 = 0; float t = 0.0f;
    framePos(*wt, pos, f0, f1, t);

    for (int i = 0; i < n; ++i)
    {
        if (moving) framePos(*wt, posIn ? pos + posK * posIn[i] : pos, f0, f1, t);
        // phase-mod offset: amt * (m + 1) keeps it >= 0, so the plain fractional part wraps it
        const float pmOff = pmIn ? mod.pmAmt * (pmIn[i] + 1.0f) : 0.0f;
        const float* la = wt->frame(mip.lo, f0);
        const float* lb = wt->frame(mip.lo, f1);
        const float* ha = wt->frame(mip.hi, f0);
        const float* hb = wt->frame(mip.hi, f1);
        auto read = [&](float q) {
            float x = q * fnLo; int j = (int)x; float fr = x - (float)j; j &= nLo - 1;
            const float sa = la[j] + (la[j + 1] - la[j]) * fr, sb = lb[j] + (lb[j + 1] - lb[j]) * fr;
            float s = sa + (sb - sa) * t;
            if (two)
            {
                x = q * fnHi; j = (int)x; fr = x - (float)j; j &= nHi - 1;
                const float ha0 = ha[j] + (ha[j + 1] - ha[j]) * fr, hb0 = hb[j] + (hb[j + 1] - hb[j]) * fr;
                s += (ha0 + (hb0 - ha0) * t - s) * mixHi;
            }
            return s;
        };

        if (simd)
        {
            float4 accL, accR, accM;
            const float4 T(t), N(fnLo), NH(fnHi), MIX(mixHi);
            for (int g = 0; g < groups; ++g)
            {
                float4 ph = float4::load(phs + 4 * g);
                const float4 rp = pmIn ? fracPos(ph + float4(pmOff)) : ph;
                alignas(16) int32_t j[4];
                alignas(16) float va[4], vb[4], vc[4], vd[4];
                float4 x = rp * N;
                truncToInt(x, j);
                const float4 fr = fracPos(x);
                for (int k = 0; k < 4; ++k)
                {
                    const int jj = j[k] & (nLo - 1);
                    va[k] = la[jj]; vb[k] = la[jj + 1]; vc[k] = lb[jj]; vd[k] = lb[jj + 1];
                }
                const float4 A = float4::load(va), B = float4::load(vb), C = float4::load(vc), D = float4::load(vd);
                const float4 sa = float4::madd(A, B - A, fr), sb = float4::madd(C, D - C, fr);
                float4 sv = float4::madd(sa, sb - sa, T);
                if (two)
                {
                    float4 xh = rp * NH;
                    truncToInt(xh, j);
                    const float4 frh = fracPos(xh);
                    for (int k = 0; k < 4; ++k)
                    {
                        const int jj = j[k] & (nHi - 1);
                        va[k] = ha[jj]; vb[k] = ha[jj + 1]; vc[k] = hb[jj]; vd[k] = hb[jj + 1];
                    }
                    const float4 HA = float4::load(va), HB = float4::load(vb), HC = float4::load(vc), HD = float4::load(vd);
                    const float4 ha0 = float4::madd(HA, HB - HA, frh), hb0 = float4::madd(HC, HD - HC, frh);
                    const float4 hv = float4::madd(ha0, hb0 - ha0, T);
                    sv = float4::madd(sv, hv - sv, MIX);
                }
                accL = float4::madd(accL, sv, GL[g]);
                accR = float4::madd(accR, sv, GR[g]);
                accM = float4::madd(accM, sv, GM[g]);
                fracPos(ph + INC[g]).store(phs + 4 * g);
            }
            l[i] += accL.sum() * lev;
            r[i] += accR.sum() * lev;
            mono[i] = accM.sum() * lev;
        }
        else
        {
            const float ms0 = m0 ? m0[i] : 0.0f, ms1 = m1 ? m1[i] : 0.0f;
            const float aa0 = std::min(a0, lim0), aa1 = std::min(a1, lim1);
            float subL[2] = {0, 0}, subR[2] = {0, 0}, sm = 0.0f;
            for (int o = 0; o < os; ++o)
            {
                float sl = 0.0f, sr = 0.0f;
                for (int k = 0; k < lanes_; ++k)
                {
                    const float p = phase_[k];
                    float q = pmIn ? wrap01(p + pmOff) : p;
                    if (phase0) q = warpPhase(w0, q, aa0, ms0);
                    if (phase1) q = warpPhase(w1, q, aa1, ms1);
                    float s = read(q);
                    if (out0) s = warpOutput(w0, s, p, aa0, ms0);
                    if (out1) s = warpOutput(w1, s, p, aa1, ms1);
                    sl += s * gainL_[k];
                    sr += s * gainR_[k];
                    sm += s * gainM_[k];
                    const float np = p + inc[k];
                    phase_[k] = np - (float)(int)np;
                }
                subL[o] = sl; subR[o] = sr;
            }
            float outL = subL[0], outR = subR[0];
            if (os == 2) { outL = decL_.process(subL[0], subL[1]); outR = decR_.process(subR[0], subR[1]); sm *= 0.5f; }
            l[i] += outL * lev;
            r[i] += outR * lev;
            mono[i] = sm * lev;
        }
        pos += dPos; lev += dLev; a0 += dA0; a1 += dA1;
    }
    if (simd) for (int k = 0; k < lanes_; ++k) phase_[k] = phs[k];
}

// --------------------------------------------------------------------------------------------- sub

static inline float polyBlep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

void SubOscillator::render(int shape, float freqHz, float level, float* out, int n)
{
    const float dt = clamp(freqHz * invSr_, 0.0f, 0.45f);
    for (int i = 0; i < n; ++i)
    {
        const float p = phase_;
        float s;
        switch (shape)
        {
            case 0: s = std::sin(kTwoPi * p); break;
            case 1:
            {
                // Integrated band-limited square -> triangle (leaky integrator keeps DC away)
                float sq = p < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep(p, dt);
                sq -= polyBlep(wrap01(p + 0.5f), dt);
                triState_ = triState_ * 0.9995f + 4.0f * dt * sq;
                s = triState_;
                break;
            }
            case 2:
            {
                s = p < 0.5f ? 1.0f : -1.0f;
                s += polyBlep(p, dt);
                s -= polyBlep(wrap01(p + 0.5f), dt);
                s *= 0.7f;
                break;
            }
            default: s = (2.0f * p - 1.0f - polyBlep(p, dt)) * 0.8f; break;
        }
        out[i] = s * level;
        float np = p + dt;
        phase_ = np - (float)(int)np;
    }
}

// --------------------------------------------------------------------------------------------- noise

static std::vector<float>* g_noiseLoops = nullptr;   // kNumTypes * kLoopLen, immutable once built
static std::once_flag g_noiseOnce;

void NoiseSource::buildSharedLoops()
{
    std::call_once(g_noiseOnce, [] {
        auto* buf = new std::vector<float>((size_t)kNumTypes * kLoopLen);
        Rng rng(0x1234567u);
        auto loop = [&](int t) { return buf->data() + (size_t)t * kLoopLen; };
        const float sr = (float)kLoopRate;
        // 0 white
        for (int i = 0; i < kLoopLen; ++i) loop(0)[i] = rng.bi() * 0.5f;
        // 1 pink (Paul Kellet's refined filter)
        {
            float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
            for (int i = 0; i < kLoopLen; ++i)
            {
                const float w = rng.bi();
                b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f;
                b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
                b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
                loop(1)[i] = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f) * 0.11f;
                b6 = w * 0.115926f;
            }
        }
        // 2 brown
        {
            float y = 0;
            for (int i = 0; i < kLoopLen; ++i) { y = 0.998f * y + rng.bi() * 0.04f; loop(2)[i] = y * 1.5f; }
        }
        // 3 vinyl: rumble + soft hiss + crackle pops with a little ring
        {
            float rum = 0, hiss = 0, ring = 0;
            for (int i = 0; i < kLoopLen; ++i)
            {
                rum = 0.9995f * rum + rng.bi() * 0.002f;
                hiss = 0.6f * hiss + 0.4f * rng.bi();
                if (rng.uni() < 6.0f / sr) ring += rng.bi() * 0.9f;
                if (rng.uni() < 40.0f / sr) ring += rng.bi() * 0.2f;
                ring *= 0.82f;
                const float wow = 0.6f + 0.4f * std::sin(kTwoPi * 0.555f * (float)i / sr);   // 33 rpm
                loop(3)[i] = rum * 3.0f + hiss * 0.03f * wow + ring;
            }
        }
        // 4 tape hiss: bright noise with flutter modulation
        {
            float lp = 0, hp = 0;
            for (int i = 0; i < kLoopLen; ++i)
            {
                const float w = rng.bi();
                lp += 0.6f * (w - lp);
                const float hiPass = lp - hp; hp += 0.02f * hiPass;
                const float flutter = 1.0f + 0.15f * std::sin(kTwoPi * 7.0f * (float)i / sr);
                loop(4)[i] = hiPass * 0.35f * flutter;
            }
        }
        // 5 crackle: sparse clicks only
        {
            float ring = 0;
            for (int i = 0; i < kLoopLen; ++i)
            {
                if (rng.uni() < 25.0f / sr) ring += rng.bi();
                ring *= 0.7f;
                loop(5)[i] = ring * 0.8f;
            }
        }
        // Click-free loop seam: crossfade the last 2048 samples into the first.
        for (int t = 0; t < kNumTypes; ++t)
        {
            float* x = loop(t);
            const int fade = 2048;
            for (int i = 0; i < fade; ++i)
            {
                const float g = (float)i / fade;
                x[kLoopLen - fade + i] = x[kLoopLen - fade + i] * (1.0f - g) + x[i] * g;
            }
        }
        g_noiseLoops = buf;
    });
}

void NoiseSource::render(int type, float pitchRatio, bool loop, float level, float* out, int n)
{
    if (!g_noiseLoops || done_) { for (int i = 0; i < n; ++i) out[i] = 0.0f; return; }
    type = clamp(type, 0, kNumTypes - 1);
    const float* x = g_noiseLoops->data() + (size_t)type * kLoopLen;
    const double step = (double)(rateScale_ * clamp(pitchRatio, 0.0625f, 16.0f));
    for (int i = 0; i < n; ++i)
    {
        if (pos_ >= kLoopLen)
        {
            if (loop) pos_ -= kLoopLen;
            else { done_ = true; for (; i < n; ++i) out[i] = 0.0f; return; }
        }
        const int i0 = (int)pos_;
        const float fr = (float)(pos_ - i0);
        const int i1 = i0 + 1 < kLoopLen ? i0 + 1 : 0;
        out[i] = (x[i0] + (x[i1] - x[i0]) * fr) * level;
        pos_ += step;
    }
}

} // namespace helix
