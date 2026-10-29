// Paula's Revenge - CV out to a modular (an Expert Sleepers ES-9 or any DC-coupled interface):
// the CV OUT instrument's page (outputs, scale, the tuner and AUTO-TUNE) and the song's
// CV SOURCES page (clock, reset, run, LFOs, envelopes, knobs from the pattern).
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
juce::String cvOutName (int output);   // "OFF", "OUT 3"...

class CvOutPage : public juce::Component
{
public:
    explicit CvOutPage (PaulaProcessor&);
    ~CvOutPage() override;

    std::function<void()> onClose, onChanged, onSources, onChords;
    std::function<void (const juce::String&)> onStatus;

    void opened();
    void refresh();
    void tick();                         // ~30 a second: the tuner, AUTO-TUNE's steps
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }
    void visibilityChanged() override;

    // the buttons (tests call them too)
    void setTuner (bool on);
    void calibrate();                    // AUTO-TUNE: step through the voltages, measure, make the map
    void stopCalibration (const juce::String& why);
    void clearMap();
    bool isCalibrating() const { return calStep >= 0; }
    bool tunerOn() const { return tuning; }
    double tunerHz() const { return lastHz; }
    // tests: what the tuner hears (else it reads the processor's input)
    std::function<double (double volts)> fakeOscillator;

    static constexpr double kCalLow = -2.5, kCalHigh = 2.5, kCalStep = 0.25;   // the voltages AUTO-TUNE measures

private:
    PaulaProcessor& proc;
    NumberBox pitchBox { "PITCH OUT", 0, kCvOutputs, 3 }, gateBox { "GATE OUT", 0, kCvOutputs, 4 },
              velBox { "VEL OUT", 0, kCvOutputs, 0 }, modBox { "MOD OUT", 0, kCvOutputs, 0 };
    NumberBox scaleBox { "FULL SCALE", 10, 200, 100 }, gateVBox { "GATE", 5, 120, 50 }, octBox { "OCTAVE", -4, 4, 0 }, bendBox { "BEND", 1, 48, 12 };
    NumberBox inputBox { "INPUT", 0, kCvOutputs, 0 };
    PixButton tunerBtn { "TUNER", true }, calBtn { "AUTO-TUNE" }, clearBtn { "CLEAR MAP" };
    PixButton sourcesBtn { "CV SOURCES..." }, chordsBtn { "CHORDS..." }, sampleBtn { "BACK TO A SAMPLE" }, closeBtn { "CLOSE" }, onBtn { "ON", true };
    juce::Rectangle<float> titleArea, infoArea, tunerArea, mapArea;
    std::map<int, int> heldKeys;
    int shown = -1;

    bool tuning = false;
    double lastHz = 0;
    int calStep = -1, calWait = 0;      // AUTO-TUNE: which voltage, ticks left at it
    std::vector<double> calVolts, calHz, calReads;
    std::vector<float> ring;

    Instrument& inst();
    template <typename Fn> void edit (const juce::String& what, Fn&& fn);
    double measure();                    // the input's pitch now (0 = none)
    void hold (bool on, double volts);   // the pitch output held, the gate open
};

class CvSourcesPage : public juce::Component
{
public:
    explicit CvSourcesPage (PaulaProcessor&);
    std::function<void()> onClose;
    std::function<void (const juce::String&)> onStatus;

    void opened() { refresh(); grabKeyboardFocus(); }
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    // tests
    void setKind (int source, CvKind);
    void setOut (int source, int output);

private:
    PaulaProcessor& proc;
    struct Row
    {
        std::unique_ptr<PixButton> kind, shape, knob;
        std::unique_ptr<NumberBox> out, a, b, c, d, e, f;   // what they set depends on the kind
        juce::Rectangle<float> label;
    };
    Row rows[kCvSources];
    NumberBox scaleBox { "FULL SCALE", 10, 200, 100 };
    PixButton closeBtn { "CLOSE" };
    juce::Rectangle<float> titleArea, helpArea;
    template <typename Fn> void edit (int source, const juce::String& what, Fn&& fn);
    void layoutRow (int i, juce::Rectangle<int> r);
    void chooseKind (int i);
    void chooseShape (int i);
};
} // namespace prv::ui
