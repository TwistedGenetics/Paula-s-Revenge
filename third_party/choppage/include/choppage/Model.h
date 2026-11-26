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

    enum class FxType : uint8_t { none, stutter, reverse, stretch, tapeStop };
    constexpr int kNumFxTypes = 5;
    inline const char* fxTypeName (FxType t) noexcept
    {
        switch (t) { case FxType::stutter: return "STUTTER"; case FxType::reverse: return "REVERSE";
                     case FxType::stretch: return "STRETCH"; case FxType::tapeStop: return "TAPE STOP"; case FxType::none: return ""; }
        return "";
    }

    struct FxEvent
    {
        FxType type = FxType::none;
        uint8_t rate = 16;      // 8, 16, 32, 64 (stutter repeat size as a note value)
    };

    /** One step sequence: which slice plays on each 16th, plus the FX lane. */
    struct Pattern
    {
        int numSteps = 32;
        std::array<int16_t, kMaxSteps> slice {};     // -1 = rest
        std::array<FxEvent, kMaxSteps> fx {};
        std::array<bool, kMaxSteps>    locked {};

        Pattern() { slice.fill (-1); }
        bool operator== (const Pattern& o) const noexcept
        {
            if (numSteps != o.numSteps) return false;
            for (int i = 0; i < numSteps; ++i)
                if (slice[(size_t) i] != o.slice[(size_t) i] || fx[(size_t) i].type != o.fx[(size_t) i].type
                     || fx[(size_t) i].rate != o.fx[(size_t) i].rate)
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
