// Paula's Revenge - GEAR: a MIDI-out instrument can be told what's on the other end, and its controls are then
// picked by name ("FILTER: CUTOFF") instead of by CC number, and sent the way that synth wants them: a plain CC,
// a 14-bit CC pair (MSB then LSB), or an NRPN. Where the numbers come from: Korg's own MIDI documents (the
// microKORG 2 Owner's Manual's chart; the NTS-1 mkII MIDI Implementation), Dreadbox's Artemis Product Manual
// (v1.20), and for the Virus TI's PAGE A the midi.guide chart (Access's manuals leave the list to their site).
// Nothing is guessed: what those don't give isn't here.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prv
{
enum class GearSend : uint8_t { Cc = 0, Cc14, Nrpn };

struct GearParam
{
    std::string group, name;   // "FILTER", "CUTOFF"
    GearSend how = GearSend::Cc;
    int num = 0;               // CC: the CC (Cc14: the MSB's); NRPN: the parameter's MSB (CC 99)
    int lsb = -1;              // Cc14: the LSB's CC; NRPN: the parameter's LSB (CC 98)
    int steps = 0;             // 0: continuous; else a switch with this many positions
    bool direct = false;       // a switch sent as its position (0, 1, 2 ...) rather than spread over 0 - 127
    std::string label() const { return group + ": " + name; }
    std::string where() const; // "CC 74", "CC 43+107", "NRPN 0:3"
};

struct GearMap
{
    std::string name;          // "MICROKORG 2"
    std::string note;          // a line about it
    std::vector<GearParam> params;
    std::vector<std::string> groups() const;    // in order
};

constexpr int kGearNone = 0;
constexpr int kGearKnobBase = 128;   // a knob's CC of 128 + n: the gear's parameter n
int gearCount();                     // the maps (ids 1 .. gearCount())
constexpr int kGearAkaiS1000 = 6;
bool gearHasEditor (int id);         // its own editor page (the S1000: SysEx) instead of ALL CONTROLS
const GearMap* gearMap (int id);     // nullptr for 0 / out of range
std::string gearName (int id);       // "NONE" for 0

// the MIDI bytes that set a parameter to v (0..1) on channel 0..15: up to 4 three-byte messages
// (a switch lands on its nearest position - the values the device's chart lists)
int gearMessages (const GearParam&, float v01, int channel, uint8_t out[4][3]);
// a switch's value for position i of n, as Korg's charts give them (0, 128 / n, 2 x 128 / n ... and 127 last)
int gearStepValue (int i, int n);
} // namespace prv
