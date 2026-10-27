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

// Make the block at sequence position `seqPos` unique: copy it into a new
// block numbered (old + 1), renumber every later block up by one (sequence
// updated so nothing else changes), and point this position at the copy.
// Returns the new block number, or -1 if the song already has 256 blocks.
int cloneInPlace (Song& song, int seqPos);

// Double the block's length; the new second half is an exact copy of the
// first (notes and both effect columns). False if it would pass 256 lines.
bool doubleBlockLength (Block& b);

// Halve the block's length, keeping the first half. `secondHalfDiffered` says
// whether the removed half was different from the first (so data was lost).
// False if the block has fewer than 2 lines.
bool halveBlockLength (Block& b, bool& secondHalfDiffered);
} // namespace prv
