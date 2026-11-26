#include "helix/MasterOtt.h"
#include "helix/Math.h"
#include "helix/Params.h"
#include <cmath>

namespace helix {

constexpr float MasterOtt::kAbove[];
constexpr float MasterOtt::kBelow[];
constexpr float MasterOtt::kMakeup[];

static void butter(Biquad& b, bool hp, float f, double sr)
{
    const float w = kTwoPi * f / (float)sr, cs = std::cos(w), al = std::sin(w) / (2.0f * 0.70710678f), a0 = 1.0f + al;
    if (hp) { b.b0 = (1.0f + cs) * 0.5f / a0; b.b1 = -(1.0f + cs) / a0; }
    else    { b.b0 = (1.0f - cs) * 0.5f / a0; b.b1 = (1.0f - cs) / a0; }
    b.b2 = b.b0;
    b.a1 = -2.0f * cs / a0;
    b.a2 = (1.0f - al) / a0;
}

void MasterOtt::prepare(double sampleRate)
{
    sr_ = sampleRate;
    for (int k = 0; k < 2; ++k)
    {
        butter(lpLo_[k], false, kSplitLow, sr_);  butter(hpLo_[k], true, kSplitLow, sr_);
        butter(lpHi_[k], false, kSplitHigh, sr_); butter(hpHi_[k], true, kSplitHigh, sr_);
    }
    lastTime_ = -1.0f;
    reset();
}

void MasterOtt::reset()
{
    for (auto* set : {lpLo_, hpLo_, lpHi_, hpHi_}) for (int k = 0; k < 2; ++k) set[k].reset();
    for (int b = 0; b < kBands; ++b)
    {
        env_[b] = 0.0f;
        meterLvl_[b] = -120.0f;
        level_[b].store(-120.0f, std::memory_order_relaxed);
        gain_[b].store(0.0f, std::memory_order_relaxed);
    }
    meterOut_ = -120.0f;
    out_.store(-120.0f, std::memory_order_relaxed);
}

void MasterOtt::process(float* l, float* r, int n, const float* p)
{
    if (p[P_MOTT_ON] < 0.5f) return;
    const float time = clamp(p[P_MOTT_TIME], 0.0f, 1.0f);
    if (time != lastTime_)
    {
        lastTime_ = time;
        const float a = std::max(0.0001f, 0.0135f * time), rl = std::max(0.002f, 0.132f * time);
        att_ = 1.0f - std::exp(-1.0f / (a * (float)sr_));
        rel_ = 1.0f - std::exp(-1.0f / (rl * (float)sr_));
    }
    const float depth = clamp(p[P_MOTT_DEPTH], 0.0f, 1.0f);
    const float up = clamp(p[P_MOTT_UP], 0.0f, 1.0f), down = clamp(p[P_MOTT_DOWN], 0.0f, 1.0f);
    const float inG = dbToGain(p[P_MOTT_IN]), outG = dbToGain(p[P_MOTT_OUT]);
    const float bandDb[kBands] = {p[P_MOTT_LOW], p[P_MOTT_MID], p[P_MOTT_HIGH]};
    const float kDown = 1.0f - 1.0f / 66.0f, kUp = 1.0f - 1.0f / 4.17f;
    float lastGain[kBands] = {0, 0, 0};
    float peakOut = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float xl = l[i] * inG, xr = r[i] * inG;
        float bl[kBands], br[kBands];
        bl[0] = lpLo_[1].process(0, lpLo_[0].process(0, xl));
        br[0] = lpLo_[1].process(1, lpLo_[0].process(1, xr));
        const float rl = hpLo_[1].process(0, hpLo_[0].process(0, xl));
        const float rr = hpLo_[1].process(1, hpLo_[0].process(1, xr));
        bl[1] = lpHi_[1].process(0, lpHi_[0].process(0, rl));
        br[1] = lpHi_[1].process(1, lpHi_[0].process(1, rr));
        bl[2] = hpHi_[1].process(0, hpHi_[0].process(0, rl));
        br[2] = hpHi_[1].process(1, hpHi_[0].process(1, rr));

        float wl = 0.0f, wr = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            const float x = std::max(std::fabs(bl[b]), std::fabs(br[b]));
            env_[b] += (x > env_[b] ? att_ : rel_) * (x - env_[b]);
            const float lv = gainToDb(env_[b] + 1e-9f);
            float g = 0.0f;
            if (lv > kAbove[b]) g = -(lv - kAbove[b]) * kDown * down;                 // squash from above
            else if (lv < kBelow[b] && lv > -90.0f)                                  // lift from below (not silence)
                g = std::min(36.0f, (kBelow[b] - lv) * kUp * up) * std::min(1.0f, (lv + 90.0f) / 20.0f);
            lastGain[b] = g;
            const float lin = dbToGain(g + kMakeup[b] + bandDb[b]);
            wl += bl[b] * lin;
            wr += br[b] * lin;
            if (lv > meterLvl_[b]) meterLvl_[b] = lv;
        }
        const float ol = (xl + (wl - xl) * depth) * outG, orr = (xr + (wr - xr) * depth) * outG;
        l[i] = ol;
        r[i] = orr;
        peakOut = std::max(peakOut, std::max(std::fabs(ol), std::fabs(orr)));
    }
    // meters: peak-hold per block, falling ~20 dB/s
    const float fall = 20.0f * (float)n / (float)sr_;
    for (int b = 0; b < kBands; ++b)
    {
        level_[b].store(meterLvl_[b], std::memory_order_relaxed);
        gain_[b].store(lastGain[b], std::memory_order_relaxed);
        meterLvl_[b] = std::max(-120.0f, meterLvl_[b] - fall);
    }
    meterOut_ = std::max(gainToDb(peakOut), meterOut_ - fall);
    out_.store(meterOut_, std::memory_order_relaxed);
}

} // namespace helix
