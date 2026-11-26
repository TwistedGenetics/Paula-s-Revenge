#include "helix/Effects.h"
#include "helix/ModSystem.h"
#include <algorithm>
#include <cmath>

namespace helix {

// ------------------------------------------------------------------------------------------- biquad (RBJ)

static void normalise(Biquad& q, float b0, float b1, float b2, float a0, float a1, float a2)
{
    q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
}
void Biquad::lowShelf(float sr, float f, float db)
{
    const float A = std::pow(10.0f, db / 40.0f), w = kTwoPi * clamp(f, 10.0f, 0.45f * sr) / sr;
    const float cs = std::cos(w), sn = std::sin(w), al = sn / 2.0f * std::sqrt(2.0f), sq = 2.0f * std::sqrt(A) * al;
    normalise(*this, A * ((A + 1) - (A - 1) * cs + sq), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sq),
              (A + 1) + (A - 1) * cs + sq, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sq);
}
void Biquad::highShelf(float sr, float f, float db)
{
    const float A = std::pow(10.0f, db / 40.0f), w = kTwoPi * clamp(f, 10.0f, 0.45f * sr) / sr;
    const float cs = std::cos(w), sn = std::sin(w), al = sn / 2.0f * std::sqrt(2.0f), sq = 2.0f * std::sqrt(A) * al;
    normalise(*this, A * ((A + 1) + (A - 1) * cs + sq), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sq),
              (A + 1) - (A - 1) * cs + sq, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sq);
}
void Biquad::peak(float sr, float f, float db, float q)
{
    const float A = std::pow(10.0f, db / 40.0f), w = kTwoPi * clamp(f, 10.0f, 0.45f * sr) / sr;
    const float cs = std::cos(w), al = std::sin(w) / (2.0f * std::max(q, 0.05f));
    normalise(*this, 1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
}

// ------------------------------------------------------------------------------------------- chain

void FxChain::prepare(float sampleRate)
{
    sr_ = sampleRate;
    for (auto& d : choLine_) d.allocate((int)(0.05f * sampleRate));
    for (auto& d : dlyLine_) d.allocate((int)(4.0f * sampleRate));
    for (auto& d : revLine_) d.allocate((int)(0.2f * sampleRate));
    for (auto& d : revDiff_) d.allocate((int)(0.02f * sampleRate));
    revPre_.allocate((int)(0.25f * sampleRate));
    reset();
}

void FxChain::reset()
{
    upL_.reset(); upR_.reset(); dnL_.reset(); dnR_.reset();
    crushHold_[0] = crushHold_[1] = 0; crushPhase_ = 0; toneZ_[0] = toneZ_[1] = 0;
    for (auto& d : choLine_) d.clear();
    choPhase_ = 0;
    phPhase_ = 0;
    for (auto& c : phAp_) for (float& v : c) v = 0;
    phFb_[0] = phFb_[1] = 0;
    for (auto& d : dlyLine_) d.clear();
    dlyTime_ = -1.0f; dlyLp_[0] = dlyLp_[1] = 0;
    for (auto& d : revLine_) d.clear();
    for (auto& d : revDiff_) d.clear();
    revPre_.clear();
    for (float& v : revLp_) v = 0;
    for (auto& c : xo_) for (auto& s : c) s.reset();
    for (float& e : ottEnv_) e = 0;
    eqLow_.reset(); eqMid_.reset(); eqHigh_.reset();
    for (float& k : eqKey_) k = -1;
}

void FxChain::process(float* l, float* r, int n, const float* p, double bpm, bool oversample)
{
    // Slot order: each slot names an effect; repeats are skipped and missing effects are appended.
    int order[(int)FxType::Count];
    bool used[(int)FxType::Count] = {};
    int count = 0;
    for (int s = 0; s < kNumFxSlots; ++s)
    {
        const int t = clamp((int)p[P_FX_ORDER1 + s], 0, (int)FxType::Count - 1);
        if (!used[t]) { used[t] = true; order[count++] = t; }
    }
    for (int t = 0; t < (int)FxType::Count; ++t) if (!used[t]) order[count++] = t;

    heavy_ = p[P_OTT_ON] >= 0.5f;
    for (int k = 0; k < count; ++k)
    {
        switch ((FxType)order[k])
        {
            case FxType::Distortion: if (p[P_DIST_ON] >= 0.5f) distortion(l, r, n, p, oversample); break;
            case FxType::Chorus: if (p[P_CHO_ON] >= 0.5f) chorus(l, r, n, p); break;
            case FxType::Phaser: if (p[P_PH_ON] >= 0.5f) phaser(l, r, n, p); break;
            case FxType::Delay: if (p[P_DLY_ON] >= 0.5f) delay(l, r, n, p, bpm); break;
            case FxType::Reverb: if (p[P_REV_ON] >= 0.5f) reverb(l, r, n, p); break;
            case FxType::Ott: if (p[P_OTT_ON] >= 0.5f) ott(l, r, n, p); break;
            case FxType::Eq: if (p[P_EQ_ON] >= 0.5f) eq(l, r, n, p); break;
            default: break;
        }
    }
}

// ------------------------------------------------------------------------------------------- distortion

static inline float shape(int type, float x, float g)
{
    switch (type)
    {
        case 0: { const float bias = 0.25f; return (fastTanh(g * x + bias) - fastTanh(bias)) * 1.15f; }   // tube
        case 1: return clamp(g * x, -1.0f, 1.0f) * 0.9f;                                                    // hard clip
        case 2: return std::sin(0.5f * kPi * g * x) * 0.9f;                                                 // sine fold
        case 4: { const float u = g * x; return u > 0.0f ? fastTanh(u) : fastTanh(0.35f * u) * 0.8f; }       // asym rectify
        default: return x;
    }
}

void FxChain::distortion(float* l, float* r, int n, const float* p, bool oversample)
{
    const int type = (int)p[P_DIST_TYPE];
    const float d = clamp(p[P_DIST_DRIVE], 0.0f, 1.0f);
    const float g = 1.0f + 39.0f * d * d;
    const float mix = clamp(p[P_DIST_MIX], 0.0f, 1.0f);
    const float toneA = 1.0f - std::exp(-kTwoPi * p[P_DIST_TONE] / sr_);
    for (int i = 0; i < n; ++i)
    {
        const float dl = l[i], dr = r[i];
        float wl, wr;
        if (type == 3)
        {
            // Crush: sample-rate reduction (zero-order hold) + bit depth, SP-1200 / S950 style grit.
            crushPhase_ += p[P_DIST_RATE] / sr_;
            if (crushPhase_ >= 1.0f)
            {
                crushPhase_ -= std::floor(crushPhase_);
                const float levels = std::pow(2.0f, std::max(1.0f, p[P_DIST_BITS]) - 1.0f);
                crushHold_[0] = std::round(fastTanh(dl * g * 0.7f) * levels) / levels;
                crushHold_[1] = std::round(fastTanh(dr * g * 0.7f) * levels) / levels;
            }
            wl = crushHold_[0]; wr = crushHold_[1];
        }
        else if (oversample)
        {
            float a, b;
            upL_.process(dl, a, b); wl = dnL_.process(shape(type, a, g), shape(type, b, g));
            upR_.process(dr, a, b); wr = dnR_.process(shape(type, a, g), shape(type, b, g));
        }
        else { wl = shape(type, dl, g); wr = shape(type, dr, g); }
        toneZ_[0] += (wl - toneZ_[0]) * toneA;
        toneZ_[1] += (wr - toneZ_[1]) * toneA;
        l[i] = dl + (toneZ_[0] - dl) * mix;
        r[i] = dr + (toneZ_[1] - dr) * mix;
    }
}

// ------------------------------------------------------------------------------------------- chorus

void FxChain::chorus(float* l, float* r, int n, const float* p)
{
    const bool ens = (int)p[P_CHO_MODE] == 1;
    const float depth = clamp(p[P_CHO_DEPTH], 0.0f, 1.0f);
    const float mix = clamp(p[P_CHO_MIX], 0.0f, 1.0f);
    const float inc = p[P_CHO_RATE] / sr_;
    const float base = (ens ? 0.012f : 0.007f) * sr_, swing = (ens ? 0.003f : 0.004f) * sr_ * depth;
    for (int i = 0; i < n; ++i)
    {
        choLine_[0].push(l[i]);
        choLine_[1].push(r[i]);
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            if (!ens)
            {
                const float ph = choPhase_ + 0.25f * (float)c;
                out[c] = choLine_[c].read(base + swing * (0.5f + 0.5f * std::sin(kTwoPi * ph)));
            }
            else
            {
                float s = 0.0f;
                for (int v = 0; v < 3; ++v)
                {
                    const float ph = choPhase_ + (float)v / 3.0f + (float)c / 6.0f;
                    const float wob = 0.0003f * sr_ * depth * std::sin(kTwoPi * (ph * 6.3f));   // fast shimmer
                    s += choLine_[c].read(base + swing * (0.5f + 0.5f * std::sin(kTwoPi * ph)) + wob);
                }
                out[c] = s * 0.45f;
            }
        }
        l[i] += (out[0] - l[i]) * mix;
        r[i] += (out[1] - r[i]) * mix;
        choPhase_ += inc;
        if (choPhase_ >= 1.0f) choPhase_ -= 1.0f;
    }
}

// ------------------------------------------------------------------------------------------- phaser

void FxChain::phaser(float* l, float* r, int n, const float* p)
{
    const float mix = clamp(p[P_PH_MIX], 0.0f, 1.0f), fb = clamp(p[P_PH_FEEDBACK], -0.95f, 0.95f);
    const float depth = clamp(p[P_PH_DEPTH], 0.0f, 1.0f);
    float coef[2];
    for (int c = 0; c < 2; ++c)
    {
        const float sweep = std::sin(kTwoPi * (phPhase_ + 0.25f * (float)c));
        const float f = clamp(p[P_PH_FREQ] * fastExp2(2.0f * depth * sweep), 30.0f, 0.45f * sr_);
        const float t = std::tan(kPi * f / sr_);
        coef[c] = (t - 1.0f) / (t + 1.0f);
    }
    phPhase_ += p[P_PH_RATE] * (float)n / sr_;
    phPhase_ -= std::floor(phPhase_);
    float* io[2] = {l, r};
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            const float x = io[c][i];
            float y = x + fb * phFb_[c];
            for (int k = 0; k < 6; ++k)
            {
                const float o = coef[c] * y + phAp_[c][k];
                phAp_[c][k] = y - coef[c] * o;
                y = o;
            }
            phFb_[c] = y;
            io[c][i] = x + (0.5f * (x + y) - x) * mix;
        }
}

// ------------------------------------------------------------------------------------------- delay

void FxChain::delay(float* l, float* r, int n, const float* p, double bpm)
{
    const bool sync = p[P_DLY_SYNC] >= 0.5f;
    float target = sync ? (float)(syncDivBeats((int)p[P_DLY_DIV]) * 60.0 / std::max(bpm, 20.0)) * sr_
                        : p[P_DLY_TIME] * 0.001f * sr_;
    target = clamp(target, 2.0f, (float)dlyLine_[0].capacity() - 8.0f);
    if (dlyTime_ < 0.0f) dlyTime_ = target;
    const float fb = clamp(p[P_DLY_FEEDBACK], 0.0f, 0.95f), mix = clamp(p[P_DLY_MIX], 0.0f, 1.0f);
    const float lpA = 1.0f - std::exp(-kTwoPi * p[P_DLY_TONE] / sr_);
    const bool ping = (int)p[P_DLY_MODE] == 1;
    const float slew = 1.0f - std::exp(-1.0f / (0.05f * sr_));   // 50 ms glide on time changes
    for (int i = 0; i < n; ++i)
    {
        dlyTime_ += (target - dlyTime_) * slew;
        const float ol = dlyLine_[0].read(dlyTime_), or_ = dlyLine_[1].read(dlyTime_);
        dlyLp_[0] += (ol - dlyLp_[0]) * lpA;
        dlyLp_[1] += (or_ - dlyLp_[1]) * lpA;
        if (ping)
        {
            dlyLine_[0].push(0.5f * (l[i] + r[i]) + fb * dlyLp_[1]);
            dlyLine_[1].push(fb * dlyLp_[0]);
        }
        else
        {
            dlyLine_[0].push(l[i] + fb * dlyLp_[0]);
            dlyLine_[1].push(r[i] + fb * dlyLp_[1]);
        }
        l[i] += ol * mix;    // send style: the dry signal stays, the echoes come in on top
        r[i] += or_ * mix;
    }
}

// ------------------------------------------------------------------------------------------- reverb (FDN)

void FxChain::reverb(float* l, float* r, int n, const float* p)
{
    static const float plateMs[kLines] = {9.7f, 11.3f, 13.1f, 15.7f, 17.9f, 19.3f, 23.1f, 27.1f};
    static const float hallMs[kLines] = {29.7f, 37.1f, 41.1f, 43.7f, 53.9f, 59.3f, 67.1f, 73.1f};
    static const float diffMs[4] = {1.43f, 2.11f, 3.37f, 4.71f};
    const bool hall = (int)p[P_REV_TYPE] == 1;
    const float size = 0.4f + 1.2f * clamp(p[P_REV_SIZE], 0.0f, 1.0f);
    const float rt60 = 0.2f + 12.0f * p[P_REV_DECAY] * p[P_REV_DECAY] * (hall ? 1.0f : 0.6f);
    const float damp = 0.05f + 0.85f * clamp(p[P_REV_DAMP], 0.0f, 1.0f);
    const float mix = clamp(p[P_REV_MIX], 0.0f, 1.0f);
    const float pre = std::max(1.0f, p[P_REV_PREDELAY] * 0.001f * sr_);
    float len[kLines], g[kLines];
    for (int k = 0; k < kLines; ++k)
    {
        len[k] = std::min((hall ? hallMs[k] : plateMs[k]) * size * 0.001f * sr_, (float)revLine_[k].capacity() - 8.0f);
        g[k] = std::pow(10.0f, -3.0f * len[k] / (rt60 * sr_));
    }
    const float modDepth = 0.0004f * sr_;
    for (int i = 0; i < n; ++i)
    {
        revPre_.push(0.5f * (l[i] + r[i]));
        float x = revPre_.read(pre);
        for (int d = 0; d < 4; ++d)   // input diffusion: Schroeder allpasses
        {
            const int m = (int)(diffMs[d] * (hall ? 1.6f : 1.0f) * 0.001f * sr_);
            const float wd = revDiff_[d].readInt(m);
            const float w = x + 0.6f * wd;
            revDiff_[d].push(w);
            x = wd - 0.6f * w;
        }
        float y[kLines], sum = 0.0f;
        const float wob = modDepth * std::sin(kTwoPi * revPhase_);
        for (int k = 0; k < kLines; ++k)
        {
            float v = revLine_[k].read(len[k] + ((k & 3) == 0 ? wob : 0.0f));
            revLp_[k] += (v - revLp_[k]) * (1.0f - damp);
            y[k] = revLp_[k] * g[k];
            sum += y[k];
        }
        const float h = sum * (2.0f / kLines);   // Householder reflection
        float outL = 0.0f, outR = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            revLine_[k].push(y[k] - h + ((k & 1) ? -x : x) * 0.5f);
            outL += (k & 1) ? -y[k] : y[k];
            outR += (k & 2) ? -y[k] : y[k];
        }
        revPhase_ += 0.7f / sr_;
        if (revPhase_ >= 1.0f) revPhase_ -= 1.0f;
        l[i] += (outL * 0.35f - l[i]) * mix;
        r[i] += (outR * 0.35f - r[i]) * mix;
    }
}

// ------------------------------------------------------------------------------------------- OTT

void FxChain::ott(float* l, float* r, int n, const float* p)
{
    // Linkwitz-Riley 4th-order crossovers at 120 Hz and 2.5 kHz: two Butterworth (k = sqrt 2) SVF stages each.
    const float butterRes = (2.0f - 1.41421356f) / 1.98f;
    SvfCoefs c1, c2;
    c1.set(prewarp(120.0f, 1.0f / sr_), butterRes);
    c2.set(prewarp(2500.0f, 1.0f / sr_), butterRes);
    const float depth = clamp(p[P_OTT_DEPTH], 0.0f, 1.0f), up = clamp(p[P_OTT_UP], 0.0f, 1.0f), dn = clamp(p[P_OTT_DOWN], 0.0f, 1.0f);
    const float tScale = clamp(p[P_OTT_TIME], 0.1f, 10.0f);
    const float att = 1.0f - std::exp(-1.0f / (0.004f * tScale * sr_));
    const float rel = 1.0f - std::exp(-1.0f / (0.09f * tScale * sr_));
    const float outGain = dbToGain(p[P_OTT_GAIN]);

    // Band gains from the envelope at the block start (dB maths once per block per band).
    float gain[3];
    for (int b = 0; b < 3; ++b)
    {
        const float envDb = gainToDb(ottEnv_[b] + 1e-6f);
        float gDb = 0.0f;
        if (envDb > -24.0f) gDb -= (envDb + 24.0f) * 0.75f * dn;                 // downward, 4:1 above -24 dB
        if (envDb < -36.0f && envDb > -90.0f) gDb += std::min(24.0f, (-36.0f - envDb) * 0.67f) * up;   // upward 3:1
        gain[b] = dbToGain(gDb * depth);
    }
    float* io[2] = {l, r};
    for (int i = 0; i < n; ++i)
    {
        float band[2][3];
        for (int c = 0; c < 2; ++c)
        {
            float lp, bp, hp, x = io[c][i];
            float lo, rest, mid, hi;
            xo_[c][0].tick(c1, x, lp, bp, hp); xo_[c][1].tick(c1, lp, lo, bp, hp);
            xo_[c][2].tick(c1, x, lp, bp, hp); xo_[c][3].tick(c1, hp, lp, bp, rest);
            xo_[c][4].tick(c2, rest, lp, bp, hp); xo_[c][5].tick(c2, lp, mid, bp, hp);
            xo_[c][6].tick(c2, rest, lp, bp, hp); xo_[c][7].tick(c2, hp, lp, bp, hi);
            band[c][0] = lo; band[c][1] = mid; band[c][2] = hi;
        }
        float out[2] = {0, 0};
        for (int b = 0; b < 3; ++b)
        {
            const float a = std::max(std::fabs(band[0][b]), std::fabs(band[1][b]));
            ottEnv_[b] += (a - ottEnv_[b]) * (a > ottEnv_[b] ? att : rel);
            out[0] += band[0][b] * gain[b];
            out[1] += band[1][b] * gain[b];
        }
        l[i] = out[0] * outGain;
        r[i] = out[1] * outGain;
    }
}

// ------------------------------------------------------------------------------------------- EQ

void FxChain::eq(float* l, float* r, int n, const float* p)
{
    const float key[7] = {p[P_EQ_LOW_FREQ], p[P_EQ_LOW_GAIN], p[P_EQ_MID_FREQ], p[P_EQ_MID_GAIN], p[P_EQ_MID_Q],
                          p[P_EQ_HIGH_FREQ], p[P_EQ_HIGH_GAIN]};
    if (!std::equal(key, key + 7, eqKey_))
    {
        eqLow_.lowShelf(sr_, key[0], key[1]);
        eqMid_.peak(sr_, key[2], key[3], key[4]);
        eqHigh_.highShelf(sr_, key[5], key[6]);
        std::copy(key, key + 7, eqKey_);
    }
    for (int i = 0; i < n; ++i)
    {
        l[i] = eqHigh_.process(0, eqMid_.process(0, eqLow_.process(0, l[i])));
        r[i] = eqHigh_.process(1, eqMid_.process(1, eqLow_.process(1, r[i])));
    }
}

} // namespace helix
