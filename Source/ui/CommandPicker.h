// Paula's Revenge - right-click EFFECT COMMANDS (v1.01, mock-up B): a short list of commands on the left; on the
// right a how-to card for the one picked: what it does, its digits big with what each one means (click a digit and
// type, or turn the wheel over it), the values each digit can be, and examples to click into the pattern
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
    std::function<std::vector<CommandPart> (const CommandInfo&)> partsFor;   // what each digit means (default: commandParts)
    std::function<std::vector<uint8_t> (const CommandInfo&)> examplesFor;   // (default: commandExamples)
    juce::String rowTextFor (int row) const;

    // shows the list, the current command highlighted, its value on the card
    void open (int currentCmd, uint8_t currentParam);
    int  highlighted() const { return list.selected; }
    void select (int row);                 // a command onto the card (it starts from its first example)
    void pick (int row);                   // (select + INSERT)
    void insert();                         // INSERT: the command, with the value on the card, into the pattern
    uint8_t value() const { return param; }
    void setValue (uint8_t v) { param = v; repaint(); }
    void choose (int part, int choice);    // a digit set to one of its values (tests)
    void pickExample (int k);              // an example into the pattern
    int  exampleCount() const { return (int) examples.size(); }
    juce::String readout() const;          // "U 2 3 = ROLL FOR 3 LINES"
    int partCount() const { return (int) parts.size(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void inputAttemptWhenModal() override { if (onCancel) onCancel(); }

private:
    PixList list;
    PixButton insertBtn { "INSERT (ENTER)" }, cancelBtn { "CANCEL (ESC)" };
    std::vector<CommandPart> parts;
    std::vector<uint8_t> examples;
    std::vector<juce::Rectangle<float>> digitBoxes, exampleRows;   // (where they were drawn: clicks)
    juce::Rectangle<float> cardArea;
    uint8_t param = 0;
    int shownRow = -1;
    int digit = -1;                        // the digit being typed into (0: X / XX, 1: Y), -1 none

    void loadCard();
    void nudge (int part, int by);
    void typeDigit (int hex);
    static juce::StringArray wrap (const juce::String& text, int chars);
};
} // namespace prv::ui
