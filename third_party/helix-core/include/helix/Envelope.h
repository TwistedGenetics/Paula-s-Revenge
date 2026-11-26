// ADSR envelope with curve controls, per-sample. Analogue-style exponential segments whose "overshoot" target
// sets the curvature: curve -1 = very exponential (fast start), 0 = mildly exponential, +1 = almost linear.
// Attack curve is mirrored (+1 = linear, -1 = slow start, "log").
#pragma once
#include "helix/Math.h"

namespace helix {

class Envelope
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate(float sr) { sr_ = sr; }
    // Times in ms, sustain 0..1, curves -1..1. Cheap enough to call once per control block.
    void set(float aMs, float dMs, float s, float rMs, float aCurve, float dCurve, float rCurve)
    {
        if (aMs != aMs_ || aCurve != aCurve_) { aMs_ = aMs; aCurve_ = aCurve; calc(aMs, aCurve, aCoef_, aOver_); }
        if (dMs != dMs_ || dCurve != dCurve_) { dMs_ = dMs; dCurve_ = dCurve; calc(dMs, dCurve, dCoef_, dOver_); }
        if (rMs != rMs_ || rCurve != rCurve_) { rMs_ = rMs; rCurve_ = rCurve; calc(rMs, rCurve, rCoef_, rOver_); }
        sus_ = clamp(s, 0.0f, 1.0f);
    }
    void gate(bool on, bool legato = false)
    {
        if (on) { if (!legato || stage_ == Stage::Idle || stage_ == Stage::Release) stage_ = Stage::Attack; }
        else if (stage_ != Stage::Idle) stage_ = Stage::Release;
    }
    void hardReset() { stage_ = Stage::Idle; v_ = 0.0f; }
    bool active() const { return stage_ != Stage::Idle; }
    bool releasing() const { return stage_ == Stage::Release; }
    Stage stage() const { return stage_; }
    float value() const { return v_; }

    inline float process()
    {
        switch (stage_)
        {
            case Stage::Idle: return 0.0f;
            case Stage::Attack:
            {
                // Attack approaches (1 + over) from below; the curve flips the shape for slow starts.
                v_ = (1.0f + aOver_) + (v_ - (1.0f + aOver_)) * aCoef_;
                if (v_ >= 1.0f) { v_ = 1.0f; stage_ = Stage::Decay; }
                return shapedAttack();
            }
            case Stage::Decay:
            {
                const float target = sus_ - dOver_ * (1.0f - sus_);
                v_ = target + (v_ - target) * dCoef_;
                if (v_ <= sus_ + 1e-6f) { v_ = sus_; stage_ = Stage::Sustain; }
            }
                return v_;
            case Stage::Sustain:
                v_ = sus_;
                return v_;
            case Stage::Release:
                v_ = (-rOver_) + (v_ + rOver_) * rCoef_;
                if (v_ <= 0.0f) { v_ = 0.0f; stage_ = Stage::Idle; }
                return v_;
        }
        return v_;
    }
    // Advance n samples, return the last value (for control-rate envelopes).
    float advance(int n) { float y = v_; for (int i = 0; i < n; ++i) y = process(); return y; }

private:
    float shapedAttack() const
    {
        // Negative attack curve: slow start (square the linear-ish rise).
        if (aCurve_ < 0.0f) { const float k = -aCurve_; return lerp(v_, v_ * v_ * v_, k); }
        return v_;
    }
    void calc(float ms, float curve, float& coef, float& over)
    {
        // over: target overshoot as a fraction of the segment span. Small = exponential, large = linear.
        const float c = clamp(curve, -1.0f, 1.0f);
        over = 0.0005f * std::pow(2000.0f, (c + 1.0f) * 0.5f) ;    // 0.0005 .. ~1.0
        if (over > 1.0f) over = 1.0f;
        const float samples = std::max(1.0f, ms * 0.001f * sr_);
        // Time for the curve to cross the segment span exactly once.
        coef = std::exp(-std::log((1.0f + over) / over) / samples);
    }
    float sr_ = 44100.0f;
    Stage stage_ = Stage::Idle;
    float v_ = 0.0f, sus_ = 1.0f;
    float aMs_ = -1, dMs_ = -1, rMs_ = -1, aCurve_ = 9, dCurve_ = 9, rCurve_ = 9;
    float aCoef_ = 0, dCoef_ = 0, rCoef_ = 0, aOver_ = 1, dOver_ = 1, rOver_ = 1;
};

} // namespace helix
