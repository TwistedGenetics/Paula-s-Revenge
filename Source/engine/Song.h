// Paula's Revenge - song data model
// Plain C++ (no JUCE) so the engine can be tested on its own.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "MixerTypes.h"

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

// Rate (Hz) a sample plays at on C-3 in the Amiga convention
constexpr double kAmigaC3Rate = 16574.27;

// Effect commands: 0-F are the ProTracker set; G-Z (16..35) are extended.
constexpr int kNumCommands = 36;
inline char commandChar (int c) { return c < 10 ? (char) ('0' + c) : (char) ('A' + c - 10); }
inline int  commandFromChar (char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'z') ch = (char) (ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    return -1;
}
constexpr uint8_t cmdLetter (char c) { return (uint8_t) (c - 'A' + 10); }

struct Cell
{
    uint8_t note   = 0;   // 0 none, 1..96, kNoteOff
    uint8_t instr  = 0;   // 0 none, 1..63
    uint8_t cmd    = 0;   // 0..35 (0-F ProTracker, G-Z extended)
    uint8_t param  = 0;   // 0x00 .. 0xFF
    uint8_t cmd2   = 0;   // second effect column
    uint8_t param2 = 0;

    bool fx1Empty() const { return cmd == 0 && param == 0; }
    bool fx2Empty() const { return cmd2 == 0 && param2 == 0; }
    bool isEmpty() const { return note == 0 && instr == 0 && fx1Empty() && fx2Empty(); }
    bool operator== (const Cell& o) const { return note == o.note && instr == o.instr && cmd == o.cmd && param == o.param && cmd2 == o.cmd2 && param2 == o.param2; }
    // looks for a command in either column
    bool has (uint8_t c, uint8_t& p) const
    {
        if (cmd == c && ! (c == 0 && param == 0)) { p = param; return true; }
        if (cmd2 == c && ! (c == 0 && param2 == 0)) { p = param2; return true; }
        return false;
    }
};

struct Block
{
    int lines = kDefaultLines;
    std::vector<std::array<Cell, kMaxTracks>> rows = std::vector<std::array<Cell, kMaxTracks>> (kMaxLines);

    Cell&       at (int line, int track)       { return rows[(size_t) line][(size_t) track]; }
    const Cell& at (int line, int track) const { return rows[(size_t) line][(size_t) track]; }
    void clear() { for (auto& r : rows) r.fill (Cell{}); }
};

// Times in seconds, sustain 0..1
struct Envelope
{
    bool  on = false;
    float attack = 0.005f, decay = 0.3f, sustain = 1.0f, release = 0.2f;
    bool operator== (const Envelope& o) const { return on == o.on && attack == o.attack && decay == o.decay && sustain == o.sustain && release == o.release; }
};

enum class FilterType : uint8_t { Off = 0, LowPass, HighPass, BandPass };

// Tracker note -> MIDI note (C-3 = 60)
inline int midiNoteFor (int note) { return std::clamp (note + 23, 0, 127); }

struct Instrument
{
    std::string name;
    std::vector<int16_t> data;   // left (or mono), nominally 16574 Hz at C-3 (Amiga convention)
    std::vector<int16_t> dataR;  // right channel; empty for mono samples
    int loopStart  = 0;          // in samples
    int loopLength = 0;          // in samples, < 4 = no loop
    bool pingPong  = false;      // loop back and forth instead of jumping to the start
    int volume     = 64;         // 0..64
    int finetune   = 0;          // -8..7 (1/8 semitone steps)
    int transpose  = 0;          // semitones
    double rate    = kAmigaC3Rate; // Hz the sample plays at on C-3
    double natRate = 0;            // rate at which it sounds as recorded (0 = same as rate); RATE changes move away from it
    double pitchShift() const { return natRate > 0 ? 12.0 * std::log2 (rate / natRate) : 0.0; }   // semitones

    // sound shaping (all neutral by default, so MOD/MED/old songs sound as before)
    Envelope   volEnv;                      // when off, notes play at full level and OFF cuts
    FilterType filter      = FilterType::Off;
    float      cutoff      = 20000.0f;      // Hz
    float      resonance   = 0.0f;          // 0..1
    Envelope   filterEnv { true, 0.005f, 0.3f, 0.0f, 0.2f };
    float      filterEnvAmount = 0.0f;      // octaves, -8..+8
    float      keyTrack    = 0.0f;          // 0..1 (1 = cutoff follows the note fully)
    bool       amigaPan    = true;          // follow the Amiga L/R track layout
    int        pan         = 128;           // 0..255 when amigaPan is off

    // slicing: start points of each slice (sorted, first is 0). Empty = no slices.
    std::vector<int> slices;
    bool       sliceKeys   = false;         // notes play slices: C-3 = slice 1, C#3 = slice 2...
    int  numSlices() const { return (int) slices.size(); }
    // start/end of slice i (end exclusive); false if there's no such slice
    bool sliceRange (int i, int& start, int& end) const
    {
        if (i < 0 || i >= (int) slices.size()) return false;
        start = std::clamp (slices[(size_t) i], 0, (int) data.size());
        end = i + 1 < (int) slices.size() ? std::clamp (slices[(size_t) i + 1], start, (int) data.size()) : (int) data.size();
        return end > start;
    }

    bool hasShaping() const { return volEnv.on || filter != FilterType::Off || ! amigaPan; }

    // a VST3 / AU instrument instead of a sample
    bool plugin = false;
    std::string pluginKey, pluginName;
    std::vector<uint8_t> pluginState;
    int  pluginOutput = -1;                 // the track whose mixer strip it plays into; -1 = AUTO
    int  bendRange = 2;                     // semitones, to match the synth's pitch bend range
    AutoTarget knobs[kInstrumentKnobs];     // its own knobs (L, Q, H, W); target is this instrument
    bool chord = false;                     // 0xy on a note's line plays a held chord (plug-in / MIDI out)
    bool isPlugin() const { return plugin; }   // (also true for MIDI out: both play through the synth rack)

    // MIDI out to hardware instead of a sample (plugin is true as well; pluginKey = "MIDI:" + device)
    bool midiOut = false;
    std::string midiDevice;                 // the MIDI output's name
    int  midiChannel = 1;                   // 1..16
    int  midiDelayMs = 0;                   // -50..+200: later MIDI, or (negative) later audio
    int  midiCC[kInstrumentKnobs] { 74, 71, 73, 10 };   // the CCs knobs L, Q, H, W send
    bool midiClock = false;                 // send MIDI clock and start / stop to the device
    bool isMidiOut() const { return plugin && midiOut; }

    // any instrument can be switched off (the ON button): its notes are silent
    bool switchedOff = false;

    bool hasData() const { return ! data.empty(); }
    bool isStereo() const { return ! dataR.empty() && dataR.size() == data.size(); }
    bool isLooped() const { return loopLength >= 4 && loopStart >= 0 && loopStart + loopLength <= (int) data.size(); }
    void clear() { *this = Instrument{}; }
};

// A labelled divider in the song order: the section starts at sequence position `pos`
struct SongSection
{
    int pos = 0;
    std::string name;
};

struct Song
{
    std::string name = "UNTITLED";
    int numTracks = 8;                 // 4 or 8 typically
    int tempo     = 125;               // BPM (32..255)
    int speed     = 6;                 // ticks per line (1..31)
    bool ledFilterAtStart = false;
    int  trackPan[kMaxTracks] = { -1, -1, -1, -1, -1, -1, -1, -1 };  // -1 = automatic, else 0..255 (128 = centre)
    std::vector<Block> blocks;
    std::vector<int> sequence;         // block indices
    std::vector<SongSection> sections; // sorted by pos, at most one per position
    MixerSettings mixer;               // flat by default: plays exactly as without a mixer
    std::array<Instrument, kMaxInstruments> instruments;

    Song() { reset(); }

    void reset()
    {
        name = "UNTITLED";
        numTracks = 8; tempo = 125; speed = 6; ledFilterAtStart = false;
        for (auto& p : trackPan) p = -1;
        blocks.assign (1, Block{});
        sequence.assign (1, 0);
        sections.clear();
        mixer = MixerSettings {};
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
