// Paula's Revenge - a MIDI-out instrument's page: device, channel, CCs, clock
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "Panels.h"
#include "Learn.h"

namespace prv::ui
{
class MidiOutPage : public juce::Component
{
public:
    explicit MidiOutPage (PaulaProcessor&);
    ~MidiOutPage() override;

    std::function<void()> onClose, onChanged, onChords;   // onChords: CHORDS... (the chord table page)
    std::function<void (const juce::String&)> onStatus;

    void opened (bool browse);      // browse = show the list of MIDI outputs
    void refresh();
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    // tests
    bool isBrowsing() const { return browsing; }
    int  choiceCount() const { return devices.size(); }
    void choose (int row);
    void moveKnob (int k, float v) { knobBar[k].setValue (v, true); }
    void learnCc (int k);
    void learnTrackKnob (int k);    // 0 = A (V), 1 = B (J) for the TRACK box's track
    bool isLearning() const { return learner.armed || ccLearn >= 0; }
    void toggleOn();

private:
    PaulaProcessor& proc;
    bool browsing = false;
    int shown = -1;
    PixList list;
    juce::StringArray devices;
    PixButton rescanBtn { "RESCAN" }, cancelBtn { "CANCEL" };
    PixButton changeBtn { "CHANGE DEVICE..." }, sampleBtn { "BACK TO A SAMPLE" }, panicBtn { "PANIC" }, closeBtn { "CLOSE" };
    NumberBox chanBox { "CHANNEL", 1, 16, 1 }, bendBox { "BEND", 1, 48, 2 }, delayBox { "DELAY", -50, 200, 0 }, trackBox { "TRACK", 1, 8, 1 };
    PixButton chordsBtn { "CHORDS..." };
    PixButton chordBtn { "CHORD", true }, onBtn { "ON", true }, clockBtn { "SEND MIDI CLOCK", true };
    std::unique_ptr<NumberBox> ccBox[kInstrumentKnobs];
    PixSlider knobBar[kInstrumentKnobs] { PixSlider ("SEND"), PixSlider ("SEND"), PixSlider ("SEND"), PixSlider ("SEND") };
    PixButton learnCcBtn[kInstrumentKnobs] { PixButton ("LEARN CC", true), PixButton ("LEARN CC", true), PixButton ("LEARN CC", true), PixButton ("LEARN CC", true) };
    PixButton learnA { "LEARN AUTO A (V)", true }, learnB { "LEARN AUTO B (J)", true };
    ParamLearner learner;           // what: 10 + k = track knob k
    int ccLearn = -1, ccSerialAtArm = 0;
    std::map<int, int> heldKeys;
    juce::Rectangle<float> titleArea, infoArea, knobLabel[kInstrumentKnobs], trackArea;

    Instrument& inst();
    void setBrowsing (bool);
    void refreshList();
    void learned (int param);
    void setKnobNames();   // knobs L, Q, H, W send their CCs
    template <typename Fn> void edit (const juce::String& what, Fn&& fn, bool merge = true);
};
} // namespace prv::ui
