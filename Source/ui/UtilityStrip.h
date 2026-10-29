// Paula's Revenge - an instrument's UTILITY: WIDTH, MONO BELOW and VOLUME (the instrument's VOL itself,
// so moving either one moves the other), on the PARAMS page and a plug-in instrument's page
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
class UtilityStrip : public juce::Component
{
public:
    explicit UtilityStrip (PaulaProcessor& p) : proc (p)
    {
        width.labelChars = 5; mono.labelChars = 4; vol.labelChars = 3;
        width.valueChars = 4.5f; mono.valueChars = 6.5f; vol.valueChars = 2.5f;
        width.format = [] (float v) { return juce::String (juce::roundToInt (v * 200.0f)) + "%"; };
        width.defaultValue = 0.5f;
        mono.format = [] (float v) { const int hz = sliderToHz (v); return hz == 0 ? juce::String ("OFF") : "<" + juce::String (hz) + "HZ"; };
        mono.defaultValue = 0.0f;
        vol.format = [] (float v) { return juce::String (juce::roundToInt (v * 64.0f)); };
        vol.defaultValue = 1.0f;
        width.onChange = [this] (float v) { setWidth (v * 2.0f); };
        mono.onChange  = [this] (float v) { setMonoBelow (sliderToHz (v)); };
        vol.onChange   = [this] (float v) { setVolume (juce::roundToInt (v * 64.0f)); };
        for (auto* s : { &width, &mono, &vol }) addAndMakeVisible (s);
    }

    std::function<void()> onChanged;
    std::function<void (const juce::String&)> onStatus;

    // (tests use these too)
    void setWidth (float w)
    {
        w = juce::jlimit (0.0f, 2.0f, std::round (w * 100.0f) / 100.0f);
        edit ("UTILITY WIDTH", [w] (Instrument& in) { in.utilWidth = w; });
        if (onStatus) onStatus ("WIDTH " + juce::String (juce::roundToInt (w * 100.0f)) + "%" + (w == 0.0f ? " (MONO)" : w == 1.0f ? " (AS IT IS)" : ""));
    }
    void setMonoBelow (int hz)
    {
        hz = hz <= 0 ? 0 : juce::jlimit (20, 500, hz);
        edit ("UTILITY MONO BELOW", [hz] (Instrument& in) { in.utilMonoHz = hz; });
        if (onStatus) onStatus (hz == 0 ? juce::String ("MONO BELOW: OFF") : "MONO BELOW " + juce::String (hz) + " HZ: THE LOWS IN THE MIDDLE");
    }
    void setVolume (int v)
    {
        v = juce::jlimit (0, 64, v);
        // (the same history step as the instrument list's VOL, so the two are one control)
        proc.history.instrument (proc.editorState.instrument, "INSTRUMENT VOLUME", true);
        proc.withLock ([&] { inst().volume = v; });
        refresh();
        if (onChanged) onChanged();
    }

    void refresh()
    {
        const auto& in = inst();
        width.setValue (in.utilWidth * 0.5f);
        mono.setValue (hzToSlider (in.utilMonoHz));
        vol.setValue (in.volume / 64.0f);
        const bool can = ! in.isMidiOut() && ! in.isCvOut();
        for (auto* s : { &width, &mono }) { s->setEnabled (can); s->setAlpha (can ? 1.0f : 0.4f); }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int gap = (int) std::max (2.0f, Theme::px * 1.5f);
        const int room = r.getWidth() - gap * 8;
        width.setBounds (r.removeFromLeft (room * 36 / 100)); r.removeFromLeft (gap * 4);
        mono.setBounds (r.removeFromLeft (room * 38 / 100)); r.removeFromLeft (gap * 4);
        vol.setBounds (r);
    }

    // MONO BELOW's slider: OFF at the left, then 20 .. 500 Hz (logarithmic)
    static int sliderToHz (float v)
    {
        if (v < 0.04f) return 0;
        const float t = (v - 0.04f) / 0.96f;
        return juce::roundToInt (20.0f * std::pow (25.0f, juce::jlimit (0.0f, 1.0f, t)));
    }
    static float hzToSlider (int hz)
    {
        if (hz <= 0) return 0.0f;
        return 0.04f + 0.96f * (float) (std::log ((double) juce::jlimit (20, 500, hz) / 20.0) / std::log (25.0));
    }

    PixSlider width { "WIDTH" }, mono { "MONO" }, vol { "VOL" };

private:
    PaulaProcessor& proc;
    Instrument& inst() { return proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)]; }
    template <typename Fn> void edit (const juce::String& what, Fn&& fn)
    {
        proc.history.instrument (proc.editorState.instrument, what, true);
        proc.withLock ([&] { fn (inst()); });
        refresh();
        if (onChanged) onChanged();
    }
};
} // namespace prv::ui
