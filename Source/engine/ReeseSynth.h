// Paula's Revenge - REESE: the built-in reese / hoover bass. Up to 7 saws detuned against each other (slowly
// drifting, so the phasing never sits still), or HOOVER's pulse-saws with a pitch bend up into each note and a
// chorus; a sub an octave down, a 24 dB low-pass with its own envelope and a wobble LFO, drive. Click-free like SUB:
// the oscillators never restart, the envelope goes on from where it is, overlapping notes in LEGATO slide.
// (v0.82) Much more to twist it with: the oscillators' WAVE, how the DETUNE spreads them, DRIFT, a STACK an octave or a
// fifth away on every other one, NOISE; five FILTER types with KEY TRACK, FILTER DRIVE and an envelope ATTACK; the
// WOBBLE's SHAPE, tempo SYNC, TARGET (cutoff, pitch, level, detune, phaser, flanger) and RETRIGGER; DRIVE TYPEs (WARM,
// HARD, FOLD, RECTIFY, CRUSH), a PHASER and a FLANGER (the neuro twist), SQUASH, WIDTH and MONO BASS. Five tabs: OSC,
// MOVEMENT, FILTER, FX, AMP; ten presets.
// Plays through the synth rack like SUB (pluginKey "SYN:REESE"). Plain C++, unit tested.
#pragma once

#include "PluginSynth.h"
#include "Oversample.h"
#include <algorithm>
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
             // (v0.82)
             OscWave, DetuneShape, Stack, Noise, Drift,
             FilterMode, KeyTrack, FilterDrive, EnvAttack,
             WobShape, WobSync, WobTarget, WobRetrig,
             DriveType, Squash,
             PhaserMix, PhaserRate, PhaserDepth, PhaserFb,
             FlangeMix, FlangeRate, FlangeDepth, FlangeFb,
             Width, MonoBass,
             Count };
    enum WaveKind { WSaw = 0, WSquare, WSawSquare, WTri };
    enum FilterKind { Low24 = 0, Low12, Band, High, Notch };
    enum WobTargetKind { TCutoff = 0, TPitch, TLevel, TDetune, TPhaser, TFlanger };
    enum DriveKind { Warm = 0, Hard, Fold, Rectify, Crush };
    enum ModeKind { Legato = 0, Always, Retrigger, Poly };
    enum TypeKind { Reese = 0, Hoover };
}
const std::vector<SynthParam>& reeseParams();
constexpr int kReesePresets = 10;
const char* reesePresetName (int i);              // "CLASSIC REESE"
std::vector<float> reesePreset (int i);

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
    void updateSong (const Song& s) override { songBpm = (double) s.tempo * 6.0 / std::max (1, s.speed); }
    void setValues (const std::vector<float>& v);   // (tests)
    float pitchNow() const { return voices[0].pitch + voices[0].bendEnv; }   // (tests) the first voice, semitones
    float levelNow() const { return voices[0].env; }
    float cutoffNow() const { return voices[0].cutNow; }

private:
    struct Event { int offset; int type; int note; int vel; };
    std::vector<Event> events;
    std::array<float, reese::Count> p {};
    std::array<float, reese::Count> base {};   // (v0.86) the settings as loaded - restoreParameter puts one back
  public:
    void restoreParameter (int i) override { if (i >= 0 && i < (int) p.size()) p[(size_t) i] = base[(size_t) i]; }
  private:
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
        float drift[kOsc] {}, driftTo[kOsc] {};   // (each oscillator's wander)
        int   mode = 0;                 // (the filter type its coefficients are for)
    };
    static constexpr int kVoices = 6;
    Voice voices[kVoices];
    std::vector<int> held;
    int pendingRelease = -1;
    float bend = 0; int bendRange = 2;
    int orderCount = 0;
    double move = 0, wob = 0, pwmPh = 0;   // the LFOs
    float wobHold = 0, wobSmooth = 0;
    double songBpm = 125.0;
    uint32_t seed = 909;
    float randomDetune[kOsc] {};
    float rnd();
    // (v0.82) the FX: phaser, flanger, squash, mono bass; the crusher's held sample
    struct Ap { float z = 0; };
    Ap phs[2][6];
    float phFb[2] {};
    double phPh = 0, flPh = 0;
    std::vector<float> flBuf[2]; int flPos = 0;
    float sqEnv = 0, sqGain = 1;
    float mbL[2] {}, mbR[2] {};
    float crushHold[2] {}; int crushCount = 0;
    Oversampler driveOs[2];   // (v0.88) the DRIVE at 4x, each side: no aliasing whistle
    float wobNowValue = 0;
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
