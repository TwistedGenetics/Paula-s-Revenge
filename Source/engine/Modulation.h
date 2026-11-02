// Paula's Revenge - MODULATION: the sources' descriptions and the engine that runs them (plain C++).
// The engine works a block at a time inside the mixer: it hears every track (for the followers),
// gets the notes from the player (retrigger, KEY, VELOCITY) and the song position (synced rates),
// and says what each route adds to its target.
#pragma once

#include "MixerTypes.h"
#include <atomic>
#include <vector>

namespace prv
{
std::string modSourceName (ModSource);                    // "LFO", "FOLLOWER" ...
const std::vector<FxParamInfo>& modParams (ModSource);
std::string modParamText (ModSource, int i, const float* p);
void initModulator (Modulator&, ModSource);               // the source with its defaults (routes kept)
bool modBipolar (ModSource);                              // swings both ways (LFO, RANDOM)
int  modNotesFrom (const Modulator&);                     // the track whose notes it follows (-1 = any)
std::string modSummary (const Modulator&);               // "LFO SINE 1/4", "FOLLOWER TRACK 1" ...
double modBeats (float v);                                // a synced rate's length in beats (1/32 .. 4 BARS)
std::string modDivisionName (float v);

class ModEngine
{
public:
    void prepare (double sampleRate);
    void reset();

    // ---- from the player (audio thread)
    void noteOn (int track, int note, float velocity01);
    void setTransport (double ppq, bool playing) { songPpq = ppq; isPlaying = playing; }
    void setMacro (int mod, float v01);                    // V / J on a MACRO (-1 = back to its knob)
    // ---- from the mixer: each track's sound this block (before its EQ), then run
    void listen (int track, const float* L, const float* R, int n);
    void run (const ModSettings&, int n, double bpm);

    // what the routes add, after run()
    bool  slotModded[kTotalSlots] {};
    float slotOffset[kTotalSlots][kFxParams] {};
    float stripMul[kMixTracks][kModStripParams] {};        // VOLUME, SEND A, SEND B multipliers
    struct PluginSet { int slot, param; float value; };
    std::vector<PluginSet> pluginSets;                     // plug-in parameters to set this block

    // for the page (any thread): each modulator's output now
    std::atomic<float> uiOut[kMods] {};
    std::atomic<int>   uiStep[kMods] {};                  // a STEPPER's step now
    float output (int m) const { return out[m]; }

private:
    double sr = 48000, songPpq = 0;
    bool isPlaying = false, wasPlaying = false;
    float out[kMods] {};
    double phase[kMods] {}, holdPpq[kMods] {};
    float sh[kMods] {}, target[kMods] {}, env[kMods] {}, glideOut[kMods] {};
    int   lastStep[kMods] {};
    bool  retrigged[kMods] {};
    // notes heard: per track, and the latest from any
    int   noteOfTrack[kMixTracks + 1] {};
    float velOfTrack[kMixTracks + 1] {};
    int   noteSerial[kMixTracks + 1] {}, seenSerial[kMods] {};
    float level[kMixTracks] {};                            // each track's peak this block
    float macroAuto[kMods];
    uint32_t rng = 0x2545F491u;
    float random01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng >> 8) * (1.0f / 16777216.0f); }
};
} // namespace prv
