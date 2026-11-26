// The effects library (0.3.5): four Massive-style slots, Insert 1 / Insert 2 (before HELIX's FX rack) and FX 1 / FX 2
// (after it). Each slot picks one of the library's effects and has Mix plus six knobs (A-F) whose meaning follows the
// effect; the knob names and value texts come from here, so every front end shows the same thing.
//
// The library merges Jamie's effects from his other projects, duplicates once:
//   DREADNOUGHT (FM-1 firmware): ping-pong, tape and BBD echoes, spring, Tomb, Dattorro halls (Mod, Celestial, Dolmen,
//                                Tumulus), Quadraverb, Super-Massive (Horizon), filter, ring mod, drive
//   Mega Synth:                  dub delay, plate, gated reverb, Vastness, ensemble / BBD / multi / dimension choruses,
//                                widener, flanger, Script / Colour / Dual / Barber-pole phasers, Vibe, beat repeat,
//                                volume shaper, stereo tools
//   EraCrush (microKORG 2):      SP-1200, MPC60, S950, S1000, Amiga
// Their DSP is compiled in unchanged (helix-core/third_party); this file and FxLibrary.cpp only connect it.
#pragma once
#include <memory>
#include <string>

namespace helix {
namespace fxlib {

constexpr int kNumSlots = 4;          // Insert 1, Insert 2, FX 1, FX 2
constexpr int kNumKnobs = 6;          // A..F
int numTypes();                       // including Off (type 0)
const char* typeName(int type);
const char* typeGroup(int type);      // "Delay", "Reverb", "Chorus", "Phaser", "Character", "Rhythm", ...
// Knob k of an effect: its name ("" = not used). knobs: the slot's current knob values (0..1), so names that depend
// on another knob (Quadraverb's configuration) are right; may be null.
std::string knobName(int type, int k, const float* knobs = nullptr);
// The value text for knob k at v (0..1), e.g. "2.40 s", "1/8 Dotted", "On".
std::string knobText(int type, int k, float v, const float* knobs = nullptr, double bpm = 120.0);
float knobDefault(int type, int k);   // 0..1
float mixDefault(int type);           // 0..1

struct Context
{
    double bpm = 120.0, beat = 0.0;
    const float* params = nullptr;   // the engine's (modulated) parameters: the HELIX effects read their other settings
    bool oversample = false;         // HELIX Dist's oversampling (engine quality)
};

// 0.3.6: the old FX rack folds into the slots. Each switched-on rack effect (in rack order) moves into the next free
// slot (Insert 1, Insert 2, FX 1, FX 2) as its HELIX type, with its settings and Mix, and its switch goes off; matrix
// routings aimed at its settings follow it. Returns how many switched-on effects did not fit (they stay in the rack,
// which still runs). Safe to call again (nothing left to move).
int foldRack(float* params);
bool isHeavy(int type);   // OTT (the CPU meter marks it)

// One slot. prepare() allocates everything (every effect's memory) so changing type never allocates.
class Slot
{
public:
    Slot();
    ~Slot();
    void prepare(double sampleRate, int maxBlock);
    void reset();
    // In place. type, mix (0..1), knobs[6] (0..1). n <= maxBlock.
    void process(float* l, float* r, int n, int type, float mix, const float* knobs, const Context& ctx);
    int activeType() const { return cur_; }
    bool heavy() const { return isHeavy(cur_); }
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    int cur_ = 0;
};

} // namespace fxlib
} // namespace helix
