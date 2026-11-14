// Paula's Revenge - ACID: a bass line synth in the spirit of the TB-303, with the Devil Fish mods. One voice (it's a
// mono bass line): a saw or square into a resonant 4-pole low-pass, the filter's envelope (the MEG) and the ACCENT
// that pushes it and the level harder on loud notes - with the accent SWEEP that builds up over a run of accents - and
// SLIDE, the glide from one note into the next with no new envelope.
// The Devil Fish's extras: separate NORMAL and ACCENT DECAY, SOFT ATTACK, the amp's own DECAY and RELEASE, SLIDE
// TIME, SWEEP SPEED, FILTER TRACKING (the cutoff follows the keys), FILTER FM (the output back into the cutoff, at
// audio rate), RES BASS (how much low end survives a screaming resonance), resonance up to self-oscillation, and the
// MUFFLER (a soft or hard clip on the output) with OVERDRIVE.
// ACCENT: a note played at ACCENT FROM (C38) or louder - C40 in a pattern (an ACID instrument's VOL starts at 48, C30).
// SLIDE: notes that overlap (keys / MIDI) - and with SLIDE: TIED NOTES, a note that runs straight into the next with
// no OFF between (a 303's slide step); an OFF in the pattern ends the note so the next one plays fresh.
// Plays through the synth rack like SUB (pluginKey "SYN:ACID").
#pragma once

#include "PluginSynth.h"
#include "Oversample.h"
#include <array>
#include <vector>

namespace prv
{
struct SynthParam;
constexpr const char* kAcidKey = "SYN:ACID";

namespace acid
{
    enum P { Wave, Tune, Fine, PulseShape,
             Cutoff, Resonance, EnvMod, Decay, Accent, AccentAt,
             AccentDecay, SoftAttack, SweepSpeed, AmpDecay, Release,
             FilterTrack, FilterFm, ResBass,
             SlideTime, SlideMode,
             Muffler, Drive, SubOsc, VelSens, Level, Count };
    enum WaveKind { Saw = 0, Square };
    enum SlideKind { Overlap = 0, Tied };
}
const std::vector<SynthParam>& acidSynthParams();
constexpr int kAcidPresets = 10;
const char* acidPresetName (int i);              // "CLASSIC"
std::vector<float> acidPreset (int i);           // its settings (PRESETS lists them)

class AcidSynth : public PluginSynth
{
public:
    AcidSynth();
    void prepare (double sampleRate, int maxBlock) override;
    void noteOn (int midiNote, int velocity, int offset) override;
    void noteOff (int midiNote, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int, int, int) override {}
    void setParameter (int index, float value01) override;
    void render (float* left, float* right, int numSamples) override;
    void update (const Instrument&) override;
    void lineVolume (int cxx) override { lineVol = cxx; }
    void setValues (const std::vector<float>& v);   // (tests)
    // (tests)
    bool  sounding() const { return ampEnv > 1.0e-5f || gate; }
    float pitchNow() const { return pitch; }
    float cutoffNow() const { return cutoffHz; }
    float sweepNow() const { return sweep; }
    int   attacks() const { return attackCount; }    // notes that started their envelopes (a slide doesn't)
    int   accents() const { return accentCount; }    // notes that were accented
    float valueOf (int i) const { return i >= 0 && i < (int) p.size() ? p[(size_t) i] : 0.0f; }   // a control, as it is now (automation)

private:
    struct Event { int offset; int type; int note; int vel; int line = -1; };
    int lineVol = -1;   // (v0.86) the next notes' line: a Cxx (1), none (0), played live (-1)
    std::vector<Event> events;
    std::array<float, acid::Count> p {};
    std::array<float, acid::Count> base {};   // (v0.86) the settings as loaded - restoreParameter puts one back
  public:
    void restoreParameter (int i) override { if (i >= 0 && i < (int) p.size()) p[(size_t) i] = base[(size_t) i]; }
  private:
    std::vector<uint8_t> lastState;
    double sr = 48000.0 * kOs, outSr = 48000.0;   // (sr: the voice's own rate, 4x the song's)
    static constexpr int kChunk = 128;
    std::array<float, kChunk * kOs> hi {};
    Decimator dec;
    void renderHi (float* out, int n);
    // the one voice
    std::vector<int> held;            // keys down, oldest first
    bool gate = false;
    int  key = -1;
    float pitch = 48, target = 48;    // semitones (MIDI)
    float vel = 1;
    bool accented = false;
    int accentCount = 0;
    double ph = 0, subPh = 0;
    float meg = 0, megDecay = 0;      // the filter's envelope
    float accEnv = 0, sweep = 0;      // the accent's envelope, and the sweep it charges
    float ampEnv = 0; int ampStage = 0;   // 1 attack, 2 decay / hold, 3 release
    float attackRate = 1;
    float cutoffHz = 500;
    // the filter: 4 one-pole stages, the feedback's high-pass, the FILTER FM's last output
    float s[4] {}, fbHp = 0, lastOut = 0;
    float dcX = 0, dcY = 0;
    float bend = 0; int bendRange = 2;
    long long sampleNow = 0, lastOffAt = -1;   // (a note-off and a note-on at the same sample: TIED)
    int attackCount = 0;
    void handle (const Event&);
    void startNote (int note, int velocity, bool slide, int line = -1);
    void releaseGate();
};
} // namespace prv
