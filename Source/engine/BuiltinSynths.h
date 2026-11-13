// Paula's Revenge - the built-in synths: SUB (a click-free mono / poly sub bass) and GRANULAR. They play through
// the synth rack as plug-in instruments do (pluginKey "SYN:SUB" / "SYN:GRAIN"), so chords, the filter and
// envelopes, EQ / FX, OUTPUT, learnable knobs and renders all work; their settings are the instrument's
// pluginState (saved with the song as a plug-in's are). Plain C++, unit tested.
#pragma once

#include "PluginSynth.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace prv
{
constexpr const char* kSubKey = "SYN:SUB";
constexpr const char* kGrainKey = "SYN:GRAIN";   // (and kReeseKey "SYN:REESE", kRaveKey "SYN:RAVE": their own headers)
inline bool isBuiltinKey (const std::string& k) { return k.rfind ("SYN:", 0) == 0; }

// one control of a built-in synth: shown on its page, saved in its state, learnable as a knob (0..1 = min..max)
struct SynthParam
{
    const char* name;      // "GLIDE"
    const char* section;   // "OSCILLATOR"
    float min, max, def;
    int   kind;            // 0 linear, 1 exponential (times, frequencies), 2 whole numbers, 3 a choice (choices)
    const char* unit;      // "MS", "HZ", "%", "ST", ""
    const char* choices;   // kind 3: "LEGATO|ALWAYS|OFF|POLY"
    float toValue (float v01) const;     // 0..1 -> the value
    float to01 (float value) const;
    std::string text (float value) const;   // "120MS", "LEGATO"
};

const std::vector<SynthParam>& builtinParams (const std::string& key);   // (empty: not a built-in)
std::vector<float> builtinDefaults (const std::string& key);
// the values in an instrument's state (its defaults where missing) / written back into it
std::vector<float> readBuiltinState (const Instrument&);
void writeBuiltinState (Instrument&, const std::vector<float>& values);
// make an instrument a built-in synth (its sample, plug-in and MIDI / CV settings go; its name becomes the synth's)
void makeBuiltin (Instrument&, const std::string& key);
// RAVE (v0.73): the jungle keys - STAB, PAD, ORGAN, BLEEP or BELL, every control set for that sound
void makeRave (Instrument&, int type);
std::unique_ptr<PluginSynth> createBuiltinSynth (const Instrument&, double sampleRate = 48000.0);   // (prepared at that rate)

// ---- SUB: one oscillator that never restarts (a new note moves its pitch - instantly or gliding - so the wave
// never jumps), the envelope restarting from where it is (never from 0), overlapping notes in LEGATO sliding without
// a new attack; anything that has to start afresh (POLY's voices, the 808 drop) starts at a zero crossing or fades.
namespace sub
{
    enum P { Mode, Glide, Shape, Octave, Fine, Osc2Level, Osc2Interval, Noise, NoiseDecay, DropAmount, DropTime,
             Drive, DriveType, Tone, Attack, Decay, Sustain, Release, VelSens, Level,
             BeatOn, BeatRate, BeatDepth,   // (v0.60: added at the end, so older songs read as before - BEAT off; BeatOn 1 = BEAT HZ, 2 = BEAT ST)
             BeatSemis,                     // (v0.72: BEAT ST - BEAT's 2 - a number of semitones up, as a second key held)
             BeatDelay,                     // (v0.73: how long a note plays plain before BEAT comes in, ms)
             // (v0.83) HARMONICS for small speakers, an attack CLICK, how the DROP plays, a resonant FILTER with its own
             // envelope and key tracking, an LFO (to pitch, level or the filter; tempo sync), more DRIVE types, a HIGH-PASS
             Harm2, Harm3, Click, ClickTone, DropMode,
             Resonance, ToneEnv, ToneDecay, KeyTrack,
             LfoRate, LfoDepth, LfoTarget, LfoSync, LfoShape,
             HighPass,
             Count };
    enum ModeKind { Legato = 0, Always, Retrigger, Poly };
    enum DriveKind { Tape = 0, Tube, Fold, Clip, Bits };
    enum LfoTargetKind { LPitch = 0, LLevel, LTone };
}

constexpr int kSubPresets = 10;
const char* subPresetName (int i);              // "CLEAN SUB"
std::vector<float> subPreset (int i);

class SubSynth : public PluginSynth
{
public:
    SubSynth();
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
    float pitchNow() const { return voices[0].pitch; }   // (tests) the first voice's pitch, in semitones (MIDI)
    float levelNow() const { return voices[0].env; }
    float toneNow() const { return toneHz; }            // (tests: the filter's cutoff now)
    void updateSong (const Song& s) override { songBpm = (double) s.tempo * 6.0 / std::max (1, s.speed); }

private:
    struct Event { int offset; int type; int note; int vel; };   // type 0 on, 1 off, 2 all off, 3 bend
    std::vector<Event> events;
    std::array<float, sub::Count> p {};
    std::array<float, sub::Count> base {};   // (v0.86) the settings as loaded - restoreParameter puts one back
  public:
    void restoreParameter (int i) override { if (i >= 0 && i < (int) p.size()) p[(size_t) i] = base[(size_t) i]; }
  private:
    std::vector<uint8_t> lastState;
    double sr = 48000.0;
    struct Voice
    {
        int   note = -1;           // (-1: free)
        bool  gate = false;
        double phase = 0, phase2 = 0;
        double phaseB = 0, phase2B = 0;   // BEAT: a copy of the oscillator (and its OSC 2) above it (it never restarts either)
        float beatMix = 0;         // (BEAT on / off and DEPTH, eased so switching it never clicks)
        int   beatAge = 0;         // (samples since the note's attack: BEAT DELAY waits this long)
        float pitch = 48, target = 48, glideStep = 0;   // semitones; glide: semitones a sample
        float env = 0;             // the amp envelope, 0..1
        int   stage = 0;           // 0 off, 1 attack, 2 decay / sustain, 3 release
        float vel = 1;
        float drop = 0;            // the 808 drop: semitones still to fall
        float noiseEnv = 0;
        float fade = 1;            // (POLY: a stolen voice fading out fast)
        bool  fading = false;
        int   order = 0;
        float clickEnv = 0; double clickPh = 0;   // (v0.83: the attack CLICK)
    };
    static constexpr int kVoices = 8;
    Voice voices[kVoices];
    std::vector<int> held;         // (mono: the notes held, newest last)
    int pendingRelease = -1;       // (mono: the last note let go, a moment before it releases - a note at once is legato)
    float bend = 0;                // semitones
    int bendRange = 2;
    int orderCount = 0;
    uint32_t noiseSeed = 12345;
    // the sound after the voices: drive, DC blocker, TONE (12 dB low-pass)
    float dcX = 0, dcY = 0, lp1 = 0, lp2 = 0;
    // (v0.83) the resonant filter, its envelope, the LFO, the high-pass, the BITS drive's held sample
    float svf1 = 0, svf2 = 0, toneEnv = 0, toneHz = 12000, lastPitch = 48;
    double lfoPh = 0; float lfoHold = 0, lfoNow = 0;
    float hp1 = 0, hp2 = 0;
    float bitsHold = 0; int bitsCount = 0;
    double songBpm = 125.0;
    void handle (const Event&);
    void startVoice (Voice&, int note, int vel, bool retrigger, bool glide);
    void releaseVoice (Voice&);
    float renderVoice (Voice&);
    float shapeWave (double phase) const;
};

// ---- GRANULAR: grains from a built-in wavetable, any instrument's sample or a recording, scattered round a
// position that can scan; FREEZE holds it; two LFOs and an envelope can move any of its controls.
namespace grain
{
    enum P { Source, Table, SampleSlot, Position, Scan, Size, Density, Spray, Pitch, PitchSpread, Reverse, Stereo,
             Freeze, Attack, Decay, Sustain, Release, Lfo1Rate, Lfo1Depth, Lfo1Target, Lfo2Rate, Lfo2Depth, Lfo2Target,
             EnvDepth, EnvTarget, Cutoff, Resonance, Diffuse, Level,
             // (v0.84) the grains' WINDOW, a SCALE for the pitch spread, DETUNE, KEY > POSITION, SHIMMER (octave-up
             // grains), the LFOs' SHAPE and SYNC, a ping-pong ECHO, WIDTH
             Window, Scale, Detune, KeyPos, Shimmer, Lfo1Shape, Lfo1Sync, Lfo2Shape, Lfo2Sync, Echo, EchoTime, EchoFeedback, Width,
             Count };
}
constexpr int kGrainPresets = 10;
const char* grainPresetName (int i);
std::vector<float> grainPreset (int i);

class GrainSynth : public PluginSynth
{
public:
    GrainSynth();
    void prepare (double sampleRate, int maxBlock) override;
    void noteOn (int midiNote, int velocity, int offset) override;
    void noteOff (int midiNote, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int, int, int) override {}
    void setParameter (int index, float value01) override;
    void render (float* left, float* right, int numSamples) override;
    void update (const Instrument&) override;
    void updateSong (const Song& s) override { song = &s; }
    void liveInput (const float* left, const float* right, int numSamples) override;
    void setValues (const std::vector<float>& v);                 // (tests)
    void setTestSource (std::vector<float> mono, double rate);    // (tests: a sample without a song)
    int  activeGrains() const;                                    // (tests, the page)
    float positionNow() const { return posNow; }                  // 0..1: where it's reading (the newest note's)
    float valueNow (int param) const { return p[(size_t) param]; }
    // the sound it takes its grains from, for its page: `count` peaks (0..1) across it (the message thread, under
    // the app's lock); false when there's nothing to take grains from (no sample in that slot, say)
    bool sourceOverview (std::vector<float>& peaks, int count) const;
    static const std::vector<float>& table (int which);           // the built-in wavetables (C-3, 48 kHz)
    static constexpr double kTableRate = 48000.0;

private:
    std::array<float, grain::Count> p {};
    std::array<float, grain::Count> base {};   // (v0.86) the settings as loaded - restoreParameter puts one back
  public:
    void restoreParameter (int i) override { if (i >= 0 && i < (int) p.size()) p[(size_t) i] = base[(size_t) i]; }
  private:
    std::vector<uint8_t> lastState;
    double sr = 48000.0;
    const Song* song = nullptr;
    // where the grains read: a table, a song's sample (16 bit) or the live recording (a ring, oldest first)
    struct Src { const float* f = nullptr; const int16_t* s = nullptr; size_t len = 0, ringStart = 0; double rate = 48000; };
    Src source() const;
    std::vector<float> testSrc; double testRate = 48000;
    std::vector<float> ring;      // the live recording: the last few seconds of the input
    size_t ringPos = 0, ringFill = 0;
    struct Note { int note = -1; bool gate = false; float env = 0; int stage = 0; float vel = 1; double spawn = 0; double scan = 0, age = 0; int order = 0; };
    static constexpr int kNotes = 6;
    Note notes[kNotes];
    int orderCount = 0;
    struct Grain { bool on = false; int note = 0; double readPos = 0, step = 1; int age = 0, len = 1; float amp = 0, panL = 1, panR = 1; int win = 0; };
    static constexpr int kGrains = 128;
    Grain grains[kGrains];
    struct Event { int offset; int type; int note; int vel; };
    std::vector<Event> events;
    float bend = 0;
    int bendRange = 12;
    double lfo1 = 0, lfo2 = 0;
    float lfoHold[2] {}, lfoSmooth[2] {};
    std::vector<float> echoBuf[2]; int echoPos = 0; float echoLp[2] {};   // (v0.84: ECHO)
    float posNow = 0;
    uint32_t seed = 22222;
    // after the grains: the filter (a resonant 12 dB low-pass) and the diffusion (four all-passes a side)
    float ic1L = 0, ic2L = 0, ic1R = 0, ic2R = 0, cutNow = 18000;
    std::vector<float> ap[8];
    int apPos[8] {};
    float rnd();                  // -1..1
    void handle (const Event&);
    void spawn (Note&, const Src&, const float mod[8]);
    float envNow() const;         // the newest sounding note's envelope (for ENV -> CUTOFF / LEVEL)
};
} // namespace prv
