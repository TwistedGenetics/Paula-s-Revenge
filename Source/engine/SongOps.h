// Paula's Revenge - song structure operations (plain C++, unit tested)
#pragma once

#include "Song.h"

namespace prv
{
// Move one sequence entry from `from` to `to`. Returns false if nothing moved.
bool moveSequenceEntry (Song& song, int from, int to);

// Swap the contents of blocks a and b and update the sequence, so the song
// sounds exactly the same; only the block numbers change.
bool swapBlockNumbers (Song& song, int a, int b);

// The lowest-numbered block that is empty and not used in the sequence, or a
// new block number at the end. -1 if the song already has 256 blocks.
int nextFreeBlock (const Song& song);

// Give sequence position `seqPos` its own copy of its block, in the next free
// block (see above). No other block numbers change. Returns the new block
// number, or -1 if there's no room.
int cloneInPlace (Song& song, int seqPos);

// Double the block's length; the new second half is an exact copy of the
// first (notes and both effect columns). False if it would pass 256 lines.
bool doubleBlockLength (Block& b);

// Halve the block's length, keeping the first half. `secondHalfDiffered` says
// whether the removed half was different from the first (so data was lost).
// False if the block has fewer than 2 lines.
bool halveBlockLength (Block& b, bool& secondHalfDiffered);

// ---- song sections (labelled dividers in the sequence)
// Index of the section starting exactly at `pos`, or -1
int sectionAt (const std::vector<SongSection>& sections, int pos);
// The positions [from, to] of the section containing seqPos (the part before
// the first divider counts as a section too)
void sectionRange (const Song& song, int seqPos, int& from, int& to);
// Keep sections sorted, one per position, inside a sequence of `length`
void tidySections (std::vector<SongSection>& sections, int length);
// Positions were inserted at `at` / removed from `at`: move the dividers with the music
void sectionsInserted (std::vector<SongSection>& sections, int at, int count, int newLength);
void sectionsRemoved (std::vector<SongSection>& sections, int at, int count, int newLength);
} // namespace prv

namespace prv
{
// ---- chords
// A v0.11 song played 0xy on a CHORD instrument's note as two intervals (x and y semitones up).
// This gives each different 0xy used that way a chord slot with those notes and writes the slot
// numbers into the pattern, so the song sounds the same with the chord table. Returns the number
// of slots used, or -1 if the table had no room (nothing is changed then).
int convertLegacyChords (Song& song);
} // namespace prv
