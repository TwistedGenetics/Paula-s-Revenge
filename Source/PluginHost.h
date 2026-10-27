// Paula's Revenge - hosting VST3 / AU effects in the mixer's insert slots
#pragma once

#include <JuceHeader.h>
#include "engine/Effects.h"
#include "engine/PluginSynth.h"

namespace prv
{
// The song's tempo and transport, for plug-ins (synced delays etc.)
class HostPlayHead : public juce::AudioPlayHead
{
public:
    std::atomic<double> bpm { 125.0 }, sampleRate { 48000.0 };
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> samples { 0 };
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm.load());
        p.setIsPlaying (playing.load());
        p.setTimeInSamples (samples.load());
        p.setTimeInSeconds ((double) samples.load() / std::max (1.0, sampleRate.load()));
        p.setPpqPosition ((double) samples.load() / std::max (1.0, sampleRate.load()) * bpm.load() / 60.0);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

class PluginHost;

// A loaded plug-in in an insert slot
class PluginEffect : public Effect
{
public:
    PluginEffect (PluginHost&, std::unique_ptr<juce::AudioPluginInstance>, HostPlayHead*);
    ~PluginEffect() override;
    void prepare (double sampleRate) override;
    void reset() override;
    void process (float* left, float* right, int numSamples, const float* params, double bpm) override;
    void setParameter (int index, float value01) override;
    juce::AudioPluginInstance& instance() { return *plugin; }
    std::vector<uint8_t> state();

private:
    PluginHost& host;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    HostPlayHead* playHead = nullptr;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int blockSize = 512;
    double rate = 48000.0;
};

// A loaded plug-in instrument
class PluginSynthImpl : public PluginSynth
{
public:
    PluginSynthImpl (PluginHost&, std::unique_ptr<juce::AudioPluginInstance>, HostPlayHead*);
    ~PluginSynthImpl() override;
    void prepare (double sampleRate, int maxBlock) override;
    void noteOn (int note, int velocity, int offset) override;
    void noteOff (int note, int offset) override;
    void allNotesOff (int offset) override;
    void pitchBend (int value14, int offset) override;
    void controller (int cc, int value, int offset) override;
    void setParameter (int index, float value01) override;
    void render (float* left, float* right, int numSamples) override;
    juce::AudioPluginInstance& instance() { return *plugin; }
    std::vector<uint8_t> state();

private:
    PluginHost& host;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    HostPlayHead* playHead = nullptr;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int blockSize = 512;
};

class PluginHost
{
public:
    PluginHost();
    ~PluginHost();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    HostPlayHead playHead;

    // ---- the list of effect plug-ins on this computer (remembered between runs)
    void startScan();                           // in the background
    bool isScanning() const { return scanning.load(); }
    juce::String scanStatus() const;            // e.g. "SCANNING 12 OF 80: VALHALLA..."
    std::vector<juce::PluginDescription> effects() const;   // effects only, sorted by name
    std::vector<juce::PluginDescription> instruments() const;   // instruments only, sorted by name
    juce::StringArray failedFiles() const;
    std::function<void()> onScanFinished;       // (message thread)
    void saveList();

    // the child copy of the app that looks at one plug-in file ("--prv-scan format file out.xml")
    static bool isScanChild (const juce::StringArray& args);
    static void runScanChild (const juce::StringArray& args);

    static std::string keyFor (const juce::PluginDescription& d) { return d.createIdentifierString().toStdString(); }
    bool findDescription (const std::string& key, juce::PluginDescription& out) const;

    // ---- making the effect for a slot. Null when the plug-in isn't available here
    // (the slot then passes audio through). `slot` >= 0 allows loading in the
    // background for plug-ins that can't load instantly; onAsyncReady(slot) then fires.
    std::unique_ptr<Effect> create (int slot, const FxSlot&, double sampleRate);
    std::function<void (int slot)> onAsyncReady;
    // a plug-in instrument for an instrument slot (null = not available here)
    std::unique_ptr<PluginSynth> createSynth (int instrument, const Instrument&, double sampleRate);
    juce::String lastError (const std::string& key) const;

    // ---- the plug-in's own window
    void openEditor (juce::AudioPluginInstance&, const juce::String& title);
    void closeEditor (juce::AudioPluginInstance&);
    bool isEditorOpen (juce::AudioPluginInstance&) const;
    void closeAllEditors();

    // parameters, for LEARN and for showing what a knob controls
    static juce::String parameterName (juce::AudioPluginInstance&, int index);
    static std::vector<float> parameterValues (juce::AudioPluginInstance&);

    int maxBlock = 512;
    bool showWindows = true;   // (tests without a screen turn this off)

private:
    std::atomic<bool> scanning { false };
    mutable juce::CriticalSection statusLock;
    juce::String status;
    juce::StringArray failed;
    std::unique_ptr<juce::Thread> scanThread;
    std::map<std::string, juce::String> errors;
    std::map<int, std::unique_ptr<juce::AudioPluginInstance>> asyncReady;   // slot -> loaded in the background
    std::map<int, std::string> asyncPending;                                 // slot -> key being loaded
    struct Window;
    std::map<juce::AudioPluginInstance*, std::unique_ptr<Window>> windows;
    std::unique_ptr<Effect> wrap (std::unique_ptr<juce::AudioPluginInstance>, const FxSlot&, double sampleRate);
    friend class ScanThread;
};
} // namespace prv
