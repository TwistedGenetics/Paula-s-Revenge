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
enum class GearSend : uint8_t { Cc = 0, Cc14, Nrpn, KorgM1, RolandDt1 };   // (the last two: one parameter by System Exclusive)

struct GearParam
{
    std::string group, name;   // "FILTER", "CUTOFF"
    GearSend how = GearSend::Cc;
    int num = 0;               // CC: the CC (Cc14: the MSB's); NRPN: the parameter's MSB (CC 99)
    int lsb = -1;              // Cc14: the LSB's CC; NRPN: the parameter's LSB (CC 98)
    int steps = 0;             // 0: continuous; else a switch with this many positions
    bool direct = false;       // a switch sent as its position (0, 1, 2 ...) rather than spread over 0 - 127
    // KorgM1: num = page, lsb = position. RolandDt1: address = the parameter's (7-bit digits, e.g. 0x03001051),
    // tones = 0 for one address, or 4: the same offset in all four tones (address = tone 1's; each tone is 0x200 on)
    int address = 0, tones = 0;
    bool nibbles = false;      // (RolandDt1: the value as two bytes of 4 bits, high first)
    int lo = 0, hi = 127;      // KorgM1 / RolandDt1: the raw values sent
    int show = 0;              // (shown as raw + show)
    std::string text (float v01) const;   // the value as the synth shows it
    std::string label() const { return group + ": " + name; }
    std::string where() const; // "CC 74", "CC 43+107", "NRPN 0:3"
};

struct GearMap
{
    std::string name;          // "MICROKORG 2"
    std::string note;          // a line about it
    std::vector<GearParam> params;
    std::vector<std::string> groups() const;    // in order
    std::vector<std::string> knobs;             // the knobs' usual four (labels)
};

constexpr int kGearNone = 0;
constexpr int kGearKnobBase = 128;   // a knob's CC of 128 + n: the gear's parameter n
int gearCount();                     // the maps (ids 1 .. gearCount())
constexpr int kGearAkaiS1000 = 6, kGearAkaiS950 = 7, kGearTyphon = 8, kGearM1Single = 9, kGearM1Double = 10, kGearJv1080 = 11;
bool gearHasEditor (int id);         // its own editor page (the S1000, the S950: SysEx) instead of ALL CONTROLS
const GearMap* gearMap (int id);     // nullptr for 0 / out of range
std::string gearName (int id);       // "NONE" for 0

// the MIDI bytes that set a parameter to v (0..1) on channel 0..15: up to 4 three-byte messages
// (a switch lands on its nearest position - the values the device's chart lists)
int gearMessages (const GearParam&, float v01, int channel, uint8_t out[4][3]);
// System Exclusive ones (KorgM1, RolandDt1): the whole messages, one after another (each F0 ... F7)
std::vector<uint8_t> gearSysex (const GearParam&, float v01, int channel);
bool gearIsSysex (const GearParam& p);
constexpr int kGearAutoBase = 512;   // an automation lane's param of 512 + n: the GEAR's control n
// a switch's value for position i of n, as Korg's charts give them (0, 128 / n, 2 x 128 / n ... and 127 last)
int gearStepValue (int i, int n);
} // namespace prv
