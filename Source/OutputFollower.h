// Paula's Revenge - FOLLOW MAC OUTPUT: when macOS switches its sound output (headphones
// plugged in, AirPods connected, an interface added or removed), the app goes with it.
#pragma once

#include <JuceHeader.h>

namespace prv
{
// what the follower needs from the audio system (the app gives it the real one, tests a fake)
struct OutputSystem
{
    virtual ~OutputSystem() = default;
    virtual juce::String systemDefaultOutput() = 0;          // the Mac's current output ("" = unknown)
    virtual juce::String currentOutput() = 0;                // the device the app plays through ("" = none)
    virtual juce::StringArray outputs() = 0;                 // output devices the app can open
    virtual bool switchTo (const juce::String& name) = 0;    // keeps the rate / buffer where it can, stereo 1+2
};

class OutputFollower
{
public:
    explicit OutputFollower (OutputSystem& s) : sys (s) {}

    bool enabled = true;
    std::function<void (const juce::String&)> onStatus;
    std::function<void (bool)> onEnabledChanged;    // (switched off because a device was picked by hand)

    // call often (a timer) and when the device list changes: moves to the Mac's output if it changed
    bool check()
    {
        armed = true;
        const auto def = sys.systemDefaultOutput();
        const auto cur = sys.currentOutput();
        if (! enabled || def.isEmpty()) return false;
        if (def == cur) { expected = cur; return false; }
        if (! sys.outputs().contains (def)) return false;     // (not listed yet: next time)
        if (def == failed) return false;                       // (it wouldn't open: don't keep trying)
        switching = true;
        const bool ok = sys.switchTo (def);
        switching = false;
        if (ok) { expected = def; failed = {}; }
        else failed = def;
        status (ok ? "OUTPUT: " + def.toUpperCase() : "COULDN'T OPEN " + def.toUpperCase() + " - STILL ON " + sys.currentOutput().toUpperCase());
        return ok;
    }

    // the app's audio device changed (the settings window, or the system): picked by hand?
    void deviceChanged()
    {
        if (switching || ! enabled || ! armed) return;
        const auto cur = sys.currentOutput();
        if (cur.isEmpty() || cur == expected) return;
        const auto def = sys.systemDefaultOutput();
        if (cur == def) { expected = cur; return; }
        // a device that isn't the Mac's output: the user chose it, so it stays
        enabled = false;
        if (onEnabledChanged) onEnabledChanged (false);
        status ("FOLLOW MAC OUTPUT OFF - STAYING ON " + cur.toUpperCase());
    }

    void setEnabled (bool on)
    {
        enabled = on;
        failed = {};
        if (on) check();
    }

private:
    OutputSystem& sys;
    juce::String expected, failed;
    bool switching = false, armed = false;
    void status (const juce::String& m) { if (onStatus) onStatus (m); }
};

juce::String macDefaultOutputName();   // CoreAudio's default output device's name ("" elsewhere)
} // namespace prv
