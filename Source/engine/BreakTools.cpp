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

// ============================================================================ BREAK LAB
namespace
{
    struct LabRng
    {
        uint32_t s;
        explicit LabRng (uint32_t seed) : s (seed * 2654435761u + 0x9E3779B9u) { next(); }
        uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        float uni() { return (float) (next() >> 8) / 16777216.0f; }          // 0..1
        int   pick (int n) { return n <= 1 ? 0 : (int) (next() % (uint32_t) n); }
        bool  chance (float p) { return uni() < p; }
    };

    // per style: bar grooves to choose from (16 steps: K S H G .), and how much of each edit it likes (x WILD)
    struct LabStyle
    {
        std::vector<const char*> grooves;
        float ghost, roll, reverse, drop, stutter, cut, gap, half, swing;
        int   rollSpeed[2];   // Rxy x: the retrigger ticks it picks from (at speed 6: 3 = 32nds, 2 = triplet 32nds, 1 = a buzz)
    };
    const LabStyle& labStyle (int style)
    {
        //                                   1...2...3...4...
        static const LabStyle st[kLabStyles] = {
            // '94 ROLLERS: busy, rolling, plenty of ghosts and hats pushed a touch late
            { { "K.H.S.HKK.HKS.HG", "K.HKS.HG.KK.S.HS", "K.H.S.HK.KHKS.HG", "K.HGS.HKK.H.S.KG" },
              0.9f, 0.5f, 0.25f, 0.2f, 0.25f, 0.1f, 0.3f, 0.1f, 0.6f, { 3, 2 } },
            // '96 TECHSTEP: tight and sparse, the kick skipping, hats cut short, few frills
            { { "K......KS.......", "K..K....S..K....", "K.....K.S..H..K.", "K......KS....K.G" },
              0.35f, 0.25f, 0.2f, 0.35f, 0.15f, 0.8f, 0.45f, 0.25f, 0.0f, { 3, 3 } },
            // AMEN CHOPS: the amen cut up - reversed snares, pitched-down hits, stutters
            { { "K.HKS.HG.KK.S.HS", "K.H.S.HKS.HKK.HS", "K.HKS.H.SKK.S.HG", "KKH.S.HGK.HKS.SS" },
              0.6f, 0.45f, 0.6f, 0.5f, 0.5f, 0.2f, 0.35f, 0.15f, 0.3f, { 3, 2 } },
            // DRILL'N'BASS: chaos - buzzing rolls, stutters, pitch everywhere, hits moved about
            { { "KSHKSGHSKKSHGSKS", "K.SSHKGSKSHHSKGS", "KKSGSHSKGKSSHKSG", "SKHGKSSKHGSKKSHS" },
              0.7f, 1.0f, 0.6f, 0.8f, 0.9f, 0.4f, 0.3f, 0.2f, 0.4f, { 2, 1 } } };
        return st[std::clamp (style, 0, kLabStyles - 1)];
    }

    int lineOf (int step, int steps, int lines) { return (int) ((long long) step * lines / steps); }
    bool isHit (const Cell& c) { return c.note > 0 && c.note != kNoteOff; }
    int  sliceOfNote (const Cell& c) { return (int) c.note - kSliceC3; }
    HitKind kindOfCell (const Cell& c, const std::vector<HitKind>& kinds)
    {
        const int s = sliceOfNote (c);
        return s >= 0 && s < (int) kinds.size() ? kinds[(size_t) s] : HitKind::Other;
    }
    // a command in a free effect column (the first, else the second); false if both are taken
    bool addCmd (Cell& c, uint8_t cmd, uint8_t param)
    {
        if (c.fx1Empty()) { c.cmd = cmd; c.param = param; return true; }
        if (c.cmd == cmd) { c.param = param; return true; }
        if (c.fx2Empty()) { c.cmd2 = cmd; c.param2 = param; return true; }
        if (c.cmd2 == cmd) { c.param2 = param; return true; }
        return false;
    }
    bool hasCmd (const Cell& c, uint8_t cmd) { uint8_t p; return c.has (cmd, p); }

    const uint8_t kR = cmdLetter ('R'), kU = cmdLetter ('U'), kI = cmdLetter ('I'), kM = cmdLetter ('M'), kE = 0x0E, kC = 0x0C;

    // the edits, each on one step of one bar (they say whether they did anything)
    bool edGhost (Block& b, int track, int ins, const std::vector<HitKind>& kinds, int line, LabRng& r)
    {
        if (isHit (b.at (line, track))) return false;
        put (b, line, track, ins, sliceFor (kinds, HitKind::Snare, 1 + r.pick (3)), 0x0C + r.pick (0x0C));
        return true;
    }
    bool edRoll (Block& b, int track, const LabStyle& st, int line, LabRng& r)
    {
        auto& c = b.at (line, track);
        if (! isHit (c) || hasCmd (c, kR)) return false;
        const int x = st.rollSpeed[r.pick (2)];
        const int y = r.chance (0.6f) ? 0x0A + r.pick (4) : 0;   // mostly getting louder
        return addCmd (c, kR, (uint8_t) (x * 16 + y));
    }
    bool edReverse (Block& b, int track, int line)
    {
        auto& c = b.at (line, track);
        if (! isHit (c) || hasCmd (c, kU)) return false;
        return addCmd (c, kU, 0x01);
    }
    bool edDrop (Block& b, int track, int line, LabRng& r)
    {
        auto& c = b.at (line, track);
        if (! isHit (c) || hasCmd (c, kI)) return false;
        static const int semis[] = { -12, -7, -5, -3, -2, 5, 7 };
        return addCmd (c, kI, (uint8_t) (0x80 + semis[r.pick (r.chance (0.8f) ? 5 : 7)]));
    }
    // the hit again on the lines after it, cut short (ECx), quieter each time
    int edStutter (Block& b, int track, int line, int stepLines, int upTo, LabRng& r)
    {
        const Cell src = b.at (line, track);
        if (! isHit (src)) return 0;
        const int reps = 2 + r.pick (3);
        const int gap = std::max (1, stepLines / (r.chance (0.5f) ? 2 : 1));
        int n = 0;
        for (int k = 1; k <= reps; ++k)
        {
            const int l = line + k * gap;
            if (l >= upTo) break;
            Cell c {}; c.note = src.note; c.instr = src.instr;
            c.cmd = kC; c.param = (uint8_t) std::max (0x10, 0x40 - k * 0x0A);
            c.cmd2 = kE; c.param2 = (uint8_t) (0xC0 + 2 + r.pick (2));   // cut at tick 2-3
            b.at (l, track) = c; ++n;
        }
        return n;
    }
    bool edCut (Block& b, int track, int line, LabRng& r)
    {
        auto& c = b.at (line, track);
        if (! isHit (c) || hasCmd (c, kE)) return false;
        return addCmd (c, kE, (uint8_t) (0xC0 + 2 + r.pick (3)));
    }
    bool edSwing (Block& b, int track, int line, LabRng& r)
    {
        auto& c = b.at (line, track);
        if (! isHit (c) || hasCmd (c, kM)) return false;
        return addCmd (c, kM, (uint8_t) (0x06 + r.pick (0x0C)));   // a touch late
    }

    // one bar of the groove (bar `bar` of `bars`, its lines from..to), written fresh
    void writeGroove (Block& b, int track, int ins, const std::vector<HitKind>& kinds, const char* tmpl, int bar, int steps, int lines, int pick)
    {
        for (int s = 0; s < 16; ++s)
        {
            const char c = tmpl[s];
            const int step = bar * 16 + s;
            const int line = lineOf (step, steps, lines);
            if (line >= lines) break;
            switch (c)
            {
                case 'K': put (b, line, track, ins, sliceFor (kinds, HitKind::Kick, pick + bar)); break;
                case 'S': put (b, line, track, ins, sliceFor (kinds, HitKind::Snare, pick + bar)); break;
                case 'H': put (b, line, track, ins, sliceFor (kinds, HitKind::Hat, pick + s)); break;
                case 'G': put (b, line, track, ins, sliceFor (kinds, HitKind::Snare, pick + 1), 0x14); break;
                default: break;
            }
        }
    }

    // the edits over one bar, as many as the style and WILD ask for; `fill`: the bar before a phrase ends
    void editBar (Block& b, int track, int ins, const std::vector<HitKind>& kinds, const LabStyle& st, float w,
                  int bar, int steps, int lines, bool fill, LabRng& r, LabReport& rep)
    {
        const int stepLines = std::max (1, lines / steps);
        const int from = lineOf (bar * 16, steps, lines), to = std::min (lines, lineOf ((bar + 1) * 16, steps, lines));
        const float boost = fill ? 1.8f : 1.0f;
        for (int s = 0; s < 16; ++s)
        {
            const int line = lineOf (bar * 16 + s, steps, lines);
            if (line >= to) break;
            const auto& c = b.at (line, track);
            const HitKind k = isHit (c) ? kindOfCell (c, kinds) : HitKind::Other;
            const bool off = (s % 2) == 1;
            if (! isHit (c))
            {
                if (off && r.chance (st.ghost * w * 0.35f) && edGhost (b, track, ins, kinds, line, r)) ++rep.ghosts;
                continue;
            }
            if (k == HitKind::Snare && s >= 8 && r.chance (st.roll * w * 0.3f * boost) && edRoll (b, track, st, line, r)) ++rep.rolls;
            else if (k != HitKind::Kick && r.chance (st.reverse * w * 0.15f * boost) && edReverse (b, track, line)) ++rep.reverses;
            if (r.chance (st.drop * w * 0.12f * boost) && edDrop (b, track, line, r)) ++rep.drops;
            if (k == HitKind::Hat && r.chance (st.cut * 0.6f) && edCut (b, track, line, r)) ++rep.cuts;
            if (k == HitKind::Hat && off && r.chance (st.swing * 0.5f) && edSwing (b, track, line, r)) { /* (feel, not counted) */ }
            if (s >= 10 && r.chance (st.stutter * w * 0.12f * boost))
            {
                const int n = edStutter (b, track, line, stepLines, to, r);
                if (n > 0) { ++rep.stutters; s += 3; }   // (past what it wrote over)
            }
        }
        // the gap before the drop: the last two steps of a phrase's last bar left empty
        if (fill && r.chance (st.gap * (0.4f + w * 0.6f)))
        {
            for (int s = 14; s < 16; ++s)
            {
                const int line = lineOf (bar * 16 + s, steps, lines);
                for (int l = line; l < std::min (to, lineOf (bar * 16 + s + 1, steps, lines)); ++l) b.at (l, track) = Cell {};
            }
            ++rep.gaps;
        }
        (void) from;
    }
    // a bar as half-time: its first half spread over all of it
    void halfBar (Block& b, int track, int bar, int steps, int lines)
    {
        const int from = lineOf (bar * 16, steps, lines), to = std::min (lines, lineOf ((bar + 1) * 16, steps, lines));
        std::vector<Cell> src;
        for (int l = from; l < to; ++l) src.push_back (b.at (l, track));
        for (int l = from; l < to; ++l) b.at (l, track) = Cell {};
        for (int k = 0; from + k * 2 < to && k < (int) src.size(); ++k) b.at (from + k * 2, track) = src[(size_t) k];
    }
    int countHits (const Block& b, int track, int lines)
    {
        int n = 0;
        for (int l = 0; l < lines; ++l) n += isHit (const_cast<Block&> (b).at (l, track)) ? 1 : 0;
        return n;
    }
}

const char* labStyleName (int style)
{
    static const char* n[kLabStyles] = { "'94 ROLLERS", "'96 TECHSTEP", "AMEN CHOPS", "DRILL'N'BASS" };
    return n[std::clamp (style, 0, kLabStyles - 1)];
}

std::string LabReport::text() const
{
    std::string t;
    auto add = [&] (int n, const char* one, const char* many) { if (n <= 0) return; if (! t.empty()) t += ", "; t += std::to_string (n) + " " + (n == 1 ? one : many); };
    add (ghosts, "GHOST", "GHOSTS"); add (rolls, "ROLL", "ROLLS"); add (reverses, "REVERSE", "REVERSES");
    add (drops, "PITCH DROP", "PITCH DROPS"); add (stutters, "STUTTER", "STUTTERS"); add (cuts, "CUT", "CUTS");
    add (gaps, "GAP", "GAPS"); add (halfBars, "HALF-TIME BAR", "HALF-TIME BARS");
    const std::string head = changes > 0 ? std::to_string (changes) + (changes == 1 ? " CHANGE" : " CHANGES")
                                         : std::to_string (notes) + " HITS";
    return t.empty() ? head : head + ": " + t;
}

LabReport breakLab (Block& b, int track, int ins, const std::vector<HitKind>& kinds, int lines, int bars, int style, int wild, uint32_t seed)
{
    LabReport rep;
    if (track < 0 || track >= kMaxTracks) return rep;
    lines = std::clamp (lines, 1, b.lines);
    bars = std::max (1, bars);
    const int steps = 16 * bars;
    const auto& st = labStyle (style);
    const float w = std::clamp (wild, 0, 100) / 100.0f;
    LabRng r (seed);
    clearTrack (b, track, lines);
    const int pick = r.pick (4);
    // the groove: a bar from the style each bar (the first bar the style's own; wilder, more of them mixed)
    for (int bar = 0; bar < bars; ++bar)
    {
        const int g = bar == 0 ? r.pick (2) : (r.chance (0.3f + w * 0.6f) ? r.pick ((int) st.grooves.size()) : 0);
        writeGroove (b, track, ins, kinds, st.grooves[(size_t) g], bar, steps, lines, pick);
    }
    // a half-time bar now and then (never the first)
    for (int bar = 1; bar < bars; ++bar)
        if (r.chance (st.half * w * 0.5f)) { halfBar (b, track, bar, steps, lines); ++rep.halfBars; }
    // the edits: the last bar of every 2 (and the very last bar) is a fill
    for (int bar = 0; bar < bars; ++bar)
        editBar (b, track, ins, kinds, st, w, bar, steps, lines, bar == bars - 1 || (bar % 2) == 1, r, rep);
    // with only one bar, still something to hear at WILD > 0
    if (w > 0.0f && rep.ghosts + rep.rolls + rep.reverses + rep.drops + rep.stutters == 0)
    {
        for (int s = 15; s >= 8; --s)
        {
            const int line = lineOf ((bars - 1) * 16 + s, steps, lines);
            if (line < lines && isHit (b.at (line, track)) && edRoll (b, track, st, line, r)) { ++rep.rolls; break; }
        }
    }
    rep.notes = countHits (b, track, lines);
    return rep;
}

LabReport mutateBreak (Block& b, int track, int ins, const std::vector<HitKind>& kinds, int lines, int bars, int style, int wild, uint32_t seed)
{
    LabReport rep;
    if (track < 0 || track >= kMaxTracks) return rep;
    lines = std::clamp (lines, 1, b.lines);
    bars = std::max (1, bars);
    const int steps = 16 * bars;
    const auto& st = labStyle (style);
    const float w = std::clamp (wild, 0, 100) / 100.0f;
    LabRng r (seed);
    if (countHits (b, track, lines) == 0) return breakLab (b, track, ins, kinds, lines, bars, style, wild, seed);
    const int wanted = 1 + (int) std::lround (w * 4.0f) + r.pick (2);
    int tries = 0;
    while (rep.changes < wanted && ++tries < 200)
    {
        const int bar = r.pick (bars), s = r.pick (16);
        const int line = lineOf (bar * 16 + s, steps, lines);
        if (line >= lines) continue;
        auto& c = b.at (line, track);
        bool done = false;
        switch (r.pick (8))
        {
            case 0: done = edGhost (b, track, ins, kinds, line, r); if (done) ++rep.ghosts; break;
            case 1: done = edRoll (b, track, st, line, r); if (done) ++rep.rolls; break;
            case 2: done = edReverse (b, track, line); if (done) ++rep.reverses; break;
            case 3: done = edDrop (b, track, line, r); if (done) ++rep.drops; break;
            case 4: { const int to = std::min (lines, lineOf ((bar + 1) * 16, steps, lines));
                      done = edStutter (b, track, line, std::max (1, lines / steps), to, r) > 0; if (done) ++rep.stutters; } break;
            case 5:   // another hit of the same kind
                if (isHit (c))
                {
                    const HitKind k = kindOfCell (c, kinds);
                    const int ns = sliceFor (kinds, k == HitKind::Other ? HitKind::Hat : k, r.pick (8));
                    if (ns != sliceOfNote (c)) { c.note = (uint8_t) std::min ((int) kNoteMax, kSliceC3 + ns); done = true; }
                }
                break;
            case 6:   // a hit moves a step (to an empty one)
                if (isHit (c))
                {
                    const int dir = r.chance (0.5f) ? 1 : -1;
                    const int nl = lineOf (bar * 16 + std::clamp (s + dir, 0, 15), steps, lines);
                    if (nl != line && nl >= 0 && nl < lines && ! isHit (b.at (nl, track))) { b.at (nl, track) = c; c = Cell {}; done = true; }
                }
                break;
            default:  // a hit taken out (not the first kick)
                if (isHit (c) && line > 0 && kindOfCell (c, kinds) != HitKind::Kick) { c = Cell {}; done = true; }
                break;
        }
        if (done) ++rep.changes;
    }
    rep.notes = countHits (b, track, lines);
    return rep;
}
} // namespace prv
