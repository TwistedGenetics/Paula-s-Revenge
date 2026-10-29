// Paula's Revenge - main window
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Widgets.h"
#include "Autosave.h"
#include "ui/PatternEditor.h"
#include "ui/Panels.h"
#include "ui/Spectrum.h"
#include "ui/SampleEditor.h"
#include "ui/SampleBrowser.h"
#include "ui/ParamsPage.h"
#include "ui/SynthPage.h"
#include "ui/MidiOutPage.h"
#include "ui/ChoiceBox.h"
#include "ui/HelpPage.h"
#include "ui/ChordsPage.h"
#include "ui/SamplePage.h"
#include "ui/ArrangePage.h"
#include "ui/MixerPage.h"
#include "ui/CvPage.h"

class PaulaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PaulaEditor (PaulaProcessor&);
    ~PaulaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void keyStateFromElsewhere (bool isDown);   // key releases while a plug-in window is in front
    void refreshMeters() { timerCallback(); }   // also used by the screenshot test
    void setView (PaulaProcessor::EditorState::View);
    prv::ui::SampleEditor& getSampleEditor() { return sampleEditor; }
    prv::ui::SampleBrowser& getBrowser() { return browser; }
    prv::ui::ParamsPage& getParams() { return params; }
    prv::ui::ArrangePage& getArrange() { return arrange; }
    prv::ui::MixerPage& getMixer() { return mixer; }
    prv::ui::SynthPage& getSynthPage() { return synthPage; }
    prv::ui::MidiOutPage& getMidiPage() { return midiPage; }
    prv::ui::ChoiceBox& getChoice() { return choice; }
    prv::ui::HelpPage& getHelp() { return help; }
    prv::ui::ChordsPage& getChords() { return chords; }
    prv::ui::SamplePage& getSamplePage() { return samplePage; }
    prv::ui::CvOutPage& getCvPage() { return cvPage; }
    prv::ui::CvSourcesPage& getCvSources() { return cvSources; }
    void makeCvOut();                                 // the current instrument plays a modular (CV OUT)
    void openChords (int slot = -1);                  // CHORDS (Cmd+3; EDIT CHORDS... in the chord list)
    void renderTo (const juce::File&, bool stems);   // RENDER WAV after the questions (tests call it directly)
    void recordAutomation();                          // REC AUTO: called by the timer
    void toggleHelp();                                // HELP (F1, Cmd+/)
    enum BounceFx { BounceNoFx = 0, BounceInstrumentFx, BounceMixerFx, BounceAllFx };
    bool bounceSelection (int fx);                    // the highlighted area to a new instrument (a BounceFx)
    void askBounce();                                 // DRY or WITH FX?
    int  captureNow();                                // CAPTURE (Cmd+R): what was played into the pattern; notes written
    void showMeterColourMenu (bool forSpectrum);      // right-click a level meter / the spectrum: its colour
    std::function<void()> openAudioSettings;          // the app's Audio/MIDI Settings (the standalone sets it)
    void openSample();                                // SAMPLE (Cmd+4): record audio into the instrument
    void toggleSample();                              // Cmd+4: open it, or close it
    prv::ui::PatternEditor& getPattern() { return pattern; }
    prv::ui::SequencePanel& getSequence() { return sequence; }
    prv::ui::Spectrum& getSpectrum() { return spectrum; }
    prv::ui::InstrumentPanel& getInstruments() { return instruments; }
    bool exportSamplesTo (const juce::File& folder, int& written, juce::String& error);
    // the File menu (the standalone's menu bar; tests)
    void newSong();
    void loadSong();
    void saveSong();
    void saveSongAs();
    void exportMod();
    void exportWav();
    void exportSamples();
    void setFullVelocity (bool on);                   // FULL VEL: ignore the controller's velocity (play at 127)
    void setFollowInstrument (bool on);
    void showSeparationMenu();
    void showGrooveMenu();                            // GROOVE (bottom row): the song's feel, then how much
    void setGroove (int kind, int amount);                        // SEP: right-click a machine button (or the View menu)
    void setSeparation (float v01);
    float separation() const { return sepSlider.value; }               // FOLLOW INSTRUMENT: a click on a note picks its instrument
    void setNoteOffs (bool on);                       // NOTE-OFFS: recording writes OFFs at all
    bool fullVelocityOn() const { return proc.fullVelocity.load(); }
    bool writeSongTo (const juce::File&);             // SAVE to that file (the song takes the file's name)
    bool loadSongFrom (const juce::File&);            // LOAD from that file
    // autosave (the standalone app): every couple of minutes when something changed; after a
    // crash it offers the autosave back
    void startAutosave();
    void endAutosave();                               // a clean quit
    bool recoverAutosave();                           // (the RECOVER answer; tests too)
    prv::Autosave* getAutosave() { return autosave.get(); }
    bool saveNow() { return saveSongDirect(); }   // tests: SAVE without a window when a file is set
    bool saveSongDirect();
    void setStatus (const juce::String&);           // the status line (also the app: output changes)

    void refreshAll();   // (also used by the tests)

private:
    void timerCallback() override;
    void applyOptions();

    static juce::String songNameFor (const juce::File&);   // the song's name: its file's
    void historyRestored (const juce::String&);
    void exportWav (bool stems);

    PaulaProcessor& proc;
    using PixButton = prv::ui::PixButton;
    using NumberBox = prv::ui::NumberBox;
    using PixSlider = prv::ui::PixSlider;
    using PixLabel  = prv::ui::PixLabel;

    // title bar
    PixButton newBtn { "NEW" }, loadBtn { "LOAD" }, saveBtn { "SAVE" }, saveAsBtn { "SAVE AS" };
    PixButton fullVelBtn { "FULL VEL", true }, noteOffsBtn { "NOTE-OFFS", true };   // (SAVE MOD / RENDER WAV / EXPORT SAMPLES: the File menu)
    PixButton undoBtn { "UNDO" }, redoBtn { "REDO" }, helpBtn { "HELP", true };
    PixButton zoomOut { "A-" }, zoomIn { "A+" };

    // transport
    PixButton playSongBtn { juce::String::charToString ((juce::juce_wchar) 1) + " SONG" };
    PixButton playBlockBtn { juce::String::charToString ((juce::juce_wchar) 1) + " BLOCK" };
    PixButton stopBtn { juce::String::charToString ((juce::juce_wchar) 2) + " STOP" };
    PixButton editBtn { juce::String::charToString ((juce::juce_wchar) 5) + " EDIT", true };
    PixButton captureBtn { "CAPTURE" };
    PixButton followBtn { "FOLLOW", true };
    PixButton loopBtn { "LOOP", true };
    PixButton panelsBtn { "PANELS", true };
    NumberBox tempoBox { "BPM", 32, 255, 125 }, speedBox { "SPD", 1, 31, 6 }, octBox { "OCT", 0, 6, 3 }, stepBox { "STEP", 0, 16, 1 };
    PixLabel  posLabel;

    // paula strip
    PixButton a500Btn { "A500", true }, a1200Btn { "A1200", true }, a4000Btn { "A4000", true }, cleanBtn { "CLEAN", true };
    PixButton s950Btn { "S950", true }, s1000Btn { "S1000", true };
    PixButton steppedBtn { "STEPPED", true }, eightBitBtn { "8-BIT", true }, crunchBtn { "8CH GRIT", true }, ledBtn { "LED", true };
    PixLabel  ledLabel { {}, false };
    PixSlider sepSlider { "SEP" }, volSlider { "VOL" };   // (not shown: they hold SEP and VOL)
    NumberBox volBox { "VOL", 0, 200, 100 };
    std::unique_ptr<prv::Autosave> autosave;

    // song strip
    PixButton viewBtn { "BIG VIEW", true };
    PixButton mixerBtn { "MIXER", true };
    PixButton skinBtn { "DARK", true }, specBtn { "SPECTRUM", true }, fx2Btn { "FX2", true }, recAutoBtn { "REC AUTO", true }, grooveBtn { "GROOVE", true };
    PixLabel  statusLabel;

    prv::ui::PatternEditor pattern;
    prv::ui::SequencePanel sequence;
    prv::ui::BlockPanel blockPanel;
    prv::ui::InstrumentPanel instruments;
    prv::ui::Spectrum spectrum;
    prv::ui::SampleEditor sampleEditor;
    prv::ui::SampleBrowser browser;
    prv::ui::ParamsPage params;
    prv::ui::SynthPage synthPage;
    prv::ui::MidiOutPage midiPage;
    prv::ui::ChoiceBox choice;
    prv::ui::HelpPage help;
    prv::ui::ChordsPage chords;
    prv::ui::SamplePage samplePage;
    prv::ui::CvOutPage cvPage;
    prv::ui::CvSourcesPage cvSources;
    PaulaProcessor::EditorState::View sampleReturn = PaulaProcessor::EditorState::View::Pattern;
    PaulaProcessor::EditorState::View fxReturn = PaulaProcessor::EditorState::View::Params;
    PaulaProcessor::EditorState::View chordsReturn = PaulaProcessor::EditorState::View::Pattern;
    PaulaProcessor::EditorState::View helpReturn = PaulaProcessor::EditorState::View::Pattern;
    prv::ui::ArrangePage arrange;
    prv::ui::MixerPage mixer;
    prv::ui::OutputMeter outMeter;
    using View = PaulaProcessor::EditorState::View;
    View returnView = View::Pattern;

    std::unique_ptr<juce::FileChooser> chooser;
    bool lastDirty = false;
    juce::String statusText;
    int statusTimer = 0;
    bool showPanels = true;
    bool lastPlaying = false;
    int lastSeq = -1;

    // REC AUTO: what each learned control was last seen at, and what was last written
    float recSeenTrack[prv::kMaxTracks][prv::kTrackKnobs];
    float recSeenInst[prv::kMaxInstruments][prv::kInstrumentKnobs];
    int   recLastTrack[prv::kMaxTracks][prv::kTrackKnobs];
    int   recLastInst[prv::kMaxInstruments][prv::kInstrumentKnobs];
    bool  recPass = false;           // this pass has an undo step already
    float targetValue (const prv::AutoTarget&);   // a learned control's value now (-1 = can't tell)
    void  recordValue (int track, char letter, float value, int& lastWritten);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaulaEditor)
};
