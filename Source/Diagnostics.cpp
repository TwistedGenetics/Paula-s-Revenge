// Paula's Revenge - DIAGNOSTICS (see Diagnostics.h)
#include "Diagnostics.h"

namespace prv
{
Diagnostics& Diagnostics::get()
{
    static Diagnostics d;
    return d;
}

Diagnostics::Diagnostics()
{
    folder().createDirectory();
    write ("==== SESSION START: PAULA'S REVENGE " JucePlugin_VersionString " on " + juce::SystemStats::getOperatingSystemName()
           + ", " + juce::SystemStats::getCpuModel() + " (" + juce::String (juce::SystemStats::getNumCpus()) + " cores), "
           + juce::String (juce::SystemStats::getMemorySizeInMegabytes() / 1024.0, 1) + " GB");
}

juce::File Diagnostics::folder() const
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/Paula's Revenge");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Paula's Revenge").getChildFile ("Logs");
   #endif
}

juce::File Diagnostics::logFile() const { return folder().getChildFile ("Paula's Revenge.log"); }

void Diagnostics::audioBlock (double tookMs, double budgetMs, double lockWaitMs)
{
    if (budgetMs <= 0) return;
    const float load = (float) (tookMs / budgetMs);
    ++blocks;
    avgLoad = avgLoad.load (std::memory_order_relaxed) * 0.995f + load * 0.005f;
    if (load > maxLoad.load (std::memory_order_relaxed)) maxLoad = load;
    if (load > 1.0f) ++overruns;
    else if (load > 0.8f) ++close;
    if (lockWaitMs > 1.0) { ++lockWaits; if (lockWaitMs > maxLock.load (std::memory_order_relaxed)) maxLock = (float) lockWaitMs; }
    if (load > 0.8f || lockWaitMs > 1.0)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 == 0) { ++lost; return; }
        events[n1 > 0 ? s1 : s2] = { juce::Time::getMillisecondCounterHiRes(), tookMs, budgetMs, lockWaitMs };
        fifo.finishedWrite (1);
    }
}

Diagnostics::Counts Diagnostics::counts() const
{
    Counts c;
    c.blocks = blocks.load(); c.overruns = overruns.load(); c.close = close.load(); c.lockWaits = lockWaits.load();
    c.stalls = stalls; c.xruns = xrunsTotal;
    c.maxLoad = maxLoad.load(); c.avgLoad = avgLoad.load(); c.maxLockMs = maxLock.load();
    return c;
}

void Diagnostics::resetCounts()
{
    blocks = 0; overruns = 0; close = 0; lockWaits = 0; lost = 0;
    maxLoad = 0; avgLoad = 0; maxLock = 0;
    stalls = 0; xrunsTotal = 0;
}

void Diagnostics::write (const juce::String& line)
{
    const juce::ScopedLock l (cs);
    const auto stamped = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "  " + line;
    recent.add (stamped);
    if (recent.size() > 400) recent.removeRange (0, recent.size() - 400);
    const auto f = logFile();
    if (f.getSize() > (2 << 20))
    {
        // (2 MB: the old one kept as .old)
        const auto old = f.getSiblingFile ("Paula's Revenge.old.log");
        old.deleteFile();
        f.moveFileTo (old);
    }
    f.appendText (stamped + "\n", false, false, "\n");
}

void Diagnostics::log (const juce::String& line) { write (line); }

void Diagnostics::tick()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    // the window itself: a long gap between ticks is a stall (the pattern froze)
    if (lastTick > 0 && now - lastTick > 400)
    {
        ++stalls;
        juce::String w = "THE WINDOW STALLED FOR " + juce::String (juce::roundToInt (now - lastTick)) + " MS";
        if (doing.isNotEmpty() && now - doingSince < (now - lastTick) + 2000) w << " (WHILE: " << doing << ")";
        write (w);
    }
    lastTick = now;
    if (doing.isNotEmpty() && now - doingSince > 10000) doing.clear();

    // slow audio blocks
    int shown = 0, more = 0;
    while (fifo.getNumReady() > 0)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        const auto e = events[n1 > 0 ? s1 : s2];
        fifo.finishedRead (1);
        if (shown++ >= 12) { ++more; continue; }
        const double load = e.took / e.budget;
        juce::String w = juce::String (load > 1.0 ? "AUDIO BLOCK TOO SLOW (A DROPOUT): " : "AUDIO BLOCK NEARLY TOO SLOW: ")
                       + juce::String (e.took, 2) + " MS OF " + juce::String (e.budget, 2) + " MS (" + juce::String (juce::roundToInt (load * 100)) + "%)";
        if (e.lock > 1.0) w << ", WAITED " << juce::String (e.lock, 2) << " MS FOR THE SONG";
        if (doing.isNotEmpty()) w << " (WHILE: " << doing << ")";
        write (w);
    }
    if (more > 0) write ("... AND " + juce::String (more) + " MORE SLOW BLOCKS");
    if (const auto l = lost.exchange (0); l > 0) write ("... AND " + juce::String (l) + " MORE (TOO MANY TO LIST)");

    // the driver's own count
    if (xruns)
    {
        const int n = xruns();
        if (n >= 0)
        {
            if (xrunsSeen >= 0 && n > xrunsSeen)
            {
                xrunsTotal += n - xrunsSeen;
                write ("THE AUDIO DRIVER REPORTED " + juce::String (n - xrunsSeen) + " DROPOUT(S)" + (doing.isNotEmpty() ? " (WHILE: " + doing + ")" : juce::String()));
            }
            xrunsSeen = n;
        }
    }

    // a summary a minute while something went wrong in it (and one every 10 minutes anyway)
    const auto c = counts();
    const bool trouble = c.overruns > summaryOverruns;
    if (lastSummary == 0) lastSummary = now;
    if ((trouble && now - lastSummary > 60000) || now - lastSummary > 600000)
    {
        write ("AUDIO SO FAR: " + juce::String (c.blocks) + " BLOCKS, LOAD AVERAGE " + juce::String (juce::roundToInt (c.avgLoad * 100)) + "% (HIGHEST "
               + juce::String (juce::roundToInt (c.maxLoad * 100)) + "%), " + juce::String (c.overruns) + " TOO SLOW, " + juce::String (c.close) + " NEARLY, "
               + juce::String (c.xruns) + " FROM THE DRIVER, " + juce::String (c.stalls) + " WINDOW STALLS");
        lastSummary = now;
        summaryOverruns = c.overruns;
    }
}

juce::String Diagnostics::report()
{
    juce::String r;
    r << "PAULA'S REVENGE DIAGNOSTICS REPORT\n"
      << "Made " << juce::Time::getCurrentTime().toString (true, true) << "\n\n"
      << "App:      " << JucePlugin_VersionString << "\n"
      << "System:   " << juce::SystemStats::getOperatingSystemName() << (juce::SystemStats::isOperatingSystem64Bit() ? " (64-bit)" : "") << "\n"
      << "CPU:      " << juce::SystemStats::getCpuModel() << ", " << juce::SystemStats::getNumCpus() << " cores (" << juce::SystemStats::getNumPhysicalCpus() << " physical)\n"
      << "Memory:   " << juce::SystemStats::getMemorySizeInMegabytes() << " MB\n";
    if (describeAudio) r << "Audio:    " << describeAudio() << "\n";
    if (describeSong)  r << "Song:     " << describeSong() << "\n";
    if (describeRadio) r << "Radio:    " << describeRadio() << "\n";
    const auto c = counts();
    r << "\nTHIS SESSION\n"
      << "  Audio blocks:            " << c.blocks << "\n"
      << "  Load average / highest:  " << juce::roundToInt (c.avgLoad * 100) << "% / " << juce::roundToInt (c.maxLoad * 100) << "%\n"
      << "  Blocks too slow:         " << c.overruns << "   (each one is a dropout)\n"
      << "  Blocks nearly too slow:  " << c.close << "   (over 80%)\n"
      << "  Waits for the song:      " << c.lockWaits << " (longest " << juce::String (c.maxLockMs, 2) << " ms)\n"
      << "  Dropouts from the driver:" << c.xruns << (xruns && xruns() < 0 ? "   (this driver doesn't report them)" : "") << "\n"
      << "  Window stalls:           " << c.stalls << "\n\n"
      << "THE LOG (newest last)\n";
    auto lines = juce::StringArray::fromLines (logFile().loadFileAsString());
    lines.removeEmptyStrings();
    for (int i = std::max (0, lines.size() - 600); i < lines.size(); ++i) r << lines[i] << "\n";
    return r;
}

juce::File Diagnostics::saveReport()
{
    auto desk = juce::File::getSpecialLocation (juce::File::userDesktopDirectory);
    if (! desk.isDirectory()) desk = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    if (! desk.isDirectory()) desk = folder();
    const auto f = desk.getNonexistentChildFile ("Paula's Revenge Diagnostics " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M"), ".txt", true);
    write ("DIAGNOSTICS REPORT SAVED: " + f.getFullPathName());
    f.replaceWithText (report());
    return f;
}
} // namespace prv
