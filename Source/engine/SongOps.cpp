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
