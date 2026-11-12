// Paula's Revenge - a built-in synth's page (SUB, GRANULAR): its controls in boxes by section, the keys to play
// it, and the way to its OUTPUT / knobs page. Built from the synth's list of controls (BuiltinSynths.h).
#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../engine/BuiltinSynths.h"
#include "../engine/ReeseSynth.h"
#include "../engine/RaveSynth.h"
#include "../engine/RomSynth.h"
#include "../engine/MorphSynth.h"
#include <map>
#include "Widgets.h"

namespace prv::ui
{
class EnginePage : public juce::Component
{
public:
    explicit EnginePage (PaulaProcessor&);
    ~EnginePage() override;
    std::function<void()> onClose, onChanged, onOptions, onLoadSample;   // onLoadSample: GRANULAR's LOAD SAMPLE... (the sample browser)   // onOptions: OUTPUT & KNOBS... (the plug-in instrument page)
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
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    juce::Rectangle<float> sourceArea() const { return waveArea; }   // (GRANULAR: the sound it reads; tests)
    bool sourceShown() const { return waveOk; }

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
    struct Box { juce::String title; std::vector<int> rowIndex; juce::Rectangle<float> area; int page = 0; };
    std::vector<Box> boxes;
    PixButton playBtn { juce::String::charToString ((juce::juce_wchar) 1) + " HOLD TO PLAY" }, optionsBtn { "OUTPUT & KNOBS..." }, closeBtn { "CLOSE" };
  public:
    // PRESETS (v0.74): the synth's own and yours (kept on this computer, for any song)
    PixButton presetsBtn { "PRESETS..." };
    void showPresets();
    void savePreset (const juce::String& name);          // (the prompt's answer; also tests)
    void loadPreset (const std::string& name, const std::vector<float>& values);
    // ROM (v0.77): each TONE's samples - from the ROM FOLDER (by category and name), or WAV files picked
    PixButton toneBtn[kRomTones] { PixButton ("TONE 1"), PixButton ("TONE 2"), PixButton ("TONE 3"), PixButton ("TONE 4") };
    void showToneMenu (int tone);
    bool loadTone (int tone, const juce::Array<juce::File>& wavs, const juce::String& name);   // (also tests)
    void emptyTone (int tone);
    static juce::File romFolder();
    static void setRomFolder (const juce::File&);
    // the folder's samples: category -> group -> its files (scanned once, or again when asked)
    using RomLibrary = std::map<juce::String, std::map<juce::String, juce::Array<juce::File>>>;
    static const RomLibrary& romLibrary (bool rescan = false);
  private:
    juce::Rectangle<float> titleArea;
    std::map<int, int> heldKeys;
    // GRANULAR: the sound its grains come from, where it's reading, how many grains are playing
    juce::Rectangle<float> waveArea;
    // (GRANULAR has more than fits at once: its controls on two pages)
    PixButton loadSampleBtn { "LOAD SAMPLE..." };
    std::unique_ptr<juce::FileChooser> chooser;
    void updateToneButtons();
    PixButton pageBtn[2] { PixButton ("GRAINS", true), PixButton ("LFOS + FILTER", true) };
    int pageShown = 0;
    bool subPages = false, relayout = false;   // (SUB: on two pages only when it doesn't all fit - BEAT's rows on)
  public:
    void showPage (int);
    int  pageNow() const { return pageShown; }
  private:
    std::vector<float> wavePeaks;
    bool waveOk = false;
    float wavePos = 0;
    int waveGrains = 0, waveTick = 0;
    juce::String waveName;
    void readWave();
    void positionFrom (juce::Point<float>);
    bool playingHeld = false;

    Instrument& inst();
    void build();
    void sync();
    void edit (int param, float value, bool merge);
};
} // namespace prv::ui
