#pragma once
/*
    Model — the plain data Break Chopper is made of.

    Everything here is fixed-size POD (no heap) so the message thread can hand a
    whole slot's program to the audio thread with one memcpy under a try-lock.
    The audio itself (SlotContent) is immutable and reference counted.
*/
#include "choppage/Sample.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bc
{
    constexpr int kSlots     = 8;
    constexpr int kMaxSlices = SliceTable::kMax;   // 64
    constexpr int kMaxSteps  = 64;                 // 4 bars of 16ths
    constexpr int kFxPads    = 8;
    constexpr int kMemories  = 16;

    // MIDI note map (C1 = 36, as in Ableton and MPC)
    constexpr int kFirstBreakNote  = 36;   // C1  .. G1   : break slots 1-8
    constexpr int kFirstFxNote     = 44;   // G#1 .. D#2  : FX pads 1-8
    constexpr int kFirstSliceNote  = 52;   // E2  .. G3   : slices 1-16 of the playing break
    constexpr int kFirstMemoryNote = 68;   // G#3 .. B4   : pattern memories 1-16

    // MIDI CCs for a host that sequences CHOPPAGE closely (Paula's Revenge's Sxx / 9xx; 0.17.2), any channel:
    //   CC 102 = v : play slice v + 1 (0-63) of the break played last, now (as a slice note, past slice 16)
    //   CC 103 + p = v : FX pad p's (0-7) setting for its next press by note, until that note lets go; v 0 = its own knob.
    //     STUTTER / ROLL: v 1-4 = 1/8 1/16 1/32 1/64 · TAPE STOP: v 1-127 = 0.1 - 3 s · HALF-TIME: v 1-5 = its 5 modes
    //     AKAIZE: v 1-4 = stretch 1.5 2 3 4 · FILT DIVE: v 1-127 = resonance · S950 DROP: v 1-127 = 4 - 15.6 kHz
    constexpr int kCcPlaySlice = 102;
    constexpr int kCcPadSetting = 103;

    // What a note does. Map 0 (CHROMATIC, above). Map 1 (C MAJOR, white keys only):
    //   breaks 1-8 C0 D0 E0 F0 G0 A0 B0 C1 (24-36), FX pads 1-8 D1 .. D2 (38-50),
    //   pattern memories 1-12 E2 .. B3 (52-71), slices from C4 (72) upwards (C4 = slice 1, D4 = 2 ...).
    enum class NoteRole { none, breakSlot, fxPad, slice, memory };
    struct NoteUse { NoteRole role = NoteRole::none; int index = 0; };
    inline NoteUse noteUse (int note, int map) noexcept
    {
        if (map == 0)
        {
            if (note >= kFirstBreakNote && note < kFirstBreakNote + kSlots)    return { NoteRole::breakSlot, note - kFirstBreakNote };
            if (note >= kFirstFxNote && note < kFirstFxNote + kFxPads)         return { NoteRole::fxPad, note - kFirstFxNote };
            if (note >= kFirstSliceNote && note < kFirstSliceNote + 16)        return { NoteRole::slice, note - kFirstSliceNote };
            if (note >= kFirstMemoryNote && note < kFirstMemoryNote + kMemories) return { NoteRole::memory, note - kFirstMemoryNote };
            return {};
        }
        static constexpr int white[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
        if (note < 0 || note > 127 || white[note % 12] < 0) return {};
        const int w = (note / 12) * 7 + white[note % 12];        // white keys counted from C-1 (note 0)
        if (w >= 14 && w <= 21) return { NoteRole::breakSlot, w - 14 };     // C0 .. C1
        if (w >= 22 && w <= 29) return { NoteRole::fxPad, w - 22 };         // D1 .. D2
        if (w >= 30 && w <= 41) return { NoteRole::memory, w - 30 };        // E2 .. B3
        if (w >= 42 && w - 42 < kMaxSlices) return { NoteRole::slice, w - 42 };   // C4 ..
        return {};
    }

    /** What a slice sounds like, as the classifier hears it. */
    enum class Badge : uint8_t { kick, snare, ghost, hat, open, other };   // other = PERC (P); saved files may say X
    constexpr int kNumBadges = 6;
    inline const char* badgeLetter (Badge b) noexcept
    {
        switch (b) { case Badge::kick: return "K"; case Badge::snare: return "S"; case Badge::ghost: return "G";
                     case Badge::hat: return "H"; case Badge::open: return "O"; case Badge::other: return "P"; }
        return "P";
    }
    /** What the screens show: K S GS H O P (saved files keep the one-letter badgeLetter). */
    inline const char* badgeLabel (Badge b) noexcept
    {
        switch (b) { case Badge::kick: return "K"; case Badge::snare: return "S"; case Badge::ghost: return "GS";
                     case Badge::hat: return "H"; case Badge::open: return "O"; case Badge::other: return "P"; }
        return "P";
    }
    inline const char* badgeName (Badge b) noexcept
    {
        switch (b) { case Badge::kick: return "KICK"; case Badge::snare: return "SNARE"; case Badge::ghost: return "GHOST";
                     case Badge::hat: return "HAT"; case Badge::open: return "OPEN"; case Badge::other: return "PERC"; }
        return "PERC";
    }

    /** The loaded break: audio + where it is cut + what each cut is. Immutable. */
    struct SlotContent
    {
        SamplePtr buffer;
        SliceTable slices;
        std::array<Badge, kMaxSlices> badges {};
        double bpm  = 170.0;    // the break's own tempo
        int    bars = 2;        // how long the loop is musically
        std::string name;

        int numSlices() const noexcept { return std::max (1, slices.count); }
        Range region (int slice) const noexcept
        {
            return slices.region (slice, buffer != nullptr ? buffer->numSamples : 0);
        }
    };
    using ContentPtr = std::shared_ptr<const SlotContent>;

    /** Per-slice settings (SLICE row of the SAMPLE tab). */
    struct SliceParams
    {
        float pitch   = 0.0f;   // semitones, -24..24
        float gate    = 1.0f;   // 0.05..1 of the time until the next hit
        float stretch = 1.0f;   // 0.5..4, cyclic (Akai) time stretch
        bool  reverse = false;
        int   stutter = 0;      // 0 = off, else repeats (2, 3, 4, 6, 8)
        float trans   = 0.5f;   // 0 = tight, 0.5 = as is, 1 = more sustain
        float xfadeMs = 2.0f;   // crossfade at the join, 0.5..20 ms
        bool  mute    = false;
        float level   = 1.0f;
    };

    /** ALL row: offsets / multipliers applied on top of every slice. */
    struct GlobalSliceParams
    {
        float pitch   = 0.0f;
        float gate    = 1.0f;
        float stretch = 1.0f;
        bool  reverse = false;
        int   stutter = 0;
        float trans   = 0.5f;
        float xfadeMs = 2.0f;
    };

    enum class FxType : uint8_t { none, stutter, reverse, stretch, tapeStop, roll };   // (roll: 0.17.1, appended)
    constexpr int kNumFxTypes = 6;
    inline const char* fxTypeName (FxType t) noexcept
    {
        switch (t) { case FxType::stutter: return "STUTTER"; case FxType::reverse: return "REVERSE";
                     case FxType::stretch: return "STRETCH"; case FxType::tapeStop: return "TAPE STOP"; case FxType::roll: return "ROLL";
                     case FxType::none: return ""; }
        return "";
    }

    struct FxEvent
    {
        FxType type = FxType::none;
        uint8_t rate = 16;      // 8, 16, 32, 64 (stutter repeat size as a note value)
    };

    /** 0.17: trig conditions, as Elektron's sequencers have them, on a step's note (NOTE PROB) and its FX (FX PROB).
        0 = ALWAYS (none). Then FILL, NOT FILL (plays only while / unless FILL is on), PRE, NOT PRE (the last condition
        evaluated on the same row was true / false), NEI, NOT NEI (the same, on the other row: the note row's
        neighbour is the FX row and the other way round), 1ST, NOT 1ST (the first time round the pattern / every
        time but the first), 21 probabilities (1% .. 99%, Elektron's steps), and A:B (true on the A-th time round
        out of every B, B 2 .. 8). PRE and NEI conditions don't count as "evaluated" for the next PRE / NEI. */
    namespace cond
    {
        enum : uint8_t { always = 0, fill, notFill, pre, notPre, nei, notNei, first, notFirst, percentBase };
        inline const uint8_t* percents() noexcept
        {
            static const uint8_t p[] = { 1, 2, 4, 6, 9, 13, 19, 25, 33, 41, 50, 59, 67, 75, 81, 87, 91, 94, 96, 98, 99 };
            return p;
        }
        constexpr int kPercents = 21;
        constexpr int abBase = percentBase + kPercents;            // 30: 1:2 2:2 1:3 2:3 3:3 1:4 ... 8:8
        constexpr int kCount = abBase + 35;                        // 65 settings
        inline bool isPercent (int c) noexcept { return c >= percentBase && c < abBase; }
        inline bool isAB (int c) noexcept { return c >= abBase && c < kCount; }
        inline void abOf (int c, int& a, int& b) noexcept
        {
            int k = c - abBase;
            for (b = 2; b <= 8; ++b) { if (k < b) { a = k + 1; return; } k -= b; }
            a = 1; b = 2;
        }
        inline std::string name (int c)
        {
            static const char* fixed[] = { "ALWAYS", "FILL", "NOT FILL", "PRE", "NOT PRE", "NEI", "NOT NEI", "1ST", "NOT 1ST" };
            if (c <= 0 || c >= kCount) return "ALWAYS";
            if (c < percentBase) return fixed[c];
            if (isPercent (c)) return std::to_string (percents()[c - percentBase]) + "%";
            int a = 1, b = 2; abOf (c, a, b);
            return std::to_string (a) + ":" + std::to_string (b);
        }
        /** Short, for a grid cell: "", "F", "!F", "P", "!P", "N", "!N", "1", "!1", "50", "1:2". */
        inline std::string shortName (int c)
        {
            static const char* fixed[] = { "", "F", "!F", "P", "!P", "N", "!N", "1", "!1" };
            if (c <= 0 || c >= kCount) return "";
            if (c < percentBase) return fixed[c];
            return name (c).substr (0, isPercent (c) ? name (c).size() - 1 : std::string::npos);
        }
        inline std::vector<std::string> names()
        {
            std::vector<std::string> v;
            for (int c = 0; c < kCount; ++c) v.push_back (name (c));
            return v;
        }
    }

    /** One step sequence: which slice plays on each 16th, plus the FX lane. */
    struct Pattern
    {
        int numSteps = 32;
        std::array<int16_t, kMaxSteps> slice {};     // -1 = rest
        std::array<FxEvent, kMaxSteps> fx {};
        std::array<bool, kMaxSteps>    locked {};
        std::array<uint8_t, kMaxSteps> noteCond {};  // 0.17: NOTE PROB (a cond:: value, 0 = ALWAYS)
        std::array<uint8_t, kMaxSteps> fxCond {};    // 0.17: FX PROB

        Pattern() { slice.fill (-1); }
        bool operator== (const Pattern& o) const noexcept
        {
            if (numSteps != o.numSteps) return false;
            for (int i = 0; i < numSteps; ++i)
                if (slice[(size_t) i] != o.slice[(size_t) i] || fx[(size_t) i].type != o.fx[(size_t) i].type
                     || fx[(size_t) i].rate != o.fx[(size_t) i].rate
                     || noteCond[(size_t) i] != o.noteCond[(size_t) i] || fxCond[(size_t) i] != o.fxCond[(size_t) i])
                    return false;
            return true;
        }
        int hits() const noexcept { int n = 0; for (int i = 0; i < numSteps; ++i) n += slice[(size_t) i] >= 0; return n; }
    };

    /** Everything the audio thread needs to play one slot apart from the audio. */
    constexpr int kMuteGroups = 4;

    struct SlotProgram
    {
        std::array<SliceParams, kMaxSlices> slice {};
        GlobalSliceParams all;
        Pattern pattern;
        int muteGroup = 0;   // 0-3: breaks in the same group cut each other; different groups play together
    };

    /** Styles the generator knows. */
    enum class Style : int { jungle, amenEdit, rollers, breakbeat, halftime, garage, house, electro };
    constexpr int kNumStyles = 8;
    inline const std::vector<std::string>& styleNames()
    {
        static const std::vector<std::string> n { "JUNGLE", "AMEN EDIT", "ROLLERS", "BREAKBEAT", "HALFTIME", "UK GARAGE", "HOUSE", "ELECTRO" };
        return n;
    }

    /** Machine characters (SET > CHARACTER). */
    struct Character
    {
        const char* name;
        double rate;        // the machine's sample rate (0 = no decimation)
        int    bits;        // 0 = float
        float  lowpassHz;   // output filter (0 = none)
    };
    constexpr int kNumCharacters = 6;
    inline const Character& character (int i) noexcept
    {
        static const Character table[kNumCharacters] = {
            { "CLEAN",   0.0,     0,  0.0f     },
            { "S950",    31250.0, 12, 9000.0f  },
            { "SP-1200", 26040.0, 12, 0.0f     },
            { "MPC60",   40000.0, 12, 15000.0f },
            { "S1000",   44100.0, 16, 18000.0f },
            { "LO-FI",   11025.0, 8,  4500.0f  },
        };
        return table[clampv (i, 0, kNumCharacters - 1)];
    }
}
