// Paula's Revenge - the BREAK page's tools (plain C++, unit tested): which slices of a break are
// kicks, snares and hats, and the one-click re-edits that rebuild a chopped break from them.
#pragma once

#include "Song.h"
#include <cstdint>
#include <vector>

namespace prv
{
enum class HitKind : uint8_t { Kick, Snare, Hat, Other };
const char* hitName (HitKind);          // "KICK", "SNARE", "HAT", "OTHER"
char hitLetter (HitKind);               // 'K', 'S', 'H', 'O'

// each slice's kind, from its first 60 ms: how much of it is low (a kick), how noisy and loud
// (a snare), how bright and quiet (a hat). An instrument without slices: empty.
std::vector<HitKind> classifySlices (const Instrument&);

// the first slice of a kind (-1: none); `pick` chooses among several (wraps)
int sliceOfKind (const std::vector<HitKind>&, HitKind, int pick = 0);

// ---- re-edits, on one track of a block, over its first `lines` lines (the chopped break)
// Notes are slice notes (C-3 = slice 1) of instrument `ins` (0-based). Each returns how many notes it wrote.

// GHOST ROLL: the last quarter of the part becomes a roll of the snare slice on every line,
// quiet to loud (C08 up to C40), with a kick on its first line
int ghostRoll (Block&, int track, int ins, const std::vector<HitKind>&, int lines);
// HALF-TIME: the first half of the part spread over all of it (line l to 2l)
int halfTime (Block&, int track, int lines);
// DOUBLE-TIME: all of it squeezed into the first half (every other line), then again
int doubleTime (Block&, int track, int lines);
// a REBUILD from kicks, snares and hats, `bars` bars of 16 steps each over the part:
// template letters K S H (and G for a ghost snare, quiet), '.' for a rest. pick varies which
// of several kicks / snares / hats plays.
int rebuild (Block&, int track, int ins, const std::vector<HitKind>&, int lines, int bars, const char* tmpl16, int pick);
// the named rebuilds: CLASSIC '94 and three more for VARIATIONS
constexpr int kRebuilds = 4;
const char* rebuildName (int i);        // "CLASSIC '94", "ROLLER", "TWO-STEP", "HALF-STEP"
const char* rebuildTemplate (int i);
// CHOP TO PHRASE: the instrument's sample cut up (division: N equal slices, 0 = by its hits, -1 = its own
// slices as they are) and written into a new phrase of its own - each hit a note (C-3 = slice 1, SLICE TO KEYS
// on), each where it falls, at enough lines a beat that every hit has a line. TRANSPOSE is off, so any note
// plays the chopped break as it is. lineSec: a line at the song's tempo (to find how many bars it is).
// Returns the new phrase's index (-1: no sample, or 16 phrases already).
int chopToPhrase (Song&, int instrument, int division, double lineSec);
} // namespace prv
