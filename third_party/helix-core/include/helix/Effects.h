// Global effects, after the voices: distortion, chorus / ensemble, phaser, delay, reverb, OTT, EQ.
// Re-orderable through the seven FX slot parameters. All buffers are allocated in prepare().
#pragma once
#include "helix/Config.h"
#include "helix/Halfband.h"
#include "helix/Math.h"
#include "helix/Params.h"
#include "helix/Svf.h"
#include <vector>

namespace helix {

enum class FxType : int { Distortion = 0, Chorus, Phaser, Delay, Reverb, Ott, Eq, Count };

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] = {0, 0}, z2[2] = {0, 0};
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    inline float process(int ch, float x)
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
    void lowShelf(float sr, float f, float db);
    void highShelf(float sr, float f, float db);
    void peak(float sr, float f, float db, float q);
};

class DelayLine
{
public:
    void allocate(int maxSamples) { int n = 1; while (n < maxSamples + 4) n <<= 1; buf_.assign((size_t)n, 0.0f); mask_ = n - 1; w_ = 0; }
    void clear() { std::fill(buf_.begin(), buf_.end(), 0.0f); }
    inline void push(float v) { buf_[(size_t)w_] = v; w_ = (w_ + 1) & mask_; }
    // delay >= 1 samples, fractional (linear interpolation)
    inline float read(float delay) const
    {
        const float rp = (float)w_ - delay;
        const int i = (int)std::floor(rp);
        const float t = rp - (float)i;
        const float a = buf_[(size_t)(i & mask_)], b = buf_[(size_t)((i + 1) & mask_)];
        return a + (b - a) * t;
    }
    inline float readInt(int delay) const { return buf_[(size_t)((w_ - delay) & mask_)]; }
    int capacity() const { return mask_ + 1; }
private:
    std::vector<float> buf_ = std::vector<float>(4, 0.0f);
    int mask_ = 3, w_ = 0;
};

class FxChain
{
public:
    void prepare(float sampleRate);
    void reset();
    // p: plain parameter values (already modulated). n <= kCtrlBlock. bpm for synced delay.
    void process(float* l, float* r, int n, const float* p, double bpm, bool oversample);
    // True when the OTT (the heavy effect) is running; the UI marks it in the CPU meter.
    bool heavyActive() const { return heavy_; }

private:
    void distortion(float* l, float* r, int n, const float* p, bool oversample);
    void chorus(float* l, float* r, int n, const float* p);
    void phaser(float* l, float* r, int n, const float* p);
    void delay(float* l, float* r, int n, const float* p, double bpm);
    void reverb(float* l, float* r, int n, const float* p);
    void ott(float* l, float* r, int n, const float* p);
    void eq(float* l, float* r, int n, const float* p);

    float sr_ = 44100.0f;
    bool heavy_ = false;

    // distortion
    HalfbandInterpolator upL_, upR_;
    HalfbandDecimator dnL_, dnR_;
    float crushHold_[2] = {0, 0}, crushPhase_ = 0.0f, toneZ_[2] = {0, 0};

    // chorus
    DelayLine choLine_[2];
    float choPhase_ = 0.0f;

    // phaser
    float phPhase_ = 0.0f, phAp_[2][6] = {}, phFb_[2] = {0, 0};

    // delay
    DelayLine dlyLine_[2];
    float dlyTime_ = -1.0f, dlyLp_[2] = {0, 0};

    // reverb (8-line FDN)
    static constexpr int kLines = 8;
    DelayLine revLine_[kLines], revPre_;
    DelayLine revDiff_[4];
    float revLp_[kLines] = {}, revPhase_ = 0.0f;

    // OTT: 3 bands x 2 channels (LR4 crossovers built from two Butterworth SVF stages)
    SvfState xo_[2][8];
    float ottEnv_[3] = {0, 0, 0};

    // EQ
    Biquad eqLow_, eqMid_, eqHigh_;
    float eqKey_[7] = {-1, -1, -1, -1, -1, -1, -1};
};

} // namespace helix
