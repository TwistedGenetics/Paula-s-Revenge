// Paula's Revenge - band-limited steps (BLEP) for Paula's stepped output
//
// Paula holds each sample until the next one (a staircase). Played straight at the
// computer's rate, the staircase's high copies fold back as out-of-tune tones. Instead,
// every change of level is added as a band-limited step: a precomputed, minimum-phase
// step that rises from 0 to 1 with nothing above about 20 kHz in it. What's below
// 20 kHz - including Paula's own staircase images - is kept exactly, nothing folds back,
// and the sound is the same at 44.1, 48 or 96 kHz. (After OpenMPT / Antti S. Lankila.)
#pragma once

#include <vector>
#include <cstddef>
#include <cmath>

namespace prv
{
// the step table for one output sample rate
class BlepTable
{
public:
    static constexpr int kTaps  = 64;    // the step's length, in output samples
    static constexpr int kPhase = 128;   // phases per output sample (finer than Paula's own 0.28 us clock)

    void build (double sampleRate);      // (a few ms: on a sample-rate change, not in the audio)
    double rate() const { return builtRate; }
    double cutoff() const { return fc; }
    int length() const { return len; }   // output samples until the step has settled (below -100 dB)
    // the step's value x output samples after it started (0 before, 1 once settled)
    float at (double x) const
    {
        if (x <= 0.0) return 0.0f;
        const double p = x * kPhase;
        if (p >= (double) (kTaps * kPhase)) return 1.0f;
        const int i = (int) p;
        const float f = (float) (p - i);
        return step[(size_t) i] + (step[(size_t) i + 1] - step[(size_t) i]) * f;
    }
    // step(k + q / kPhase) - 1 for k = 0 .. length() - 1: what an event q/kPhase of a sample
    // before an output instant still lacks at that instant and the ones after it
    const float* residual (int q) const { return res.data() + (size_t) q * (size_t) kTaps; }

private:
    std::vector<float> step, res;
    double builtRate = 0, fc = 0;
    int len = kTaps;
};

// one Paula channel's output: the held level, made of band-limited steps.
// Each change adds its whole step to a ring of the coming output samples at once
// (a short run of multiply-adds the compiler vectorises), so playing it back is one read.
class BlepChannel
{
public:
    static constexpr int kRing = 128;    // > kTaps + the grid's delay, a power of two

    // before the steps for an output sample: `instant` is the one out() will give next
    void begin (long long instant)
    {
        if (started && next == instant) return;
        if (started && ! idle()) return;   // (can't happen: a busy channel is played every sample)
        const float l = lastL, r = lastR;
        const bool held = l != 0.0f || r != 0.0f;
        reset();
        next = instant; started = true;
        if (held) { lastL = l; lastR = r; sumL = l; sumR = r; stereo = l != r; }
    }
    // the level becomes (l, r) at time t (absolute, in output samples)
    void set (const BlepTable& tab, double t, float l, float r)
    {
        const float dl = l - lastL, dr = r - lastR;
        if (dl == 0.0f && dr == 0.0f) return;
        lastL = l; lastR = r;
        if (! started) { next = (long long) std::floor (t) + 1; started = true; }
        long long m = (long long) std::ceil (t - 1e-9);
        if (m < next) m = next;                              // (no going back before what's been played)
        if (m - next > kRing - BlepTable::kTaps - 1) m = next + kRing - BlepTable::kTaps - 1;
        double f = (double) m - t;                           // how far before instant m it happened
        f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
        const int q = (int) (f * BlepTable::kPhase + 0.5);
        const float* rs = tab.residual (q);
        const int n = tab.length();
        const int at = (int) ((pos + (m - next)) & (kRing - 1));
        incL[at] += dl; if (stereo) incR[at] += dr;
        // two straight runs (round the end of the ring): a step never reaches a lap ahead
        const int first = n < kRing - at ? n : kRing - at;
        if (dl != dr && ! stereo)
        {
            // the first time left and right differ: the right side so far was the left's
            for (int k = 0; k < kRing; ++k) { accR[k] = accL[k]; incR[k] = incL[k]; }
            sumR = sumL;
            incR[at] += dr - dl;   // (incL already has dl; the copy put dl here too)
            stereo = true;
        }
        addRun (accL + at, rs, dl, first);
        addRun (accL, rs + first, dl, n - first);
        if (stereo)
        {
            addRun (accR + at, rs, dr, first);
            addRun (accR, rs + first, dr, n - first);
        }
        if (m + n > busyUntil) busyUntil = m + n;
    }
    // the output at the next instant (call once per output sample, after any set() for it)
    void out (float& l, float& r)
    {
        if (! started) { l = r = 0.0f; return; }
        const int i = pos;
        sumL += incL[i];
        incL[i] = 0.0f;
        l = (float) (sumL + accL[i]);
        accL[i] = 0.0f;
        if (stereo)
        {
            sumR += incR[i];
            incR[i] = 0.0f;
            r = (float) (sumR + accR[i]);
            accR[i] = 0.0f;
        }
        else r = l;
        pos = (i + 1) & (kRing - 1);
        ++next;
    }
    bool idle() const { return ! started || (next > busyUntil && lastL == 0.0f && lastR == 0.0f && std::abs (sumL) < 1e-9 && std::abs (sumR) < 1e-9); }
    bool settled() const { return ! started || next > busyUntil; }
    float targetL() const { return lastL; }
    float targetR() const { return lastR; }
    long long nextInstant() const { return next; }
    void reset()
    {
        for (auto& x : accL) x = 0.0f;
        for (auto& x : accR) x = 0.0f;
        for (auto& x : incL) x = 0.0f;
        for (auto& x : incR) x = 0.0f;
        sumL = sumR = 0.0; lastL = lastR = 0.0f;
        pos = 0; next = 0; busyUntil = 0; started = false; stereo = false;
    }

private:
    float accL[kRing] {}, accR[kRing] {};   // what the steps still lack at each coming instant
    float incL[kRing] {}, incR[kRing] {};
    double sumL = 0, sumR = 0;           // the settled level
    float lastL = 0, lastR = 0;          // the level the channel is heading for
    int pos = 0;                         // ring index of the next instant
    long long next = 0, busyUntil = 0;   // the next instant; the last one a step still touches
    bool started = false;
    bool stereo = false;                 // left and right have differed (else the right is the left)
    static void addRun (float* __restrict acc, const float* __restrict rs, float d, int n)
    {
        for (int k = 0; k < n; ++k) acc[k] += d * rs[k];
    }
};
} // namespace prv
