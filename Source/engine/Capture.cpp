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
                            const std::vector<bool>& offsFor, bool detectChords, bool noteOffs)
{
    CaptureResult res;
    if (log.empty() || song.blocks.empty()) return res;

    // the take: (playing) the latest pass of playback with a note played in it - a pass is one
    // time round before playback goes back to the start, so loops don't pile up on each other;
    // (stopped) the notes since the last long gap
    size_t last = log.size() - 1;
    if (log.back().playing)
    {
        // the latest note-on (releases alone don't make a take)
        size_t on = log.size();
        while (on > 0 && ! log[on - 1].on) --on;
        if (on > 0 && log[on - 1].playing) last = on - 1;
    }
    const bool played = log[last].playing;
    res.played = played;
    size_t from = last;
    while (from > 0)
    {
        const auto& prev = log[from - 1];
        const auto& cur = log[from];
        if (prev.playing != played) break;
        if (! played && cur.timeMs - prev.timeMs > 4000.0) break;
        if (played && (prev.pass != log[last].pass || prev.timeMs > cur.timeMs)) break;
        --from;
    }
    // the take's notes, plus the releases that came after (a note held across the wrap)
    std::vector<CapturedNote> take (log.begin() + (long) from, log.begin() + (long) last + 1);
    for (size_t i = last + 1; i < log.size(); ++i)
        if (! log[i].on && log[i].playing == played) take.push_back (log[i]);

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
    // a line's length in ms (a release this close after the next note is legato: the track is free)
    auto lineMs = [&] (const CapturedNote& c) { return 2500.0 / bpm * std::max (1, played ? c.speed : speed); };
    double busyUntil[kMaxTracks];
    for (auto& b : busyUntil) b = -1.0;

    const int nt = std::clamp (song.numTracks, 1, kMaxTracks);
    bool first = true;
    std::vector<bool> consumed (take.size(), false);   // (the other notes of a chord already written)
    auto releaseOf = [&] (size_t i, size_t& offIdx) -> double
    {
        for (size_t j = i + 1; j < take.size(); ++j)
            if (! take[j].on && take[j].note == take[i].note) { offIdx = j; return take[j].timeMs; }
        return -1;
    };
    auto plugIn = [&] (int ins) { return ins >= 0 && ins < (int) offsFor.size() && offsFor[(size_t) ins]; };
    // an OFF waits until the track's next note is known: if that note starts on the OFF's line, or
    // just after (legato - let go as the next key went down), the OFF never goes in; the next
    // note ends the old one by itself, and nothing is pushed onto another track
    CapturePlace pendingOff[kMaxTracks];
    double pendingOffMs[kMaxTracks] = {};
    bool hasPending[kMaxTracks] = {};
    auto commitOff = [&] (int t)
    {
        if (! hasPending[t]) return;
        hasPending[t] = false;
        const auto& o = pendingOff[t];
        if (! freeCell (o.block, o.line, t)) return;
        res.places.push_back (o);
        used.insert ({ o.block, o.line, t });
    };
    for (size_t i = 0; i < take.size(); ++i)
    {
        const auto& n = take[i];
        if (! n.on || consumed[i]) continue;
        // its release, if it has one in the take
        size_t offIdx = 0;
        double offMs = releaseOf (i, offIdx);

        // a chord: the next notes on the same plug-in / MIDI-out instrument pressed within the window
        ChordMatch chord;
        std::vector<size_t> group { i };
        if (detectChords && plugIn (n.instrument))
        {
            for (size_t j = i + 1; j < take.size(); ++j)
            {
                if (take[j].timeMs - n.timeMs > kChordWindowMs) break;
                if (! take[j].on) break;   // (a key let go in between: not played together)
                if (take[j].instrument == n.instrument && ! consumed[j]) group.push_back (j);
            }
            if (group.size() >= 2)
            {
                std::vector<int> notes;
                for (auto g : group) notes.push_back (take[g].note);
                chord = matchChord (song.chords, notes);
                if (chord.root < 1 || chord.root > (int) kNoteMax) chord = {};
            }
            if (chord)
            {
                // the chord ends when its last key is let go (no OFF if one is still held)
                for (auto g : group)
                {
                    size_t oi = 0;
                    const double t = releaseOf (g, oi);
                    if (t < 0) { offMs = -1; offIdx = 0; break; }
                    if (t >= offMs) { offMs = t; offIdx = oi; }
                }
            }
        }

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
        // (a chord's #xy needs a free effect column in its cell)
        auto fxFree = [&] (int t) { const auto& c = blk.at (line, t); return c.fx1Empty() || c.fx2Empty(); };
        int track = -1;
        for (int t = std::clamp (cursorTrack, 0, nt - 1); t < nt && track < 0; ++t)
            if (busyUntil[t] <= n.timeMs + lineMs (n) && freeCell (p.block, line, t) && (! chord || fxFree (t))) track = t;
        if (track < 0 && chord)
        {
            // no room for the #: the notes go in one by one after all
            chord = {};
            offMs = releaseOf (i, offIdx);
            for (int t = std::clamp (cursorTrack, 0, nt - 1); t < nt && track < 0; ++t)
                if (busyUntil[t] <= n.timeMs + lineMs (n) && freeCell (p.block, line, t)) track = t;
        }
        if (track < 0) { ++res.skipped; continue; }

        if (hasPending[track])
        {
            const auto& o = pendingOff[track];
            // legato: the next note on the OFF's own line (or before it), or on the line after but less
            // than half a line after the key was let go
            const bool legato = o.block == p.block && line > o.forLine
                             && (line <= o.line || (line == o.line + 1 && n.timeMs - pendingOffMs[track] < lineMs (n) * 0.5));
            if (legato) hasPending[track] = false;
            else commitOff (track);
        }

        CapturePlace pl;
        pl.block = p.block; pl.line = line; pl.track = track;
        pl.note = (uint8_t) std::clamp (chord ? chord.root : n.note, 1, (int) kNoteMax);
        pl.instr = (uint8_t) std::clamp (n.instrument + 1, 1, kMaxInstruments);
        pl.micro = micro;
        if (chord)
        {
            pl.chordSlot = (uint8_t) chord.slot;
            pl.chordExact = chord.exact;
            for (auto g : group) consumed[g] = true;
            ++res.chords;
        }
        res.places.push_back (pl);
        used.insert ({ p.block, line, track });
        busyUntil[track] = offMs >= 0 ? offMs : 1e300;
        if (first) { res.firstBlock = p.block; res.firstLine = line; first = false; }

        // its release: OFF on the line it was let go (plug-in / MIDI-out instruments)
        const bool wantsOff = noteOffs && plugIn (n.instrument);
        if (wantsOff && offMs >= 0)
        {
            const Pos q = where (take[offIdx]);
            int offLine = (int) std::lround (q.line);
            // let go after playback went round again: the OFF goes where it was let go, earlier in
            // the block, so the note rings over the wrap when the block loops - as played
            const bool wrapped = played && take[offIdx].pass != n.pass;
            if (q.block == p.block && offLine <= line && ! (wrapped && offLine < line)) offLine = line + 1;
            if (q.block == p.block && offLine >= 0 && offLine < blk.lines)
            {
                CapturePlace off = pl;
                off.line = offLine; off.note = kNoteOff; off.instr = 0; off.micro = 0; off.chordSlot = 0; off.forLine = line;
                pendingOff[track] = off;
                pendingOffMs[track] = take[offIdx].timeMs;
                hasPending[track] = true;
            }
        }
    }
    for (int t = 0; t < kMaxTracks; ++t) commitOff (t);
    return res;
}
} // namespace prv
