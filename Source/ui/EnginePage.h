// Paula's Revenge - a built-in synth's page (SUB, GRANULAR): its controls in boxes by section, the keys to play
// it, and the way to its OUTPUT / knobs page. Built from the synth's list of controls (BuiltinSynths.h).
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../engine/BuiltinSynths.h"
#include "Widgets.h"

namespace prv::ui
{
class EnginePage : public juce::Component
{
public:
    explicit EnginePage (PaulaProcessor&);
    ~EnginePage() override;
    std::function<void()> onClose, onChanged, onOptions;   // onOptions: OUTPUT & KNOBS... (the plug-in instrument page)
    std::function<void (const juce::String&)> onStatus;

    void refresh();                      // the current instrument's values into the controls (rebuilt for another synth)
    void tick();
    void setValue (int param, float value);   // (the controls, tests)
    float valueOf (int param) const;
    int  controlCount() const { return (int) rows.size(); }
    juce::Component* controlFor (int param) const;   // (tests)
    void knobMenu (int param);           // right-click a control: make it knob L / Q / H / W or a track's AUTO A / B

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

private:
    PaulaProcessor& proc;
    std::string shownKey;
    int shownInstrument = -1;
    struct Row
    {
        int param = 0;
        std::unique_ptr<PixSlider> slider;
        std::unique_ptr<NumberBox> number;
        std::vector<std::unique_ptr<PixButton>> choices;   // (a few choices: a button each)
        std::unique_ptr<PixButton> cycle;                  // (many choices: one button, a menu)
    };
    std::vector<Row> rows;
    struct Box { juce::String title; std::vector<int> rowIndex; juce::Rectangle<float> area; };
    std::vector<Box> boxes;
    PixButton playBtn { juce::String::charToString ((juce::juce_wchar) 1) + " HOLD TO PLAY" }, optionsBtn { "OUTPUT & KNOBS..." }, closeBtn { "CLOSE" };
    juce::Rectangle<float> titleArea;
    std::map<int, int> heldKeys;
    bool playingHeld = false;

    Instrument& inst();
    void build();
    void sync();
    void edit (int param, float value, bool merge);
};
} // namespace prv::ui
