#pragma once
#include <cstdint>

namespace helix {

struct MidiMsg
{
    int offset = 0;              // sample offset inside the block
    uint8_t data[3] = {0, 0, 0};
    uint8_t status() const { return data[0] & 0xF0; }
    static MidiMsg make(int off, uint8_t a, uint8_t b, uint8_t c) { MidiMsg m; m.offset = off; m.data[0] = a; m.data[1] = b; m.data[2] = c; return m; }
};

struct Transport
{
    bool   valid = false;        // host gave tempo / position
    bool   playing = false;
    double bpm = 120.0;
    double ppq = 0.0;            // song position in quarter notes at block start
};

} // namespace helix
