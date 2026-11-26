// The character oscillator: one per voice, a third sound source that plays one of Twisted Genetics' bass / rave
// engines (ported from the logue SDK units Hoover, Dread Bass and Rollers). Six generic knobs; what each one does
// depends on the engine (charKnobName).
#pragma once
#include <memory>
#include <string>

namespace helix {

enum CharType : int
{
    kCharOff = 0,
    kCharHoover, kCharMentasm, kCharHooverReese, kCharRave,          // Hoover unit modes
    kCharDread, kCharDonk, kCharGrowl, kCharDreadHoover,             // Dread Bass modes
    kCharRollSub, kCharRollReese, kCharRollSubReese, kCharRollHoover, // Rollers modes
    kNumCharTypes
};

struct CharKnobs
{
    float timbre = 0.5f, character = 0.5f, drive = 0.2f, sub = 0.3f;   // 0..1
    float env = 0.0f;                                                  // -1..1 pitch envelope / dive
    float time = 0.35f;                                                // 0..1 envelope time
};

// Knob k (0 Timbre, 1 Character, 2 Drive, 3 Sub, 4 Env, 5 Time) for an engine, e.g. "Detune", "Reso".
const char* charKnobName(int type, int k);
// 0 Hoover, 1 Dread Bass, 2 Rollers, -1 off
int charFamily(int type);

class CharOsc
{
public:
    CharOsc();
    ~CharOsc();
    void prepare(float sr) { sr_ = sr; }
    // Note start. fresh: the voice was silent (phases restart, every hit the same).
    void start(bool fresh);
    // Mono output, n samples (overwrites). note: fractional MIDI note including bend and glide.
    void render(int type, float note, const CharKnobs& k, float* out, int n);
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    float sr_ = 48000.0f;
    int lastType_ = -1;
    bool pendingStart_ = false, pendingFresh_ = false;
};

} // namespace helix
