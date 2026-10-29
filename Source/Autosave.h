// Paula's Revenge - autosave and crash recovery (the standalone app)
//
// While the app runs, a copy of the song goes to the app's own folder every couple of minutes
// when something changed (written in the background, so the music doesn't stutter). A marker
// says "running"; a clean quit removes it. If the app finds the marker at the next start, it
// didn't quit cleanly (a crash, a plug-in taking it down, power), and offers the autosave back.
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "SongFile.h"

namespace prv
{
class Autosave
{
public:
    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Paula's Revenge").getChildFile ("Autosave");
    }
    static juce::File songFile()   { return folder().getChildFile ("autosave.prv"); }
    static juce::File olderFile()  { return folder().getChildFile ("autosave-previous.prv"); }
    static juce::File infoFile()   { return folder().getChildFile ("autosave.txt"); }   // the song's own file, when it had one
    static juce::File runningFile(){ return folder().getChildFile ("running.flag"); }

    explicit Autosave (PaulaProcessor& p) : proc (p) {}
    ~Autosave() { if (worker.joinable()) worker.join(); }

    int intervalSeconds = 120;

    // the start of a session: was the last one cut short (with an autosave to offer)?
    bool startSession()
    {
        folder().createDirectory();
        const bool crashed = runningFile().existsAsFile() && songFile().existsAsFile();
        runningFile().replaceWithText (juce::Time::getCurrentTime().toISO8601 (true));
        lastId = proc.history.changeId();
        lastSave = juce::Time::getMillisecondCounterHiRes();
        return crashed;
    }
    // a clean quit: the autosave stays (as a spare) but won't be offered
    static void endSession() { runningFile().deleteFile(); }

    juce::Time savedAt() const { return songFile().getLastModificationTime(); }
    static juce::String savedFrom() { return infoFile().loadFileAsString().trim(); }

    // every timer tick: saves when something changed and the time has come (or now, if forced)
    bool tick (bool force = false)
    {
        const auto id = proc.history.changeId();
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (busy.load() || id == lastId || (! force && now - lastSave < intervalSeconds * 1000.0)) return false;
        lastId = id; lastSave = now;
        // a copy of the song (fast) here; packing and writing it in the background
        proc.capturePluginStates();
        auto copy = std::make_shared<Song>();
        proc.withLock ([&] { *copy = proc.song; });
        const juce::String from = proc.editorState.currentFile;
        if (worker.joinable()) worker.join();
        busy = true;
        worker = std::thread ([this, copy, from]
        {
            const auto data = writeSong (*copy);
            const auto tmp = folder().getChildFile ("autosave.tmp");
            if (tmp.replaceWithData (data.getData(), data.getSize()))
            {
                if (songFile().existsAsFile()) songFile().copyFileTo (olderFile());
                tmp.moveFileTo (songFile());
                infoFile().replaceWithText (from);
                ++saves;
            }
            busy = false;
        });
        return true;
    }
    void waitForIt() { if (worker.joinable()) worker.join(); }
    int savesMade() const { return saves.load(); }

private:
    PaulaProcessor& proc;
    uint64_t lastId = 0;
    double lastSave = 0;
    std::thread worker;
    std::atomic<bool> busy { false };
    std::atomic<int> saves { 0 };
};
} // namespace prv
