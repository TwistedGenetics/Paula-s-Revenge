// Paula's Revenge - the meters' colours: one colour for every level meter (the pattern's VU
// bars, the L/R output meter, the mixer's meters) and one for the spectrum, each picked by
// right-clicking it and kept in the prefs
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "../Prefs.h"

namespace prv::ui::meters
{
struct Choice { const char* name; juce::uint32 dark, light; };   // dark == 0: the track colours

inline const std::vector<Choice>& choices()
{
    static const std::vector<Choice> c {
        { "GREEN", 0xff5ee05e, 0xff1f7a1f }, { "AMBER", 0xfff2b23a, 0xff9a5a00 }, { "CYAN", 0xff3fd0e6, 0xff00768a },
        { "RED", 0xffff5a4a, 0xffc01818 },   { "BLUE", 0xff6aa4ff, 0xff1f4fb0 },  { "PINK", 0xffe864c8, 0xff9c1f94 },
        { "CREAM / BLACK", 0xfff4ecd6, 0xff000000 }, { "TRACK COLOURS", 0, 0 },
    };
    return c;
}

inline int count() { return (int) choices().size(); }

// the menu entries; the last one is "TRACK COLOURS" for the meters, "RAINBOW" for the spectrum
inline juce::StringArray names (bool spectrum)
{
    juce::StringArray a;
    for (auto& c : choices()) a.add (c.name);
    if (spectrum) a.set (a.size() - 1, "RAINBOW (THE TRACK COLOURS)");
    return a;
}

inline int vuChoice()       { return juce::jlimit (0, count() - 1, prv::prefs().getIntValue ("vuColour", 0)); }
inline int spectrumChoice() { return juce::jlimit (0, count() - 1, prv::prefs().getIntValue ("spectrumColour", count() - 1)); }
inline void setVuChoice (int i)       { prv::prefs().setValue ("vuColour", juce::jlimit (0, count() - 1, i)); prv::prefs().saveIfNeeded(); }
inline void setSpectrumChoice (int i) { prv::prefs().setValue ("spectrumColour", juce::jlimit (0, count() - 1, i)); prv::prefs().saveIfNeeded(); }

inline juce::Colour colourOf (int choice, int track)
{
    const auto& c = choices()[(size_t) juce::jlimit (0, count() - 1, choice)];
    if (c.dark == 0) return Theme::skin.track[juce::jlimit (0, 7, track)];
    return juce::Colour (Theme::skin.dark ? c.dark : c.light);
}

// a level meter's colour (track: the track it belongs to, for TRACK COLOURS)
inline juce::Colour vu (int track) { return colourOf (vuChoice(), track); }

// a spectrum band's colour (RAINBOW: the track colours left to right, as before)
inline juce::Colour spectrum (int band, int bands)
{
    if (spectrumChoice() == count() - 1)
        return Theme::skin.dark ? Theme::skin.track[(band * 8) / std::max (1, bands)] : Theme::skin.specBar;
    return colourOf (spectrumChoice(), 0);
}
} // namespace prv::ui::meters
