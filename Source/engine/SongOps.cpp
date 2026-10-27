// Paula's Revenge - song structure operations
#include "SongOps.h"

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

int cloneInPlace (Song& song, int seqPos)
{
    if (seqPos < 0 || seqPos >= (int) song.sequence.size()) return -1;
    if ((int) song.blocks.size() >= kMaxBlocks) return -1;
    const int b = song.safeBlock (song.sequence[(size_t) seqPos]);
    const int nb = b + 1;
    Block copy = song.blocks[(size_t) b];
    song.blocks.insert (song.blocks.begin() + nb, std::move (copy));
    for (auto& s : song.sequence) if (s >= nb) ++s;
    song.sequence[(size_t) seqPos] = nb;
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
} // namespace prv
