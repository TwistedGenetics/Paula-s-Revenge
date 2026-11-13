// Paula's Revenge - ROM: the built-in ROMpler (in the spirit of the JV-1080 and M1). Up to four TONES, each a
// multisample of small looped samples (your WAVs, laid across the keys by their root notes - RomBank.h), each with
// its own LEVEL, PAN, TUNE and FINE; together through a 12 dB low-pass with its own envelope, the amp envelope,
// VIBRATO, CHORD (one key, a chord), CHORUS and a stereo ECHO. POLY: 16 voices (the oldest fades out in 4 ms).
// A sample's loop plays for as long as the note (and through its release); one without a loop plays once.
// Plays through the synth rack like SUB (pluginKey "SYN:ROM"); its samples are the instrument's `rom` bank.
#pragma once

#include "PluginSynth.h"
#include "RomBank.h"
#include "MorphFilter.h"
#include <array>
#include <memory>
#include <vector>

namespace prv
{
struct SynthParam;
constexpr const char* kRomKey = "SYN:ROM";

namespace rom
{
    enum P { Level1, Pan1, Tune1, Fine1, Level2, Pan2, Tune2, Fine2, Level3, Pan3, Tune3, Fine3, Level4, Pan4, Tune4, Fine4,
             Chord, Octave, Vibrato, VibRate, Cutoff, Resonance, EnvAmount, FilterDecay,
             Attack, Decay, Sustain, Release, Chorus, Echo, EchoTime, VelSens, Level,
             // (v0.79) the MORPH FILTER in place of the low-pass: its MODEL, where it sits, moved by the filter
             // envelope, an LFO and how hard the note is played; FREQ and KEY TRACK move the whole shape
             FilterType, MorphModel, Morph, MorphEnv, MorphLfo, MorphRate, MorphVel, MorphFreq, MorphKey, Emphasis,
             Count };
    enum FilterKind { LowPass = 0, MorphKind };
    constexpr int kPerTone = 4;   // (LEVEL, PAN, TUNE, FINE)
}
const std::vector<SynthParam>& romParams();

class RomSynth : public PluginSynth
{
public:
    RomSynth();
    void prepare (double sampleRate, int maxBlock) override;
    void noteOn (int midiNote, int velocity, int offset) override;
    void noteOff (int midiNote, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int, int, int) override {}
    void setParameter (int index, float value01) override;
    void render (float* left, float* right, int numSamples) override;
    void update (const Instrument&) override;
    void setValues (const std::vector<float>& v);      // (tests)
    void setBank (std::shared_ptr<const RomBank> b);   // (tests; update() does it from the instrument)
    int  voicesPlaying() const;                        // (tests)
    float morphOf (int voice) const;                   // (tests: where a voice's MORPH is)

private:
    struct Event { int offset; int type; int note; int vel; };
    std::vector<Event> events;
    std::array<float, rom::Count> p {};
    std::array<float, rom::Count> base {};   // (v0.86) the settings as loaded - restoreParameter puts one back
  public:
    void restoreParameter (int i) override { if (i >= 0 && i < (int) p.size()) p[(size_t) i] = base[(size_t) i]; }
  private:
    std::vector<uint8_t> lastState;
    std::shared_ptr<const RomBank> bank;
    double sr = 48000.0;
    struct Tone { const RomZone* zone = nullptr; double pos = 0; bool done = true; };
    struct Voice
    {
        int   key = -1;
        float pitch = 60;
        bool  gate = false;
        int   stage = 0;            // 0 off, 1 attack, 2 decay / sustain, 3 release
        float env = 0, fenv = 0, vel = 1;
        std::array<Tone, kRomTones> tones {};
        std::shared_ptr<const RomBank> bank;   // (the samples it plays, kept while it plays)
        float s1[2][2] {};
        float a1 = 0, a2 = 0, a3 = 0;
        int   upd = 0, order = 0;
        float fade = 1; bool fading = false;
        double vibPh = 0;
        MorphFilter morphL, morphR;   // (FILTER: MORPH)
        float morphNow = 0;
    };
    static constexpr int kVoices = 16;
    Voice voices[kVoices];
    float bend = 0; int bendRange = 2;
    double morphLfoPh = 0;
    int orderCount = 0;
    std::vector<float> chBuf[2]; int chPos = 0; double chPh = 0;
    std::vector<float> echoBuf[2]; int echoPos = 0; float echoLp[2] {};
    void handle (const Event&);
    void startVoice (int key, float pitch, int vel);
    void renderVoice (Voice&, float& l, float& r);
};
} // namespace prv
