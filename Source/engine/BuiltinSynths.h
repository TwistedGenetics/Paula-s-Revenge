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
constexpr const char* kGrainKey = "SYN:GRAIN";
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
std::unique_ptr<PluginSynth> createBuiltinSynth (const Instrument&);

// ---- SUB: one oscillator that never restarts (a new note moves its pitch - instantly or gliding - so the wave
// never jumps), the envelope restarting from where it is (never from 0), overlapping notes in LEGATO sliding without
// a new attack; anything that has to start afresh (POLY's voices, the 808 drop) starts at a zero crossing or fades.
namespace sub
{
    enum P { Mode, Glide, Shape, Octave, Fine, Osc2Level, Osc2Interval, Noise, NoiseDecay, DropAmount, DropTime,
             Drive, DriveType, Tone, Attack, Decay, Sustain, Release, VelSens, Level, Count };
    enum ModeKind { Legato = 0, Always, Retrigger, Poly };
}

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

private:
    struct Event { int offset; int type; int note; int vel; };   // type 0 on, 1 off, 2 all off, 3 bend
    std::vector<Event> events;
    std::array<float, sub::Count> p {};
    std::vector<uint8_t> lastState;
    double sr = 48000.0;
    struct Voice
    {
        int   note = -1;           // (-1: free)
        bool  gate = false;
        double phase = 0, phase2 = 0;
        float pitch = 48, target = 48, glideStep = 0;   // semitones; glide: semitones a sample
        float env = 0;             // the amp envelope, 0..1
        int   stage = 0;           // 0 off, 1 attack, 2 decay / sustain, 3 release
        float vel = 1;
        float drop = 0;            // the 808 drop: semitones still to fall
        float noiseEnv = 0;
        float fade = 1;            // (POLY: a stolen voice fading out fast)
        bool  fading = false;
        int   order = 0;
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
             EnvDepth, EnvTarget, Cutoff, Resonance, Diffuse, Level, Count };
}

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
    void setValues (const std::vector<float>& v);                    // (tests)
    void setSource (std::shared_ptr<const std::vector<float>> mono, double rate);   // (tests; the song's sample / the recording)
    int  activeGrains() const;                                       // (tests)
    // the sound it takes its grains from, for its page: the samples it reads, and where it's reading
    std::shared_ptr<const std::vector<float>> sourceNow() const { return src; }
    float positionNow() const { return posNow; }

    // the song's samples and the recording: set by whoever owns them (the processor), read when the source changes
    std::function<std::shared_ptr<const std::vector<float>> (int instrument, double& rate)> sampleOf;
    std::function<std::shared_ptr<const std::vector<float>> (double& rate)> recording;

private:
    std::array<float, grain::Count> p {};
    std::vector<uint8_t> lastState;
    double sr = 48000.0;
    std::shared_ptr<const std::vector<float>> src;   // mono, at srcRate
    double srcRate = 48000.0;
    int srcKind = -1, srcTable = -1, srcSlot = -1;
    struct Note { int note = -1; bool gate = false; float env = 0; int stage = 0; float vel = 1; double spawn = 0; float envMod = 0; float pos = 0; };
    static constexpr int kNotes = 6;
    Note notes[kNotes];
    struct Grain { bool on = false; double readPos = 0, step = 1; int age = 0, len = 1; float amp = 0, panL = 1, panR = 1; };
    static constexpr int kGrains = 96;
    Grain grains[kGrains];
    float bend = 0;
    double lfo1 = 0, lfo2 = 0;
    float posNow = 0;
    uint32_t seed = 22222;
    // after the grains: the filter (12 dB low-pass) and the diffusion (four all-passes)
    float s1L = 0, s2L = 0, s1R = 0, s2R = 0;
    std::vector<float> apBuf[4];
    int apPos[4] {};
    float rnd();
    void chooseSource();
    void spawn (Note&, float mod[grain::Count]);
};
} // namespace prv
