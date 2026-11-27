// Paula's Revenge - undo for what happens inside a plug-in's own window
//
// A VST / AU instrument or insert effect changes its settings in its own window, out of Paula's sight. This
// watches every loaded plug-in (instruments and mixer inserts) for the user turning something (the plug-in's
// gesture begin / end, or a new preset), waits until it settles, and puts the plug-in's settings from before
// into the one undo history, so UNDO / REDO step through plug-in tweaks like any other edit.
// Paula's own changes (KNOB automation, MIDI) don't count: plug-ins only send gestures for the user's hand.
// CHOPPAGE is left out: it keeps its own UNDO / REDO for everything in it.
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

class PaulaProcessor;

class PluginUndoWatch : private juce::Timer
{
public:
    explicit PluginUndoWatch (PaulaProcessor& p) : proc (p) {}
    ~PluginUndoWatch() override;

    void start() { startTimer (100); }
    // after UNDO / REDO put a plug-in's settings back: that is the new starting point (and its echo is no edit)
    void rebaseline (bool fx, int index);

    static constexpr int kSettleMs = 450;   // quiet this long after the last touch: one undo step

private:
    struct Tap : juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override
        {
            if (touching.load()) lastMs.store (juce::Time::getMillisecondCounter());
        }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
        {
            if (d.programChanged) { edited.store (true); lastMs.store (juce::Time::getMillisecondCounter()); }
        }
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override
        {
            touching.store (true); edited.store (true); lastMs.store (juce::Time::getMillisecondCounter());
        }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override
        {
            touching.store (false); lastMs.store (juce::Time::getMillisecondCounter());
        }
        std::atomic<bool> touching { false }, edited { false };
        std::atomic<uint32_t> lastMs { 0 };
        void* wrapper = nullptr;
        juce::AudioProcessor* inst = nullptr;
        std::string key;
        std::vector<uint8_t> baseline;          // the settings before the user's next touch
        uint32_t ignoreUntil = 0;
        uint32_t baselineAt = 0;
    };

    void timerCallback() override;
    void watch (bool fx, int index, void* wrapper, juce::AudioProcessor* inst, const std::string& key, const juce::String& name);
    std::vector<uint8_t> stateOf (bool fx, int index) const;

    PaulaProcessor& proc;
    std::map<int, std::unique_ptr<Tap>> taps;      // instruments: index, inserts: 100000 + slot
    std::vector<std::unique_ptr<Tap>> retired;     // (taps of plug-ins since unloaded: kept, a late callback stays safe)
};
