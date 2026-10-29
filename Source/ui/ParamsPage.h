// Paula's Revenge - instrument parameters page (envelopes, filter, pan)
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "InstrumentFx.h"
#include "UtilityStrip.h"

namespace prv::ui
{
class ParamsPage : public juce::Component
{
public:
    explicit ParamsPage (PaulaProcessor&);
    ~ParamsPage() override;

    std::function<void()> onClose, onChanged, onUsePlugin;   // onUsePlugin: PLUG-IN / MIDI OUT...
    std::function<void (const juce::String&)> onStatus;
    std::function<void (int slot)> onOpenFx;   // an instrument FX slot clicked: edit it (the mixer's effect page)
    std::function<void()> onOpenEqFx;          // EQ: the instrument's EQ AND EFFECTS page
    std::function<void()> onBackToPlugin;      // a VST3 / AU instrument's filter and envelopes: back to its page

    // a VST3 / AU instrument: the same envelopes and filter, on what the plug-in plays (no sample tuning)
    bool pluginMode() const;
    std::function<void (int lines)> onFitStretch;   // STRETCH TO FIT...: the sample editor's stretch, set to that many lines

    // tuning and BEAT SYNC (buttons; tests call them too)
    void setPitch (int semitones);
    void setFine (int cents);
    bool tuneToC();                          // detects the pitch, sets PITCH and FINE so C-3 plays a C
    void setBeatSync (bool on);
    void setSyncLines (int lines);

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
    InstrumentFxRow fxRow { proc };   // the instrument's own effects (after proc: it uses it)
public:
    InstrumentFxRow& effectsRow() { return fxRow; }   // (tests)
private:
    int shownInstrument = -1;

    PixButton envOnBtn { "VOL ENVELOPE", true };
    PixSlider atk { "ATK" }, dec { "DEC" }, sus { "SUS" }, rel { "REL" };

    PixButton fOff { "OFF", true }, fLp { "LOW", true }, fHp { "HIGH", true }, fBp { "BAND", true };
    PixSlider cutoff { "CUT" }, reso { "RES" }, envAmt { "AMT" }, keyTrk { "KEY" };
    PixSlider fAtk { "ATK" }, fDec { "DEC" }, fSus { "SUS" }, fRel { "REL" };

    NumberBox pitchBox { "PITCH", -48, 48, 0 }, fineBox { "FINE", -100, 100, 0 }, linesBox { "LINES", 1, 512, 16 };
    PixButton tuneBtn { "TUNE TO C" }, syncBtn { "BEAT SYNC", true }, fitBtn { "STRETCH..." };
    PixButton amigaBtn { "AMIGA L/R", true };
    PixSlider pan { "PAN" };
    UtilityStrip utility { proc };   // WIDTH, MONO BELOW, VOL (= the instrument's VOL)
public:
    UtilityStrip& utilityStrip() { return utility; }   // (tests)
private:

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
