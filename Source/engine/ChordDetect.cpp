// Paula's Revenge - chord detection
#include "ChordDetect.h"

#include <algorithm>
#include <climits>

namespace prv
{
namespace
{
    int pc (int n) { return ((n % 12) + 12) % 12; }

    std::vector<int> sortedUnique (std::vector<int> v)
    {
        std::sort (v.begin(), v.end());
        v.erase (std::unique (v.begin(), v.end()), v.end());
        return v;
    }

    // how good a candidate is: lower is better
    struct Score
    {
        int notLowest = 1, notFirst = 1, slot = INT_MAX;
        bool operator< (const Score& o) const
        {
            if (notLowest != o.notLowest) return notLowest < o.notLowest;
            if (notFirst != o.notFirst) return notFirst < o.notFirst;
            return slot < o.slot;
        }
    };
}

ChordMatch matchChord (const ChordTable& table, const std::vector<int>& notes)
{
    ChordMatch best;
    const auto played = sortedUnique (notes);
    if (played.size() < 2) return best;
    const int lowest = played.front();
    const int first = notes.front();

    std::vector<int> playedPcs;
    for (int n : played) playedPcs.push_back (pc (n));
    playedPcs = sortedUnique (playedPcs);

    auto scoreOf = [&] (int slot, int root)
    {
        Score s;
        s.notLowest = root == lowest ? 0 : (pc (root) == pc (lowest) ? 1 : 2);
        s.notFirst = pc (root) == pc (first) ? 0 : 1;
        s.slot = slot;
        return s;
    };

    // 1. exact: the slot's notes from some root are exactly the notes played
    Score bestScore;
    bool found = false;
    for (int slot = 1; slot < kChordSlots; ++slot)
    {
        const auto& d = table[(size_t) slot];
        if (d.empty()) continue;
        std::vector<int> iv (d.notes.begin(), d.notes.begin() + std::min (d.count, kMaxChordNotes));
        iv = sortedUnique (iv);
        if (iv.size() != played.size()) continue;
        const int root = lowest - iv.front();
        bool same = true;
        for (size_t i = 0; i < iv.size() && same; ++i) same = root + iv[i] == played[i];
        if (! same) continue;
        const auto s = scoreOf (slot, root);
        if (! found || s < bestScore) { bestScore = s; best = { slot, root, true }; found = true; }
    }
    if (found) return best;

    // 2. by note names: the slot's notes, folded into one octave, are the note names played
    for (int slot = 1; slot < kChordSlots; ++slot)
    {
        const auto& d = table[(size_t) slot];
        if (d.empty()) continue;
        std::vector<int> iv (d.notes.begin(), d.notes.begin() + std::min (d.count, kMaxChordNotes));
        const int minIv = *std::min_element (iv.begin(), iv.end());
        for (int p = 0; p < 12; ++p)
        {
            std::vector<int> pcs;
            for (int n : iv) pcs.push_back (pc (p + n));
            if (sortedUnique (pcs) != playedPcs) continue;
            // the root's octave: the chord's lowest note as close to the lowest note played as it goes
            int root = lowest - minIv;
            root += ((p - pc (root)) % 12 + 12) % 12;   // up to the right note name...
            if (root + minIv > lowest + 6) root -= 12;   // ...or the one below, whichever is closer
            const auto s = scoreOf (slot, root);
            if (! found || s < bestScore) { bestScore = s; best = { slot, root, false }; found = true; }
        }
    }
    return best;
}
} // namespace prv
