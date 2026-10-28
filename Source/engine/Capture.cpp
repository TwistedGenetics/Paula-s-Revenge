// Paula's Revenge - CAPTURE
#include "Capture.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace prv
{
CaptureResult placeCapture (const Song& song, const std::vector<CapturedNote>& log,
                            int cursorBlock, int cursorLine, int cursorTrack, int bpm, int speed,
                            const std::vector<bool>& offsFor)
{
    CaptureResult res;
    if (log.empty() || song.blocks.empty()) return res;

    // the take: the latest stretch of playback, or (stopped) the notes since the last long gap
    const bool played = log.back().playing;
    res.played = played;
    size_t from = log.size() - 1;
    while (from > 0)
    {
        const auto& prev = log[from - 1];
        const auto& cur = log[from];
        if (prev.playing != played) break;
        if (! played && cur.timeMs - prev.timeMs > 4000.0) break;
        if (played && prev.timeMs > cur.timeMs) break;
        --from;
    }
    std::vector<CapturedNote> take (log.begin() + (long) from, log.end());

    bpm = std::max (32, bpm); speed = std::max (1, speed);
    const double msPerLine = 2500.0 / bpm * speed;
    double t0 = -1;
    for (const auto& n : take) if (n.on) { t0 = n.timeMs; break; }
    if (t0 < 0) return res;   // (only releases)

    struct Pos { int block; double line; int speed; };
    auto where = [&] (const CapturedNote& n) -> Pos
    {
        if (played) return { song.safeBlock (n.block), n.line, std::max (1, n.speed) };
        return { song.safeBlock (cursorBlock), cursorLine + (n.timeMs - t0) / msPerLine, speed };
    };

    std::set<std::tuple<int, int, int>> used;   // cells this capture has filled
    auto freeCell = [&] (int b, int l, int t)
    {
        const auto& blk = song.blocks[(size_t) b];
        return l >= 0 && l < blk.lines && blk.at (l, t).note == 0 && used.count ({ b, l, t }) == 0;
    };
    // which captured note each track is holding, and until when (ms)
    double busyUntil[kMaxTracks];
    for (auto& b : busyUntil) b = -1.0;

    const int nt = std::clamp (song.numTracks, 1, kMaxTracks);
    bool first = true;
    for (size_t i = 0; i < take.size(); ++i)
    {
        const auto& n = take[i];
        if (! n.on) continue;
        // its release, if it has one in the take
        double offMs = -1;
        size_t offIdx = 0;
        for (size_t j = i + 1; j < take.size(); ++j)
            if (! take[j].on && take[j].note == n.note) { offMs = take[j].timeMs; offIdx = j; break; }

        const Pos p = where (n);
        int line = (int) std::lround (p.line);
        const double f = p.line - line;                       // lines early (-) or late (+)
        uint8_t micro = 0;
        if (std::abs (f * p.speed) > 1.0 / 3.0)
        {
            if (f > 0) micro = (uint8_t) std::clamp ((int) std::lround (f * 128.0), 1, 0x7F);
            else if (line > 0) micro = (uint8_t) std::clamp (0x80 + (int) std::lround (-f * 128.0), 0x81, 0xFF);
        }
        const auto& blk = song.blocks[(size_t) p.block];
        if (line >= blk.lines) { ++res.skipped; continue; }

        // the cursor's track, else the next free one (a chord spreads across tracks)
        int track = -1;
        for (int t = std::clamp (cursorTrack, 0, nt - 1); t < nt && track < 0; ++t)
            if (busyUntil[t] <= n.timeMs && freeCell (p.block, line, t)) track = t;
        if (track < 0) { ++res.skipped; continue; }

        CapturePlace pl;
        pl.block = p.block; pl.line = line; pl.track = track;
        pl.note = (uint8_t) std::clamp (n.note, 1, (int) kNoteMax);
        pl.instr = (uint8_t) std::clamp (n.instrument + 1, 1, kMaxInstruments);
        pl.micro = micro;
        res.places.push_back (pl);
        used.insert ({ p.block, line, track });
        busyUntil[track] = offMs >= 0 ? offMs : 1e300;
        if (first) { res.firstBlock = p.block; res.firstLine = line; first = false; }

        // its release: OFF on the line it was let go (plug-in / MIDI-out instruments)
        const bool wantsOff = n.instrument >= 0 && n.instrument < (int) offsFor.size() && offsFor[(size_t) n.instrument];
        if (wantsOff && offMs >= 0)
        {
            const Pos q = where (take[offIdx]);
            int offLine = (int) std::lround (q.line);
            if (q.block == p.block && offLine <= line) offLine = line + 1;
            if (q.block == p.block && freeCell (q.block, offLine, track))
            {
                CapturePlace off = pl;
                off.line = offLine; off.note = kNoteOff; off.instr = 0; off.micro = 0;
                res.places.push_back (off);
                used.insert ({ q.block, offLine, track });
            }
        }
    }
    return res;
}
} // namespace prv
