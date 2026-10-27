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
    static juce::String rowText (const CommandInfo&);
};
} // namespace prv::ui
