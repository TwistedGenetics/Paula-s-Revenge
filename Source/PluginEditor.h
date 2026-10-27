// Paula's Revenge - main window
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Widgets.h"
#include "ui/PatternEditor.h"
#include "ui/Panels.h"
#include "ui/Spectrum.h"
#include "ui/SampleEditor.h"
#include "ui/SampleBrowser.h"

class PaulaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PaulaEditor (PaulaProcessor&);
    ~PaulaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void refreshMeters() { timerCallback(); }   // also used by the screenshot test
    void setView (PaulaProcessor::EditorState::View);
    prv::ui::SampleEditor& getSampleEditor() { return sampleEditor; }
    prv::ui::SampleBrowser& getBrowser() { return browser; }

private:
    void timerCallback() override;
    void refreshAll();
    void setStatus (const juce::String&);
    void applyOptions();

    void newSong();
    void loadSong();
    void saveSong();
    void exportMod();
    void exportWav();

    PaulaProcessor& proc;
    using PixButton = prv::ui::PixButton;
    using NumberBox = prv::ui::NumberBox;
    using PixSlider = prv::ui::PixSlider;
    using PixLabel  = prv::ui::PixLabel;

    // title bar
    PixButton newBtn { "NEW" }, loadBtn { "LOAD" }, saveBtn { "SAVE" }, modBtn { "SAVE MOD" }, wavBtn { "RENDER WAV" };
    PixButton zoomOut { "A-" }, zoomIn { "A+" };

    // transport
    PixButton playSongBtn { juce::String::charToString ((juce::juce_wchar) 1) + " SONG" };
    PixButton playBlockBtn { juce::String::charToString ((juce::juce_wchar) 1) + " BLOCK" };
    PixButton stopBtn { juce::String::charToString ((juce::juce_wchar) 2) + " STOP" };
    PixButton editBtn { juce::String::charToString ((juce::juce_wchar) 5) + " EDIT", true };
    PixButton followBtn { "FOLLOW", true };
    PixButton panelsBtn { "PANELS", true };
    NumberBox tempoBox { "BPM", 32, 255, 125 }, speedBox { "SPD", 1, 31, 6 }, octBox { "OCT", 0, 6, 3 }, stepBox { "STEP", 0, 16, 1 };
    PixLabel  posLabel;

    // paula strip
    PixButton a500Btn { "A500", true }, a1200Btn { "A1200", true }, cleanBtn { "CLEAN", true };
    PixButton steppedBtn { "STEPPED", true }, eightBitBtn { "8-BIT", true }, crunchBtn { "CRUNCH", true }, ledBtn { "LED", true };
    PixLabel  ledLabel { {}, false };
    PixSlider sepSlider { "SEP" }, volSlider { "VOL" };

    // song strip
    PixButton tracksBtn { "8 TRACKS", true }, viewBtn { "BIG VIEW", true };
    PixButton hostPlayBtn { "HOST PLAY", true }, hostTempoBtn { "HOST BPM", true };
    PixButton nameBtn { "NAME" };
    PixButton skinBtn { "DARK", true }, specBtn { "SPECTRUM", true };
    PixLabel  statusLabel;

    prv::ui::PatternEditor pattern;
    prv::ui::SequencePanel sequence;
    prv::ui::BlockPanel blockPanel;
    prv::ui::InstrumentPanel instruments;
    prv::ui::Spectrum spectrum;
    prv::ui::SampleEditor sampleEditor;
    prv::ui::SampleBrowser browser;
    using View = PaulaProcessor::EditorState::View;
    View returnView = View::Pattern;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File currentFile;
    juce::String statusText;
    int statusTimer = 0;
    bool showPanels = true;
    bool lastPlaying = false;
    int lastSeq = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaulaEditor)
};
