// Paula's Revenge - song data model
// Plain C++ (no JUCE) so the engine can be tested on its own.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace prv
{
constexpr int kMaxTracks      = 8;
constexpr int kMaxInstruments = 63;
constexpr int kMaxLines       = 256;
constexpr int kMaxBlocks      = 256;
constexpr int kMaxSequence    = 256;
constexpr int kDefaultLines   = 64;

// Note values: 0 = empty, 1..96 = C-0 .. B-7, kNoteOff = key off
constexpr uint8_t kNoteNone = 0;
constexpr uint8_t kNoteMax  = 96;
constexpr uint8_t kNoteOff  = 127;

// Amiga PAL Paula clock (Hz). Sample rate of a note = clock / period.
constexpr double kPaulaClock = 3546895.0;
constexpr double kPi = 3.14159265358979323846;

struct Cell
{
    uint8_t note  = 0;   // 0 none, 1..96, kNoteOff
    uint8_t instr = 0;   // 0 none, 1..63
    uint8_t cmd   = 0;   // 0x0 .. 0xF
    uint8_t param = 0;   // 0x00 .. 0xFF

    bool isEmpty() const { return note == 0 && instr == 0 && cmd == 0 && param == 0; }
    bool operator== (const Cell& o) const { return note == o.note && instr == o.instr && cmd == o.cmd && param == o.param; }
};

struct Block
{
    int lines = kDefaultLines;
    std::vector<std::array<Cell, kMaxTracks>> rows = std::vector<std::array<Cell, kMaxTracks>> (kMaxLines);

    Cell&       at (int line, int track)       { return rows[(size_t) line][(size_t) track]; }
    const Cell& at (int line, int track) const { return rows[(size_t) line][(size_t) track]; }
    void clear() { for (auto& r : rows) r.fill (Cell{}); }
};

struct Instrument
{
    std::string name;
    std::vector<int16_t> data;   // left (or mono), nominally 16574 Hz at C-3 (Amiga convention)
    std::vector<int16_t> dataR;  // right channel; empty for mono samples
    int loopStart  = 0;          // in samples
    int loopLength = 0;          // in samples, < 4 = no loop
    int volume     = 64;         // 0..64
    int finetune   = 0;          // -8..7 (1/8 semitone steps)
    int transpose  = 0;          // semitones

    bool hasData() const { return ! data.empty(); }
    bool isStereo() const { return ! dataR.empty() && dataR.size() == data.size(); }
    bool isLooped() const { return loopLength >= 4 && loopStart >= 0 && loopStart + loopLength <= (int) data.size(); }
    void clear() { *this = Instrument{}; }
};

struct Song
{
    std::string name = "UNTITLED";
    int numTracks = 8;                 // 4 or 8 typically
    int tempo     = 125;               // BPM (32..255)
    int speed     = 6;                 // ticks per line (1..31)
    bool ledFilterAtStart = false;
    std::vector<Block> blocks;
    std::vector<int> sequence;         // block indices
    std::array<Instrument, kMaxInstruments> instruments;

    Song() { reset(); }

    void reset()
    {
        name = "UNTITLED";
        numTracks = 8; tempo = 125; speed = 6; ledFilterAtStart = false;
        blocks.assign (1, Block{});
        sequence.assign (1, 0);
        for (auto& i : instruments) i.clear();
    }

    int safeBlock (int b) const { return std::clamp (b, 0, (int) blocks.size() - 1); }
};

// ---- Pitch helpers -------------------------------------------------------
// Period of C-1 (finetune 0) is 856, the classic Amiga value. Notes are
// spaced on an equal-tempered grid; finetune is in 1/8 semitone steps.
inline double periodFor (int note, int finetune)
{
    const double semis = (double) (note - 13) + finetune / 8.0;
    return 856.0 * std::pow (2.0, -semis / 12.0);
}

inline int periodForInt (int note, int finetune)
{
    return (int) std::lround (periodFor (note, finetune));
}

inline int noteForPeriod (int period)
{
    if (period <= 0) return 0;
    int best = 0; double bestDiff = 1e9;
    for (int n = 1; n <= kNoteMax; ++n)
    {
        const double d = std::abs (periodFor (n, 0) - period);
        if (d < bestDiff) { bestDiff = d; best = n; }
    }
    return best;
}

inline std::string noteName (uint8_t note)
{
    if (note == kNoteNone) return "---";
    if (note == kNoteOff)  return "OFF";
    static const char* names[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    const int n = note - 1;
    return std::string (names[n % 12]) + std::to_string (n / 12);
}

} // namespace prv
