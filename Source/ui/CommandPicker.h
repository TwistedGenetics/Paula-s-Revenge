// Paula's Revenge - right-click menu of effect commands for the pattern
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "Panels.h"
#include "../engine/Commands.h"

namespace prv::ui
{
class CommandPicker : public juce::Component
{
public:
    CommandPicker();
    std::function<void (const CommandInfo&)> onPick;
    std::function<void()> onCancel;
    // context for the cell the list was opened on: a name and an overview to use instead of
    // the usual ones (empty = usual), and the example line for a command + value
    std::function<juce::String (const CommandInfo&)> nameFor, overviewFor;
    std::function<juce::String (uint8_t cmd, uint8_t param)> describe;
    juce::String rowTextFor (int row) const;

    // shows the list, the current command highlighted
    void open (int currentCmd, uint8_t currentParam);
    int  highlighted() const { return list.selected; }
    void pick (int row);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void inputAttemptWhenModal() override { if (onCancel) onCancel(); }

private:
    PixList list;
    PixButton cancelBtn { "CANCEL (ESC)" };
    juce::Rectangle<float> titleArea, infoArea;

};
} // namespace prv::ui
