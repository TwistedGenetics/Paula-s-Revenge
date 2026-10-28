// Paula's Revenge - the sample editor's tools
#include "SampleTools.h"
#include "Song.h"
#include "Recorder.h"

#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    int16_t clip16 (double v) { return (int16_t) std::clamp ((int) std::lround (v), -32768, 32767); }

    // a position after [a, b) is taken out
    int afterRemove (int p, int a, int b) { return p <= a ? p : (p >= b ? p - (b - a) : a); }
    int afterInsert (int p, int at, int n) { return p < at ? p : p + n; }

    void fixLoop (Instrument& in)
    {
        const int n = (int) in.data.size();
        in.loopStart = std::clamp (in.loopStart, 0, n);
        in.loopLength = std::clamp (in.loopLength, 0, n - in.loopStart);
        if (in.loopLength < 4) in.loopStart = in.loopLength = 0;
        in.playStart = std::clamp (in.playStart, 0, std::max (0, n - 1));
        if (in.playEnd >= n || in.playEnd <= in.playStart) in.playEnd = 0;
        std::vector<int> sl;
        for (int p : in.slices) if (p >= 0 && p < n && (sl.empty() || p > sl.back())) sl.push_back (p);
        if (sl.size() <= 1) sl.clear();
        in.slices = sl;
    }
}

void sampleGain (std::vector<int16_t>& d, int a, int b, double db)
{
    const double g = std::pow (10.0, db / 20.0);
    a = std::clamp (a, 0, (int) d.size()); b = std::clamp (b, a, (int) d.size());
    for (int i = a; i < b; ++i) d[(size_t) i] = clip16 (d[(size_t) i] * g);
}

void removeDc (std::vector<int16_t>& d, int a, int b)
{
    a = std::clamp (a, 0, (int) d.size()); b = std::clamp (b, a, (int) d.size());
    if (b <= a) return;
    double sum = 0;
    for (int i = a; i < b; ++i) sum += d[(size_t) i];
    const double mean = sum / (b - a);
    for (int i = a; i < b; ++i) d[(size_t) i] = clip16 (d[(size_t) i] - mean);
}

bool findSoundBounds (const Instrument& in, double floorDb, int& start, int& end)
{
    const int floor = (int) std::lround (32768.0 * std::pow (10.0, floorDb / 20.0));
    const int n = (int) in.data.size();
    auto loud = [&] (int i) { return std::abs ((int) in.data[(size_t) i]) > floor || (in.isStereo() && std::abs ((int) in.dataR[(size_t) i]) > floor); };
    start = 0; end = n;
    while (start < n && ! loud (start)) ++start;
    if (start >= n) { start = end = 0; return false; }
    while (end > start && ! loud (end - 1)) --end;
    return true;
}

bool crossfadeLoop (Instrument& in, int fade)
{
    if (! in.isLooped()) return false;
    const int ls = in.loopStart, le = in.loopStart + in.loopLength;
    fade = std::min ({ fade, ls, in.loopLength / 2 });
    if (fade < 2) return false;
    auto run = [&] (std::vector<int16_t>& d)
    {
        // what plays just before the loop end turns into what played just before the loop start,
        // so jumping from the end to the start continues the same sound
        for (int k = 0; k < fade; ++k)
        {
            const double t = (k + 1) / (double) fade;
            const int dst = le - fade + k, src = ls - fade + k;
            d[(size_t) dst] = clip16 (d[(size_t) dst] * (1.0 - t) + d[(size_t) src] * t);
        }
    };
    run (in.data);
    if (in.isStereo()) run (in.dataR);
    return true;
}

void removeFrames (Instrument& in, int a, int b)
{
    const int n = (int) in.data.size();
    a = std::clamp (a, 0, n); b = std::clamp (b, a, n);
    if (b <= a) return;
    in.data.erase (in.data.begin() + a, in.data.begin() + b);
    if (in.isStereo() || (int) in.dataR.size() == n) in.dataR.erase (in.dataR.begin() + a, in.dataR.begin() + b);
    const int le = afterRemove (in.loopStart + in.loopLength, a, b);
    in.loopStart = afterRemove (in.loopStart, a, b); in.loopLength = le - in.loopStart;
    const bool hadEnd = in.playEnd > 0;
    in.playStart = afterRemove (in.playStart, a, b);
    if (hadEnd) in.playEnd = afterRemove (in.playEnd, a, b);
    for (auto& p : in.slices) p = afterRemove (p, a, b);
    fixLoop (in);
}

void insertSilence (Instrument& in, int at, int frames)
{
    const int n = (int) in.data.size();
    at = std::clamp (at, 0, n);
    if (frames <= 0) return;
    const bool st = (int) in.dataR.size() == n && n > 0;
    in.data.insert (in.data.begin() + at, (size_t) frames, (int16_t) 0);
    if (st) in.dataR.insert (in.dataR.begin() + at, (size_t) frames, (int16_t) 0);
    if (in.loopLength >= 4)
    {
        const int le = afterInsert (in.loopStart + in.loopLength, at, frames);
        in.loopStart = afterInsert (in.loopStart, at, frames);
        in.loopLength = le - in.loopStart;
    }
    if (in.playStart > at) in.playStart += frames;
    if (in.playEnd > 0 && in.playEnd >= at) in.playEnd += frames;
    for (auto& p : in.slices) p = afterInsert (p, at, frames);
    fixLoop (in);
}

void cropTo (Instrument& in, int a, int b)
{
    const int n = (int) in.data.size();
    a = std::clamp (a, 0, n); b = std::clamp (b, a, n);
    removeFrames (in, b, n);
    removeFrames (in, 0, a);
}

void pitchBy (Instrument& in, double semis, int a, int b)
{
    const int n = (int) in.data.size();
    if (b < 0) b = n;
    a = std::clamp (a, 0, n); b = std::clamp (b, a, n);
    if (b - a < 2 || std::abs (semis) < 1e-9) return;
    const double f = std::pow (2.0, semis / 12.0);   // >1: faster, higher, shorter
    auto one = [&] (std::vector<int16_t>& d)
    {
        std::vector<float> mid ((size_t) (b - a));
        for (int i = a; i < b; ++i) mid[(size_t) (i - a)] = d[(size_t) i] / 32768.0f;
        auto out = resample (mid, f, 1.0);
        std::vector<int16_t> nd (d.begin(), d.begin() + a);
        for (float v : out) nd.push_back (clip16 (v * 32768.0));
        nd.insert (nd.end(), d.begin() + b, d.end());
        d.swap (nd);
    };
    const bool st = in.isStereo();
    one (in.data);
    if (st) { one (in.dataR); in.dataR.resize (in.data.size(), 0); }
    const int newMid = (int) in.data.size() - (n - b) - a;
    auto map = [&] (int p) { return p <= a ? p : p >= b ? p - (b - a) + newMid : a + (int) std::lround ((p - a) / f); };
    if (in.loopLength >= 4) { const int le = map (in.loopStart + in.loopLength); in.loopStart = map (in.loopStart); in.loopLength = le - in.loopStart; }
    in.playStart = map (in.playStart);
    if (in.playEnd > 0) in.playEnd = map (in.playEnd);
    for (auto& p : in.slices) p = map (p);
    fixLoop (in);
}

void swapSides (Instrument& in)
{
    if (in.isStereo()) in.data.swap (in.dataR);
}
double detectPitch (const std::vector<int16_t>& d, int a, int b, double rate)
{
    a = std::clamp (a, 0, (int) d.size()); b = std::clamp (b, a, (int) d.size());
    if (rate <= 0) return 0;
    const int maxLag = (int) std::ceil (rate / 30.0), minLag = std::max (2, (int) std::floor (rate / 2000.0));
    const int W = std::min (b - a, std::max (2048, maxLag * 3));
    if (W < maxLag * 2 + 16) return 0;
    // the loudest stretch of W frames (past the attack, usually the clearest pitch)
    int best = a; double bestE = -1;
    const int hop = std::max (1, W / 4);
    for (int s = a; s + W <= b; s += hop)
    {
        double e = 0;
        for (int i = s; i < s + W; i += 4) e += (double) d[(size_t) i] * d[(size_t) i];
        if (e > bestE) { bestE = e; best = s; }
    }
    if (bestE <= 0) return 0;
    const int N = W - maxLag;
    std::vector<double> diff ((size_t) maxLag + 1, 0.0);
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double sum = 0;
        for (int i = 0; i < N; ++i) { const double x = (double) d[(size_t) (best + i)] - d[(size_t) (best + i + tau)]; sum += x * x; }
        diff[(size_t) tau] = sum;
    }
    // the cumulative mean normalised difference
    std::vector<double> cm ((size_t) maxLag + 1, 1.0);
    double running = 0;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        running += diff[(size_t) tau];
        cm[(size_t) tau] = running > 0 ? diff[(size_t) tau] * tau / running : 1.0;
    }
    int tau = -1;
    for (int t = minLag; t < maxLag; ++t)
        if (cm[(size_t) t] < 0.15)
        {
            while (t + 1 < maxLag && cm[(size_t) t + 1] < cm[(size_t) t]) ++t;
            tau = t; break;
        }
    if (tau < 0)
    {
        // no clear dip: the deepest one, if it's reasonably deep
        double m = 1e9;
        for (int t = minLag; t < maxLag; ++t) if (cm[(size_t) t] < m) { m = cm[(size_t) t]; tau = t; }
        if (m > 0.4) return 0;
    }
    double t = tau;
    if (tau > 1 && tau < maxLag)
    {
        const double y0 = cm[(size_t) tau - 1], y1 = cm[(size_t) tau], y2 = cm[(size_t) tau + 1];
        const double den = y0 - 2 * y1 + y2;
        if (std::abs (den) > 1e-12) t += 0.5 * (y0 - y2) / den;
    }
    return t > 0 ? rate / t : 0;
}

bool tuneToC (double hz, int& semis, int& cents)
{
    if (hz <= 0) return false;
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const double nearestC = std::round (midi / 12.0) * 12.0;
    const double shift = nearestC - midi;                       // -6..+6 semitones
    const double whole = std::round (shift);
    semis = (int) whole;
    cents = (int) std::lround ((shift - whole) * 100.0);
    return true;
}

int suggestSyncLines (double seconds, double lineSeconds)
{
    static const int nice[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512 };
    if (seconds <= 0 || lineSeconds <= 0) return 16;
    const double lines = seconds / lineSeconds;
    int best = 16; double bestD = 1e9;
    for (int n : nice) { const double dd = std::abs (std::log2 (lines / n)); if (dd < bestD) { bestD = dd; best = n; } }
    return best;
}
} // namespace prv
