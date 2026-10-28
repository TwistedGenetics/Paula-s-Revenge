// Paula's Revenge - instrument parameters page (envelopes, filter, pan)
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
class ParamsPage : public juce::Component
{
public:
    explicit ParamsPage (PaulaProcessor&);
    ~ParamsPage() override;

    std::function<void()> onClose, onChanged, onUsePlugin;   // onUsePlugin: PLUG-IN / MIDI OUT...
    std::function<void (const juce::String&)> onStatus;

    void refresh();     // load the current instrument's values into the controls
    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    // value <-> slider mappings (public for tests)
    static float timeToSlider (float seconds);
    static float sliderToTime (float v);
    static float cutoffToSlider (float hz);
    static float sliderToCutoff (float v);

private:
    PaulaProcessor& proc;
    int shownInstrument = -1;

    PixButton envOnBtn { "VOL ENVELOPE", true };
    PixSlider atk { "ATK" }, dec { "DEC" }, sus { "SUS" }, rel { "REL" };

    PixButton fOff { "OFF", true }, fLp { "LOW", true }, fHp { "HIGH", true }, fBp { "BAND", true };
    PixSlider cutoff { "CUT" }, reso { "RES" }, envAmt { "AMT" }, keyTrk { "KEY" };
    PixSlider fAtk { "ATK" }, fDec { "DEC" }, fSus { "SUS" }, fRel { "REL" };

    PixButton amigaBtn { "AMIGA L/R", true };
    PixSlider pan { "PAN" };

    PixButton playBtn { juce::String::charToString ((juce::juce_wchar) 1) + " HOLD TO PLAY" }, resetBtn { "RESET ALL" }, closeBtn { "CLOSE" },
              pluginBtn { "PLUG-IN / MIDI OUT..." };
    // switching the instrument on / off (and learning that as a track's AUTO A / B)
    PixButton onBtn { "ON", true }, learnA { "LEARN A (V)", true }, learnB { "LEARN B (J)", true };
    NumberBox trackBox { "TRACK", 1, 8, 1 };
    int learning = -1;                   // 0 = AUTO A, 1 = AUTO B: waiting for a click on ON
    void toggleOn();
    void armLearn (int knob);
public:
    // tests
    void clickOn() { toggleOn(); }
    void learn (int track, int knob) { trackBox.setValue (track + 1); armLearn (knob); }
    bool isLearning() const { return learning >= 0; }
private:

    juce::Rectangle<float> titleArea, envBox, filterBox, fenvBox, panBox, envGraph, fenvGraph;
    std::map<int, int> heldKeys;
    bool playingHeld = false;

    template <typename Fn> void edit (Fn&& fn);
    Instrument& inst();
    void drawEnvelope (juce::Graphics&, juce::Rectangle<float>, const Envelope&, bool active);
};
} // namespace prv::ui
