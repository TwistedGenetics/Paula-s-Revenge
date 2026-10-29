// Paula's Revenge - the mixer: per-track EQ, inserts, sends, returns, master (plain C++)
#pragma once

#include "MixerTypes.h"
#include "Effects.h"
#include "DelayLine.h"
#include "Utility.h"
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
    // an instrument's own inserts, in place (before its audio goes to a track's strip)
    void processInstrument (const MixerSettings& m, int ins, float* L, float* R, int n, double bpm)
    {
        if (ins >= 0 && ins < kMixInstruments) runInserts (m.inst[ins], instrumentSlot (ins, 0), L, R, n, bpm);
    }

    // the EQ's spectrum display: the channel it shows (source = track), after its EQ;
    // the Player sets tapsLive while the song (or a note) plays, so the display freezes when it stops
    AudioTap chTap;
    bool tapsLive = false;

    // meters (peaks since the UI last read them)
    std::atomic<float> trackPeak[kMixTracks] {}, returnPeak[kReturns] {}, masterPeakL { 0 }, masterPeakR { 0 };

    bool hasEffect (int slot) const { return slot >= 0 && slot < kTotalSlots && fx[slot] != nullptr; }

    // ---- plug-in delay compensation. Each track is delayed so all of them line up with the
    // slowest one (its inserts' latencies plus what comes in late, e.g. a plug-in instrument);
    // `extra` delays every track (MIDI-out instruments that play ahead of the audio).
    static constexpr int kMaxTrackDelay = 65535, kMaxSlotDelay = 16383;
    void setInputLatency (int track, int samples) { if (track >= 0 && track < kMixTracks) inputLat[track] = std::max (0, samples); }
    void setExtraDelay (int samples) { extraDelay = std::clamp (samples, 0, kMaxTrackDelay / 2); }
    int  slotLatency (int slot) const;                          // the effect's latency (0 = none / empty)
    int  trackLatency (int t) const { return t >= 0 && t < kMixTracks ? uiTrackLat[t].load (std::memory_order_relaxed) : 0; }
    int  totalLatency() const { return uiTotalLat.load (std::memory_order_relaxed); }   // everything, return and master included
    // the returns' output (after their faders) from the last process(), for stems
    const float* returnLeft (int r) const { return sendL[r].data(); }
    const float* returnRight (int r) const { return sendR[r].data(); }

private:
    double sr = 48000.0;
    ChannelEqProcessor eq[kMixTracks];
    std::unique_ptr<Effect> fx[kTotalSlots];
    FxType fxType[kTotalSlots] {};
    std::string fxKey[kTotalSlots];   // the plug-in in a plug-in slot
    float autoParam[kTotalSlots][kFxParams];   // automated built-in values (< 0 = not automated)
    int   autoOn[kTotalSlots];                 // automated on/off: -1 = follow BYPASS, 0 = off, 1 = on
    std::vector<float> sendL[kReturns], sendR[kReturns];
    DelayLine slotDelay[kTotalSlots];      // a bypassed plug-in keeps its delay
    DelayLine trackAlign[kMixTracks];
    int inputLat[kMixTracks] {};
    int extraDelay = 0;
    std::atomic<int> uiTrackLat[kMixTracks] {}, uiTotalLat { 0 };
    bool slotReady (const FxSlot&, int slot) const;
    void runInserts (const FxSlot* slots, int firstSlot, float* L, float* R, int n, double bpm);
};
} // namespace prv
