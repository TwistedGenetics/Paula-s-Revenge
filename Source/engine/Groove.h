// Paula's Revenge - groove (swing and feels, applied as the song plays) and break tools
// (chop a sliced break into the pattern, re-sequence slices, reverse / stutter / ratchet a
// highlighted part). Plain C++.
#pragma once

#include "Song.h"
#include <cstdint>

namespace prv
{
// ---- GROOVE: the song's feel. Each kind pushes some lines' notes a little late (a fraction of a
// line), scaled by the amount; the notes still start on their line, only later inside it.
enum class GrooveKind : uint8_t { Off = 0, Swing16, Swing8, LazyBreak, Push, Humanise, Count };
constexpr int kGrooveKinds = (int) GrooveKind::Count;
const char* grooveName (GrooveKind);
// how late a note on this line (and track) plays, as a fraction of a line (0 = on the line)
double grooveOffset (GrooveKind, int amountPercent, int line, int track);

// ---- break tools
constexpr int kSliceC3 = 37;   // C-3: the first slice when SLICE TO KEYS is on

// CHOP TO PATTERN: the instrument's slices into the block on that track, each where it falls in
// the sample (START to END spread over `lines` lines; the block's lines when 0), as notes with
// SLICE TO KEYS switched on. Slices that would land on the same line move to the next free one.
// The track's other notes in those lines are cleared. Returns how many slices went in (0: no slices).
int chopToPattern (Song&, int instrument, int block, int track, int lines = 0);

// RE-SEQUENCE: the slice notes in lines l0..l1, tracks t0..t1 shuffled (the rhythm stays, which slice
// plays changes; a slice 1 on the first line stays put so the bar still starts on the downbeat).
// Returns how many notes changed.
int resequenceSlices (Block&, int l0, int l1, int t0, int t1, uint32_t seed);
// REVERSE: the lines in the part, last first
void reverseLines (Block&, int l0, int l1, int t0, int t1);
// STUTTER: the part's first `len` lines (a quarter of it when 0) over and over to its end
void stutterLines (Block&, int l0, int l1, int t0, int t1, int len = 0);
// RATCHET: each note in the part gets a roll (Rx0) that speeds up across it - every 4 ticks at the
// start down to every tick at the end. Goes in a free effect column; returns how many notes got one.
int ratchetNotes (Block&, int l0, int l1, int t0, int t1);
} // namespace prv
