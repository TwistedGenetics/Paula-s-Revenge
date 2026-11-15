// Paula's Revenge - 808 and 909: drum machines built in, synthesised the way the originals make their sounds (no
// samples). One key per drum, the same in every octave: C KICK, C# RIM SHOT, D SNARE, D# CLAP, E LOW TOM, F CLOSED
// HAT, F# MID TOM, G OPEN HAT, G# HI TOM, A CYMBAL (909: CRASH), A# COWBELL (909: RIDE), B CLAVE (909: the kick again).
// So the laptop keys play a kit (Z kick, S rim, X snare, D clap...), and a note's octave doesn't matter.
//   808: the kick a bridged-T "boom" (a sine that rings down, the tiny pitch blip at its start); the snare two tones
//        and SNAPPY noise; the hats, cymbal and cowbell the 808's six square-wave oscillators through band-passes;
//        the toms, clave and rim shot ringing sines; the clap a burst of noise that hits four times, then a tail.
//   909: the kick a sine swept down from high (the punch) with an attack click; the snare tuned tones that drop, and
//        noise; toms swept down; hats, crash and ride metallic (the 909's were 6-bit samples of real ones - here, the
//        oscillators and noise, brighter).
// Every drum: LEVEL, TUNE, DECAY, TONE (the kick's click, the snare's SNAPPY, the hats' brightness...) and PAN. The
// CLOSED HAT chokes the OPEN HAT. OUTPUT: LEVEL, DRIVE, and ACCENT (how much a note's volume - Cxx, or how hard a key
// is hit - changes the hit). Runs at 4x the song's rate inside, so the metal doesn't fold back.
// Plays through the synth rack like the others (pluginKey "SYN:808" / "SYN:909").
#pragma once

#include "PluginSynth.h"
#include "Oversample.h"
#include <array>
#include <vector>
#include <string>

namespace prv
{
struct SynthParam;
constexpr const char* kDrum808Key = "SYN:808";
constexpr const char* kDrum909Key = "SYN:909";

namespace drums
{
    enum Ctl { Level, Tune, Decay, Tone, Pan, PerDrum };   // each drum's controls, in this order
    constexpr int kSlots = 12;                                // the keys C .. B
    enum Out { OutLevel, OutDrive, OutAccent, OutCount };     // after the drums
    enum Kind { Kick, Rim, Snare, Clap, LowTom, ClHat, MidTom, OpHat, HiTom, Cymbal, Cowbell, Clave, Crash, Ride, None };
}
bool isDrumKey (const std::string& key);
int  drumSlots (bool is909);                                 // 12 (808) or 11 (909)
drums::Kind drumKind (bool is909, int slot);                 // what that key plays
const char* drumName (bool is909, int slot);                 // "KICK"
int  drumParam (bool is909, int slot, drums::Ctl c);         // its control's index
int  drumOutParam (bool is909, drums::Out o);
const std::vector<SynthParam>& drumParams (bool is909);
constexpr int kDrumPresets = 4;
const char* drumPresetName (bool is909, int i);
std::vector<float> drumPreset (bool is909, int i);

class DrumSynth : public PluginSynth
{
public:
    explicit DrumSynth (bool is909);
    void prepare (double sampleRate, int maxBlock) override;
    void noteOn (int midiNote, int velocity, int offset) override;
    void noteOff (int, int) override {}
    void allNotesOff (int offset) override;
    void pitchBend (int, int) override {}
    void controller (int, int, int) override {}
    void setParameter (int index, float value01) override;
    void render (float* left, float* right, int numSamples) override;
    void update (const Instrument&) override;
    void setValues (const std::vector<float>& v);   // (tests)
    float valueOf (int i) const { return i >= 0 && i < (int) p.size() ? p[(size_t) i] : 0.0f; }
    void restoreParameter (int i) override { if (i >= 0 && i < (int) p.size()) p[(size_t) i] = base[(size_t) i]; }
    // (tests)
    int  hits() const { return hitCount; }
    bool sounding (int slot) const { return slot >= 0 && slot < drums::kSlots && v[(size_t) slot].active; }

private:
    struct Event { int offset; int note; int vel; };
    struct Svf
    {
        float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
        void set (double hz, double q, double fs);
        // low, band, high
        void run (float x, float& lp, float& bp, float& hp);
        void reset() { ic1 = ic2 = 0; }
    };
    struct Voice
    {
        drums::Kind kind = drums::None;
        bool active = false;
        double t = 0;                 // seconds since the hit
        float gain = 1;               // velocity
        float fade = 1, fadeStep = 0; // a choke / all notes off: down to 0
        double ph[6] {};              // oscillators' phases (0..1)
        float env = 0, env2 = 0, penv = 0, click = 0;
        Svf f1, f2, f3;
        bool coefs = false;           // its filters are set for this chunk
        float panL = 0.707f, panR = 0.707f;
    };
    bool is909;
    std::vector<float> p, base;
    std::vector<uint8_t> lastState;
    std::vector<Event> events;
    std::array<Voice, drums::kSlots> v;
    double fs = 48000.0 * kOs, outSr = 48000.0;
    static constexpr int kChunk = 128;
    std::array<float, kChunk * kOs> hiL {}, hiR {};
    Decimator decL, decR;
    uint32_t rng = 0x9E3779B9u;
    int hitCount = 0;
    float noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) ((int32_t) rng) * (1.0f / 2147483648.0f); }
    float ctl (int slot, drums::Ctl c) const { return p[(size_t) drumParam (is909, slot, c)]; }
    void trigger (int slot, int vel);
    float voiceOut (int slot, Voice& w);
    void renderHi (int n);
};
} // namespace prv
