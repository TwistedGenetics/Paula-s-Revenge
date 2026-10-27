// Paula's Revenge - mixer effects and EQ (plain C++)
#pragma once

#include "MixerTypes.h"
#include <memory>
#include <vector>

namespace prv
{
// A stereo effect for an insert slot. Parameters are the slot's 0..1 values.
class Effect
{
public:
    virtual ~Effect() = default;
    virtual void prepare (double sampleRate) = 0;   // may allocate
    virtual void reset() = 0;
    virtual void process (float* left, float* right, int numSamples, const float* params, double bpm) = 0;
    // automation of a plug-in's own parameter (built-in effects are automated through their slot values)
    virtual void setParameter (int /*index*/, float /*value01*/) {}
};

std::unique_ptr<Effect> makeEffect (FxType);

// Five-band channel EQ (high-pass, low shelf, two bells, high shelf), stereo
class ChannelEqProcessor
{
public:
    void prepare (double sampleRate) { sr = sampleRate; dirty = true; }
    void reset();
    void process (float* left, float* right, int numSamples, const ChannelEq& eq);
    // the EQ's gain in dB at a frequency (for drawing the curve)
    static double responseDb (const ChannelEq& eq, double freq, double sampleRate);

private:
    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { double z1 = 0, z2 = 0; };
    static Coeffs design (int band, const EqBand& b, double sampleRate);
    double sr = 48000.0;
    ChannelEq last;
    bool dirty = true;
    Coeffs c[kEqBands];
    State s[kEqBands][2];
};
} // namespace prv
