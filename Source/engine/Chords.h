// Paula's Revenge - the chord table: 0xy on a CHORD instrument's note plays chord slot xy
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace prv
{
constexpr int kChordSlots    = 256;   // slots 01-FF (00 = no chord)
constexpr int kMaxChordNotes = 8;

// one voicing in the built-in chord database (generated: ChordDatabase.cpp)
struct ChordEntry
{
    const char* category;   // MAJOR, MINOR, DOMINANT, SUSPENDED, DIMINISHED, AUGMENTED, OTHER
    const char* symbol;     // MIN7, 7\x07 9 ... (\x07 = flat sign in the pixel font)
    const char* name;       // MINOR SEVENTH
    const char* voicing;    // ROOT, INV 1.., DROP 2, DROP 3, OPEN
    int8_t notes[kMaxChordNotes];
    int count;
};

const ChordEntry* chordDatabase (int& count);
std::vector<std::string> chordCategories();

// a slot in a song's chord table: notes are semitones from the played note
// (the played note itself only sounds when 0 is one of them)
struct ChordDef
{
    std::string name;                           // what the slot shows, e.g. "MIN7 INV 1"
    std::array<int8_t, kMaxChordNotes> notes {};
    int count = 0;

    bool empty() const { return count <= 0; }
    bool operator== (const ChordDef& o) const
    {
        if (count != o.count || name != o.name) return false;
        for (int i = 0; i < count; ++i) if (notes[(size_t) i] != o.notes[(size_t) i]) return false;
        return true;
    }
    bool operator!= (const ChordDef& o) const { return ! (*this == o); }
    bool sameNotes (const ChordDef& o) const
    {
        if (count != o.count) return false;
        for (int i = 0; i < count; ++i) if (notes[(size_t) i] != o.notes[(size_t) i]) return false;
        return true;
    }
};

using ChordTable = std::array<ChordDef, kChordSlots>;

std::string chordLabel (const ChordEntry&);       // symbol + voicing ("MIN7 INV 1"; root position: "MIN7")
ChordDef    chordFrom (const ChordEntry&);
int         findChord (const std::string& symbol, const std::string& voicing = "ROOT");   // -1 = none
const ChordTable& defaultChordTable();             // the factory set (01 = MIN, 02 = MAJ ...)
std::string notesText (const ChordDef&);           // "+0 +3 +7"
std::vector<int> differingSlots (const ChordTable& a, const ChordTable& b);   // slots whose notes differ
bool        tableIsEmpty (const ChordTable&);
} // namespace prv
