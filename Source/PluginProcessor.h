// Paula's Revenge - audio processor (shared by the plugin and the standalone app)
#pragma once

#include <JuceHeader.h>
#include "engine/Song.h"
#include "engine/Player.h"
#include "History.h"
#include "PluginHost.h"
#include "MidiOutHost.h"

class PaulaProcessor : public juce::AudioProcessor
{
public:
    PaulaProcessor();
    ~PaulaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Paula's Revenge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- shared state. Lock `lock` for any change to song or player. ------
    juce::SpinLock lock;
    prv::Song song;
    prv::PluginHost pluginHost;   // (before the player: plug-ins in the mixer go first)
    prv::MidiOutHost midiHost;
    prv::Player player;

    template <typename Fn> void withLock (Fn&& fn)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        fn();
    }

    // one undo/redo history shared by every page (kept while the window is closed)
    History history { *this };

    // Make the mixer's effect objects match song.mixer (message thread; call after
    // changing an effect type, loading, undo).
    void updateMixerEffects();
    // Copy each loaded plug-in's own settings into the song (before saving, rendering, undo steps)
    void capturePluginStates();
    bool pluginLoaded (int slot) const;              // a plug-in slot whose plug-in is running
    prv::PluginEffect* pluginAt (int slot) const;
    // force a plug-in slot to load again (DEFAULTS, or after a background load)
    void reloadMixerSlot (int slot);
    // what RENDER WAV uses to make plug-ins
    std::function<std::unique_ptr<prv::Effect> (int, const prv::FxSlot&, double)> pluginFactory();
    std::function<std::unique_ptr<prv::PluginSynth> (int, const prv::Instrument&, double)> synthFactory();
    // plug-in instruments
    prv::PluginSynthImpl* synthAt (int instrument) const;
    prv::MidiOutSynth* midiAt (int instrument) const;
    // the instrument's plug-in (or MIDI output) is running
    bool pluginLoadedInstrument (int instrument) const { return const_cast<PaulaProcessor*> (this)->player.synths().at (instrument) != nullptr; }
    juce::String instrumentError (int instrument) const;   // why it isn't
    // every MIDI-out instrument: all notes off now (STOP when already stopped)
    void midiPanic();
    // the last CC that came in on a MIDI input (for LEARN CC); serial counts them
    std::atomic<int> lastCcNumber { -1 }, ccSerial { 0 };
    void reloadInstrumentPlugin (int instrument);

    // Replace the whole song (loading). Stops playback.
    void replaceSong (prv::Song&& newSong);
    bool isRunningStandalone() const { return true; }   // v0.8: the app is standalone only

    // editor state that should survive closing the window / reloading the set
    struct EditorState
    {
        int instrument = 0;     // 0-based
        int octave = 3;
        int step = 1;
        int block = 0;
        int seqPos = 0;
        int line = 0;
        int track = 0;
        int column = 0;
        bool editMode = false;
        bool follow = true;
        bool fourTrackView = true;  // "BIG VIEW": 4 tracks at a time with large text
        bool darkSkin = true;
        enum class View { Pattern, SampleEdit, Browser, Params, Arrange, Mixer, Synth, MidiOut, Help } view = View::Pattern;
        bool recAuto = false;         // REC AUTO: moving a learned control while EDIT + playing writes its command
        bool showSpectrum = true;
        bool showFx2 = false;         // second effect column (FX2 button)
        int  uiZoom = 0;        // -2..+3 text size adjust
        juce::String lastFolder;      // songs
        juce::String sampleFolder;    // sample browser
        juce::String currentFile;     // where SAVE writes ("" = ask)
        // marked loop in the pattern (lines, inclusive); loopBlock < 0 = none
        bool loopOn = false;
        int  loopBlock = -1, loopFrom = 0, loopTo = 0;
        bool sectionLoop = false;     // song play loops the selected position's section
        // highlighted song positions: seqAnchor .. seqPos (equal = just the one); shared clipboard
        int  seqAnchor = -1, seqSelCursor = -1;   // the range holds while seqPos == seqSelCursor
        std::vector<int> seqClip;
    } editorState;

    // MIDI notes jam the current instrument; the editor records them when in edit mode
    std::atomic<int> jamInstrument { 0 };
    // when set (>= 0) MIDI notes play only this region of the instrument (sample editor "SEL KEYS")
    std::atomic<int> jamRangeStart { -1 }, jamRangeEnd { -1 };
    std::atomic<bool> jamRangeLoop { false };
    bool popMidiNote (int& note);   // message thread

    // ---- audition player for the sample browser and the sample editor ----
    struct PreviewAudio
    {
        std::vector<float> left, right;   // right empty = mono
        double rate = 44100.0;
    };
    // Starts playing `audio` from `start` to `end` (samples). Returns the
    // previous preview so it is freed on the calling (message) thread.
    // `pitch` speeds it up (2 = an octave higher); playNow = false just loads it.
    std::unique_ptr<PreviewAudio> startPreview (std::unique_ptr<PreviewAudio> audio, int start = 0, int end = -1, bool loop = false,
                                                double pitch = 1.0, bool playNow = true);
    std::unique_ptr<PreviewAudio> stopPreview();
    void restartPreview (int start, double pitch);   // replay what's loaded (no-op if nothing is)
    void pausePreview();                              // stop but keep it loaded
    bool hasPreview() const { return preview != nullptr; }
    // while set, MIDI notes play the loaded preview at their pitch (MIDI 60 = original)
    std::atomic<bool> midiToPreview { false };
    std::atomic<int>  previewPos { -1 };    // current position, -1 when silent
    std::atomic<float> previewGain { 0.8f };

    // mono copy of the output for the spectrum display (message thread reads)
    int readScope (float* dest, int maxSamples);

private:
    double currentRate = 44100.0;
    juce::HeapBlock<float> scratchL, scratchR;
    int scratchSize = 0;
    juce::AbstractFifo midiFifo { 64 };
    int midiNotes[64] {};
    std::unique_ptr<PreviewAudio> preview;
    double prevPos = 0, prevPitch = 1.0;
    int prevStart = 0, prevEnd = 0;
    bool prevLoop = false, prevPlaying = false;
    void renderPreview (float* l, float* r, int n);
    juce::AbstractFifo scopeFifo { 8192 };
    float scopeBuf[8192] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PaulaProcessor)
};
