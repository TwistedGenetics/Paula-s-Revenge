// Paula's Revenge - CV out to a modular through a DC-coupled interface (an Expert Sleepers
// ES-9 or the like): a CV OUT instrument (pitch, gate, velocity, mod), the song's CV SOURCES
// (clock, reset, run, LFO, envelope, a knob from the pattern) and AUTO-TUNE's map. Plain C++.
#pragma once

#include "PluginSynth.h"
#include "Song.h"
#include <cstdint>
#include <vector>

namespace prv
{
// Where the voltages go: one buffer per interface output (1..16), for the block being rendered.
// The app points them at the device's channels; base = where this chunk starts in them.
struct CvBus
{
    float* out[kCvOutputs] {};
    int base = 0;
    bool any() const { for (auto* p : out) if (p) return true; return false; }
    void put (int output, int i, float sample) { if (output >= 1 && output <= kCvOutputs && out[output - 1] != nullptr) out[output - 1][base + i] = sample; }
};

// volts as a sample (the interface puts out fullScale volts at 1.0); clamped to the interface's range
inline float cvSample (double volts, double fullScale)
{
    const double fs = fullScale > 0.1 ? fullScale : 10.0;
    const double s = volts / fs;
    return (float) (s > 1.0 ? 1.0 : s < -1.0 ? -1.0 : s);
}

// the volts for a pitch (a MIDI note, fractions = bends): through the AUTO-TUNE map when there is
// one (straight lines between the measured points, carried on past the ends), else 1 V / octave
// with C-3 (MIDI 60) at 0 V
double cvVoltsFor (const Instrument&, double midiNote);
double cvVoltsFor (const std::vector<float>& notes, const std::vector<float>& volts, double midiNote);

// AUTO-TUNE: the measured (volts, Hz) pairs into a map of (pitch as a MIDI note, volts), sorted
// by pitch; points that don't rise with the voltage (a bad reading) are dropped. false = too few.
bool solveCvMap (const std::vector<double>& volts, const std::vector<double>& hz, std::vector<float>& notes, std::vector<float>& mapVolts);

// The CV OUT instrument: mono, last note wins (a released note goes back to one still held)
class CvOutSynth : public PluginSynth
{
public:
    void prepare (double sampleRate, int) override { rate = sampleRate; }
    void noteOn (int note, int velocity, int offset) override;
    void noteOff (int note, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int, int, int) override {}
    void setParameter (int knob, float v) override { setParameterAt (knob, v, 0); }
    void setParameterAt (int knob, float value01, int offset) override;   // knob L (Lxx): the MOD output
    void render (float* left, float* right, int numSamples) override;   // (no audio)
    void renderCv (CvBus&, int numSamples) override;
    void update (const Instrument&) override;

    double pitchVolts() const { return lastVolts; }
    bool gateOpen() const { return gate; }

private:
    enum EvType : uint8_t { On, Off, AllOff, Bend, Mod };
    struct Ev { EvType type; int offset; int a, b; };
    Ev events[256];
    int numEvents = 0;
    void push (EvType t, int offset, int a = 0, int b = 0) { if (numEvents < 256) events[numEvents++] = { t, offset, a, b }; }

    double rate = 48000.0;
    Instrument cfg;                 // outputs, scale, map (copied by update)
    int held[32]; int numHeld = 0;  // the keys down, newest last
    int note = 60, velocity = 100;
    double bend = 0;                // semitones
    float mod = 0;
    bool gate = false;
    int retrigger = 0;              // samples of gate low before it opens again
    double testVolts = 0; bool testing = false;
    double lastVolts = 0;
    void apply (const Ev&);
public:
    // AUTO-TUNE / the tuner: hold these volts on the pitch output with the gate open
    void setTesting (bool on, double volts) { testing = on; testVolts = volts; }
};

// The song's CV SOURCES, sample by sample (the player calls it in its sample loop)
class CvSourceRunner
{
public:
    void prepare (double sampleRate);
    void reset();
    // ppq: the song's position in beats; beatsPerSample: how far one sample goes; playing; started: this
    // sample is the first one playing (RESET's pulse); trig / rel: notes starting / ending on each track
    void process (const Song&, CvBus&, int i, double ppq, double beatsPerSample, bool playing, bool started,
                  const bool* trig, const bool* rel, const float* knobA, const float* knobB);
private:
    double rate = 48000.0;
    struct State { int pulse = 0; double lastTick = -1; double phase = 0; float rnd = 0; int env = 0; float envLevel = 0; uint32_t seed = 12345; };
    State st[kCvSources];
};
} // namespace prv
