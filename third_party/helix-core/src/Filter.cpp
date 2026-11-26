#include "helix/Filter.h"
#include <cmath>
#include <cstring>

namespace helix {

void Filter::prepare(float sampleRate)
{
    sr_ = sampleRate;
    invSr_ = 1.0f / sampleRate;
    // Comb: down to ~16 Hz
    combSize_ = 1;
    while (combSize_ < (int)(sampleRate / 16.0f) + 4) combSize_ <<= 1;
    combMem_.assign((size_t)combSize_ * 2, 0.0f);
    ch_[0].comb = combMem_.data();
    ch_[1].comb = combMem_.data() + combSize_;
    reset();
}

void Filter::reset()
{
    for (Chan& c : ch_)
    {
        float* keep = c.comb;
        c = Chan();
        c.comb = keep;
    }
    if (!combMem_.empty()) std::memset(combMem_.data(), 0, combMem_.size() * sizeof(float));
}

void Filter::update(float cutoffHz, float res, float drive, float fat)
{
    cutoffHz = clamp(cutoffHz, 16.0f, 0.49f * sr_);
    res_ = clamp(res, 0.0f, 1.0f);
    drive_ = 1.0f + 7.0f * drive * drive;
    fat_ = clamp(fat, 0.0f, 1.0f);
    makeup_ = 1.0f / (1.0f + 0.6f * drive);
    const float g = prewarp(cutoffHz, invSr_);
    switch (type_)
    {
        case FilterType::LP12: case FilterType::BP12: case FilterType::HP12: case FilterType::Notch12:
        case FilterType::LP24: case FilterType::BP24: case FilterType::HP24: case FilterType::Notch24:
            svf_.set(g, res_);
            svf2_.set(g, 0.25f * res_);
            break;
        case FilterType::Ladder24:
            ladG_ = g / (1.0f + g);         // TPT one-pole gain
            ladK_ = 4.2f * res_;            // self-oscillates near 4
            break;
        case FilterType::CombPlus: case FilterType::CombMinus:
            combDelay_ = clamp(sr_ / cutoffHz, 2.0f, (float)combSize_ - 4.0f);
            combFb_ = 0.98f * res_;
            combDamp_ = 0.03f;   // a touch of loop damping keeps very high resonance from fizzing
            break;
        case FilterType::Formant:
        {
            // Cutoff sweeps the vowel A-E-I-O-U (20 Hz -> A, 20 kHz -> U): log position
            static const float F[5][3] = {{800, 1150, 2900}, {400, 1600, 2700}, {250, 1750, 2600},
                                          {400, 750, 2400}, {350, 600, 2400}};
            static const float A[5][3] = {{1.0f, 0.5f, 0.025f}, {1.0f, 0.063f, 0.032f}, {1.0f, 0.032f, 0.16f},
                                          {1.0f, 0.28f, 0.089f}, {1.0f, 0.1f, 0.025f}};
            const float pos = clamp(std::log(cutoffHz / 20.0f) / std::log(1000.0f), 0.0f, 1.0f) * 4.0f;
            const int v = std::min(3, (int)pos);
            const float t = pos - (float)v;
            for (int k = 0; k < 3; ++k)
            {
                const float f = lerp(F[v][k], F[v + 1][k], t);
                fmt_[k].set(prewarp(f, invSr_), 0.80f + 0.19f * res_);
                fmtGain_[k] = lerp(A[v][k], A[v + 1][k], t) * 2.0f;
            }
            break;
        }
        case FilterType::Phaser:
        {
            const float gg = g;
            apA_ = (gg - 1.0f) / (gg + 1.0f);   // first-order allpass coefficient at cutoff
            phRes_ = 0.9f * res_;
            break;
        }
        default: break;
    }
}

float Filter::processChan(Chan& c, float x)
{
    float lp, bp, hp;
    switch (type_)
    {
        case FilterType::LP12: c.s1.tick(svf_, x, lp, bp, hp); return lp;
        case FilterType::BP12: c.s1.tick(svf_, x, lp, bp, hp); return bp * svf_.k;   // unity at the centre
        case FilterType::HP12: c.s1.tick(svf_, x, lp, bp, hp); return hp;
        case FilterType::Notch12: c.s1.tick(svf_, x, lp, bp, hp); return lp + hp;
        case FilterType::LP24: c.s1.tick(svf_, x, lp, bp, hp); c.s2.tick(svf2_, lp, lp, bp, hp); return lp;
        case FilterType::BP24:
        {
            c.s1.tick(svf_, x, lp, bp, hp);
            const float b1 = bp * svf_.k;
            c.s2.tick(svf2_, b1, lp, bp, hp);
            return bp * svf2_.k;
        }
        case FilterType::HP24: c.s1.tick(svf_, x, lp, bp, hp); c.s2.tick(svf2_, hp, lp, bp, hp); return hp;
        case FilterType::Notch24:
        {
            c.s1.tick(svf_, x, lp, bp, hp); const float n1 = lp + hp;
            c.s2.tick(svf2_, n1, lp, bp, hp); return lp + hp;
        }
        case FilterType::Ladder24:
        {
            // Four TPT one-poles, feedback from the last output (one-sample delay), tanh on the input sum.
            const float in = fastTanh(x - ladK_ * c.ladFb);
            float y = in;
            for (int i = 0; i < 4; ++i)
            {
                const float v = (y - c.lad[i]) * ladG_;
                const float o = v + c.lad[i];
                c.lad[i] = o + v;
                y = o;
            }
            c.ladFb = y;
            return y * (1.0f + 0.5f * ladK_);   // passband loss compensation
        }
        case FilterType::CombPlus: case FilterType::CombMinus:
        {
            const int mask = combSize_ - 1;
            float rp = (float)c.combPos - combDelay_;
            if (rp < 0) rp += (float)combSize_;
            const int i0 = (int)rp;
            const float fr = rp - (float)i0;
            const float d = c.comb[i0 & mask] + (c.comb[(i0 + 1) & mask] - c.comb[i0 & mask]) * fr;
            c.combLp += (d - c.combLp) * (1.0f - combDamp_);
            const float fb = (type_ == FilterType::CombPlus ? 1.0f : -1.0f) * combFb_ * c.combLp;
            const float y = x + fb;
            c.comb[c.combPos] = 8.0f * fastTanh(y * 0.125f);   // saturates only well above unity
            c.combPos = (c.combPos + 1) & mask;
            return y * (1.0f - 0.5f * combFb_);
        }
        case FilterType::Formant:
        {
            float y = 0.0f;
            for (int k = 0; k < 3; ++k) { c.fmt[k].tick(fmt_[k], x, lp, bp, hp); y += bp * fmtGain_[k]; }
            return y;
        }
        case FilterType::Phaser:
        {
            float y = x + phRes_ * c.phFb;
            for (int i = 0; i < 6; ++i)
            {
                const float o = apA_ * y + c.ap[i];
                c.ap[i] = y - apA_ * o;
                y = o;
            }
            c.phFb = y;
            return 0.5f * (x + y);
        }
        default: return x;
    }
}

void Filter::process(float& l, float& r)
{
    float xl = l, xr = r;
    if (drive_ > 1.0001f) { xl = fastTanh(xl * drive_) ; xr = fastTanh(xr * drive_); }
    float yl = processChan(ch_[0], xl);
    float yr = processChan(ch_[1], xr);
    if (fat_ > 0.0f)
    {
        // "Fat": parallel saturation that thickens low end, Massive style.
        yl = lerp(yl, fastTanh(yl * (1.0f + 3.0f * fat_)) * 1.1f, fat_);
        yr = lerp(yr, fastTanh(yr * (1.0f + 3.0f * fat_)) * 1.1f, fat_);
    }
    if (drive_ > 1.0001f) { yl *= makeup_ * 1.6f; yr *= makeup_ * 1.6f; }
    l = yl; r = yr;
}

} // namespace helix

namespace helix {

template <FilterType T>
void Filter::runBlock(float* l, float* r, int n)
{
    const bool drive = drive_ > 1.0001f;
    for (int i = 0; i < n; ++i)
    {
        float xl = l[i], xr = r[i];
        if (drive) { xl = fastTanh(xl * drive_); xr = fastTanh(xr * drive_); }
        float yl, yr, lp, bp, hp;
        if constexpr (T == FilterType::LP12)
        {
            ch_[0].s1.tick(svf_, xl, lp, bp, hp); yl = lp;
            ch_[1].s1.tick(svf_, xr, lp, bp, hp); yr = lp;
        }
        else if constexpr (T == FilterType::LP24)
        {
            ch_[0].s1.tick(svf_, xl, lp, bp, hp); ch_[0].s2.tick(svf2_, lp, yl, bp, hp);
            ch_[1].s1.tick(svf_, xr, lp, bp, hp); ch_[1].s2.tick(svf2_, lp, yr, bp, hp);
        }
        else if constexpr (T == FilterType::BP12)
        {
            ch_[0].s1.tick(svf_, xl, lp, bp, hp); yl = bp * svf_.k;
            ch_[1].s1.tick(svf_, xr, lp, bp, hp); yr = bp * svf_.k;
        }
        else if constexpr (T == FilterType::HP12)
        {
            ch_[0].s1.tick(svf_, xl, lp, bp, hp); yl = hp;
            ch_[1].s1.tick(svf_, xr, lp, bp, hp); yr = hp;
        }
        else { yl = processChan(ch_[0], xl); yr = processChan(ch_[1], xr); }
        if (fat_ > 0.0f)
        {
            yl = lerp(yl, fastTanh(yl * (1.0f + 3.0f * fat_)) * 1.1f, fat_);
            yr = lerp(yr, fastTanh(yr * (1.0f + 3.0f * fat_)) * 1.1f, fat_);
        }
        if (drive) { yl *= makeup_ * 1.6f; yr *= makeup_ * 1.6f; }
        l[i] = yl; r[i] = yr;
    }
}

void Filter::processBlock(float* l, float* r, int n)
{
    switch (type_)
    {
        case FilterType::LP12: runBlock<FilterType::LP12>(l, r, n); break;
        case FilterType::LP24: runBlock<FilterType::LP24>(l, r, n); break;
        case FilterType::BP12: runBlock<FilterType::BP12>(l, r, n); break;
        case FilterType::HP12: runBlock<FilterType::HP12>(l, r, n); break;
        default: runBlock<FilterType::Count>(l, r, n); break;
    }
}

} // namespace helix
