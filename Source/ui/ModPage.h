// Paula's Revenge - the MODULATION page (in the mixer): 16 modulators down the left; the selected
// one's source, its controls (and a STEPPER's 16 steps), what it's doing now, and up to 8 targets,
// each with its own AMOUNT.
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"

namespace prv::ui
{
class MiniBar;
class MixerPage;

class ModPage : public juce::Component
{
public:
    ModPage (PaulaProcessor&, MixerPage&);
    ~ModPage() override;

    std::function<void (const juce::String&)> onStatus;

    int  selected() const { return sel; }
    void select (int mod);
    void refresh();                          // controls follow the song
    void tick();                             // what the modulators are doing now
    void setSource (ModSource);              // for the selected one (an undo step)
    void addRoute (ModRoute);                // to the selected one (an undo step); false when it has 8
    void removeRoute (int k);
    void setStep (int step, float v, bool merge);
    void showSourceMenu();
    void showTargetMenu();
    juce::String modButtonText (int m) const;   // (tests)
    juce::String routeText (int k) const;       // (tests)
    juce::Rectangle<int> stepsBounds() const { return stepsArea.toNearestInt(); }   // (tests)

    // a target's name: "TRACK 1 TAPE ECHO: FEEDBACK", "TRACK 2: VOLUME"
    static juce::String targetName (PaulaProcessor&, int target, int param);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    PaulaProcessor& proc;
    MixerPage& mixer;
    int sel = 0;
    std::unique_ptr<PixButton> modBtn[kMods];
    PixButton sourceBtn { "SOURCE: OFF" }, clearBtn { "CLEAR" }, addBtn { "ADD TARGET..." };
    PixButton macroA { "AUTO A (V)" }, macroB { "AUTO B (J)" };
    NumberBox macroTrack { "TRACK", 1, 8, 1 };
    std::unique_ptr<PixSlider> param[kModParams];
    std::unique_ptr<PixButton> paramToggle[kModParams];
    struct Row;
    std::unique_ptr<Row> rows[kModRoutes];
    juce::Rectangle<float> listMeters[kMods], meterArea, stepsArea, routesHead, macroInfo, infoArea;
    float shown[kMods] {};
    int dragStep = -1;
    Modulator& cur();
    void edit (const juce::String& label, bool merge, std::function<void (Modulator&)> fn);
    void setMacroKnob (int knob);
    void stepAt (juce::Point<float>, bool merge);
};
} // namespace prv::ui
