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
#include "Chords.h"

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

// Effect commands: 0-F are the ProTracker set; G-Z (16..35) are extended; 36 is # (CHORD).
constexpr int kNumCommands = 37;
constexpr uint8_t kCmdChord = 36;   // #xy: plays chord slot xy from the song's chord table
inline char commandChar (int c) { return c == kCmdChord ? '#' : c < 10 ? (char) ('0' + c) : (char) ('A' + c - 10); }
inline int  commandFromChar (char ch)
{
    if (ch == '#') return kCmdChord;
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
    uint8_t cmd    = 0;   // 0..36 (0-F ProTracker, G-Z extended, # chord)
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
    std::string name;                  // shown in the sequence list (empty = none)
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

static_assert (kMaxInstruments == kMixInstruments, "each instrument has its own mixer inserts");

struct Instrument
{
    std::string name;
    std::vector<int16_t> data;   // left (or mono), nominally 16574 Hz at C-3 (Amiga convention)
    std::vector<int16_t> dataR;  // right channel; empty for mono samples
    int loopStart  = 0;          // in samples
    int loopLength = 0;          // in samples, < 4 = no loop
    int playStart  = 0;          // START / END: the part of the sample that plays (not destructive);
    int playEnd    = 0;          //   playEnd 0 = to the end. The loop is cut at END.
    bool pingPong  = false;      // loop back and forth instead of jumping to the start
    int volume     = 64;         // 0..64
    int finetune   = 0;          // -8..7 (1/8 semitone steps)
    int transpose  = 0;          // semitones
    int cents      = 0;          // FINE on PARAMS: -100..+100 cents (on top of finetune)
    int syncLines  = 0;          // BEAT SYNC: the sample (START to END) lasts this many lines at C-3, repitched (0 = off)
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
    bool chord = false;                     // (v0.13 songs: 0xy played chords; converted to #xy on loading)
    bool isPlugin() const { return plugin; }   // (also true for MIDI out: both play through the synth rack)

    // MIDI out to hardware instead of a sample (plugin is true as well; pluginKey = "MIDI:" + device)
    bool midiOut = false;
    std::string midiDevice;                 // the MIDI output's name
    int  midiChannel = 1;                   // 1..16
    int  midiDelayMs = 0;                   // -50..+200: later MIDI, or (negative) later audio
    int  midiCC[kInstrumentKnobs] { 74, 71, 73, 10 };   // the CCs knobs L, Q, H, W send
    bool midiClock = false;                 // send MIDI clock and start / stop to the device
    bool isMidiOut() const { return plugin && midiOut; }

    // CV OUT to a modular (a DC-coupled interface like the Expert Sleepers ES-9): pitch (1 V/oct,
    // through the calibration map when there is one), gate, velocity and a mod voltage on the
    // interface's outputs (1-16; 1-2 are the music). plugin is true as well; pluginKey = "CV:"
    bool cvOut = false;
    int  cvPitchOut = 3, cvGateOut = 4, cvVelOut = 0, cvModOut = 0;   // 0 = not used
    float cvFullScale = 10.0f;              // the volts the interface puts out at full scale (ES-9: 10)
    float cvGateVolts = 5.0f;               // a gate's (and a trigger's) level
    int  cvOctave = 0;                      // -4..+4: the notes an octave or more up / down
    std::vector<float> cvMapNotes, cvMapVolts;   // AUTO-TUNE: measured (pitch as a MIDI note, volts), sorted; empty = 1 V/oct, C-3 = 0 V
    std::string cvMapName;                  // the synth it was measured on
    bool isCvOut() const { return plugin && cvOut; }

    // UTILITY (samples and VST3 / AU instruments): stereo WIDTH and MONO BELOW, before the instrument's
    // inserts. Its VOLUME is the instrument's VOL itself. At the defaults it does nothing.
    float utilWidth = 1.0f;                 // 0 (mono) .. 2 (200%)
    int   utilMonoHz = 0;                   // 0 = off, else 20..500: everything below it mono
    bool  utilSteep = false;                // MONO BELOW at 48 dB/oct (LR8) instead of 24 (LR4): the lows fully mono sooner
    bool  hasUtility() const { return ! isMidiOut() && ! isCvOut() && (utilWidth != 1.0f || utilMonoHz > 0); }
    // the instrument's own EQ (up to 8 points, as a mixer channel's), after the UTILITY, before its inserts
    ChannelEq eq;
    bool  hasEq() const { return ! isMidiOut() && ! isCvOut() && eq.anyOn(); }
    // a VST3 / AU instrument's filter and envelopes work on its output: the envelopes start with each
    // note of it and release when its last note ends
    bool  shapesSynth() const { return isPlugin() && ! isMidiOut() && ! isCvOut() && (volEnv.on || filter != FilterType::Off); }

    // any instrument can be switched off (the ON button): its notes are silent
    bool switchedOff = false;

    bool hasData() const { return ! data.empty(); }
    int  rangeStart() const { return std::clamp (playStart, 0, std::max (0, (int) data.size() - 1)); }
    int  rangeEnd() const   { const int n = (int) data.size(); return playEnd > rangeStart() && playEnd < n ? playEnd : n; }
    bool hasRange() const   { return rangeStart() > 0 || rangeEnd() < (int) data.size(); }
    bool isStereo() const { return ! dataR.empty() && dataR.size() == data.size(); }
    bool isLooped() const { return loopLength >= 4 && loopStart >= 0 && loopStart + loopLength <= (int) data.size(); }
    void clear() { *this = Instrument{}; }
};

// CV SOURCES: voltages the song sends to a modular on the interface's outputs (ES-9 style)
constexpr int kCvSources = 8;
constexpr int kCvOutputs = 16;   // the interface's outputs (1-2 are the music)
enum class CvKind : uint8_t { Off = 0, Clock, Reset, Run, Lfo, Env, Knob, Count };
struct CvSource
{
    CvKind kind = CvKind::Off;
    int   out = 0;                 // the output, 1..16 (0 = none)
    float volts = 5.0f;            // CLOCK / RESET / RUN: the level; LFO: depth; ENV: the peak; KNOB: the top
    float low = 0.0f;              // LFO: centre; KNOB: the bottom
    int   ppqn = 4;                // CLOCK: pulses a beat (1-24)
    int   shape = 0;               // LFO: 0 sine, 1 triangle, 2 saw, 3 square, 4 random
    float beats = 4.0f;            // LFO: beats a cycle (0.25-64)
    int   track = 0;               // ENV: the track whose notes trigger it; KNOB: the track whose AUTO A / B sets it
    int   knob = 0;                // KNOB: 0 = AUTO A (Vxx), 1 = AUTO B (Jxx)
    float attack = 0.01f, decay = 0.2f, sustain = 0.5f, release = 0.3f;   // ENV (seconds; sustain 0..1)
    bool operator== (const CvSource& o) const
    {
        return kind == o.kind && out == o.out && volts == o.volts && low == o.low && ppqn == o.ppqn && shape == o.shape && beats == o.beats
            && track == o.track && knob == o.knob && attack == o.attack && decay == o.decay && sustain == o.sustain && release == o.release;
    }
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
    CvSource cvSources[kCvSources];    // CV SOURCES (all off by default)
    float cvFullScale = 10.0f;         // the volts at full scale for the CV SOURCES (ES-9: 10)
    int   cvDelayMs = 0;               // CV DELAY: -100..+100 ms. Positive: the voltages later; negative: the music (outputs 1-2) later
    uint8_t grooveKind = 0;            // GROOVE: a GrooveKind (0 = off): some lines' notes a little late, as it plays
    int   grooveAmount = 50;           // 0..100 %
    std::array<Instrument, kMaxInstruments> instruments;
    ChordTable chords;                 // 0xy on a CHORD instrument's note plays slot xy; saved in the song
    bool chordsLegacy = false;         // (a v0.11 song: 0xy = two intervals, x and y semitones up)
    bool chordsSaved = true;           // (loading: false when the file had no chord table - it gets the global set)

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
        for (auto& c : cvSources) c = CvSource {};
        cvFullScale = 10.0f;
        cvDelayMs = 0;
        grooveKind = 0; grooveAmount = 50;
        for (auto& i : instruments) i.clear();
        chords = defaultChordTable();
        chordsLegacy = false;
        chordsSaved = true;
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
