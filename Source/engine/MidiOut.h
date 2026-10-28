// Paula's Revenge - an instrument that plays hardware over MIDI (plain C++)
#pragma once

#include "PluginSynth.h"
#include <cstdint>
#include <memory>

namespace prv
{
// Where a MIDI-out instrument's messages go. Messages are timed by a sample offset into
// the block being rendered; flush() hands the block's messages on (the app schedules them
// on the MIDI device `delayMs` later than the audio they belong with).
class MidiSink
{
public:
    virtual ~MidiSink() = default;
    virtual void send (const uint8_t* data, int length, int sampleOffset) = 0;
    virtual void flush (int numSamples, double sampleRate, double delayMs) = 0;
    virtual void sendNow (const uint8_t* data, int length) = 0;   // straight away (not from the audio thread)
};

class MidiOutSynth : public PluginSynth
{
public:
    explicit MidiOutSynth (std::unique_ptr<MidiSink> s) : sink (std::move (s)) {}
    ~MidiOutSynth() override;   // anything still sounding is switched off

    void prepare (double sampleRate, int) override { rate = sampleRate; }
    void noteOn (int note, int velocity, int offset) override;
    void noteOff (int note, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int cc, int value, int offset) override;
    void setParameter (int knob, float value01) override { setParameterAt (knob, value01, 0); }
    void setParameterAt (int knob, float value01, int offset) override;   // knobs L, Q, H, W: their CCs
    void render (float* left, float* right, int numSamples) override;
    void update (const Instrument&) override;
    void clock (int kind, double ppq, int offset) override;

    MidiSink* output() const { return sink.get(); }
    int channel() const { return chan; }
    float knobValue (int k) const { return k >= 0 && k < kInstrumentKnobs && lastCc[k] >= 0 ? lastCc[k] / 127.0f : -1.0f; }   // last sent, -1 = none

private:
    std::unique_ptr<MidiSink> sink;
    double rate = 48000.0;
    int chan = 0;                                  // 0..15
    int cc[kInstrumentKnobs] { 74, 71, 73, 10 };
    int lastCc[kInstrumentKnobs] { -1, -1, -1, -1 };
    bool clockOn = false;
    double delayMs = 0;
    int sounding[128] {};                          // note-ons not yet switched off
    int noteChan[128] {};                          // the channel each note went out on
    bool usedChan[16] {};
    void out (int offset, int a, int b = -1, int c = -1);
};
} // namespace prv
