// The HELIX voice filter: one stereo filter with selectable type.
#pragma once
#include "helix/Svf.h"
#include "helix/Math.h"
#include <vector>

namespace helix {

enum class FilterType : int
{
    LP12 = 0, LP24, BP12, BP24, HP12, HP24, Notch12, Notch24, Ladder24, CombPlus, CombMinus, Formant, Phaser, Count
};

class Filter
{
public:
    void prepare(float sampleRate);           // allocates delay memory: not on the audio thread
    void reset();
    void setType(FilterType t) { if (t != type_) { type_ = t; reset(); } }
    FilterType type() const { return type_; }
    // Call once per control block. cutoff Hz, res/drive/fat 0..1.
    void update(float cutoffHz, float res, float drive, float fat);
    void process(float& l, float& r);
    // Same as process() for n samples in place, with the type dispatch outside the sample loop.
    void processBlock(float* l, float* r, int n);

private:
    struct Chan
    {
        SvfState s1, s2;
        float lad[4] = {0, 0, 0, 0}, ladFb = 0;
        float* comb = nullptr; int combPos = 0; float combLp = 0;
        SvfState fmt[3];
        float ap[6] = {0, 0, 0, 0, 0, 0}, phFb = 0;
    };
    float processChan(Chan& c, float x);
    template <FilterType T> void runBlock(float* l, float* r, int n);

    FilterType type_ = FilterType::LP12;
    float sr_ = 44100.0f, invSr_ = 1.0f / 44100.0f;
    SvfCoefs svf_, svf2_;   // svf2_: the second stage of the 24 dB modes (low Q, so the resonance is not squared)
    // ladder
    float ladG_ = 0, ladK_ = 0;
    // comb
    std::vector<float> combMem_;
    int combSize_ = 0;
    float combDelay_ = 100, combFb_ = 0, combDamp_ = 0;
    // formant
    SvfCoefs fmt_[3];
    float fmtGain_[3] = {1, 1, 1};
    // phaser
    float apA_ = 0, phRes_ = 0;
    float drive_ = 1, fat_ = 0, res_ = 0, makeup_ = 1;
    Chan ch_[2];
};

} // namespace helix
