// Wavetable oscillator with up to 7 unison lanes and two warp slots (one per voice per oscillator).
//
// Signal per lane and sample:
//   phase -> + FM (from the other oscillator, the sub or the noise) -> phase warp 1 -> phase warp 2
//         -> table read (frame morph x linear interpolation x mip crossfade)
//         -> output warps (AM, RM, bit reduction, sync / formant windows)
// The mip level is chosen per block from the lane's frequency times the warp "speed-up", so phase warps that
// squeeze the cycle (PWM, sync, formant, bend) still read a band-limited level.
#pragma once
#include "helix/Config.h"
#include "helix/Math.h"
#include "helix/TableRead.h"
#include "helix/Halfband.h"

namespace helix {

enum class Warp : int
{
    Off = 0, BendPlus, BendMinus, Asym, Pwm, Mirror, Sync, Formant, FmOther, FmSub, FmNoise, Am, Rm, Quantize, BitRed, Count
};

struct OscBlock   // control-rate values for one block
{
    float freqHz = 440.0f;
    float pos0 = 0.0f, pos1 = 0.0f;        // wavetable position at block start / end (ramped)
    float level0 = 1.0f, level1 = 1.0f;    // ramped
    float pan = 0.0f;
    Warp warp[2] = {Warp::Off, Warp::Off};
    float warpAmt0[2] = {0, 0}, warpAmt1[2] = {0, 0};   // ramped
    int unison = 1;
    float detune = 0.0f, blend = 0.75f, spread = 0.5f;
    bool crossfade = true;
    bool oversample = false;              // 2x with a half-band decimator (Quality: High)
};

// Per-sample modulation inputs (may be null when not used).
struct OscModInputs
{
    const float* other = nullptr;   // the other oscillator's mono output
    const float* sub = nullptr;
    const float* noise = nullptr;
    // Modulation oscillator (-1..1 per sample): phase modulation (pmAmt 0..1 = up to +-1 cycle) and
    // wavetable position modulation (posAmt 0..1 = up to +-half the table).
    const float* pm = nullptr;
    float pmAmt = 0.0f;
    const float* posMod = nullptr;
    float posAmt = 0.0f;
};

class WtOscillator
{
public:
    void setSampleRate(float sr) { invSr_ = 1.0f / sr; }
    // phase: start phase 0..1, randomAmt: 0..1 random offset per lane
    void start(float phase, float randomAmt, Rng& rng)
    {
        for (int i = 0; i < kMaxUnison; ++i) phase_[i] = wrap01(phase + randomAmt * rng.uni());
        decL_.reset(); decR_.reset();
    }

    // Renders n samples: adds into l / r (stereo, panned) and writes the mono sum into mono (overwrites).
    void render(const Wavetable* wt, const OscBlock& b, const OscModInputs& mod, float* l, float* r, float* mono, int n);

    // Phase warps (exposed for tests). a: amount 0..1.
    static float warpPhase(Warp w, float p, float a, float modSample);
    static float warpSpeed(Warp w, float a);   // how much a warp raises the highest frequency read
    // Largest amount for warp w that keeps speed * otherSpeed * inc below 0.45 (so a squeezed cycle never
    // folds over Nyquist). Returns 1 when any amount is fine.
    static float maxWarpAmount(Warp w, float otherSpeed, float inc);

private:
    void configureLanes(const OscBlock& b);

    float invSr_ = 1.0f / 44100.0f;
    float phase_[kMaxUnison] = {};
    float ratio_[kMaxUnison] = {};
    float gainL_[kMaxUnison] = {}, gainR_[kMaxUnison] = {}, gainM_[kMaxUnison] = {};
    int lanes_ = 1;
    HalfbandDecimator decL_, decR_;
};

// Sub oscillator: sine / triangle / square / saw (polyBLEP for the edges).
class SubOscillator
{
public:
    void setSampleRate(float sr) { invSr_ = 1.0f / sr; }
    void start(float phase) { phase_ = wrap01(phase); }
    void render(int shape, float freqHz, float level, float* out, int n);
private:
    float invSr_ = 1.0f / 44100.0f;
    float phase_ = 0.0f;
    float triState_ = 0.0f;
};

// Noise and texture source: plays shared, pre-generated loops (white, pink, brown, vinyl, tape hiss, crackle).
class NoiseSource
{
public:
    static constexpr int kNumTypes = 6;
    static constexpr int kLoopRate = 44100;
    static constexpr int kLoopLen = 3 * kLoopRate;
    // Builds the shared loops (non-real-time; call once before audio, e.g. from Engine's constructor).
    static void buildSharedLoops();

    void setSampleRate(float sr) { rateScale_ = (float)kLoopRate / sr; }
    // Loops start at a random point (every note sounds a little different); one-shots start at the top.
    void start(Rng& rng, bool loop) { pos_ = loop ? (double)(rng.next() % (uint32_t)kLoopLen) : 0.0; done_ = false; }
    // pitchRatio: 1 = original speed. loop = false plays once from the start position to the end.
    void render(int type, float pitchRatio, bool loop, float level, float* out, int n);
private:
    double pos_ = 0.0;
    float rateScale_ = 1.0f;
    bool done_ = false;
};

} // namespace helix
