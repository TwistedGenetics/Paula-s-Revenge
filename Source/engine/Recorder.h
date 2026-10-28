// Paula's Revenge - SAMPLE: recording audio (the input, or the app's own output) into an
// instrument. Plain C++: the audio thread writes into a buffer allocated beforehand, so
// recording never allocates or locks; the trimming / normalising / resampling is unit tested.
#pragma once

#include "Song.h"
#include <atomic>
#include <cstdint>
#include <vector>

namespace prv
{
enum class RecTrigger { Manual, Threshold, WithPlayback };
enum class RecChannels { Stereo, Mono, Left, Right };

class TakeRecorder
{
public:
    enum class State { Idle, Armed, Recording, Done };

    // ---- the message thread (while the audio thread isn't inside process(): the app's lock)
    // Allocates room for maxSeconds and waits for the trigger (Manual: records straight away).
    void arm (double sampleRate, double maxSeconds, RecTrigger trigger, float thresholdLinear, double prerollSeconds = 0.02);
    void stop();                       // Recording -> Done; Armed -> Idle (nothing recorded)
    void reset();                      // back to Idle, the buffers freed
    // the take (when Done): moved out, the recorder back to Idle
    void takeAway (std::vector<float>& left, std::vector<float>& right);

    State state() const               { return st.load (std::memory_order_acquire); }
    int64_t frames() const            { return written.load (std::memory_order_acquire); }
    double seconds() const            { return rate > 0 ? (double) frames() / rate : 0.0; }
    double maxSeconds() const         { return rate > 0 ? (double) capacity / rate : 0.0; }
    bool hitLimit() const             { return full.load(); }
    double sampleRate() const         { return rate; }
    RecTrigger trigger() const        { return trig; }
    // what the recorder hears, for the meter: the peak since the last call (and if it clipped)
    float takePeak (int channel);
    bool  takeClip();
    // a copy of the latest audio (for drawing while recording): up to `count` frames, mono
    void recentFrames (std::vector<float>& out, int count) const;

    // ---- the audio thread
    void process (const float* left, const float* right, int n, bool playing);

private:
    std::atomic<State> st { State::Idle };
    std::atomic<int64_t> written { 0 };
    std::atomic<bool> full { false };
    std::atomic<float> peakL { 0 }, peakR { 0 };
    std::atomic<bool> clipped { false };
    RecTrigger trig = RecTrigger::Manual;
    float threshold = 0.05f;
    double rate = 0;
    int64_t capacity = 0;
    std::vector<float> bufL, bufR;
    std::vector<float> preL, preR;     // the pre-roll ring (THRESHOLD: the attack isn't cut off)
    int prePos = 0, preFill = 0;
    void write (const float* l, const float* r, int n);
};

// ---- after recording (pure functions, unit tested)

// the take without silence at either end: [start, end) of the frames louder than `floor`,
// with `padFrames` kept before the start
void findTrim (const std::vector<float>& left, const std::vector<float>& right, float floor, int padFrames, int64_t& start, int64_t& end);
// scales so the loudest sample is `peak` (silence stays silence)
void normalise (std::vector<float>& left, std::vector<float>& right, float peak = 0.989f);
// a new sample rate (filtered when going down, so it doesn't alias)
std::vector<float> resample (const std::vector<float>& in, double fromRate, double toRate);

struct TakeOptions
{
    RecChannels channels = RecChannels::Stereo;
    bool trimSilence = true;
    bool normalise = true;
    double amigaRate = 0;              // 0 = keep the recording's rate; else e.g. 8363, 16726, 22050, 28867
    std::string name = "SAMPLE";
};
// the take as an instrument's sample (16-bit, plays at the recorded pitch on C-3)
bool takeToInstrument (const std::vector<float>& left, const std::vector<float>& right, double rate, const TakeOptions&, Instrument& out);
} // namespace prv
