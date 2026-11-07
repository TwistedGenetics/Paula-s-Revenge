// Paula's Revenge - Cmd+K: the COMMAND PALETTE. Type a few letters of anything the app can do -
// a button on screen, a page, a menu command - and Enter does it. Each one shows its shortcut,
// so it teaches the keys as you go.
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"

namespace prv::ui
{
struct PaletteItem
{
    juce::String name;          // "MIXER", "SAVE AS...", "SKIN: KICKSTART 1.3"
    juce::String keys;          // "CMD+S" (empty = none)
    juce::String where;         // "BUTTON", "VIEW", "MENU"...
    std::function<void()> action;
};

class CommandPalette : public juce::Component
{
public:
    CommandPalette();
    // over `parent`; the items are gathered when it opens
    void open (juce::Component& parent, std::vector<PaletteItem> items);
    void close();
    bool isOpen() const { return isVisible(); }

    // typing / choosing (also used by tests)
    void setQuery (const juce::String&);
    const juce::String& getQuery() const { return query; }
    int  numMatches() const { return (int) matches.size(); }
    const PaletteItem* match (int i) const { return i >= 0 && i < numMatches() ? &items[(size_t) matches[(size_t) i]] : nullptr; }
    int  selectedRow() const { return selected; }
    void runSelected();
    static int score (const juce::String& name, const juce::String& query);   // 0 = no match; higher = better

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    std::vector<PaletteItem> items;
    std::vector<int> matches;
    juce::String query;
    int selected = 0, scroll = 0;
    juce::Rectangle<float> box, field, listArea, hintArea;
    float rowH() const { return Theme::px * 11; }
    int  visibleRows() const { return std::max (1, (int) (listArea.getHeight() / rowH())); }
    int  rowAt (juce::Point<float>) const;
    void refilter();
    void keepInView();
};
} // namespace prv::ui
