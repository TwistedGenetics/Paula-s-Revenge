// Paula's Revenge - right-click EFFECT COMMANDS: pick a command on the left, build its value on the right
// with named buttons (v1.01: "U 2 3 = ROLL FOR 3 LINES"), INSERT puts it in the pattern
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
    // the command with the value built (CommandInfo::param holds it)
    std::function<void (const CommandInfo&)> onPick;
    std::function<void()> onCancel;
    // context for the cell the list was opened on: a name and an overview to use instead of
    // the usual ones (empty = usual), and the line describing a command + value
    std::function<juce::String (const CommandInfo&)> nameFor, overviewFor;
    std::function<juce::String (uint8_t cmd, uint8_t param)> describe;
    std::function<std::vector<CommandPart> (const CommandInfo&)> partsFor;   // the value's buttons (default: commandParts)
    juce::String rowTextFor (int row) const;

    // shows the list, the current command highlighted, its value in the builder
    void open (int currentCmd, uint8_t currentParam);
    int  highlighted() const { return list.selected; }
    void select (int row);                 // a command into the builder (its value from the start)
    void pick (int row);                   // (select + INSERT)
    void insert();                         // INSERT: the built command into the pattern
    uint8_t value() const { return param; }
    void setValue (uint8_t v) { param = v; refreshParts(); repaint(); }
    void choose (int part, int choice);    // a part's button (tests)
    juce::String readout() const;          // "U 2 3 = ROLL FOR 3 LINES"
    int partCount() const { return (int) parts.size(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void inputAttemptWhenModal() override { if (onCancel) onCancel(); }

private:
    PixList list;
    PixButton insertBtn { "INSERT (ENTER)" }, cancelBtn { "CANCEL (ESC)" };
    std::vector<CommandPart> parts;
    std::vector<std::vector<std::unique_ptr<PixButton>>> partBtns;
    std::vector<std::unique_ptr<PixButton>> fineBtns;
    std::vector<juce::Rectangle<float>> partLabels;
    juce::Rectangle<float> readoutArea, textArea, builderArea;
    uint8_t param = 0;
    int shownRow = -1;

    void rebuildParts();
    void refreshParts();
    void layoutBuilder();
    static juce::StringArray wrap (const juce::String& text, int chars);
};
} // namespace prv::ui
