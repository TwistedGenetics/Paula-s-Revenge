// Paula's Revenge - groove and break tools
#include "Groove.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace prv
{
const char* grooveName (GrooveKind k)
{
    switch (k)
    {
        case GrooveKind::Off:       return "OFF";
        case GrooveKind::Swing16:   return "SWING 16THS";
        case GrooveKind::Swing8:    return "SWING 8THS";
        case GrooveKind::LazyBreak: return "LAZY BREAK";
        case GrooveKind::Push:      return "PUSHED HATS";
        case GrooveKind::Humanise:  return "HUMANISE";
        default:                    return "";
    }
}

double grooveOffset (GrooveKind k, int amount, int line, int track)
{
    const double a = std::clamp (amount, 0, 100) / 100.0;
    if (a <= 0.0 || line < 0) return 0.0;
    const int p = line % 8;
    switch (k)
    {
        // the off 16ths late: at 100%, two thirds of a line (a triplet feel); 50% is a light shuffle
        case GrooveKind::Swing16:   return (p % 2 == 1) ? a * 0.667 : 0.0;
        // the off 8ths (lines 2 and 6 of 8) late
        case GrooveKind::Swing8:    return (p == 2 || p == 6) ? a * 0.667 : 0.0;
        // a laid-back break: everything but the beats drags a little, the 'a's most
        case GrooveKind::LazyBreak: { static const double o[8] = { 0, 0.30, 0.12, 0.40, 0, 0.30, 0.18, 0.42 }; return a * o[p]; }
        // the off-beat 16ths of the even tracks (hats on 2, 4...) pushed a little late
        case GrooveKind::Push:      return (p % 2 == 1 && track % 2 == 1) ? a * 0.35 : 0.0;
        // a small, fixed (not random each time) drift per line and track
        case GrooveKind::Humanise:
        {
            uint32_t h = (uint32_t) line * 2654435761u ^ (uint32_t) (track + 1) * 40503u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            return a * 0.18 * (h % 1000) / 1000.0;
        }
        default: return 0.0;
    }
}

namespace
{
    bool emptyCell (const Cell& c) { return c.note == 0 && c.instr == 0 && c.cmd == 0 && c.param == 0 && c.cmd2 == 0 && c.param2 == 0; }
    void clampPart (const Block& b, int& l0, int& l1, int& t0, int& t1)
    {
        if (l0 > l1) std::swap (l0, l1);
        if (t0 > t1) std::swap (t0, t1);
        l0 = std::clamp (l0, 0, b.lines - 1); l1 = std::clamp (l1, 0, b.lines - 1);
        t0 = std::clamp (t0, 0, kMaxTracks - 1); t1 = std::clamp (t1, 0, kMaxTracks - 1);
    }
}

int chopToPattern (Song& s, int ins, int block, int track, int lines)
{
    if (ins < 0 || ins >= kMaxInstruments || block < 0 || block >= (int) s.blocks.size() || track < 0 || track >= kMaxTracks) return 0;
    auto& in = s.instruments[(size_t) ins];
    const int n = in.numSlices();
    if (n < 1 || ! in.hasData()) return 0;
    auto& b = s.blocks[(size_t) block];
    if (lines <= 0) lines = b.lines;
    lines = std::clamp (lines, 1, b.lines);
    const int from = in.rangeStart(), to = in.rangeEnd();
    const double len = std::max (1, to - from);
    for (int l = 0; l < lines; ++l)   // (the track's notes there go)
    {
        auto& c = b.at (l, track);
        c.note = 0; c.instr = 0;
    }
    std::vector<bool> taken ((size_t) lines, false);
    int placed = 0;
    for (int i = 0; i < n && i < kNoteMax - kSliceC3 + 1; ++i)
    {
        int a = 0, e = 0;
        if (! in.sliceRange (i, a, e) || a < from || a >= to) continue;
        int line = (int) std::floor ((a - from) / len * lines + 0.5);
        while (line < lines && taken[(size_t) line]) ++line;
        if (line >= lines) break;
        taken[(size_t) line] = true;
        auto& c = b.at (line, track);
        c.note = (uint8_t) (kSliceC3 + i);
        c.instr = (uint8_t) (ins + 1);
        ++placed;
    }
    if (placed > 0) in.sliceKeys = true;
    return placed;
}

int resequenceSlices (Block& b, int l0, int l1, int t0, int t1, uint32_t seed)
{
    clampPart (b, l0, l1, t0, t1);
    int changed = 0;
    std::mt19937 rng (seed);
    for (int t = t0; t <= t1; ++t)
    {
        std::vector<int> lines;
        std::vector<uint8_t> notes;
        for (int l = l0; l <= l1; ++l)
        {
            const auto& c = b.at (l, t);
            if (c.note >= kSliceC3 && c.note <= kNoteMax) { lines.push_back (l); notes.push_back (c.note); }
        }
        if (notes.size() < 2) continue;
        // keep a slice 1 on the first note put (the downbeat), shuffle the rest
        const bool anchor = notes[0] == kSliceC3;
        auto first = notes.begin() + (anchor ? 1 : 0);
        std::vector<uint8_t> before (notes);
        std::shuffle (first, notes.end(), rng);
        // a shuffle that changed nothing (few notes, repeats): rotate instead
        if (notes == before && notes.end() - first > 1) std::rotate (first, first + 1, notes.end());
        for (size_t k = 0; k < lines.size(); ++k)
        {
            auto& c = b.at (lines[k], t);
            if (c.note != notes[k]) { c.note = notes[k]; ++changed; }
        }
    }
    return changed;
}

void reverseLines (Block& b, int l0, int l1, int t0, int t1)
{
    clampPart (b, l0, l1, t0, t1);
    for (int t = t0; t <= t1; ++t)
        for (int i = l0, j = l1; i < j; ++i, --j) std::swap (b.at (i, t), b.at (j, t));
}

void stutterLines (Block& b, int l0, int l1, int t0, int t1, int len)
{
    clampPart (b, l0, l1, t0, t1);
    const int n = l1 - l0 + 1;
    if (len <= 0) len = std::max (1, n / 4);
    len = std::clamp (len, 1, n);
    for (int t = t0; t <= t1; ++t)
        for (int l = l0 + len; l <= l1; ++l) b.at (l, t) = b.at (l0 + (l - l0) % len, t);
}

int ratchetNotes (Block& b, int l0, int l1, int t0, int t1)
{
    clampPart (b, l0, l1, t0, t1);
    const uint8_t R = cmdLetter ('R');
    int done = 0;
    const int n = l1 - l0 + 1;
    for (int t = t0; t <= t1; ++t)
        for (int l = l0; l <= l1; ++l)
        {
            auto& c = b.at (l, t);
            if (c.note == 0 || c.note > kNoteMax) continue;
            // every 4 ticks at the start of the part, every tick at its end
            const int every = n <= 1 ? 2 : std::clamp (4 - (int) std::lround (3.0 * (l - l0) / (n - 1)), 1, 4);
            const uint8_t param = (uint8_t) (every << 4);
            if (c.cmd == 0 && c.param == 0) { c.cmd = R; c.param = param; ++done; }
            else if (c.cmd2 == 0 && c.param2 == 0) { c.cmd2 = R; c.param2 = param; ++done; }
            else if (c.cmd == R) { c.param = param; ++done; }
            else if (c.cmd2 == R) { c.param2 = param; ++done; }
        }
    return done;
}
} // namespace prv
