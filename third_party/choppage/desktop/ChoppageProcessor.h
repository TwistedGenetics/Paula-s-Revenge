#pragma once
#include <array>
/*
    ChoppageProcessor — the desktop (JUCE) host for choppage-core.

    Everything musical lives in bc::Session (choppage-core, shared with the MPC
    plugin). This class adds what only a desktop plugin has: JUCE parameters
    built from the core's parameter table, the host's playhead, JUCE's audio
    decoders (FLAC / MP3 as well as WAV / AIFF), WAV writing for DRAG, and
    project state.
*/
#include <JuceHeader.h>
#include "choppage/Session.h"
#include "choppage/Params.h"

// PAULA: built into Paula's Revenge as an instrument it hosts like a plug-in (an AudioPluginInstance)
#ifdef PRV_EMBED_CHOPPAGE
using ChoppageBase = juce::AudioPluginInstance;
#else
using ChoppageBase = juce::AudioProcessor;
#endif

class ChoppageProcessor final : public ChoppageBase, private juce::Timer
{
public:
#ifdef PRV_EMBED_CHOPPAGE
    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name = d.descriptiveName = "CHOPPAGE";
        d.pluginFormatName = "Built-in";
        d.category = "Instrument";
        d.manufacturerName = "Twisted Genetics";
        d.version = "0.10.0";
        d.fileOrIdentifier = "CHOPPAGE";
        d.uniqueId = d.deprecatedUid = 0x43687067;
        d.isInstrument = true;
        d.numInputChannels = 0;
        d.numOutputChannels = 2;
    }
#endif
    using Status = bc::Session::Status;
    using Slot = bc::Session::Slot;
    using SliceField = bc::Session::SliceField;

    ChoppageProcessor();
    ~ChoppageProcessor() override;

    // ---- AudioProcessor ------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using ChoppageBase::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CHOPPAGE"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- the model (forwarded to the core) -----------------------------------------------
    bc::Session& session() noexcept { return sess; }
    // presets (.chp, the same files as the MPC's VST2 Plugins/Content/Presets/CHOPPAGE): sound, breaks, slices, patterns
    juce::File presetsFolder() const;
    bool savePreset (const juce::File&);
    bool loadPreset (const juce::File&);
    const Slot& getSlot (int i) const { return sess.slot (i); }
    int  getSelectedSlot() const noexcept { return sess.selectedSlot(); }
    void selectSlot (int i) { sess.selectSlot (i); }
    int  getSelectedSlice() const noexcept { return sess.selectedSlice(); }
    void selectSlice (int k, bool audition = true) { sess.selectSlice (k, audition); }
    bool isEditingAll() const noexcept { return sess.editingAll(); }
    void setEditingAll (bool b) { sess.setEditingAll (b); }

    void loadFile (int slot, const juce::File& f) { sess.loadFile (slot, f.getFullPathName().toStdString()); }
    void loadDemo (int slot) { sess.loadDemo (slot); }
    void clearSlot (int slot) { sess.clearSlot (slot); }
    void reslice (int slot, bool grid, float sens, int gpb) { sess.reslice (slot, grid, sens, gpb); }
    bool isBusy() const { return sess.isBusy(); }

    float getSliceValue (SliceField f, bool all) const { return sess.getSliceValue (f, all); }
    void  setSliceValue (SliceField f, float v, bool all) { sess.setSliceValue (f, v, all); }
    void  toggleMute (int k) { sess.toggleMute (k); }
    void  setBadge (int k, bc::Badge b) { sess.setBadge (k, b); }
    void  moveMarker (int m, int pos) { sess.moveMarker (m, pos); }
    void  addMarker (int pos) { sess.addMarker (pos); }
    void  deleteMarker (int m) { sess.deleteMarker (m); }

    void rollPattern();
    void rollFx();
    void historyBack() { sess.historyBack(); }
    void historyForward() { sess.historyForward(); }
    void setShowOriginal (bool b) { sess.setShowOriginal (b); }
    void setPatternLength (int bars) { sess.setPatternLength (bars); }
    void toggleStep (int step, bc::Badge lane) { sess.toggleStep (step, lane); }
    void swapStep (int step) { sess.swapStep (step); }
    void toggleLock (int step) { sess.toggleLock (step); }
    void setFxStep (int step, bc::FxType t, int rate) { sess.setFxStep (step, t, rate); }
    void cycleFxRate (int step) { sess.cycleFxRate (step); }
    void saveMemory (int m) { sess.saveMemory (m); }
    void recallMemory (int m) { sess.recallMemory (m); }
    void clearPattern() { sess.clearPattern(); }

    /** One pass of the selected break's pattern as a 24-bit WAV in the temp folder (DRAG). */
    juce::File bounceLoop();
    /** AKAIZE RENDER of the selected break to the host tempo (or its own, x the Akaize stretch). */
    bool akaizeSelected();

    // break browser (DISK)
    void scanBrowser (const juce::File& folder);
    juce::File getBrowserFolder() const { return browserFolder; }

    void setPlaying (bool b) { sess.setPlaying (b); }
    bool isPlaying() const { return sess.isPlaying(); }
    void setPadHeld (int pad, bool held) { sess.setPadHeld (pad, held); }

    int   getCurrentStep()  { return sess.engine().currentStep.load(); }
    int   getCurrentSlice() { return sess.engine().currentSlice.load(); }
    int   getPlayingSlot()  { return sess.engine().playingSlot.load(); }
    int   getMuteGroupOf (int slot) const { return slot >= 0 && slot < bc::kSlots ? sess.muteGroup (slot) : 0; }
    int   getStepOf (int slot)  { return slot >= 0 && slot < bc::kSlots ? sess.engine().slotStep[(size_t) slot].load() : -1; }
    int   getSliceOf (int slot) { return slot >= 0 && slot < bc::kSlots ? sess.engine().slotSlice[(size_t) slot].load() : -1; }
    bool  isPadLit (int p)  { return p >= 0 && p < bc::kFxPads && sess.engine().padLit[(size_t) p].load(); }
    float getPeak()         { return sess.engine().getPeak(); }
    double getHostBpm() const { return hostBpm.load(); }
    bool  isHostPlaying() const { return hostPlaying.load(); }
    static juce::String statusText (Status s) { return bc::Session::statusText (s); }

    /** Plain value of a core parameter (bc::P_...). */
    float plain (int p) const { return values[(size_t) p]->load(); }

    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undo;

   #if CHOP_TEST_BUILD
    bc::Engine& getEngineForTests() { return sess.engine(); }
    void waitUntilIdle (int timeoutMs = 10000) { sess.waitUntilIdle (timeoutMs); }
   #endif

private:
    void timerCallback() override { sess.poll(); keepLinkedFrequencies(); }
    void keepLinkedFrequencies();
    bc::Engine::Settings readSettings() const;

    bc::Session sess;
    juce::AudioFormatManager formats;
    std::vector<std::atomic<float>*> values;
    std::vector<bc::MidiMsg> midiScratch;
    std::array<bool, bc::kFxPads> padParamDown {};
    juce::File browserFolder;
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostPlaying { false };
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoppageProcessor)
};
