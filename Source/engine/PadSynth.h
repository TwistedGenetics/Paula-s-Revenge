// Paula's Revenge - PAD: a layered pad synth. Four LAYERS, each a sound of its own - a wave (SAW, SQUARE, PWM, TRI,
// SINE, ORGAN, VOX, GLASS, NOISE, AIR) with up to 7 unison voices spread in stereo and drifting, its own filter and
// envelope, a start DELAY (so one layer swells in after another), its own LEVEL and PAN, and a KEY range and a
// VELOCITY zone (so layers can split the keyboard, or soft and hard notes play different layers). Then the MAIN
// FILTER with its envelope, two LFOs (tabs: LAYER A-D, MIX - the layers' levels, keys and velocity zones together -
// MOD and SPACE) (CUTOFF, PITCH, LEVEL, PAN, A<>B and C<>D crossfades, WIDTH), and SPACE: CHORUS,
// ECHO and a REVERB. 12 notes at once. Plays through the synth rack like SUB (pluginKey "SYN:PAD").
#pragma once

#include "PluginSynth.h"
#include <array>
#include <vector>

namespace prv
{
struct SynthParam;
constexpr const char* kPadKey = "SYN:PAD";

namespace pad
{
    constexpr int kLayers = 4;
    // each layer's controls, in this order (layer L's first is L * PerLayer)
    enum L { On, Wave, Octave, Tune, Fine, Unison, Detune, Spread, Drift,
             FilterType, Cutoff, Resonance, EnvCut,
             Attack, Decay, Sustain, Release, Delay,
             Level, Pan, KeyLow, KeyHigh, VelZone, PerLayer };
    // then the whole synth's
    enum G { MainType = kLayers * PerLayer, MainCutoff, MainRes, MainEnv, MainKey, MainVel,
             FAttack, FDecay, FSustain, FRelease,
             Lfo1Shape, Lfo1Rate, Lfo1Depth, Lfo1Target,
             Lfo2Shape, Lfo2Rate, Lfo2Depth, Lfo2Target,
             Chorus, ChorusRate, Echo, EchoTime, EchoFeedback, Reverb, ReverbSize, ReverbTone,
             Glide, VelSens, Width, Level_,
             SampleBase, Count = SampleBase + kLayers * 5 };
    // (v0.85) each layer's SAMPLE wave: which sample (0 = the PAD's own), its ROOT key, START, LOOP and XFADE
    enum S { SmpSlot, SmpRoot, SmpStart, SmpLoop, SmpFade, PerSample };
    inline int s (int layer, int what) { return SampleBase + layer * PerSample + what; }
    enum Wave { Saw = 0, Square, Pwm, Tri, Sine, Organ, Vox, Glass, Noise, Air, Sample, Waves };
    enum Target { TCutoff = 0, TPitch, TLevel, TPan, TAB, TCD, TWidth };
    inline int p (int layer, int what) { return layer * PerLayer + what; }
}
const std::vector<SynthParam>& padSynthParams();
constexpr int kPadPresets = 10;
const char* padPresetName (int i);
std::vector<float> padPreset (int i);

class PadSynth : public PluginSynth
{
public:
    PadSynth();
    void prepare (double sampleRate, int maxBlock) override;
    void noteOn (int midiNote, int velocity, int offset) override;
    void noteOff (int midiNote, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int, int, int) override {}
    void setParameter (int index, float value01) override;
    void render (float* left, float* right, int numSamples) override;
    void update (const Instrument&) override;
    void updateSong (const Song& s) override;
    void setTestSample (std::vector<int16_t> mono, double rate, int loopStart = 0, int loopLength = 0);   // (tests: a sample without a song)
    void setValues (const std::vector<float>& v);   // (tests)
    int  voicesPlaying() const;                     // (tests)
    float layerLevel (int layer) const;             // (tests: how loud a layer is in the notes playing now)

private:
    struct Event { int offset; int type; int note; int vel; };
    std::vector<Event> events;
    std::array<float, pad::Count> p {};
    std::array<float, pad::Count> base {};   // (v0.86) the settings as loaded - restoreParameter puts one back
  public:
    void restoreParameter (int i) override { if (i >= 0 && i < (int) p.size()) p[(size_t) i] = base[(size_t) i]; }
  private:
    std::vector<uint8_t> lastState;
    double sr = 48000.0;
    static constexpr int kUni = 7, kVoices = 12;
    struct Svf { float ic1 = 0, ic2 = 0; };
    struct Layer
    {
        bool on = false;               // (this note plays this layer: its keys and velocity)
        float env = 0; int stage = 0;  // 1 attack, 2 decay, 3 sustain, 4 release, 0 done
        int delayLeft = 0;
        std::array<double, kUni> ph {}, ph2 {};
        std::array<double, kUni> pos {};   // (SAMPLE: where each unison copy is reading)
        std::array<bool, kUni> ended {};
        std::array<double, kUni> sstep {};   // (SAMPLE: how far each copy moves a sample)
        std::array<float, kUni> drift {}, driftTo {};
        Svf fl, fr, f1, f2, f3;        // its filter (L, R), VOX's formants
        float noiseLp = 0;
        float level = 0;               // (tests)
        int upd = 0;                   // (pitches and the filter: worked out every 16 samples)
        std::array<double, kUni> dts {};
        std::array<float, kUni> panOf {};
        double baseHz = 440;
        float fg = 0.1f, fk = 1.4f;
        float g1 = 0.05f, g2 = 0.07f, g3 = 0.15f;   // (VOX's formants, AIR's bands)
    };
    struct Voice
    {
        int key = -1; bool gate = false;
        float pitch = 60, target = 60, vel = 1;
        std::array<Layer, pad::kLayers> layers;
        float fenv = 0; int fstage = 0;
        Svf ml[2], mr[2];              // the MAIN FILTER (two stages for 24 dB)
        int mupd = 0; float mg = 0.1f, mk = 1.4f;
        int order = 0;
        float fade = 1; bool fading = false;
        bool active() const;
    };
    Voice voices[kVoices];
    int orderCount = 0;
    float bend = 0; int bendRange = 2;
    uint32_t seed = 81;
    double lfoPh[2] {};
    float lfoHold[2] {}, lfoVal[2] {};
    float lastPitch = -1;
    // SAMPLE (v0.85): the song's samples (an instrument's own, or another's), read while rendering
    const Song* song = nullptr;
    const Instrument* me = nullptr;
    int self = -1;
    Instrument testSmp;   // (tests)
    bool hasTestSmp = false;
    struct Smp { const int16_t* l = nullptr; const int16_t* r = nullptr; int len = 0; double rate = 16574; int loopA = 0, loopB = 0; };
    Smp sampleFor (int layer) const;
    struct Env { float a = 0.001f, d = 0.999f, s = 1, r = 0.999f; };   // (each envelope's steps for this block)
    Env layerEnv[pad::kLayers], filterEnv;
    // SPACE
    std::vector<float> chBuf[2]; int chPos = 0; double chPh = 0;
    std::vector<float> echoBuf[2]; int echoPos = 0; float echoLp[2] {};
    static constexpr int kRev = 8;
    std::vector<float> rev[kRev]; int revPos[kRev] {}; float revLp[kRev] {};
    void handle (const Event&);
    void startVoice (int key, int vel);
    float rnd();
    void renderVoice (Voice&, const float* lfo, float& l, float& r);
};
} // namespace prv
