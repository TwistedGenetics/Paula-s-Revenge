// Paula's Revenge - STEMS in the app: the models (downloaded once, checked against their SHA-256,
// kept in the app's folder) and the separation, on a thread of its own; the results come back on
// the message thread as four stereo stems at 44.1 kHz.
#pragma once

#include <JuceHeader.h>
#include "engine/stems/Stems.h"

namespace prv
{
class StemsJob : private juce::Thread
{
public:
    StemsJob();
    ~StemsJob() override;

    static juce::File modelFolder();
    static juce::String baseUrl();                 // where the models are downloaded from
    static bool haveModels (stems::Quality);
    static int64_t downloadSize (stems::Quality);  // (roughly: for the question before downloading)

    // start: the sample (any rate), the quality; done() on the message thread
    using Done = std::function<void (bool ok, juce::String error, std::shared_ptr<stems::StemResult>)>;
    bool start (std::vector<float> left, std::vector<float> right, double rate, stems::Quality, Done done);
    void cancel();
    bool running() const { return isThreadRunning(); }

    // what it's doing, for the progress bar (any thread)
    double progress() const { return prog.load(); }
    juce::String stage() const;
    double secondsLeft() const;                    // (-1 = can't say yet)

    // (tests: other ways of getting the models and of separating)
    std::function<bool (const juce::String& name, const juce::File& to, std::function<void (double)> progress, juce::String& error)> download;
    std::function<bool (stems::Quality, const stems::ModelPath&, const std::vector<float>&, const std::vector<float>&, double,
                        stems::StemResult&, stems::Progress&, std::string&)> separator;

private:
    std::vector<float> L, R;
    double rate = 0;
    stems::Quality quality = stems::Quality::Best;
    Done done;
    std::atomic<bool> stop { false };
    std::atomic<double> prog { 0 };
    std::atomic<double> started { 0 }, sepStarted { 0 };
    mutable juce::CriticalSection cs;
    juce::String what;
    void setStage (const juce::String& s) { const juce::ScopedLock l (cs); what = s; }
    void run() override;
    bool fetchModels (juce::String& error);
    bool webDownload (const juce::String& name, const juce::File& to, std::function<void (double)> progress, juce::String& error);
};
} // namespace prv
