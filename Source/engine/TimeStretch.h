// Paula's Revenge - old-sampler cyclic time stretch / pitch shift (plain C++)
//
// Recreates the "cyclic" time stretch of Akai S-series samplers, the sound of
// 90s jungle breaks: the output is built from fixed-length cycles, each one a
// grain of the source replayed at the new pitch, so grains repeat (stretch)
// or get skipped (squash) at the cycle rate.
//
// CLASSIC was measured from recordings of the real thing (see README): the
// Amen stretched x2 and x4, traced slice by slice back to the original. Fixed
// output cycles, each starting at output position / time factor with no join
// search, and a straight-line crossfade over 2/3 of a cycle from the previous
// slice's continuation. REVISED lines joins up by shape; HARD butts them.
#pragma once

#include <vector>

namespace prv
{
struct StretchSettings
{
    enum class Mode { Classic, Revised, Hard };
    Mode   mode = Mode::Classic;
    double timeFactor = 1.0;    // output length / input length (0.25 .. 20)
    double semitones = 0.0;     // pitch shift, independent of time
    double cents = 0.0;
    int    cycleLength = 1000;  // in 44.1 kHz samples, like Akaizer (20 .. 2000)
    double sampleRate = 44100.0;  // the material's own rate (cycle length is converted)
};

// Process one or two channels with the same cycle boundaries.
std::vector<std::vector<float>> cyclicStretch (const std::vector<std::vector<float>>& channels, const StretchSettings&);

// Cycle length in the material's own samples
int cycleSamples (const StretchSettings&);
} // namespace prv
