// Paula's Revenge - TG 101 (v1.00): a monosynth in the spirit of the SH-101. One voice, as on the real thing:
//  VCO      - a saw and a pulse at once (RANGE 16' 8' 4' 2'), the pulse's width by hand, the LFO or the envelope
//             (PWM), vibrato from the LFO (MOD); and the sub-oscillator - a square one or two octaves down, or the
//             narrow pulse two octaves down - all phase-locked to the VCO, as its flip-flops are
//  MIXER    - PULSE, SAW, SUB and NOISE levels, into the filter (driven a little when they're all up)
//  VCF      - a resonant 4-pole low-pass of OTA stages (IR3109-style) that self-oscillates at the top of RES; its
//             FREQ moved by the envelope (ENV), the LFO (MOD), the keys (KYBD) and the bender
//  ENV      - one ADSR, triggered by the GATE, by GATE + TRIGGER (every note) or by the LFO (it re-fires each cycle)
//  VCA      - ENV, or GATE (on / off: the organ-like 101 trick)
//  LFO      - TRIANGLE, SQUARE, RANDOM (stepped) or NOISE; its own rate or synced to the song
//  PORTA    - OFF, ON or AUTO (glides only from a held note into the next)
//  ARP      - UP, DOWN, UP + DOWN over 1-3 octaves at the song's tempo; HOLD keeps it going
// Not a copy of any circuit or code: our own model, tuned by ear. Plays through the synth rack ("SYN:TG101").
#pragma once

#include "PluginSynth.h"
#include "Oversample.h"
#include <array>
#include <vector>

namespace prv
{
struct SynthParam;
constexpr const char* kTg101Key = "SYN:TG101";

namespace tg101
{
    enum P { Range, Tune, Fine, VcoMod, Pw, PwmSource,
             PulseLvl, SawLvl, SubLvl, SubType, NoiseLvl,
             Freq, Res, EnvAmt, VcfMod, Kybd, BendVcf,
             Attack, Decay, Sustain, Release, EnvTrig, VcaMode,
             LfoRate, LfoWave, LfoSync,
             PortaMode, PortaTime,
             ArpMode, ArpRate, ArpOct, ArpGate, ArpHold,
             VelSens, Drive, Level, Count };
    enum PwmSrc { PwmLfo = 0, PwmManual, PwmEnv };
    enum SubKind { SubOct1 = 0, SubOct2, SubPulse2 };
    enum Trig { TrigGateTrig = 0, TrigGate, TrigLfo };
    enum Vca { VcaEnv = 0, VcaGate };
    enum Lfo { LfoTri = 0, LfoSquare, LfoRandom, LfoNoise };
    enum Porta { PortaOff = 0, PortaOn, PortaAuto };
    enum Arp { ArpOff = 0, ArpUp, ArpDown, ArpUpDown };
}
const std::vector<SynthParam>& tg101Params();
constexpr int kTg101Presets = 14;
const char* tg101PresetName (int i);
std::vector<float> tg101Preset (int i);

class Tg101Synth : public PluginSynth
{
public:
    Tg101Synth();
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
    void restoreParameter (int i) override { if (i >= 0 && i < (int) p.size()) p[(size_t) i] = base[(size_t) i]; }
    // (tests)
    bool  sounding() const { return env > 1.0e-5f || gateOn || vcaGate > 1.0e-5f; }
    float pitchNow() const { return pitch; }         // semitones (MIDI), with the glide, before RANGE
    float cutoffNow() const { return cutoffHz; }
    float envNow() const { return env; }
    int   envStarts() const { return envStartCount; }
    int   keyNow() const { return playing; }          // the note sounding (the arpeggio's step), -1 none
    int   arpSteps() const { return arpStepCount; }
    float valueOf (int i) const { return i >= 0 && i < (int) p.size() ? p[(size_t) i] : 0.0f; }
    void  setBpm (double b) { songBpm = b; }          // (tests)

private:
    struct Event { int offset; int type; int note; int vel; };
    std::vector<Event> events;
    std::array<float, tg101::Count> p {}, base {};
    std::vector<uint8_t> lastState;
    double sr = 48000.0 * kOs, outSr = 48000.0, songBpm = 125.0;
    static constexpr int kChunk = 128;
    std::array<float, kChunk * kOs> hi {};
    Decimator dec;
    void renderHi (float* out, int n);
    void handle (const Event&);
    void play (int note, bool legato);   // a note starts (or the arpeggio's step)
    void stop();                         // the gate closes
    // keys and the arpeggio
    std::vector<int> held;               // keys down, oldest first
    std::vector<int> latched;            // HOLD: the chord the arpeggio keeps playing
    int  playing = -1;                   // the note sounding
    bool gateOn = false;
    float vel = 1;
    double arpPhase = 0;                 // 0..1 through a step
    int  arpIndex = -1, arpDir = 1, arpStepCount = 0;
    bool arpNoteOn = false;
    // the voice
    float pitch = 60, target = 60;
    double ph = 0;                       // the VCO, 0..1
    int   flip = 0;                      // the sub's flip-flops: counts the VCO's cycles (0..3)
    float env = 0; int envStage = 0;     // 1 attack, 2 decay, 3 sustain, 4 release
    int   envStartCount = 0;
    float vcaGate = 0;                   // VCA GATE: on / off, smoothed over a millisecond
    double lfoPh = 0; float lfoOut = 0, lfoRand = 0, lfoNoise = 0;
    uint32_t seed = 0x1234567u;
    float s[4] {}, fbHp = 0;
    float cutoffHz = 1000;
    float dcX = 0, dcY = 0;
    float bend = 0; int bendRange = 2;
    float noise();
    void  startEnv();
};
} // namespace prv
