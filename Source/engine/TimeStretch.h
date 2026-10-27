// Paula's Revenge - old-sampler cyclic time stretch / pitch shift (plain C++)
//
// Recreates the "cyclic" time stretch of late-80s/90s Akai samplers, the way
// Akaizer does: the sound is cut into short cycles that are replayed at the
// new pitch and repeated or skipped to reach the new length, with no smooth
// crossfading. That's what gives jungle breaks their metallic, warbling stretch.
#pragma once

#include <vector>

namespace prv
{
struct StretchSettings
{
    enum class Mode { Classic, Revised };
    Mode   mode = Mode::Classic;
    double timeFactor = 1.0;    // output length / input length (0.25 .. 20)
    double semitones = 0.0;     // pitch shift, independent of time
    double cents = 0.0;
    int    cycleLength = 1000;  // samples (20 .. 2000), at the sample's own rate
};

// Process one or two channels with the same cycle boundaries.
std::vector<std::vector<float>> cyclicStretch (const std::vector<std::vector<float>>& channels, const StretchSettings&);
} // namespace prv
