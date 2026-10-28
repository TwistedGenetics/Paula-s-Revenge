// Paula's Revenge - chord detection: notes played together on a MIDI keyboard or the laptop keys,
// matched against the song's chord table so they can go in as one note + #xy. Plain C++, unit tested.
#pragma once

#include "Chords.h"
#include <vector>

namespace prv
{
constexpr double kChordWindowMs = 60.0;   // notes pressed this close together are one chord

struct ChordMatch
{
    int slot = 0;        // 0 = no match
    int root = -1;       // the note to write (the chord's root, as the slot measures from it)
    bool exact = true;   // false: matched by note names only - the voicing may differ from what was played
    explicit operator bool() const { return slot > 0; }
};

// `notes` are the held notes in the order they were pressed (the first one counts for the
// tie-break). Needs at least two different notes. Exact voicings first; then by note names,
// ignoring octaves. Ties: a root that is the lowest note played, then the first note pressed,
// then the lowest slot.
ChordMatch matchChord (const ChordTable& table, const std::vector<int>& notes);

// RECORD CHORD: a slot from notes played together. When the notes are one of the database's
// voicings it takes that name and root (E G C -> MAJ INV 1, root C); else it's "USER", measured
// from the lowest note. Up to kMaxChordNotes (the lowest ones). Empty for fewer than two notes.
ChordDef chordFromNotes (const std::vector<int>& notes);
} // namespace prv
