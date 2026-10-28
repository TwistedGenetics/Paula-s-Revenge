// Paula's Revenge - right-click on a chord (0xy on a CHORD instrument's note): pick a slot from the song's table
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "Panels.h"
#include "../engine/Chords.h"

namespace prv::ui
{
class ChordPicker : public juce::Component, private juce::Timer
{
public:
    ChordPicker();
    std::function<void (int slot)> onPick;       // a slot from the table
    std::function<void()> onEdit;                 // EDIT CHORDS...
    std::function<void()> onCommands;             // EFFECT COMMANDS...
    std::function<void()> onCancel;
    // AUDITION: moving through the list (keys or mouse) plays the chord; 0 = stop
    std::function<void (int slot)> onAudition;
    bool audition = true;

    // shows the song's filled slots, the current one highlighted
    void open (const ChordTable& table, int currentSlot);
    int  rows() const { return (int) slotRows.size() + 2; }
    juce::String rowText (int row) const;
    int  slotAt (int row) const { return row >= 0 && row < (int) slotRows.size() ? slotRows[(size_t) row] : -1; }
    int  highlighted() const { return list.selected; }
    void pick (int row);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void inputAttemptWhenModal() override { if (onCancel) onCancel(); }
    void visibilityChanged() override { if (! isVisible()) { stopTimer(); if (auditioned >= 0 && onAudition) onAudition (0); auditioned = -1; } }

private:
    PixList list;
    PixButton editBtn { "EDIT CHORDS..." }, cancelBtn { "CANCEL (ESC)" };
    std::vector<int> slotRows;
    std::vector<juce::String> texts;
    juce::String typed;                            // two hex digits typed jump to that slot
    std::vector<int> colours;                      // each row's group colour
    int auditioned = -1;
    void hear (int row);                           // play the chord under the mouse / keys, briefly
    void timerCallback() override;
};
} // namespace prv::ui
