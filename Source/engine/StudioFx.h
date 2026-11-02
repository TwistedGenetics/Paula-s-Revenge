// Paula's Revenge - the studio effects (plain C++), each our own design:
//   VAST      a delay that turns into a reverb: 8 modulated delay lines feeding each other
//             (DENSITY spreads them from clean repeats to a cloud, WARP smears each pass)
//   SPACE     a plate / hall tank with a pre-delay that can echo, SHIMMER (an octave up in the
//             tail) and FREEZE
//   TAPE ECHO a tape loop with three playback heads, wow, flutter, saturation and wear
//   ENSEMBLE  the bucket-brigade chorus of the classic polysynths (I, II, I+II) and a string machine
// Parameters change smoothly (no zipper noise); the saturation runs at twice the sample rate.
#pragma once

#include "Effects.h"

namespace prv
{
std::unique_ptr<Effect> makeStudioEffect (FxType);    // nullptr for the other types
std::unique_ptr<Effect> makeStudioEffect2 (FxType);   // (v0.35's: ROOMS ... DIMENSION)
}
