// Modulation: LFOs, performers (step sequencers of curves), mod sources, the matrix.
#pragma once
#include "helix/Config.h"
#include "helix/Math.h"
#include "helix/Params.h"
#include <atomic>

namespace helix {

enum ModSource : int
{
    SRC_NONE = 0, SRC_ENV1, SRC_ENV2, SRC_ENV3, SRC_ENV4, SRC_LFO1, SRC_LFO2, SRC_LFO3, SRC_LFO4, SRC_PERF1, SRC_PERF2,
    SRC_VELOCITY, SRC_PRESSURE, SRC_CHAN_AT, SRC_MODWHEEL, SRC_PITCHBEND, SRC_NOTE, SRC_RANDOM,
    SRC_MACRO1, SRC_MACRO2, SRC_MACRO3, SRC_MACRO4, SRC_MACRO5, SRC_MACRO6, SRC_MACRO7, SRC_MACRO8,
    kNumModSources
};

// Special destinations before the parameter destinations (see choices::modDest).
enum ModDestSpecial : int { DST_NONE = 0, DST_PITCH, DST_A_PITCH, DST_B_PITCH, DST_AMP, DST_PAN, kNumSpecialDests };

// Destination index -> parameter index (or -1 for specials / none). Built once.
int modDestParam(int dest);
int numModDests();

// Length of a sync division in quarter notes (choices::syncDiv order).
double syncDivBeats(int div);

enum class LfoShape : int { Sine = 0, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, Breakpoint, Steps };
enum class LfoMode : int { Retrig = 0, Free, OneShot };

// Shared user-drawn data: step values and breakpoint shapes for the 4 LFOs. Written by the UI / preset loader,
// read by the audio thread (relaxed atomics: a torn read only means one block sees a half-edited shape).
// Pad scenes: 16 snapshots of the 8 macros. Written from any thread; the engine morphs to one on recall.
constexpr int kNumScenes = 16;
struct MacroScenes
{
    std::atomic<float> v[kNumScenes][kNumMacros];
    MacroScenes() { resetToDefaults(); }
    // Scene s = every macro at s / 15: a ladder from all-off (pad 1) to all-full (pad 16).
    void resetToDefaults() { for (int s = 0; s < kNumScenes; ++s) for (int m = 0; m < kNumMacros; ++m) v[s][m].store((float)s / 15.0f); }
};

struct LfoShapes
{
    std::atomic<float> steps[kNumLfos][kMaxSteps];
    std::atomic<int>   numPoints[kNumLfos];
    std::atomic<float> px[kNumLfos][kLfoMaxPoints], py[kNumLfos][kLfoMaxPoints];   // x 0..1 rising, y -1..1
    LfoShapes() { resetToDefaults(); }
    void resetToDefaults();
    float evalBreakpoint(int lfo, float phase) const;
};

class Lfo
{
public:
    void reset(float startPhase, uint32_t seed) { phase_ = wrap01(startPhase); rng_ = Rng(seed); held_ = rng_.bi(); next_ = rng_.bi(); done_ = false; smooth_ = 0.0f; age_ = 0.0f; first_ = true; }
    // Advance by n samples and return the output (bipolar -1..1, after rise and smoothing).
    // freePhase >= 0 forces the phase (free-running / transport-locked LFOs share one phase across voices).
    float advance(int n, float sr, float rateHz, LfoShape shape, LfoMode mode, float riseMs, float smooth,
                  float freePhase, int lfoIndex, const LfoShapes& shapes, int numSteps);
    float phase() const { return phase_; }
private:
    float shapeAt(float ph, LfoShape s, int lfoIndex, const LfoShapes& shapes, int numSteps) const;
    float phase_ = 0.0f, held_ = 0.0f, next_ = 0.0f, smooth_ = 0.0f, age_ = 0.0f;
    bool done_ = false, first_ = true;
    Rng rng_;
};

// Performer value at position pos (in steps, 0..len): per-step level -1..1 times the step's curve.
float performerValue(const float* p, int perfIndex, double posSteps);
// The pattern as drawn (steps 1..Length in order, no loop or fill), for displays.
float performerPatternValue(const float* p, int perfIndex, double posSteps);
// Which step (0-based) plays at step count n (from the start), following Loop / Loop Start / Loop End.
int performerStepAt(const float* p, int perfIndex, long long n);
// Steps of performer `perfIndex` played after `beats` quarter notes (Step rate and Feel), before swing.
double performerSteps(const float* p, int perfIndex, double beats);
// LFO rate multiplier from Jump (1 when off or the Jump Macro is below half way).
float lfoJumpFactor(const float* p, int lfo);

// Massive-style curve library: one curve per step, drawn across the step (t 0..1) and scaled by the
// step's level. Curve 3 (Smooth) glides to the next step's level instead. 0..5 are the original six.
constexpr int kNumPerfCurves = 28;
float perfCurve(int curve, float t);

// Source value curve: c in -1..1 bends |v| (c > 0 = exponential, c < 0 = logarithmic), keeps the sign.
inline float modCurve(float v, float c)
{
    if (c > -0.01f && c < 0.01f) return v;
    const float a = std::fabs(v);
    const float e = fastExp2(c * 2.0f);   // 0.25 .. 4
    const float y = a > 0.0f ? fastExp2(e * std::log2(a)) : 0.0f;
    return v < 0.0f ? -y : y;
}

} // namespace helix
