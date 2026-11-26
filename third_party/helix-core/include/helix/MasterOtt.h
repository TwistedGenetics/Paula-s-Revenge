// Master OTT: the final-output multiband upward/downward compressor, built to behave like the original OTT
// (Ableton's Multiband Dynamics "OTT" preset that Xfer's free plugin recreates): three bands split at 88.3 Hz and
// 2.5 kHz, each squashed hard from above (1:66) and lifted from below (4.17:1) toward its own thresholds, then
// pushed back up by fixed make-up gains (L +10.3, M +5.7, H +10.3 dB). Controls as on the original: Depth (dry /
// wet), Time (attack 13.5 ms / release 132 ms at 100%), Upward and Downward (how much of each), In and Out gain,
// and L / M / H band levels.
#pragma once
#include "helix/Effects.h"
#include <atomic>

namespace helix {

class MasterOtt
{
public:
    static constexpr int kBands = 3;                                   // 0 low, 1 mid, 2 high
    static constexpr float kAbove[kBands] = {-33.8f, -30.2f, -35.5f};  // downward threshold (dB)
    static constexpr float kBelow[kBands] = {-40.8f, -41.8f, -40.8f};  // upward threshold (dB)
    static constexpr float kMakeup[kBands] = {10.3f, 5.7f, 10.3f};
    static constexpr float kSplitLow = 88.3f, kSplitHigh = 2500.0f;

    MasterOtt() { reset(); }
    void prepare(double sampleRate);
    void reset();
    // p: the engine's parameter snapshot (P_MOTT_*). Processes in place; does nothing when OTT is off.
    void process(float* l, float* r, int n, const float* p);

    // Meters for the UI (any thread): each band's level going in (dBFS, after In gain) and the gain OTT applies
    // to it (dB, + = lifted, - = squashed), and the output level.
    float bandLevel(int b) const { return level_[b].load(std::memory_order_relaxed); }
    float bandGain(int b) const { return gain_[b].load(std::memory_order_relaxed); }
    float outLevel() const { return out_.load(std::memory_order_relaxed); }

private:
    double sr_ = 48000.0;
    Biquad lpLo_[2], hpLo_[2], lpHi_[2], hpHi_[2];   // Linkwitz-Riley 24 dB: two Butterworths each
    float env_[kBands] = {0, 0, 0};
    float lastTime_ = -1.0f, att_ = 0.0f, rel_ = 0.0f;
    float meterLvl_[kBands] = {-120, -120, -120}, meterOut_ = -120.0f;
    std::atomic<float> level_[kBands], gain_[kBands], out_{-120.0f};
};

} // namespace helix
