// Paula's Revenge - the chord table
#include "Chords.h"

#include <algorithm>

namespace prv
{
std::vector<std::string> chordCategories()
{
    return { "MAJOR", "MINOR", "DOMINANT", "SUSPENDED", "DIMINISHED", "AUGMENTED", "OTHER" };
}

std::string chordLabel (const ChordEntry& e)
{
    std::string s = e.symbol;
    if (std::string (e.voicing) != "ROOT") s += std::string (" ") + e.voicing;
    return s;
}

ChordDef chordFrom (const ChordEntry& e)
{
    ChordDef d;
    d.name = chordLabel (e);
    d.count = std::clamp (e.count, 0, kMaxChordNotes);
    for (int i = 0; i < d.count; ++i) d.notes[(size_t) i] = e.notes[i];
    return d;
}

int findChord (const std::string& symbol, const std::string& voicing)
{
    int n = 0;
    const ChordEntry* db = chordDatabase (n);
    for (int i = 0; i < n; ++i)
        if (symbol == db[i].symbol && voicing == db[i].voicing) return i;
    return -1;
}

const ChordTable& defaultChordTable()
{
    static const ChordTable table = []
    {
        ChordTable t;
        const char* slots[][2] = {
            { "MIN", "ROOT" }, { "MAJ", "ROOT" }, { "MIN7", "ROOT" }, { "MAJ7", "ROOT" },         // 01-04
            { "7", "ROOT" }, { "SUS2", "ROOT" }, { "SUS4", "ROOT" }, { "DIM", "ROOT" },           // 05-08
            { "AUG", "ROOT" }, { "MIN6", "ROOT" }, { "6", "ROOT" }, { "MIN9", "ROOT" },           // 09-0C
            { "MAJ9", "ROOT" }, { "9", "ROOT" }, { "5", "ROOT" }, { "MIN7\x07" "5", "ROOT" },     // 0D-10
            { "DIM7", "ROOT" }, { "ADD9", "ROOT" }, { "MINADD9", "ROOT" }, { "7SUS4", "ROOT" },   // 11-14
            { "MIN11", "ROOT" }, { "13", "ROOT" }, { "MAJ", "INV 1" }, { "MAJ", "INV 2" },         // 15-18
            { "MIN", "INV 1" }, { "MIN", "INV 2" }, { "MIN7", "DROP 2" }, { "MAJ7", "DROP 2" },   // 19-1C
            { "7#9", "ROOT" }, { "7\x07" "9", "ROOT" }, { "5 OCT", "ROOT" },                      // 1D-1F
        };
        int n = 0;
        const ChordEntry* db = chordDatabase (n);
        int slot = 1;
        for (auto& s : slots)
        {
            const int i = findChord (s[0], s[1]);
            if (i >= 0) t[(size_t) slot] = chordFrom (db[i]);
            ++slot;
        }
        return t;
    }();
    return table;
}

std::string notesText (const ChordDef& d)
{
    std::string s;
    for (int i = 0; i < d.count; ++i)
    {
        const int n = d.notes[(size_t) i];
        if (! s.empty()) s += ' ';
        s += (n < 0 ? "-" : "+") + std::to_string (std::abs (n));
    }
    return s;
}

std::vector<int> differingSlots (const ChordTable& a, const ChordTable& b)
{
    std::vector<int> out;
    for (int i = 1; i < kChordSlots; ++i)
        if (! a[(size_t) i].sameNotes (b[(size_t) i])) out.push_back (i);
    return out;
}

bool tableIsEmpty (const ChordTable& t)
{
    for (int i = 1; i < kChordSlots; ++i) if (! t[(size_t) i].empty()) return false;
    return true;
}
} // namespace prv
