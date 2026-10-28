// Paula's Revenge - the mixer: per-track EQ, inserts, sends, returns, master (plain C++)
#pragma once

#include "MixerTypes.h"
#include "Effects.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
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

    // The same in two steps, so slow work (loading plug-ins) happens outside the audio lock:
    // plan() makes and prepares the new effects; apply() (under the lock) swaps them in.
    struct Pending { int slot; FxType type; std::string key; std::unique_ptr<Effect> fx; };
    std::vector<Pending> plan (const MixerSettings&, bool reloadAll = false) const;
    void apply (std::vector<Pending>&, std::vector<std::unique_ptr<Effect>>& old);
    void invalidate (int slot) { if (slot >= 0 && slot < kTotalSlots) fxKey[slot] = "\x01"; }   // force a reload

    // makes the effect for a plug-in slot (null = not available: the slot passes audio through)
    std::function<std::unique_ptr<Effect> (int slot, const FxSlot&, double sampleRate)> pluginFactory;
    // automation (audio thread): a slot's parameter to value 0..1. Built-in effects take it
    // in place of the slot's own value until the slot changes; plug-ins get it directly.
    void setAutomation (int slot, int param, float value01);
    void clearAutomation();
    void clearOnOff (int slot = -1);   // on/off automation back to the slot's BYPASS button (-1 = every slot)
    int  onOffOverride (int slot) const { return slot >= 0 && slot < kTotalSlots ? autoOn[slot] : -1; }
    Effect* effectAt (int slot) const { return slot >= 0 && slot < kTotalSlots ? fx[slot].get() : nullptr; }
    double sampleRate() const { return sr; }
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
    std::string fxKey[kTotalSlots];   // the plug-in in a plug-in slot
    float autoParam[kTotalSlots][kFxParams];   // automated built-in values (< 0 = not automated)
    int   autoOn[kTotalSlots];                 // automated on/off: -1 = follow BYPASS, 0 = off, 1 = on
    std::vector<float> sendL[kReturns], sendR[kReturns];
    void runInserts (const FxSlot* slots, int firstSlot, float* L, float* R, int n, double bpm);
};
} // namespace prv
