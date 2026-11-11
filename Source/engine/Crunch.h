// Paula's Revenge - CRUNCH: a sample put through an old sampler for good - AMIGA 8-BIT (16.7 kHz, 8 bits, no
// filter: it aliases), SP-1200 (26 kHz, 12 bits, unfiltered and a little driven: the gritty one) or MPC60 (40 kHz,
// 12 bits, filtered: the warm one). The sample's rate becomes the sampler's, so it plays at the same pitch; its
// slices, loop and START / END move with it. Plain C++, unit tested.
#pragma once

#include "Song.h"

namespace prv
{
enum class CrunchStyle { Amiga8 = 0, Sp1200, Mpc60 };
const char* crunchName (CrunchStyle);
bool crunchInstrument (Instrument&, CrunchStyle);   // false: no sample in it
} // namespace prv
