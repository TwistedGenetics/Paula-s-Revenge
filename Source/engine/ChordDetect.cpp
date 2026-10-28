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

ChordDef chordFromNotes (const std::vector<int>& notes)
{
    auto played = sortedUnique (notes);
    if (played.size() > (size_t) kMaxChordNotes) played.resize ((size_t) kMaxChordNotes);
    ChordDef d;
    if (played.size() < 2) return d;
    const int lowest = played.front();

    // one of the database's voicings? Common chords first (the ones in the factory set: E G C is
    // C major's first inversion, not E minor #5), then a root that's the lowest note, then how
    // common, then root position
    const auto& factory = defaultChordTable();
    auto commonRank = [&] (const char* symbol)
    {
        for (int slot = 1; slot < kChordSlots; ++slot) if (factory[(size_t) slot].name == symbol) return slot;
        return 1000;
    };
    int n = 0;
    const ChordEntry* all = chordDatabase (n);
    int best = -1;
    long bestScore = LONG_MAX;
    for (int i = 0; i < n; ++i)
    {
        const auto& e = all[i];
        std::vector<int> iv (e.notes, e.notes + std::min (e.count, kMaxChordNotes));
        iv = sortedUnique (iv);
        if (iv.size() != played.size()) continue;
        const int root = lowest - iv.front();
        bool same = true;
        for (size_t k = 0; k < iv.size() && same; ++k) same = root + iv[k] == played[k];
        if (! same) continue;
        const int rank = commonRank (e.symbol);
        const long score = (rank < 1000 ? 0L : 1000000L) + (root == lowest ? 0L : 100000L) + rank * 10L
                         + (std::string (e.voicing) == "ROOT" ? 0L : 1L);
        if (score < bestScore) { bestScore = score; best = i; }
    }
    if (best >= 0) return chordFrom (all[best]);

    d.name = "USER";
    d.count = (int) played.size();
    for (size_t k = 0; k < played.size(); ++k) d.notes[k] = (int8_t) std::clamp (played[k] - lowest, -128, 127);
    return d;
}
} // namespace prv
