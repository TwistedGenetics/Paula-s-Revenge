// Paula's Revenge - the mixer: channel strips, channel EQ page, effect editor page
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "Widgets.h"
#include "Learn.h"
#include "Panels.h"

namespace prv::ui
{
// A small horizontal value bar with its label and value written on it ("A 35%")
class MiniBar : public juce::Component
{
public:
    explicit MiniBar (const juce::String& l) : label (l) { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); }
    juce::String label;
    float value = 0.0f, defaultValue = 0.0f;
    bool bipolar = false;     // fill from the centre (pan)
    bool showFill = true;
    std::function<void (float)> onChange;
    std::function<void()> onDragStart, onDoubleClick;
    std::function<juce::String (float)> format = [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };

    void setValue (float v, bool notify = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float dragStartValue = 0, wheelAccum = 0;
};

// A vertical fader (-INF .. +6 dB) with a level meter beside it
class FaderMeter : public juce::Component
{
public:
    FaderMeter() { setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false); }
    float db = 0.0f;
    bool stereo = false;
    juce::Colour colour { 0xff40c0ff };
    std::function<void (float)> onChange;
    std::function<void()> onDragStart;
    std::function<void()> onColourMenu;   // right-click on the meter: the meters' colour
    int track = 3;                        // (its colour with TRACK COLOURS)

    void setDb (float d, bool notify = false);
    void setLevels (float l, float r);    // latest peaks (linear), decays by itself
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // fader travel: 0 = off (-INF), 1 = +6 dB; 0 dB sits at 0.875
    static float dbToPos (float db);
    static float posToDb (float pos);
    static juce::String dbText (float db);

private:
    float levelL = 0, levelR = 0, holdL = 0, holdR = 0;
    int holdCountL = 0, holdCountR = 0;
    float dragStartDb = 0, wheelAccum = 0;
    juce::Rectangle<float> troughArea() const;
};

class MixStrip;

class MixerPage : public juce::Component
{
public:
    explicit MixerPage (PaulaProcessor&);
    ~MixerPage() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onStatus;
    std::function<void (bool song)> onPlayStop;
    std::function<void()> onMeterColourMenu;   // right-click a meter
    std::function<void()> onOctave;      // [ ] changed the octave

    enum class Page { Strips, Channel, Effect };
    Page page() const { return currentPage; }
    void opened();          // shown: back to the strips, refreshed
    void refresh();         // controls follow the song (after undo, load, ...)
    void tick();            // meters
    void showStrips();
    void openChannel (int track);
    void openEffect (int slot);
    int  channelTrack() const { return chTrack; }
    int  effectSlot() const { return fxSlot; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;              // note keys released
    void focusLost (FocusChangeType) override { releaseNotes(); }
    void visibilityChanged() override { if (! isVisible()) releaseNotes(); }
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }
    void releaseNotes();

    // ---- used by the strips
    template <typename Fn> void edit (const juce::String& label, bool merge, Fn&& fn, bool effectsChanged = false);
    void setTrackPan (int track, int pan, bool merge);
    void toggleMute (int track);
    void toggleSolo (int track);
    void toggleBypass (int slot);
    static juce::String slotOwner (int slot);    // "TRACK 1", "SEND A", "MASTER"
    PaulaProcessor& processor() { return proc; }

private:
    PaulaProcessor& proc;
    std::map<int, int> heldKeys;   // key code -> note being played (the computer keyboard plays the instrument)
    Page currentPage = Page::Strips, effectReturn = Page::Strips;
    int chTrack = 0, fxSlot = 0;

    // header
    PixButton closeBtn { "CLOSE" }, backBtn { "BACK" }, resetAllBtn { "RESET MIXER" };
    PixButton prevBtn { "<" }, nextBtn { ">" };

    // strips page
    std::vector<std::unique_ptr<MixStrip>> strips;   // 8 tracks, 2 returns, master

    // channel page
    PixButton bandOn[kEqBands] { PixButton ("HIGH-PASS", true), PixButton ("LOW SHELF", true), PixButton ("MID 1", true), PixButton ("MID 2", true), PixButton ("HIGH SHELF", true) };
    std::unique_ptr<MiniBar> bandFreq[kEqBands], bandGain[kEqBands], bandQ[kEqBands];
    PixButton eqFlatBtn { "EQ FLAT" };
    PixButton chInsert[kInserts] { PixButton ("---"), PixButton ("---"), PixButton ("---") };
    PixSlider chSend[kSends] { PixSlider ("SEND A"), PixSlider ("SEND B") };
    PixButton chPre[kSends] { PixButton ("PRE", true), PixButton ("PRE", true) };
    PixSlider chFader { "FADER" };
    juce::Rectangle<float> eqGraph, chInsertsBox, chSendsBox, bandBox;

    // effect page
    PixButton typeBtn[kBuiltinFxTypes] { PixButton ("EMPTY", true), PixButton ("FILTER", true), PixButton ("DRIVE", true), PixButton ("COMP", true),
                                             PixButton ("DELAY", true), PixButton ("REVERB", true), PixButton ("CHORUS", true) };
    PixButton bypassBtn { "BYPASS", true }, defaultsBtn { "DEFAULTS" };
    std::unique_ptr<PixSlider> param[kFxParams];
    std::unique_ptr<PixButton> paramToggle[kFxParams];
    juce::Rectangle<float> fxInfoArea;

    // automation: LEARN on the effect page, the track's knobs on the channel page
    PixButton learnABtn { "LEARN AUTO A (V)", true }, learnBBtn { "LEARN AUTO B (J)", true };
    NumberBox learnTrackBox { "TRACK", 1, 8, 1 };
    PixButton chClearA { "CLEAR" }, chClearB { "CLEAR" };
    juce::Rectangle<float> learnInfoArea, chAutoBox, chAutoA, chAutoB;
    ParamLearner learner;
    void armLearn (int knob);
    void finishLearn (int param);
    std::vector<float> slotParamValues (int slot);
    juce::String slotParamName (int slot, int param);

    // plug-ins (VST3 / AU)
    PixButton pluginBtn { "PLUG-IN...", true }, rescanBtn { "RESCAN" }, editPluginBtn { "OPEN PLUG-IN WINDOW" }, listCloseBtn { "BACK TO THE EFFECT" };
    PixList pluginList;
    std::vector<juce::PluginDescription> pluginChoices;
    bool browsingPlugins = false;
    juce::Rectangle<float> pluginInfoArea, listStatusArea;
    juce::String lastScanStatus;
    void showPluginList (bool);
    void choosePlugin (int row);
    void refreshPluginList();
public:
    // tests
    bool isBrowsingPlugins() const { return browsingPlugins; }
    void pickPlugin (int row) { choosePlugin (row); }
    int  pluginChoiceCount() const { return (int) pluginChoices.size(); }
    void openPluginList() { showPluginList (true); }
    void learn (int track, int knob) { learnTrackBox.setValue (track + 1); armLearn (knob); }
    bool isLearning() const { return learner.armed; }
private:

    juce::Rectangle<float> titleArea;

    void layoutStrips (juce::Rectangle<int>);
    void layoutChannel (juce::Rectangle<int>);
    void layoutEffect (juce::Rectangle<int>);
    void updateVisibility();
    void refreshChannel();
    void refreshEffect();
    void setSlotType (int slot, FxType);
    juce::String pageTitle() const;
    juce::String delayText() const;
    int shownDelay = -1;
    void paintEqGraph (juce::Graphics&, juce::Rectangle<float>);
    static juce::String effectInfo (FxType);
};

// ---- value mappings (public for tests)
namespace mixmap
{
    float freqToSlider (float hz);     // 20 Hz .. 20 kHz, logarithmic
    float sliderToFreq (float v);
    float gainToSlider (float db);     // -18 .. +18 dB
    float sliderToGain (float v);
    float qToSlider (float q);         // 0.3 .. 8
    float sliderToQ (float v);
    juce::String panText (int pan);    // -1 = AUTO
}
} // namespace prv::ui
