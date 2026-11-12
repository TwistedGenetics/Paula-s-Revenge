// Paula's Revenge - REESE: the built-in reese / hoover bass. Up to 7 saws detuned against each other (slowly
// drifting, so the phasing never sits still), or HOOVER's pulse-saws with a pitch bend up into each note and a
// chorus; a sub an octave down, a 24 dB low-pass with its own envelope and a wobble LFO, drive. Click-free like SUB:
// the oscillators never restart, the envelope goes on from where it is, overlapping notes in LEGATO slide.
// Plays through the synth rack like SUB (pluginKey "SYN:REESE"). Plain C++, unit tested.
#pragma once

#include "PluginSynth.h"
#include <array>
#include <vector>

namespace prv
{
struct SynthParam;
constexpr const char* kReeseKey = "SYN:REESE";

namespace reese
{
    enum P { Mode, Glide, Type, Voices, Detune, Spread, Octave, MoveRate, MoveDepth, Bend, BendTime, Pwm, PwmRate,
             SubLevel, Cutoff, Resonance, EnvAmount, FilterDecay, WobbleRate, WobbleDepth, Drive, Chorus,
             Attack, Decay, Sustain, Release, VelSens, Level,
             SubOctave, SubShape,           // (v0.76: the SUB 1 or 2 octaves down; AUTO / SINE / TRIANGLE / SQUARE)
             Count };
    enum ModeKind { Legato = 0, Always, Retrigger, Poly };
    enum TypeKind { Reese = 0, Hoover };
}
const std::vector<SynthParam>& reeseParams();

class ReeseSynth : public PluginSynth
{
public:
    ReeseSynth();
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
    float pitchNow() const { return voices[0].pitch + voices[0].bendEnv; }   // (tests) the first voice, semitones
    float levelNow() const { return voices[0].env; }
    float cutoffNow() const { return voices[0].cutNow; }

private:
    struct Event { int offset; int type; int note; int vel; };
    std::vector<Event> events;
    std::array<float, reese::Count> p {};
    std::vector<uint8_t> lastState;
    double sr = 48000.0;
    static constexpr int kOsc = 7;
    struct Voice
    {
        int   note = -1;
        bool  gate = false;
        double ph[kOsc] {};         // the oscillators (never restarted)
        double phSub = 0;
        float pitch = 48, target = 48, glideStep = 0;
        float bendEnv = 0;          // HOOVER's bend: semitones below the note, rising away
        float env = 0; int stage = 0; float vel = 1;
        float fenv = 0;             // the filter's envelope (up fast, down over FILTER DECAY)
        float s1[2][2] {}, s2[2][2] {};   // the 24 dB low-pass: two stages, left / right
        float g = 0.5f, k = 2.0f, a1 = 0, a2 = 0, a3 = 0, cutNow = 1000;
        float fade = 1; bool fading = false;
        int   order = 0, tick = 0, upd = 0;
    };
    static constexpr int kVoices = 6;
    Voice voices[kVoices];
    std::vector<int> held;
    int pendingRelease = -1;
    float bend = 0; int bendRange = 2;
    int orderCount = 0;
    double move = 0, wob = 0, pwmPh = 0;   // the LFOs
    // after the voices: drive and its DC blocker, the chorus
    float dcX[2] {}, dcY[2] {};
    std::vector<float> chBuf[2];
    int chPos = 0;
    double chPh = 0;
    void handle (const Event&);
    void startVoice (Voice&, int note, int vel, bool retrigger, bool glide);
    void releaseVoice (Voice& v) { v.gate = false; if (v.stage != 0) v.stage = 3; }
    void renderVoice (Voice&, float& l, float& r, float moveNow, float wobNow, float pwmNow);
};
} // namespace prv
