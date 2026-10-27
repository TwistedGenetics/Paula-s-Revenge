// Paula's Revenge - the mixer: per-track EQ, inserts, sends, returns, master (plain C++)
#pragma once

#include "MixerTypes.h"
#include "Effects.h"
#include <atomic>
#include <memory>
#include <vector>

namespace prv
{
class MixerEngine
{
public:
    static constexpr int kMaxBlock = 512;

    MixerEngine();
    void prepare (double sampleRate);   // may allocate (re-prepares existing effects)

    // Make the effect objects match the settings (creates effects for new slot
    // types; may allocate). Replaced effects are moved into `old` so they can be
    // freed outside the audio lock.
    void syncEffects (const MixerSettings&, std::vector<std::unique_ptr<Effect>>& old);
    void syncEffectsNow (const MixerSettings& m) { std::vector<std::unique_ptr<Effect>> old; syncEffects (m, old); }
    void reset();

    // Per-track stereo buffers (n <= kMaxBlock) are processed in place and mixed,
    // with `extraL/R` (preview notes) added to the master bus, into outL/outR.
    void process (const MixerSettings&, float* const* trackL, float* const* trackR, int numTracks,
                  const float* extraL, const float* extraR, float* outL, float* outR, int n, double bpm);

    // meters (peaks since the UI last read them)
    std::atomic<float> trackPeak[kMixTracks] {}, returnPeak[kReturns] {}, masterPeakL { 0 }, masterPeakR { 0 };

    bool hasEffect (int slot) const { return slot >= 0 && slot < kTotalSlots && fx[slot] != nullptr; }

private:
    double sr = 48000.0;
    ChannelEqProcessor eq[kMixTracks];
    std::unique_ptr<Effect> fx[kTotalSlots];
    FxType fxType[kTotalSlots] {};
    std::vector<float> sendL[kReturns], sendR[kReturns];
    void runInserts (const FxSlot* slots, int firstSlot, float* L, float* R, int n, double bpm);
};
} // namespace prv
