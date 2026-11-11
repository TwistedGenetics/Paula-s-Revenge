// Paula's Revenge - talking to an Akai S1000 over MIDI System Exclusive (plain C++).
// The S1000 has no message for one parameter: a program's common block (72 bytes) or a keygroup (149 bytes) is
// asked for, changed and sent back whole. Data bytes go as two MIDI bytes (low nibble, then high); program numbers
// as two 7-bit bytes, LSB first; names in Akai's own 41-character set. Layouts: Akai's "S1000 MIDI Exclusive
// Communication" (as transcribed by Frank Neumann for the lakai project).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prv::akai
{
constexpr int kProgramBytes = 72, kKeygroupBytes = 149;

enum Op : uint8_t { RSTAT = 0x00, STAT = 0x01, RPLIST = 0x02, PLIST = 0x03, RSLIST = 0x04, SLIST = 0x05,
                    RPDATA = 0x06, PDATA = 0x07, RKDATA = 0x08, KDATA = 0x09, REPLY = 0x16 };

enum class Kind : uint8_t { Number, Signed, Note, OnOff, Fine, Sixteenths };   // Fine: a 0-255 fraction shown as cents; Sixteenths: 1/16 semitones
struct Field
{
    std::string group, name;
    int offset = 0;
    int lo = 0, hi = 99;
    Kind kind = Kind::Number;
    std::vector<std::string> labels;      // (a switch's positions by name)
    int enc = 0;                          // 0: a byte (S1000, decoded); S900 / S950: 1 = DB (2 MIDI bytes), 2 = DW (4)
    int bit = -1;                         // (one bit of a DB: an on / off)
    bool invert = false;                  // (that bit means ON when it's 0)
    std::string text (int value) const;   // "C-3", "+12", "ON", "50"
};

// what the editor shows: a program's controls, and a keygroup's (zones 1 - 4 included)
const std::vector<Field>& programFields();
const std::vector<Field>& keygroupFields();
std::vector<std::string> groupsOf (const std::vector<Field>&);

int  get (const std::vector<uint8_t>& block, const Field&);
void set (std::vector<uint8_t>& block, const Field&, int value);   // (clamped to its range)

std::string fromAkai (const uint8_t* codes, int n);   // Akai name codes -> "KICK 1"
std::vector<uint8_t> toAkai (const std::string&);     // -> 12 codes

// messages to the S1000 (cc: its exclusive channel, 0 - 127)
std::vector<uint8_t> requestStatus (int cc);
std::vector<uint8_t> requestPrograms (int cc);
std::vector<uint8_t> requestProgram (int cc, int program);
std::vector<uint8_t> requestKeygroup (int cc, int program, int keygroup);
std::vector<uint8_t> programMessage (int cc, int program, const std::vector<uint8_t>& block);
std::vector<uint8_t> keygroupMessage (int cc, int program, int keygroup, const std::vector<uint8_t>& block);

// what came back
struct Reply
{
    int op = -1;
    int program = -1, keygroup = -1;
    std::vector<uint8_t> block;         // PDATA / KDATA: the block, decoded
    std::vector<std::string> names;     // PLIST / CAT: the programs' names, in order
    std::vector<int> numbers;           // CAT: each program's number
    std::vector<std::vector<uint8_t>> keygroups;   // S900 / S950 PRGM: its keygroups (each 140 MIDI bytes)
    int replyCode = -1;                 // REPLY: 0 = OK
};
bool parse (const uint8_t* msg, int length, Reply&);   // false: not an S1000 message (or broken)

// (tests: an S1000 that answers - its programs and keygroups as blocks)
struct FakeS1000
{
    std::vector<std::string> programNames;
    std::vector<std::vector<uint8_t>> programs;                // 72-byte blocks
    std::vector<std::vector<std::vector<uint8_t>>> keygroups;  // per program, 149-byte blocks
    void addProgram (const std::string& name, int keygroups);
    std::vector<std::vector<uint8_t>> receive (const uint8_t* msg, int length);   // its answers
};

// ---- the S900 / S950 (Akai's "MIDI System Exclusive Data Format for S900", disk V2.0; the S950 shares it).
// A program travels whole: 76 MIDI bytes of its own, then its keygroups at 140 each, then a checksum (the XOR of
// those). Values are packed as DB (8 bits as two MIDI bytes, low 7 first) or DW (16 bits as four); blocks here are
// kept as those MIDI bytes and the fields read and write them in place.
namespace s900
{
    constexpr int kProgramBytes = 76, kKeygroupBytes = 140;
    enum Op : uint8_t { RPRGM = 2, RCAT = 3, PRGM = 9, CAT = 11 };
    const std::vector<Field>& programFields();
    const std::vector<Field>& keygroupFields();
    std::vector<uint8_t> requestCatalog (int channel);
    std::vector<uint8_t> requestProgram (int channel, int program);
    std::vector<uint8_t> programMessage (int channel, int program, const std::vector<uint8_t>& block, const std::vector<std::vector<uint8_t>>& keygroups);
    bool parse (const uint8_t* msg, int length, Reply&);   // CAT: the programs; PRGM: block + keygroups
    std::string name (const std::vector<uint8_t>& block, int offset);   // a DB* name (10 characters)
    void setName (std::vector<uint8_t>& block, int offset, const std::string&);
    constexpr int kSoftName = 48, kLoudName = 92, kProgramName = 0;      // (offsets in their blocks)

    struct FakeS950   // (tests)
    {
        struct Prog { int number; std::vector<uint8_t> block; std::vector<std::vector<uint8_t>> keygroups; };
        std::vector<Prog> programs;
        std::vector<std::string> samples;
        void addProgram (int number, const std::string& name, int keygroups);
        std::vector<std::vector<uint8_t>> receive (const uint8_t* msg, int length);
    };
}
} // namespace prv::akai
