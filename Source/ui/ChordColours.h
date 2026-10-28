// Paula's Revenge - chord group colours: 8 colours a chord slot can be marked with (CHORDS page,
// the chord list over the pattern, #xy in the pattern). Each has a shade for the dark skin and
// one for the light skin, so it reads on both.
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "../engine/Chords.h"

namespace prv::ui::chordColours
{
struct Entry { const char* name; juce::uint32 dark, light; };

inline const Entry* table()
{
    static const Entry t[kChordColours] = {
        { "RED",    0xffff5a4a, 0xffc8201a }, { "ORANGE", 0xffff9a2e, 0xffb85a00 },
        { "YELLOW", 0xfff2dc3a, 0xff8a7600 }, { "GREEN",  0xff5ee05e, 0xff1f7a1f },
        { "CYAN",   0xff3fd0e6, 0xff00768a }, { "BLUE",   0xff6aa4ff, 0xff1f4fb0 },
        { "PURPLE", 0xffb48cff, 0xff6a2fc0 }, { "PINK",   0xffff7ad0, 0xffb01f86 },
    };
    return t;
}

inline juce::String name (int c) { return c >= 1 && c <= kChordColours ? juce::String (table()[c - 1].name) : juce::String ("NONE"); }

// the colour to draw (transparent for 0 = none)
inline juce::Colour colour (int c)
{
    if (c < 1 || c > kChordColours) return juce::Colours::transparentBlack;
    const auto& e = table()[c - 1];
    return juce::Colour (Theme::skin.dark ? e.dark : e.light);
}

inline juce::StringArray menuNames()
{
    juce::StringArray a;
    for (int c = 1; c <= kChordColours; ++c) a.add (name (c));
    a.add ("NONE");
    return a;
}
} // namespace prv::ui::chordColours
