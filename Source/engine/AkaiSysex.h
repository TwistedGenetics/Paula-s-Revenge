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
                    RPDATA = 0x06, PDATA = 0x07, RKDATA = 0x08, KDATA = 0x09, RSDATA = 0x0A, SDATA = 0x0B,
                    RSPACK = 0x0C, ASPACK = 0x0D, DELS = 0x14, REPLY = 0x16 };

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
    int sample = -1;                    // SDATA: the sample's number
    std::vector<std::string> sampleNames;   // SLIST / CAT: the samples' names, in order
    std::vector<int> sampleNumbers;     // CAT: each sample's number
};
bool parse (const uint8_t* msg, int length, Reply&);   // false: not an S1000 message (or broken)

// ---- samples over MIDI (v0.74) -------------------------------------------------------------------------------
// A sample as it travels: 16-bit words (mono), its rate, its loop (loopLength < 4: none) and its name.
struct Sample
{
    std::string name;
    std::vector<int16_t> data;
    int rate = 44100;
    int loopStart = 0, loopLength = 0;
    bool pingPong = false;
};

// The MIDI Sample Dump Standard (MIDI 1.0 Addendum): the S1000 sends and takes sample data as its packets.
// A packet is F0 7E cc 02 kk, 120 data bytes, a checksum (the XOR of 7E cc 02 kk and the data), F7. A 16-bit word
// takes three bytes, left-justified (40 words a packet); unsigned (0 = the most negative). The receiver answers each
// packet ACK (send the next), NAK (send it again), WAIT (hold on until an ACK) or CANCEL.
namespace sds
{
    enum Handshake : uint8_t { WAIT = 0x7C, CANCEL = 0x7D, NAK = 0x7E, ACK = 0x7F };
    constexpr int kWordsPerPacket = 40;   // (16-bit words)
    std::vector<uint8_t> packet (int channel, int number, const int16_t* words, int count);   // count <= 40 (padded)
    std::vector<uint8_t> handshake (int channel, Handshake, int packet);
    bool parseHandshake (const uint8_t* msg, int n, int& kind, int& packet);
    // a data packet: its number, and its words appended to `out` (bits: the dump's word size); false if broken
    bool parsePacket (const uint8_t* msg, int n, int bits, int& number, std::vector<int16_t>& out);
}

// the S1000's sample header (SDATA, about 150 bytes: Akai's layout as transcribed for lakai)
constexpr int kSampleHeaderBytes = 150;
std::vector<uint8_t> sampleHeader (const Sample&);           // a new sample's header: name, length, rate, loop 1
bool readSampleHeader (const std::vector<uint8_t>& block, Sample& meta, int& length);   // name, rate, loop; its length
std::vector<uint8_t> requestSamples (int cc);                // RSLIST
std::vector<uint8_t> requestSampleHeader (int cc, int sample);
std::vector<uint8_t> sampleHeaderMessage (int cc, int sample, const std::vector<uint8_t>& block);
std::vector<uint8_t> acceptPackets (int cc, int sample, int offset, int count);    // ASPACK: "I'm sending these"
std::vector<uint8_t> requestPackets (int cc, int sample, int offset, int count);   // RSPACK: "send me these"
std::vector<std::vector<uint8_t>> samplePackets (int cc, const std::vector<int16_t>& data);   // the whole sample as SDS packets
double secondsOnTheWire (bool s950, int words);              // (how long it takes: MIDI moves 3125 bytes a second)

// (tests: an S1000 that answers - its programs and keygroups as blocks)
struct FakeS1000
{
    std::vector<std::string> programNames;
    std::vector<std::vector<uint8_t>> programs;                // 72-byte blocks
    std::vector<std::vector<std::vector<uint8_t>>> keygroups;  // per program, 149-byte blocks
    std::vector<Sample> samples;
    int takingSample = -1, takingAt = 0, takingCount = 0;    // (ASPACK: the packets on their way)
    int nakNext = 0;                                          // (tests: answer that many packets NAK first)
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
    // samples (v0.74): the S900 / S950 speak the sample dump of the Prophet 2000 - from before the standard: no
    // channel, and the whole dump is ONE message (F0 7E 01, the header, then blocks of 60 12-bit words, then F7).
    // Its name, tuning and loop type follow in an Akai 'sample parameters' (SPRM) message. 'SECRE' first switches
    // the sampler's reception of the dump on.
    enum SampleOp : uint8_t { RSPRM = 4, SECRE = 5, SECRD = 6, SPRM = 10 };
    std::vector<uint8_t> receptionOn (int channel);                       // SECRE
    std::vector<uint8_t> sampleDump (int number, const Sample&);          // the dump (12-bit)
    std::vector<uint8_t> sampleParams (int channel, int number, const Sample&);
    std::vector<uint8_t> requestDump (int number);                        // RSD
    std::vector<uint8_t> ack();                                           // ACKS (no channel)
    bool parseDump (const uint8_t* msg, int n, Sample&, int& number, int& badBlocks);
    constexpr int kMaxWords = 475020, kMinWords = 200;
    bool parse (const uint8_t* msg, int length, Reply&);   // CAT: the programs; PRGM: block + keygroups
    std::string name (const std::vector<uint8_t>& block, int offset);   // a DB* name (10 characters)
    void setName (std::vector<uint8_t>& block, int offset, const std::string&);
    constexpr int kSoftName = 48, kLoudName = 92, kProgramName = 0;      // (offsets in their blocks)

    struct FakeS950   // (tests)
    {
        struct Prog { int number; std::vector<uint8_t> block; std::vector<std::vector<uint8_t>> keygroups; };
        std::vector<Prog> programs;
        std::vector<std::string> samples;
        std::vector<Sample> sampleData;    // (what came over MIDI, by number)
        bool receptionEnabled = false;
        void addProgram (int number, const std::string& name, int keygroups);
        std::vector<std::vector<uint8_t>> receive (const uint8_t* msg, int length);
    };
}

// ---- a sample going to (or coming from) the sampler, step by step. The page feeds it what comes in and sends
// what poll() hands it; times are in ms (the page's clock, or a test's).
//   S1000, sending: the sample list (so the new one gets the next number and a name of its own), the header
//     (SDATA: it makes the sample), ASPACK, then the SDS packets - each one waits for its ACK (NAK: again; WAIT:
//     hold on). If no answers come at all, it carries on without them ("open loop"), a packet at a time at MIDI's pace.
//   S1000, getting: the header (RSDATA), then RSPACK and its packets, each answered ACK (or NAK if damaged).
//   S950, sending: SECRE and the catalogue (for the next number), the dump - one message - then, once it's had time
//     to take that in, its name and loop (SPRM), and the catalogue again to see it there.
//   S950, getting: SECRE and the dump request; ACKs go out while it comes, in case it waits for them.
class Transfer
{
public:
    enum class State { Idle, Working, Done, Failed };
    void sendS1000 (int cc, const Sample&, double now);
    void getS1000 (int cc, int sample, double now);
    void sendS950 (int channel, const Sample&, double now);
    void getS950 (int channel, int number, const std::string& name, double now);
    void received (const uint8_t* msg, int n, double now);
    std::vector<std::vector<uint8_t>> poll (double now);   // what to send now
    void cancel();

    State state = State::Idle;
    std::string status;            // (for the page's info line)
    float progress = 0;            // 0 - 1
    Sample result;                 // (getting: the sample; sending: what went)
    int number = -1;               // the sample's number in the sampler
    bool handshaking = false;      // (an answer came back to a packet: closed loop)
    int resent = 0;                // (packets sent again after a NAK)

private:
    enum class Step { None, AskList, Header, Accept, Packets, Dump, Params, Confirm, AskHeader, Packets2, AwaitDump };
    Step step = Step::None;
    bool s950 = false, sending = false;
    int ch = 0;
    std::vector<std::vector<uint8_t>> out, packets;
    int next = 0, tries = 0;
    double sentAt = 0, startedAt = 0, ackAt = 0, wireMs = 0;
    bool waitingAnswer = false, held = false;
    int expected = 0;              // (getting: the words to come)
    std::string wantName;
    void fail (const std::string&);
    void finish (const std::string&);
    void sendPacket (double now);
};
} // namespace prv::akai
