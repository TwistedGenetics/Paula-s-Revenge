// Paula's Revenge - RAVE: the built-in jungle / hardcore keys. Nine sounds from the early 90s:
//   STAB  - detuned saws over a square an octave down, the filter snapping shut: the rave chord stab
//   PAD   - five wide saws drifting against each other, slow in and out: the rolling-jungle atmosphere
//   ORGAN - drawbar sines with a click at the front: the house / hardcore organ (bass or stabs)
//   BLEEP - a sine with a touch of its third: bleep-and-bass tones and sub bleeps
//   BELL  - two-operator FM, bright then mellow: the electric-piano / bell of the soulful side
//   HOOVER  - (v0.74) chorused pulse-saws swooping up into each note, a square under them: the hoover stab
//   MENTASM - pulse-saws an octave apart, falling into the note with the pulse width racing: the scream
//   PIANO   - a hammered piano (partials dying away faster the higher they are, two strings a note): the rave /
//             house piano stab
//   BELGIAN - a saw and a square an octave up, driven hard, CHORD on 5TH: the Belgian techno stab
// CHORD plays a whole chord from one key (the rave chord-memory trick: MINOR, MIN 7...); a 12 dB low-pass with its
// own envelope; CHORUS and a stereo ECHO after it. POLY (16 voices; the oldest fades out in 4 ms when it's full).
// (v0.83) LAYER: a second of the nine sounds under each note (its level and octave) - a stab with an organ, a piano
// with a pad; the CHORD's INVERSION, SPREAD across the stereo and STRUM; the filter's TYPE and KEY TRACK; a PITCH
// envelope, GLIDE and delayed VIBRATO; DRIVE, CRUSH (an old sampler's bits and rate), the echo's FEEDBACK, WIDTH.
// Three tabs: SOUND, SHAPE, FX.
// Plays through the synth rack like SUB (pluginKey "SYN:RAVE"). Plain C++, unit tested.
#pragma once

#include "PluginSynth.h"
#include <array>
#include <string>
#include <vector>

namespace prv
{
struct SynthParam;
constexpr const char* kRaveKey = "SYN:RAVE";

namespace rave
{
    enum P { Type, Chord, Detune, Octave, Cutoff, Resonance, EnvAmount, FilterDecay,
             Attack, Decay, Sustain, Release, Chorus, Echo, EchoTime, VelSens, Level,
             // (v0.83)
             Layer, LayerLevel, LayerOctave, Inversion, ChordSpread, Strum,
             FilterType, KeyTrack, PitchAmount, PitchTime, Glide, VibRate, VibDepth, VibDelay,
             Drive, Crush, EchoFeedback, Width,
             Count };
    enum TypeKind { Stab = 0, Pad, Organ, Bleep, Bell, Hoover, Mentasm, Piano, Belgian, Types };
}
const std::vector<SynthParam>& raveParams();
const char* raveTypeName (int type);                 // "STAB"
std::vector<float> ravePreset (int type);            // every control set for that sound (the menu's starting points)
constexpr int kRaveLayered = 5;                      // (v0.83: PRESETS' layered sounds, after the nine)
const char* raveLayeredName (int i);                 // "STAB + ORGAN"
std::vector<float> raveLayered (int i);

class RaveSynth : public PluginSynth
{
public:
    RaveSynth();
    void prepare (double sampleRate, int maxBlock) override;
    void noteOn (int midiNote, int velocity, int offset) override;
    void noteOff (int midiNote, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int, int, int) override {}
    void setParameter (int index, float value01) override;
    void render (float* left, float* right, int numSamples) override;
    void update (const Instrument&) override;
    void setValues (const std::vector<float>& v);   // (tests)
    int  voicesPlaying() const;                     // (tests)

private:
    struct Event { int offset; int type; int note; int vel; };
    std::vector<Event> events;
    std::array<float, rave::Count> p {};
    std::vector<uint8_t> lastState;
    double sr = 48000.0;
    static constexpr int kOsc = 5;
    // one of the nine sounds' oscillators (a note has two: its TYPE, and the LAYER under it)
    struct Osc
    {
        double ph[kOsc] {};
        double phLow = 0, phMod = 0;
        float click = 0, modEnv = 0;
        float drift[kOsc] {};      // (PAD: each saw's slow wander)
        float pitchEnv = 0;        // (HOOVER / MENTASM: the swoop into the note, 1 -> 0)
        double pwmPh = 0;          // (their pulse width's LFO)
        float dc = 0;              // (BELGIAN: the drive's offset taken out)
    };
    struct Voice
    {
        int   key = -1;            // the key that plays it (a CHORD's notes share one)
        float pitch = 60;          // semitones (MIDI)
        bool  gate = false;
        Osc   osc[2];
        float env = 0; int stage = 0; float vel = 1;
        float fenv = 0;
        float s1[2][2] {};         // the filter, left / right
        float fade = 1; bool fading = false;
        int   order = 0, upd = 0;
        float a1 = 0, a2 = 0, a3 = 0, k = 2;
        float pan = 0;             // (the CHORD's SPREAD)
        int   wait = 0;            // (STRUM: samples before it starts)
        float penv = 0, glideOff = 0;   // (the PITCH envelope; GLIDE: semitones still to go)
        int   age = 0;             // (samples since it started: VIB DELAY)
    };
    static constexpr int kVoices = 16;
    Voice voices[kVoices];
    float bend = 0; int bendRange = 2;
    int orderCount = 0;
    uint32_t seed = 777;
    double driftPh = 0;
    double vibPh = 0;
    float lastNote = -1;
    float crushHold[2] {}; int crushCount = 0;
    std::vector<float> chBuf[2]; int chPos = 0; double chPh = 0;    // CHORUS
    std::vector<float> echoBuf[2]; int echoPos = 0; float echoLp[2] {};   // ECHO
    void handle (const Event&);
    void startVoice (int key, float pitch, int vel);
    float renderVoice (Voice&, float& l, float& r);
    void oscillate (int type, Osc&, double semis, float det, float& l, float& r);
    void startOsc (int type, Osc&);
};
} // namespace prv
