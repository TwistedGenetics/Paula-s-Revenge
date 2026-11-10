#include "BreakTools.h"
#include "Groove.h"
#include "Slicer.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace prv
{
const char* hitName (HitKind k)
{
    switch (k) { case HitKind::Kick: return "KICK"; case HitKind::Snare: return "SNARE"; case HitKind::Hat: return "HAT"; default: return "OTHER"; }
}
char hitLetter (HitKind k) { return hitName (k)[0]; }

std::vector<HitKind> classifySlices (const Instrument& in)
{
    std::vector<HitKind> out;
    const int n = in.numSlices();
    if (n < 1 || ! in.hasData()) return out;
    const double sr = in.natRate > 0 ? in.natRate : (in.rate > 0 ? in.rate : kAmigaC3Rate);
    const int look = std::max (16, (int) (sr * 0.06));
    // a one-pole low pass at 150 Hz: the share of the energy down there
    const double a = std::exp (-2.0 * 3.14159265358979 * 150.0 / sr);
    struct F { double rms, low, zcr; };
    std::vector<F> f ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        int s = 0, e = 0;
        in.sliceRange (i, s, e);
        e = std::min (e, s + look);
        double sum = 0, lowSum = 0, lp = 0;
        int zc = 0, cnt = 0;
        float prev = 0;
        for (int k = s; k < e; ++k)
        {
            float v = in.data[(size_t) k] / 32768.0f;
            if (! in.dataR.empty() && k < (int) in.dataR.size()) v = 0.5f * (v + in.dataR[(size_t) k] / 32768.0f);
            lp = (1 - a) * v + a * lp;
            sum += v * v; lowSum += lp * lp;
            if ((v >= 0) != (prev >= 0) && k > s) ++zc;
            prev = v; ++cnt;
        }
        cnt = std::max (1, cnt);
        f[(size_t) i] = { std::sqrt (sum / cnt), sum > 0 ? lowSum / sum : 0.0, zc * sr / cnt };   // zcr in crossings per second
    }
    std::vector<double> levels;
    for (auto& x : f) levels.push_back (x.rms);
    auto sorted = levels; std::sort (sorted.begin(), sorted.end());
    const double loud = sorted[sorted.size() / 2];   // (the median slice's level)
    for (auto& x : f)
    {
        HitKind k = HitKind::Other;
        if (x.low > 0.45 && x.rms >= loud * 0.6) k = HitKind::Kick;
        else if (x.zcr > 4000 && x.rms < loud * 1.1) k = HitKind::Hat;
        else if (x.rms >= loud * 0.8 && x.zcr > 900) k = HitKind::Snare;
        else if (x.zcr > 4000) k = HitKind::Hat;
        out.push_back (k);
    }
    return out;
}

int sliceOfKind (const std::vector<HitKind>& kinds, HitKind k, int pick)
{
    std::vector<int> found;
    for (int i = 0; i < (int) kinds.size(); ++i) if (kinds[(size_t) i] == k) found.push_back (i);
    if (found.empty()) return -1;
    return found[(size_t) (((pick % (int) found.size()) + (int) found.size()) % (int) found.size())];
}

namespace
{
    void clearTrack (Block& b, int track, int lines)
    {
        for (int l = 0; l < lines; ++l) b.at (l, track) = Cell {};
    }
    // a slice that stands in for a kind the break doesn't have: kick = slice 1, snare = the one a quarter in, hat = slice 2
    int sliceFor (const std::vector<HitKind>& kinds, HitKind k, int pick)
    {
        const int s = sliceOfKind (kinds, k, pick);
        if (s >= 0) return s;
        const int n = std::max (1, (int) kinds.size());
        return k == HitKind::Kick ? 0 : k == HitKind::Snare ? std::min (n - 1, n / 4) : std::min (n - 1, 1);
    }
    void put (Block& b, int line, int track, int ins, int slice, int vol = -1)
    {
        auto& c = b.at (line, track);
        c = Cell {};
        c.note = (uint8_t) std::min ((int) kNoteMax, kSliceC3 + slice);
        c.instr = (uint8_t) (ins + 1);
        if (vol >= 0) { c.cmd = 0x0C; c.param = (uint8_t) std::clamp (vol, 0, 0x40); }
    }
}

int ghostRoll (Block& b, int track, int ins, const std::vector<HitKind>& kinds, int lines)
{
    if (track < 0 || track >= kMaxTracks) return 0;
    lines = std::clamp (lines, 1, b.lines);
    const int from = lines - std::max (2, lines / 4);
    const int snare = sliceFor (kinds, HitKind::Snare, 0), kick = sliceFor (kinds, HitKind::Kick, 0);
    int n = 0;
    for (int l = from; l < lines; ++l)
    {
        const double t = lines - 1 > from ? (double) (l - from) / (lines - 1 - from) : 1.0;
        if (l == from) put (b, l, track, ins, kick);
        else put (b, l, track, ins, snare, (int) std::lround (8 + t * (0x40 - 8)));
        ++n;
    }
    return n;
}

int halfTime (Block& b, int track, int lines)
{
    if (track < 0 || track >= kMaxTracks) return 0;
    lines = std::clamp (lines, 1, b.lines);
    std::vector<Cell> src ((size_t) lines);
    for (int l = 0; l < lines; ++l) src[(size_t) l] = b.at (l, track);
    clearTrack (b, track, lines);
    int n = 0;
    for (int l = 0; l * 2 < lines; ++l)
        if (! src[(size_t) l].isEmpty()) { b.at (l * 2, track) = src[(size_t) l]; ++n; }
    return n;
}

int doubleTime (Block& b, int track, int lines)
{
    if (track < 0 || track >= kMaxTracks) return 0;
    lines = std::clamp (lines, 1, b.lines);
    std::vector<Cell> src ((size_t) lines);
    for (int l = 0; l < lines; ++l) src[(size_t) l] = b.at (l, track);
    clearTrack (b, track, lines);
    const int half = std::max (1, lines / 2);
    int n = 0;
    for (int l = 0; l < lines; l += 2)
        if (! src[(size_t) l].isEmpty())
            for (int rep = 0; rep < 2; ++rep)
                if (l / 2 + rep * half < lines) { b.at (l / 2 + rep * half, track) = src[(size_t) l]; ++n; }
    return n;
}

int rebuild (Block& b, int track, int ins, const std::vector<HitKind>& kinds, int lines, int bars, const char* tmpl, int pick)
{
    if (track < 0 || track >= kMaxTracks || tmpl == nullptr) return 0;
    lines = std::clamp (lines, 1, b.lines);
    bars = std::max (1, bars);
    clearTrack (b, track, lines);
    const int steps = 16 * bars;
    int n = 0;
    for (int s = 0; s < steps; ++s)
    {
        const char c = tmpl[s % 16];
        if (c == '.' || c == 0) continue;
        const int line = (int) ((long long) s * lines / steps);
        if (line >= lines) break;
        const int bar = s / 16;
        switch (c)
        {
            case 'K': put (b, line, track, ins, sliceFor (kinds, HitKind::Kick, pick + bar)); break;
            case 'S': put (b, line, track, ins, sliceFor (kinds, HitKind::Snare, pick + bar)); break;
            case 'H': put (b, line, track, ins, sliceFor (kinds, HitKind::Hat, pick + s)); break;
            case 'G': put (b, line, track, ins, sliceFor (kinds, HitKind::Snare, pick + 1), 0x14); break;
            default: continue;
        }
        ++n;
    }
    return n;
}

const char* rebuildName (int i)
{
    static const char* names[kRebuilds] = { "CLASSIC '94", "ROLLER", "TWO-STEP", "HALF-STEP" };
    return names[std::clamp (i, 0, kRebuilds - 1)];
}

const char* rebuildTemplate (int i)
{
    //                                      1...2...3...4...
    static const char* t[kRebuilds] = { "K.HKS.HG.KK.S.HS",    // the cut-up amen: kick pushed, a ghost before the drop, snare on the last 16th
                                        "K.H.S.HKK.HKS.HG",    // rolling: kicks doubled, ghost into the next bar
                                        "K..KS.H..KK.S.GS",    // two-step: the kick skips, snares stay on 2 and 4
                                        "K.H.H.H.S.H.H.HK" };  // half-step: one snare a bar, hats running
    return t[std::clamp (i, 0, kRebuilds - 1)];
}
int chopToPhrase (Song& s, int ins, int division, double lineSec)
{
    if (ins < 0 || ins >= kMaxInstruments) return -1;
    auto& in = s.instruments[(size_t) ins];
    if (! in.hasData() || (int) in.phrases.size() >= kMaxPhrases) return -1;
    const int len = (int) in.data.size();
    if (division > 0) in.slices = evenSlices (len, division);
    else if (division == 0 || in.numSlices() < 2)
    {
        auto sl = detectSlices (in.data, in.dataR, in.rate > 0 ? in.rate : kAmigaC3Rate, 0.5f, 64);
        if (sl.size() < 2) sl = evenSlices (len, 16);
        in.slices = sl;
    }
    in.sliceKeys = true;
    const int n = in.numSlices();
    // how many bars: BEAT SYNC's lines, else its length at the song's tempo
    const double secs = len / std::max (1.0, in.rate);
    int bars = in.syncLines > 0 ? (int) std::lround (in.syncLines / 16.0) : (int) std::lround (secs / std::max (1e-6, lineSec * 16.0));
    bars = std::clamp (bars, 1, 8);
    // lines a beat: 4 (the song's own), or more until every hit has a line of its own
    int lpb = 4;
    while (lpb < 32 && lpb * bars * 4 < n) lpb *= 2;
    const int lines = std::min (kMaxLines, lpb * bars * 4);
    Block b;
    b.lines = lines;
    int last = -1;
    for (int i = 0; i < n; ++i)
    {
        const int note = 37 + i;
        if (note > (int) kNoteMax) break;   // (SLICE TO KEYS reaches B-7: slice 60)
        // N equal slices: evenly; by its hits: where each one falls in the break
        int l = division > 0 ? (int) std::lround ((double) i * lines / n)
                             : (int) std::lround ((double) in.slices[(size_t) i] / std::max (1, len) * lines);
        l = std::max (l, last + 1);
        if (l >= lines) break;
        b.at (l, 0) = Cell { (uint8_t) note, 0, 0, 0 };
        last = l;
    }
    s.blocks.push_back (b);
    Phrase p;
    p.block = (int) s.blocks.size() - 1;
    p.lpb = lpb;
    p.transpose = false;
    in.phrases.push_back (p);
    return (int) in.phrases.size() - 1;
}
} // namespace prv
