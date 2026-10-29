// Paula's Revenge - song structure operations
#include "SongOps.h"
#include <algorithm>

namespace prv
{
bool moveSequenceEntry (Song& song, int from, int to)
{
    const int n = (int) song.sequence.size();
    if (from < 0 || from >= n) return false;
    to = std::clamp (to, 0, n - 1);
    if (to == from) return false;
    const int v = song.sequence[(size_t) from];
    song.sequence.erase (song.sequence.begin() + from);
    song.sequence.insert (song.sequence.begin() + to, v);
    return true;
}

bool swapBlockNumbers (Song& song, int a, int b)
{
    const int n = (int) song.blocks.size();
    if (a < 0 || b < 0 || a >= n || b >= n || a == b) return false;
    std::swap (song.blocks[(size_t) a], song.blocks[(size_t) b]);
    for (auto& s : song.sequence)
        if (s == a) s = b;
        else if (s == b) s = a;
    return true;
}

int nextFreeBlock (const Song& song)
{
    for (int b = 0; b < (int) song.blocks.size(); ++b)
    {
        if (std::find (song.sequence.begin(), song.sequence.end(), b) != song.sequence.end()) continue;
        const auto& blk = song.blocks[(size_t) b];
        bool empty = true;
        for (int l = 0; l < kMaxLines && empty; ++l)
            for (int t = 0; t < kMaxTracks && empty; ++t)
            {
                const Cell& c = blk.at (l, t);
                empty = c.note == 0 && c.instr == 0 && c.cmd == 0 && c.param == 0 && c.cmd2 == 0 && c.param2 == 0;
            }
        if (empty) return b;
    }
    return (int) song.blocks.size() < kMaxBlocks ? (int) song.blocks.size() : -1;
}

int cloneInPlace (Song& song, int seqPos)
{
    if (seqPos < 0 || seqPos >= (int) song.sequence.size()) return -1;
    const int b = song.safeBlock (song.sequence[(size_t) seqPos]);
    const int nb = nextFreeBlock (song);
    if (nb < 0) return -1;
    Block copy = song.blocks[(size_t) b];
    if (nb == (int) song.blocks.size()) song.blocks.push_back (std::move (copy));
    else song.blocks[(size_t) nb] = std::move (copy);
    song.sequence[(size_t) seqPos] = nb;   // nothing else is renumbered
    return nb;
}
int combinePositions (Song& song, int from, int to)
{
    const int n = (int) song.sequence.size();
    if (from > to) std::swap (from, to);
    if (from < 0 || to >= n || from == to) return -1;
    int total = 0;
    for (int p = from; p <= to; ++p) total += song.blocks[(size_t) song.safeBlock (song.sequence[(size_t) p])].lines;
    if (total > kMaxLines) return kCombineTooLong;
    const int nb = nextFreeBlock (song);
    if (nb < 0) return -1;
    Block made;
    made.clear();
    made.lines = total;
    made.name = song.blocks[(size_t) song.safeBlock (song.sequence[(size_t) from])].name;
    int at = 0;
    for (int p = from; p <= to; ++p)
    {
        const auto& b = song.blocks[(size_t) song.safeBlock (song.sequence[(size_t) p])];
        for (int l = 0; l < b.lines; ++l) made.rows[(size_t) (at + l)] = b.rows[(size_t) l];
        at += b.lines;
    }
    if (nb == (int) song.blocks.size()) song.blocks.push_back (std::move (made));
    else song.blocks[(size_t) nb] = std::move (made);
    song.sequence[(size_t) from] = nb;
    song.sequence.erase (song.sequence.begin() + from + 1, song.sequence.begin() + to + 1);
    // sections starting inside the combined part go (it's one position now); later ones move up
    auto& secs = song.sections;
    secs.erase (std::remove_if (secs.begin(), secs.end(), [&] (const SongSection& x) { return x.pos > from && x.pos <= to; }), secs.end());
    for (auto& x : secs) if (x.pos > to) x.pos -= to - from;
    tidySections (secs, (int) song.sequence.size());
    return nb;
}

bool doubleBlockLength (Block& b)
{
    const int n = b.lines;
    if (n < 1 || n * 2 > kMaxLines) return false;
    for (int l = 0; l < n; ++l) b.rows[(size_t) (n + l)] = b.rows[(size_t) l];
    b.lines = n * 2;
    return true;
}

bool halveBlockLength (Block& b, bool& secondHalfDiffered)
{
    secondHalfDiffered = false;
    const int n = b.lines;
    if (n < 2) return false;
    const int keep = (n + 1) / 2;
    for (int l = keep; l < n; ++l)
    {
        const int src = l - keep;
        for (int t = 0; t < kMaxTracks; ++t)
        {
            const Cell& a = b.at (l, t);
            const Cell& c = b.at (src, t);
            if (a.note != c.note || a.instr != c.instr || a.cmd != c.cmd || a.param != c.param || a.cmd2 != c.cmd2 || a.param2 != c.param2)
                secondHalfDiffered = true;
        }
        b.rows[(size_t) l].fill (Cell{});
    }
    b.lines = keep;
    return true;
}

// ---------------------------------------------------------------- sections
int sectionAt (const std::vector<SongSection>& sections, int pos)
{
    for (int i = 0; i < (int) sections.size(); ++i) if (sections[(size_t) i].pos == pos) return i;
    return -1;
}

void sectionRange (const Song& song, int seqPos, int& from, int& to)
{
    const int n = (int) song.sequence.size();
    seqPos = std::clamp (seqPos, 0, std::max (0, n - 1));
    from = 0; to = n - 1;
    for (const auto& sec : song.sections)
    {
        if (sec.pos <= seqPos) from = std::max (from, sec.pos);
        else { to = std::min (to, sec.pos - 1); }
    }
}

void tidySections (std::vector<SongSection>& s, int length)
{
    s.erase (std::remove_if (s.begin(), s.end(), [length] (const SongSection& x) { return x.pos < 0 || x.pos >= length; }), s.end());
    std::stable_sort (s.begin(), s.end(), [] (const SongSection& a, const SongSection& b) { return a.pos < b.pos; });
    // one per position: keep the last one (the later section's music is what's left there)
    std::vector<SongSection> out;
    for (const auto& x : s)
    {
        if (! out.empty() && out.back().pos == x.pos) out.back() = x;
        else out.push_back (x);
    }
    s.swap (out);
}

void sectionsInserted (std::vector<SongSection>& s, int at, int count, int newLength)
{
    for (auto& x : s) if (x.pos >= at && x.pos > 0) x.pos += count;
    tidySections (s, newLength);
}

void sectionsRemoved (std::vector<SongSection>& s, int at, int count, int newLength)
{
    for (auto& x : s)
    {
        if (x.pos >= at + count) x.pos -= count;
        else if (x.pos >= at) x.pos = at;   // its first positions went: it now starts at what follows
    }
    tidySections (s, newLength);
}
} // namespace prv

namespace prv
{
namespace
{
    // the cells where a v0.11 / v0.13 song played a chord: 0xy (not 000) on a note line of an
    // instrument with CHORD on (following the instrument down each track, along the song)
    struct ChordHit { int block, line, track, column; uint8_t param; };
    std::vector<ChordHit> oldChordCells (const Song& song)
    {
        std::vector<ChordHit> hits;
        std::vector<bool> done (song.blocks.size(), false);
        int carried[kMaxTracks];
        for (int& c : carried) c = -1;
        auto visit = [&] (int bi)
        {
            const auto& b = song.blocks[(size_t) bi];
            for (int t = 0; t < kMaxTracks; ++t)
            {
                int ins = carried[t];
                for (int l = 0; l < b.lines; ++l)
                {
                    const auto& c = b.at (l, t);
                    if (c.instr > 0) ins = c.instr - 1;
                    const bool note = c.note > 0 && c.note <= kNoteMax;
                    if (! done[(size_t) bi] && note && ins >= 0 && ins < kMaxInstruments && song.instruments[(size_t) ins].chord)
                    {
                        if (c.cmd == 0 && c.param != 0) hits.push_back ({ bi, l, t, 1, c.param });
                        else if (c.cmd2 == 0 && c.param2 != 0) hits.push_back ({ bi, l, t, 2, c.param2 });
                    }
                }
                carried[t] = ins;
            }
            done[(size_t) bi] = true;
        };
        for (int sq : song.sequence) if (sq >= 0 && sq < (int) song.blocks.size()) visit (sq);
        for (int bi = 0; bi < (int) song.blocks.size(); ++bi) if (! done[(size_t) bi]) { for (int& c : carried) c = -1; visit (bi); }
        return hits;
    }
}

int convertLegacyChords (Song& song)
{
    const auto hits = oldChordCells (song);
    // a slot for each different 0xy (one already holding the same notes is used again)
    ChordTable table = song.chords;
    int slotFor[256];
    for (int& sl : slotFor) sl = -1;
    int added = 0;
    for (const auto& h : hits)
    {
        if (slotFor[h.param] >= 0) continue;
        ChordDef d;
        std::vector<int> notes { 0 };
        for (int iv : { h.param >> 4, h.param & 15 }) if (iv > 0 && std::find (notes.begin(), notes.end(), iv) == notes.end()) notes.push_back (iv);
        std::sort (notes.begin(), notes.end());
        d.count = (int) notes.size();
        for (int i = 0; i < d.count; ++i) d.notes[(size_t) i] = (int8_t) notes[(size_t) i];
        d.name = "OLD " + std::string (1, "0123456789ABCDEF"[h.param >> 4]) + std::string (1, "0123456789ABCDEF"[h.param & 15]);
        int slot = -1;
        for (int i = 1; i < kChordSlots && slot < 0; ++i) if (table[(size_t) i].sameNotes (d)) slot = i;
        for (int i = 1; i < kChordSlots && slot < 0; ++i) if (table[(size_t) i].empty()) { slot = i; table[(size_t) i] = d; ++added; }
        if (slot < 0) return -1;
        slotFor[h.param] = slot;
    }
    for (const auto& h : hits)
    {
        auto& c = song.blocks[(size_t) h.block].at (h.line, h.track);
        if (h.column == 1) { c.cmd = kCmdChord; c.param = (uint8_t) slotFor[h.param]; }
        else               { c.cmd2 = kCmdChord; c.param2 = (uint8_t) slotFor[h.param]; }
    }
    song.chords = table;
    song.chordsLegacy = false;
    for (auto& in : song.instruments) in.chord = false;
    return added;
}

int convertChordFlags (Song& song)
{
    const auto hits = oldChordCells (song);
    for (const auto& h : hits)
    {
        auto& c = song.blocks[(size_t) h.block].at (h.line, h.track);
        if (h.column == 1) c.cmd = kCmdChord; else c.cmd2 = kCmdChord;
    }
    for (auto& in : song.instruments) in.chord = false;
    return (int) hits.size();
}
} // namespace prv
