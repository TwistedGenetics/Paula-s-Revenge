// Paula's Revenge - an instrument's own effects: 3 insert slots (the mixer's built-in effects or
// a VST3 / AU plug-in) that its sound goes through before the strip of the track it plays on.
// A row on the instrument's pages; clicking a slot opens it on the mixer's effect page.
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "ChoiceBox.h"

namespace prv::ui
{
class InstrumentFxRow : public juce::Component
{
public:
    explicit InstrumentFxRow (PaulaProcessor& p) : proc (p)
    {
        for (int k = 0; k < kInserts; ++k)
        {
            auto& b = slots[k];
            b.onClick = [this, k] { if (onOpen) onOpen (instrumentSlot (instrument(), k)); };
            b.onRightClick = [this, k] { menu (k); };
            addAndMakeVisible (b);
        }
    }

    std::function<void (int slot)> onOpen;                    // the slot (in the mixer's numbering) to edit
    std::function<void (const juce::String&)> onStatus;

    int instrument() const { return juce::jlimit (0, kMixInstruments - 1, proc.editorState.instrument); }

    // the button's text: "1 VERB", "2 ---", the start of a plug-in's name (lit = on; bypassed = not lit)
    static juce::String slotText (const FxSlot& s, int k)
    {
        juce::String t;
        if (s.type == FxType::None) t = "---";
        else if (s.type == FxType::Plugin) t = juce::String (s.pluginName).toUpperCase().trim().substring (0, 5);
        else t = juce::String (fxShortName (s.type));
        return juce::String (k + 1) + " " + t;
    }

    void refresh()
    {
        const int i = instrument();
        for (int k = 0; k < kInserts; ++k)
        {
            const auto& s = proc.song.mixer.inst[i][k];
            slots[k].setButtonText (slotText (s, k));
            slots[k].setToggleState (s.active(), juce::dontSendNotification);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        PixelFont::drawLeft (g, "FX", labelArea.toFloat(), Theme::px, Theme::palFor (*this).ink);
    }

    void resized() override
    {
        const float px = Theme::px;
        auto r = getLocalBounds();
        labelArea = r.removeFromLeft ((int) (px * 6 * 3 + px * 2));
        const int gap = (int) std::max (2.0f, px);
        const int w = (r.getWidth() - gap * (kInserts - 1)) / kInserts;
        for (int k = 0; k < kInserts; ++k) { slots[k].setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); }
    }

private:
    PaulaProcessor& proc;
    PixButton slots[kInserts] { PixButton ("1 ---", true), PixButton ("2 ---", true), PixButton ("3 ---", true) };
    juce::Rectangle<int> labelArea;
    static FxSlot& clip() { static FxSlot c; return c; }
    static bool& haveClip() { static bool h = false; return h; }

    void change (const juce::String& what, std::function<void (FxSlot&)> fn)
    {
        const int i = instrument();
        proc.history.mixer (what);
        proc.withLock ([&] { fn (proc.song.mixer.inst[i][slotIndex]); });
        proc.updateMixerEffects();
        refresh();
        if (onStatus) onStatus (what);
    }
    int slotIndex = 0;

    void menu (int k)
    {
        slotIndex = k;
        const int i = instrument();
        const auto& s = proc.song.mixer.inst[i][k];
        const juce::String who = "INSTRUMENT " + juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2) + " FX " + juce::String (k + 1);
        juce::Component::SafePointer<InstrumentFxRow> safe (this);
        std::vector<MenuItem> items {
            { "OPEN", [safe, k] { if (safe != nullptr && safe->onOpen) safe->onOpen (instrumentSlot (safe->instrument(), k)); } },
            { s.bypass ? "BYPASS OFF (ON AGAIN)" : "BYPASS ON", [safe, k, who]
                { if (safe == nullptr) return; safe->slotIndex = k; const bool b = ! safe->proc.song.mixer.inst[safe->instrument()][k].bypass;
                  safe->change (who + (b ? " BYPASSED" : " ON"), [b] (FxSlot& x) { x.bypass = b; }); } },
            { "COPY", [safe, k, who] { if (safe == nullptr) return; clip() = safe->proc.song.mixer.inst[safe->instrument()][k]; haveClip() = true;
                                       if (safe->onStatus) safe->onStatus (who + " COPIED"); } },
            { "PASTE", [safe, k, who] { if (safe == nullptr) return; if (! haveClip()) { if (safe->onStatus) safe->onStatus ("NOTHING COPIED YET"); return; }
                                        safe->slotIndex = k; const FxSlot c = clip(); safe->change ("PASTE INTO " + who, [c] (FxSlot& x) { x = c; }); } },
            { "CLEAR", [safe, k, who] { if (safe == nullptr) return; safe->slotIndex = k; safe->change ("CLEAR " + who, [] (FxSlot& x) { x = FxSlot {}; }); } },
        };
        showMenu (who, items);
    }
};
} // namespace prv::ui
