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
#include "../engine/AcidSynth.h"
#include "../engine/Tg101Synth.h"
#include "../engine/PadSynth.h"
#include "../engine/DrumSynth.h"
#include <map>
#include "Widgets.h"

namespace prv::ui
{
class PixList;
class EnginePage : public juce::Component
{
public:
    explicit EnginePage (PaulaProcessor&);
    ~EnginePage() override;
    std::function<void()> onClose, onChanged, onOptions, onLoadSample;   // onLoadSample: GRANULAR's LOAD SAMPLE... (the sample browser)   // onOptions: OUTPUT & KNOBS... (the plug-in instrument page)
    std::function<void (const juce::String&)> onStatus;
    std::function<void (int target, int param)> onTouched;    // (v0.92) a control was moved: the AUTOMATION list picks it
    std::function<void (int target, int param)> onAutomate;   // (v0.86) right-click a control: AUTOMATE - its envelope

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
        std::unique_ptr<LeverSwitch> lever;                // (v1.01 II, a hardware panel: two to five choices, a lever)
    };
    std::vector<Row> rows;
    struct Box { juce::String title; std::vector<int> rowIndex; juce::Rectangle<float> area; int page = 0; juce::String key; bool display = false; };
    // (v0.81) tabs of the synth's own: a section "TAB>BOX" puts the box on tab TAB ("LAYER A|A": its name, and a short
    // one for a narrow window) - pages 10, 11, ... in the order the tabs first come
    std::vector<std::unique_ptr<PixButton>> genTabs;
    std::vector<std::pair<juce::String, juce::String>> genTabNames;
  public:
    int tabCount() const { return (int) genTabNames.size(); }                 // (tests)
    PixButton* tabButton (int k) { return k >= 0 && k < (int) genTabs.size() ? genTabs[(size_t) k].get() : nullptr; }
  private:
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
    bool loadTone (int tone, const juce::Array<juce::File>& wavs, const juce::String& name, bool undoable = true);   // (also tests)
    // (v0.79) each TONE's header: ROM (its sound from the ROM FOLDER, in the browser) and SAMPLE... (WAV files picked)
    PixButton sampleBtn[kRomTones] { PixButton ("SAMPLE..."), PixButton ("SAMPLE..."), PixButton ("SAMPLE..."), PixButton ("SAMPLE...") };
    PixButton romFolderBtn { "ROM FOLDER..." };   // (beside ROM's page tabs: where its sounds are)
    void showRomFolderMenu();
    void pickWavs (int tone);
    // the ROM browser (v0.79): the folder's CATEGORIES and their SOUNDS in two scrolling lists - every one of them, a
    // click (or up / down) loads a sound into the tone so the keys play it; KEEP, or CANCEL to put the old one back
    void openRomBrowser (int tone);
    void closeRomBrowser (bool keep);
    bool romBrowserOpen() const { return browsingTone >= 0; }
    void romPickCategory (int c);
    void romPickSound (int s);
    int  romCategoryCount() const { return (int) romCatNames.size(); }
    int  romSoundCount() const { return (int) romSoundsNow.size(); }
    PixList* romSoundList() { return romSounds.get(); }
    void emptyTone (int tone);
    static juce::File romFolder();
    static void setRomFolder (const juce::File&);
    // the folder's samples: category -> group -> its files (scanned once, or again when asked)
    using RomLibrary = std::map<juce::String, std::map<juce::String, juce::Array<juce::File>>>;
    static const RomLibrary& romLibrary (bool rescan = false);
  private:
    int browsingTone = -1, romCat = 0, romSound = -1;
    bool browseRecorded = false;
    std::shared_ptr<const RomBank> browseBefore;
    std::vector<juce::String> romCatNames;
    std::vector<std::pair<juce::String, juce::Array<juce::File>>> romSoundsNow;
    std::unique_ptr<PixList> romCats, romSounds;
    PixButton romKeepBtn { "KEEP" }, romCancelBtn { "CANCEL" }, romEmptyBtn { "EMPTY TONE" }, romAgainBtn { "LOOK AGAIN" };
    juce::Rectangle<float> romHeadArea;
    juce::Rectangle<float> titleArea;
    std::map<int, int> heldKeys;
    // GRANULAR: the sound its grains come from, where it's reading, how many grains are playing
    juce::Rectangle<float> waveArea;
    // (GRANULAR has more than fits at once: its controls on two pages)
    PixButton loadSampleBtn { "LOAD SAMPLE..." };
  public:
    // (v0.85) PAD: each layer's SAMPLE box starts with LOAD SAMPLE... - the sample browser, for that layer
    PixButton padLoadBtn[4] { PixButton ("LOAD SAMPLE..."), PixButton ("LOAD SAMPLE..."), PixButton ("LOAD SAMPLE..."), PixButton ("LOAD SAMPLE...") };
    std::function<void (int layer)> onLoadPadSample;
    // (v1.01) HELIX: OSC A / B's table (the built-in ones, or yours from the wavetable folder), and that folder
    PixButton helixTableBtn[2] { PixButton ("TABLE..."), PixButton ("TABLE...") };
    PixButton helixFolderBtn { "WAVETABLES..." };
    juce::String helixTableId (int osc) const;              // "builtin:<name>", "path:<wav>" or "dir:<folder>"
    void setHelixTable (int osc, const juce::String& id);   // yours ("path:" / "dir:"), or "" for the built-in one again
    void showHelixTableMenu (int osc, const juce::String& group = "~");
    void showHelixFolderMenu();
  private:
    std::unique_ptr<juce::FileChooser> chooser;
    void updateToneButtons();
    void updateHelixButtons();                    // (their names: the table each plays)
    int helixOscBox (const Box& b) const;          // HELIX: 0 / 1 for OSC A / B's box, else -1
    PixButton pageBtn[3] { PixButton ("GRAINS", true), PixButton ("LFOS + FILTER", true), PixButton ("MORPH", true) };   // (the third: ROM's MORPH FILTER, page 3)
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

    // (v1.01) VINTAGE PANELS: each built-in synth drawn as a piece of 80s / 90s gear (its own colours, knobs)
    Theme::PanelLook look = Theme::PanelLook::None;
    juce::Rectangle<int> faceArea;     // the panel between its cheeks / rack ears
    float knobDial = 28.0f;            // the knobs' size (in font pixels): smaller when they don't all fit
    bool shrinking = false;
    int extraCols = 0;
    bool dropDisplay = false;          // (the last resort when even the smallest knobs don't fit: no display on this page)
    bool searching = false, lastFit = true;   // (v1.01 II: the layout tried with bigger and smaller knobs, more columns)
    void layoutOnce();                 // (a column more than the heights alone ask for: boxes never pack perfectly)
    // (v1.01 II) the panel's own name and line under it; its display (what it plays, drawn from its settings)
    juce::String model, tagline;
    void paintDisplay (juce::Graphics&, juce::Rectangle<float> area);
    float vnorm (const char* name, int nth = 0) const;   // a control's setting, 0..1 (by name; nth: the nth of that name)
    float vraw (const char* name, int nth = 0) const;
    std::vector<float> shownValues;
    static juce::Image woodGrain (int w, int h);
    // the drum machines: every voice in a column, a pad each to play it
    bool drumLayout() const { return shownKey == kDrum808Key || shownKey == kDrum909Key; }
    std::vector<std::unique_ptr<PixButton>> drumPads;
    std::array<bool, 16> padDown {};
    PixButton drumHalf[3] { PixButton ("VOICES 1-4", true), PixButton ("VOICES 5-8", true), PixButton ("VOICES 9-12", true) };   // (a narrow page: four at a time)
    int drumHalfShown = 0;
    juce::Rectangle<float> padsArea, drumDispArea;
    void layoutDrums (juce::Rectangle<int> r);
  public:
    static Theme::PanelLook lookFor (const std::string& key);
    Theme::PanelLook panelLook() const { return look; }   // (tests)
  private:
    Instrument& inst();
    void build();
    void sync();
    void edit (int param, float value, bool merge);
};
} // namespace prv::ui
