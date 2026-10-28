// Paula's Revenge - an instrument that plays hardware over MIDI
#include "MidiOut.h"
#include <algorithm>
#include <cmath>

namespace prv
{
MidiOutSynth::~MidiOutSynth()
{
    if (sink == nullptr) return;
    for (int n = 0; n < 128; ++n)
        if (sounding[n] > 0)
        {
            const uint8_t m[3] { (uint8_t) (0x80 | noteChan[n]), (uint8_t) n, 0 };
            sink->sendNow (m, 3);
        }
}

void MidiOutSynth::out (int offset, int a, int b, int c)
{
    if (sink == nullptr) return;
    uint8_t m[3] { (uint8_t) a, (uint8_t) std::max (0, b), (uint8_t) std::max (0, c) };
    sink->send (m, b < 0 ? 1 : c < 0 ? 2 : 3, std::max (0, offset));
}

void MidiOutSynth::noteOn (int note, int velocity, int offset)
{
    note = std::clamp (note, 0, 127);
    if (sounding[note] > 0) out (offset, 0x80 | noteChan[note], note, 0);   // (a repeated note starts again)
    out (offset, 0x90 | chan, note, std::clamp (velocity, 1, 127));
    sounding[note] = 1;
    noteChan[note] = chan;
    usedChan[chan] = true;
}

void MidiOutSynth::noteOff (int note, int offset)
{
    note = std::clamp (note, 0, 127);
    if (sounding[note] <= 0) return;
    out (offset, 0x80 | noteChan[note], note, 0);
    sounding[note] = 0;
}

void MidiOutSynth::allNotesOff (int offset)
{
    for (int n = 0; n < 128; ++n)
        if (sounding[n] > 0) { out (offset, 0x80 | noteChan[n], n, 0); sounding[n] = 0; }
    usedChan[chan] = true;
    for (int c = 0; c < 16; ++c)
        if (usedChan[c])
        {
            out (offset, 0xB0 | c, 123, 0);          // all notes off
            out (offset, 0xE0 | c, 0, 64);           // pitch bend back to the centre
        }
}

void MidiOutSynth::pitchBend (int v, int offset)
{
    v = std::clamp (v, 0, 16383);
    out (offset, 0xE0 | chan, v & 127, v >> 7);
    usedChan[chan] = true;
}

void MidiOutSynth::controller (int number, int value, int offset)
{
    out (offset, 0xB0 | chan, std::clamp (number, 0, 127), std::clamp (value, 0, 127));
    usedChan[chan] = true;
}

void MidiOutSynth::setParameterAt (int knob, float v, int offset)
{
    if (knob < 0 || knob >= kInstrumentKnobs) return;
    const int value = std::clamp ((int) std::lround (v * 127.0f), 0, 127);
    if (value == lastCc[knob]) return;
    lastCc[knob] = value;
    controller (cc[knob], value, offset);
}

void MidiOutSynth::render (float* left, float* right, int n)
{
    std::fill (left, left + n, 0.0f);
    std::fill (right, right + n, 0.0f);
    if (sink != nullptr) sink->flush (n, rate, delayMs);
}

void MidiOutSynth::update (const Instrument& in)
{
    chan = std::clamp (in.midiChannel - 1, 0, 15);
    for (int k = 0; k < kInstrumentKnobs; ++k)
    {
        const int c = std::clamp (in.midiCC[k], 0, 127);
        if (c != cc[k]) { cc[k] = c; lastCc[k] = -1; }
    }
    clockOn = in.midiClock;
    delayMs = std::max (0, in.midiDelayMs);   // (a negative delay makes the audio later instead)
}

void MidiOutSynth::clock (int kind, double ppq, int offset)
{
    if (! clockOn) return;
    if (kind == 0) { out (offset, 0xF8); return; }
    if (kind == 2) { out (offset, 0xFC); return; }
    // start: where the song is (in 16ths), then start or continue
    const int pos = std::clamp ((int) std::lround (ppq * 4.0), 0, 16383);
    out (offset, 0xF2, pos & 127, pos >> 7);
    out (offset, pos == 0 ? 0xFA : 0xFB);
}
} // namespace prv
