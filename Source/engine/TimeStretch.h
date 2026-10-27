// Paula's Revenge - old-sampler cyclic time stretch / pitch shift (plain C++)
//
// Recreates the "cyclic" time stretch of Akai S-series samplers, the sound of
// 90s jungle breaks: the output is built from fixed-length cycles, each one a
// grain of the source replayed at the new pitch, so grains repeat (stretch)
// or get skipped (squash) at the cycle rate.
//
// Tuned against real recordings (see README): an Akai S3000XL stretch of the
// Amen and a set of 90s jungle stretches. They show
//   * joins at a fixed spacing in the OUTPUT (the cycle length),
//   * no click at the joins (the waveform steps no more than it does anywhere
//     else), a ~1 dB loudness dip and a faint, 2-sample-wide mark at each join,
// which fits a splice point moved to where the level and slope line up,
// with a very short blend. That's CLASSIC. HARD skips the smoothing for a
// rougher, clickier sound.
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
