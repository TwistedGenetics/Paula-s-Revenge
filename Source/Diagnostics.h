// Paula's Revenge - DIAGNOSTICS: a log of what could make the sound drop out, to send in.
//
// The audio thread times every block it plays against the time it has for it (the block's
// length) and how long it waited for the song (the lock the pages take to change it); slow
// ones go into a small FIFO (never a wait, never a file on the audio thread). The window's
// timer moves them into the log with the rest: the audio device (and the driver's own
// dropout count, where it gives one), stalls of the window, saves and loads, the radio
// running dry, plug-ins that wouldn't load. Help > Save Diagnostics Report... writes the lot
// to a text file on the desktop.
#pragma once

#include <JuceHeader.h>
#include <atomic>

namespace prv
{
class Diagnostics
{
public:
    static Diagnostics& get();

    // ---- the audio thread
    // one block: how long it took, how long it had, how long it waited for the lock (ms)
    void audioBlock (double tookMs, double budgetMs, double lockWaitMs);

    // ---- the message thread
    void log (const juce::String& line);                     // a line in the log (time-stamped)
    void activity (const juce::String& what) { doing = what; doingSince = juce::Time::getMillisecondCounterHiRes(); }
    void tick();                                              // the window's timer: drains, summarises, watches stalls
    std::function<int()> xruns;                               // the driver's dropout count (-1 = it doesn't say)
    std::function<juce::String()> describeAudio;              // the device, its rate and buffer
    std::function<juce::String()> describeSong;               // what the song holds (samples, plug-ins)
    std::function<juce::String()> describeRadio;              // the radio, if it's playing

    juce::File logFile() const;
    juce::File folder() const;
    juce::String report();                                    // everything, for the report file
    juce::File saveReport();                                  // on the desktop; returns the file

    // the counts so far (this session)
    struct Counts { int64_t blocks = 0, overruns = 0, close = 0, lockWaits = 0, stalls = 0, xruns = 0; double maxLoad = 0, avgLoad = 0, maxLockMs = 0; };
    Counts counts() const;
    void resetCounts();

private:
    Diagnostics();
    struct Event { double at, took, budget, lock; };
    static constexpr int kEvents = 256;
    Event events[kEvents];
    juce::AbstractFifo fifo { kEvents };
    std::atomic<int64_t> blocks { 0 }, overruns { 0 }, close { 0 }, lockWaits { 0 }, lost { 0 };
    std::atomic<float> maxLoad { 0 }, avgLoad { 0 }, maxLock { 0 };
    int64_t stalls = 0, xrunsSeen = -1, xrunsTotal = 0;
    double lastTick = 0, lastSummary = 0, doingSince = 0;
    juce::String doing;
    juce::StringArray recent;                                 // the last lines (for the report)
    juce::CriticalSection cs;
    int64_t summaryBlocks = 0, summaryOverruns = 0;
    void write (const juce::String& line);
};
} // namespace prv
