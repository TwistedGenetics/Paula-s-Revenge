// Paula's Revenge - MORPH: a synth built round the MORPH FILTER, in the spirit of E-mu's Morpheus. Two oscillators
// (SAW, PULSE, SQUARE, TRI, SINE, NOISE, SUPERSAW, ORGAN) into a MORPH FILTER of its own for every note, whose
// MORPH is moved by its own envelope, an LFO and how hard the note is played, and whose FREQ can follow the keys -
// so a note can open from "OO" to "AH", or a phaser's notches slide as it plays. Then the amp envelope, CHORD,
// CHORUS and ECHO. POLY: 16 voices. Plays through the synth rack like SUB (pluginKey "SYN:MORPH").
#pragma once

#include "PluginSynth.h"
#include "MorphFilter.h"
#include <algorithm>
#include <array>
#include <vector>

namespace prv
{
struct SynthParam;
constexpr const char* kMorphKey = "SYN:MORPH";

namespace msyn
{
    enum P { Wave1, Wave2, Mix, Detune, Osc2Tune, PulseWidth,
             Model, Morph, Freq, Emphasis, KeyTrack, VelMorph,
             EnvAmount, EnvAttack, EnvDecay, EnvSustain, EnvRelease,
             LfoRate, LfoDepth,
             Attack, Decay, Sustain, Release,
             Chord, Octave, Chorus, Echo, EchoTime, VelSens, Level,
             // (v0.84) a second MORPH FILTER (in SERIES or PARALLEL), the LFO's SHAPE and SYNC, a RANDOM morph each note,
             // SUB and NOISE, DRIVE into the filters, GLIDE, a PITCH envelope, echo FEEDBACK, WIDTH
             Route2, Model2, Morph2, Freq2, LfoShape, LfoSync, MorphRandom,
             SubLevel, NoiseLevel, Drive, Glide, PitchEnv, PitchTime, EchoFeedback, Width,
             Count };
    enum Wave { Saw = 0, Pulse, Square, Tri, Sine, Noise, SuperSaw, Organ, Waves };
}
const std::vector<SynthParam>& morphSynthParams();
constexpr int kMorphPresets = 10;
const char* morphPresetName (int i);              // "VOWEL PAD"
std::vector<float> morphPreset (int i);           // its settings (PRESETS lists them)

class MorphSynth : public PluginSynth
{
public:
    MorphSynth();
    void prepare (double sampleRate, int maxBlock) override;
    void noteOn (int midiNote, int velocity, int offset) override;
    void noteOff (int midiNote, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int, int, int) override {}
    void setParameter (int index, float value01) override;
    void render (float* left, float* right, int numSamples) override;
    void update (const Instrument&) override;
    void updateSong (const Song& s) override { songBpm = (double) s.tempo * 6.0 / std::max (1, s.speed); }
    void setValues (const std::vector<float>& v);   // (tests)
    int  voicesPlaying() const;                     // (tests)
    float morphOf (int voice) const;                // (tests: where a voice's MORPH is now)

private:
    struct Event { int offset; int type; int note; int vel; };
    std::vector<Event> events;
    std::array<float, msyn::Count> p {};
    std::array<float, msyn::Count> base {};   // (v0.86) the settings as loaded - restoreParameter puts one back
  public:
    void restoreParameter (int i) override { if (i >= 0 && i < (int) p.size()) p[(size_t) i] = base[(size_t) i]; }
  private:
    std::vector<uint8_t> lastState;
    double sr = 48000.0;
    struct Voice
    {
        int   key = -1;
        float pitch = 60;
        bool  gate = false;
        int   stage = 0, mstage = 0;
        float env = 0, menv = 0, vel = 1;
        std::array<double, 5> ph1 {}, ph2 {};
        MorphFilter filter;
        float morphNow = 0;
        int   upd = 0, order = 0;
        float fade = 1; bool fading = false;
        float pan = 0;
        MorphFilter filter2;           // (v0.84: FILTER 2)
        float rnd = 0, penv = 0, glideOff = 0;
        double phSub = 0;
    };
    static constexpr int kVoices = 16;
    Voice voices[kVoices];
    float bend = 0; int bendRange = 2;
    int orderCount = 0;
    uint32_t seed = 4242;
    double lfoPh = 0;
    float lfoHold = 0, lfoSmooth = 0, lastNote = -1;
    double songBpm = 125.0;
    std::vector<float> chBuf[2]; int chPos = 0; double chPh = 0;
    std::vector<float> echoBuf[2]; int echoPos = 0; float echoLp[2] {};
    void handle (const Event&);
    void startVoice (int key, float pitch, int vel);
    float osc (int wave, std::array<double, 5>& ph, double hz, float pw);
    void renderVoice (Voice&, float lfo, float& l, float& r);
};
} // namespace prv
