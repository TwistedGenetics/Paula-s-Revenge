// Paula's Revenge - a plug-in instrument's page: pick the plug-in, its window, output, knobs
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "InstrumentFx.h"
#include "Panels.h"
#include "Learn.h"
#include "UtilityStrip.h"

namespace prv::ui
{
class SynthPage : public juce::Component
{
public:
    explicit SynthPage (PaulaProcessor&);
    ~SynthPage() override;

    std::function<void()> onClose, onChanged, onChords;   // onChords: CHORDS... (the chord table page)
    std::function<void (const juce::String&)> onStatus;
    std::function<void (int slot)> onOpenFx;   // an instrument FX slot clicked: edit it (the mixer's effect page)

    void opened (bool browse);      // browse = show the list of instrument plug-ins
    void refresh();
    void tick();
    void openWindow();              // the plug-in's own window

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    // tests
    UtilityStrip& utilityStrip() { return utility; }
    bool isBrowsing() const { return browsing; }
    int  choiceCount() const { return (int) choices.size(); }
    void choose (int row);
    void learnKnob (int k);         // 0..3 = L, Q, H, W
    void learnTrackKnob (int k);    // 0 = A (V), 1 = B (J) for the TRACK box's track
    bool isLearning() const { return learner.armed; }
    int  learnTrack() const { return trackBox.getValue() - 1; }
    void toggleOn();                // its ON button (learns on/off while LEARN is armed)

private:
    PaulaProcessor& proc;
    InstrumentFxRow fxRow { proc };   // the instrument's own effects (after proc: it uses it)
    UtilityStrip utility { proc };   // WIDTH, MONO BELOW, VOL (= the instrument's VOL)
    juce::Rectangle<float> utilLabel;
    bool browsing = false;
    int shown = -1;
    PixList list;
    std::vector<juce::PluginDescription> choices;
    PixButton rescanBtn { "RESCAN" }, cancelBtn { "CANCEL" };
    PixButton windowBtn { "OPEN PLUG-IN WINDOW" }, changeBtn { "CHANGE PLUG-IN..." }, sampleBtn { "BACK TO A SAMPLE" }, closeBtn { "CLOSE" };
    NumberBox outBox { "OUTPUT", 0, 8, 0 }, bendBox { "BEND", 1, 48, 2 }, trackBox { "TRACK", 1, 8, 1 };
    PixButton learnK[kInstrumentKnobs] { PixButton ("LEARN"), PixButton ("LEARN"), PixButton ("LEARN"), PixButton ("LEARN") };
    PixButton clearK[kInstrumentKnobs] { PixButton ("CLEAR"), PixButton ("CLEAR"), PixButton ("CLEAR"), PixButton ("CLEAR") };
    PixButton learnA { "LEARN AUTO A (V)" }, learnB { "LEARN AUTO B (J)" };
    PixButton onBtn { "ON", true };   // the instrument switched on / off
    PixButton chordsBtn { "CHORDS..." };
    ParamLearner learner;           // what: 0..3 instrument knobs, 10 + k track knobs
    std::map<int, int> heldKeys;
    juce::Rectangle<float> titleArea, infoArea, knobArea[kInstrumentKnobs], trackArea;
    juce::String lastScan;

    Instrument& inst();
    void setBrowsing (bool);
    void refreshList();
    std::vector<float> readParams();
    void learned (int param);
};
} // namespace prv::ui
