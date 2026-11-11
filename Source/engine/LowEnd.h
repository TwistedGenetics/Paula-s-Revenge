// Paula's Revenge - the LOW-END CHECK: listens to the master output as it plays and says, in plain words,
// whether the bass is mono (and not out of phase), whether there's too little or too much sub against the rest,
// whether the low mids are muddy, and whether it's clipping. The last few seconds count (each figure is an
// average that forgets over about 2 s). Plain C++: the audio thread feeds it, the screen reads the report.
#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace prv
{
struct LowEndReport
{
    bool  signal = false;   // anything playing (above -50 dB)
    float lowCorr = 1;      // below 120 Hz: left against right, 1 = mono, 0 = unrelated, -1 = out of phase
    float subDb = -99;      // the sub (below 80 Hz) against everything, dB
    float mudDb = -99;      // 200 - 500 Hz against everything, dB
    float peakDb = -99;     // the loudest sample in the last few seconds
    int   clips = 0;        // samples at or over full scale in the last few seconds
};

// one line of advice: 0 good, 1 worth a look, 2 fix it
struct LowEndLine { int level = 0; std::string title, detail; };
std::vector<LowEndLine> lowEndAdvice (const LowEndReport&);

class LowEndCheck
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (const float* L, const float* R, int n);   // (the audio thread)
    LowEndReport report() const;                            // (any thread)

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void lowPass (double sr, double hz);
        void highPass (double sr, double hz);
        float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    double sr = 48000;
    float coef = 0;                         // the averages' memory (about 2 s)
    Biquad lowL[2], lowR[2];                // 24 dB/oct at 120 Hz, each side
    Biquad sub[2];                          // 24 dB/oct at 80 Hz, the mid
    Biquad mudHp[2], mudLp[2];              // 200 - 500 Hz, the mid
    double ll = 0, rr = 0, lr = 0, all = 0, subE = 0, mudE = 0;
    float peak = 0; int clipCount = 0; double clipAge = 1e9, peakAge = 1e9;
    std::atomic<float> aCorr { 1 }, aSub { -99 }, aMud { -99 }, aPeak { -99 }, aAll { 0 };
    std::atomic<int> aClips { 0 };
};
} // namespace prv
