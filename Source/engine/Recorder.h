// Paula's Revenge - SAMPLE: recording audio (the input, or the app's own output) into an
// instrument. Plain C++: the audio thread writes into a buffer allocated beforehand, so
// recording never allocates or locks; the trimming / normalising / resampling is unit tested.
#pragma once

#include "Song.h"
#include <atomic>
#include <algorithm>
#include <cmath>
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

// ---- sampling like the old samplers (heard live through MONITOR, and what's recorded)
enum class SamplerModel { Clean = 0, Amiga, S950 };
struct VintageSettings
{
    SamplerModel model = SamplerModel::Clean;   // CLEAN: the interface as it is (the default)
    int    bits = 16;          // 8 or 12 (AMIGA 8, S950 12; either can use the other)
    double rate = 0;           // the sampler's own rate: AMIGA 8363 / 16726 / 22050 / 28867; S950 7500...48000
    float  driveDb = 0;        // INPUT DRIVE: pushes the sampler's input stage into overload (0..+24 dB)
    bool operator== (const VintageSettings& o) const { return model == o.model && bits == o.bits && rate == o.rate && driveDb == o.driveDb; }
};
extern const double kAmigaSampleRates[4];
extern const double kS950SampleRates[8];

// The sampler's input: DRIVE into the input stage (the S950's overloads softly then hard, an
// Amiga sampler's just clips), the anti-alias filter (the S950's 6-pole low-pass following the
// rate; the Amiga samplers had none, so it aliases), then sample-and-hold at the sampler's rate
// and the bits. process() gives what it sounds like at the host rate (stepped) and the samples
// at the sampler's rate (what's recorded). No allocation on the audio thread.
class VintageAdc
{
public:
    void prepare (double hostRate, int maxBlock);
    void set (const VintageSettings&);            // (the message thread, under the app's lock)
    void setDrive (float db) { cfg.driveDb = std::clamp (db, 0.0f, 24.0f); gain = std::pow (10.0f, cfg.driveDb / 20.0f); }   // (no reset: no click)
    const VintageSettings& settings() const { return cfg; }
    bool   active() const { return cfg.model != SamplerModel::Clean && cfg.rate > 0; }
    double outRate() const { return active() ? cfg.rate : host; }
    int    maxOut (int n) const { return (int) std::ceil ((double) n * std::max (1.0, outRate() / std::max (1.0, host))) + 2; }
    // n host samples -> monL/R (n, at the host rate), decL/R (returned count, at the sampler's rate)
    int process (const float* l, const float* r, int n, float* monL, float* monR, float* decL, float* decR);
    void reset();

    static float inputStage (float x, SamplerModel m, float gain);   // DRIVE and the stage's overload
    static float quantise (float x, int bits);

private:
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; float run (float x); void lowpass (double fc, double fs, double q); };
    VintageSettings cfg;
    double host = 48000;
    double phase = 1.0;
    float heldL = 0, heldR = 0;
    float gain = 1;
    bool filtered = false;
    Biquad aaL[3], aaR[3], outL[3], outR[3];   // the S950's input (anti-alias) and output filters
};

struct TakeOptions
{
    RecChannels channels = RecChannels::Stereo;
    bool trimSilence = true;
    bool normalise = true;
    double amigaRate = 0;              // 0 = keep the recording's rate; else e.g. 8363, 16726, 22050, 28867
    int bits = 0;                      // the sampler's bits (8 / 12): kept on that grid after normalising
    std::string name = "SAMPLE";
};
// the take as an instrument's sample (16-bit, plays at the recorded pitch on C-3)
bool takeToInstrument (const std::vector<float>& left, const std::vector<float>& right, double rate, const TakeOptions&, Instrument& out);
} // namespace prv
