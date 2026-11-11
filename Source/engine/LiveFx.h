// Paula's Revenge - LIVE MODE's performance effects on the whole mix, held on a key or a button:
// REPEAT (the next 1/4, 1/8, 1/16 or 1/32 of a beat caught and repeated, in time), TAPE STOP (the mix slowing to a
// stop, pitch and all, and starting up again when let go), and a FILTER sweep (low-pass down or high-pass up while
// held, back when let go). Every change crossfades, so nothing clicks. Plain C++, unit tested.
#pragma once

#include <vector>

namespace prv
{
class LiveFx
{
public:
    enum Kind { None = 0, Repeat4, Repeat8, Repeat16, Repeat32, TapeStop, FilterLow, FilterHigh, Count };
    static const char* name (Kind);        // "REPEAT 1/8", "TAPE STOP", "FILTER DOWN"...

    void prepare (double sampleRate);      // (allocates)
    void reset();
    // hold / let go (any thread: the audio thread picks it up at its next block)
    void hold (Kind k) { wanted = k; }
    void release() { wanted = None; }
    Kind holding() const { return (Kind) wanted; }
    bool busy() const { return active != None || fade > 0.0f || tapeRate < 1.0f || filterAmt > 0.0001f; }
    // the mix, in place; bpm for REPEAT's lengths
    void process (float* left, float* right, int n, double bpm);

private:
    double sr = 48000.0;
    volatile int wanted = None;
    Kind active = None;
    std::vector<float> histL, histR;       // what's been playing (for REPEAT and TAPE STOP)
    int  w = 0;                            // where the next sample goes
    // REPEAT: a slice of the mix caught as it plays, then looped
    int  repStart = 0, repLen = 0, repPos = 0, caught = 0;
    // TAPE STOP: reading behind the live mix at a rate falling to 0 (and back up)
    double tapeRead = 0; float tapeRate = 1.0f;
    bool tapeLagging = false;
    // the crossfade between live and the effect: 0 = live, 1 = the effect
    float fade = 0.0f;
    // FILTER: how far it's swept (0 = open), which way, and its state
    float filterAmt = 0.0f; int filterDir = 0;
    float f1L = 0, f2L = 0, f1R = 0, f2R = 0;   // (low-pass)
    float h1L = 0, h1R = 0, g1L = 0, g1R = 0;   // (high-pass)
    float read (const std::vector<float>& h, double pos) const;
};
} // namespace prv
