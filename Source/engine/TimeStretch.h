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
//
// AKAIZER follows the method of Akaizer v1.2 (Ben Burchett, 2006): half-sine
// windowed grains of 2 x cycle samples, overlap-added and hard-clipped to
// 16 bits a grain at a time, with that program's rounding and constants.
#pragma once

#include <cstdint>
#include <vector>

namespace prv
{
struct StretchSettings
{
    enum class Mode { Classic, Revised, Hard, Akaizer };
    Mode   mode = Mode::Classic;
    double timeFactor = 1.0;    // output length / input length (0.25 .. 20)
    double semitones = 0.0;     // pitch shift, independent of time
    double cents = 0.0;
    int    cycleLength = 1000;  // in 44.1 kHz samples, like Akaizer (20 .. 2000)
    double sampleRate = 44100.0;  // the material's own rate (cycle length is converted)
    // AKAIZER only: the cycle length is in the material's own samples (as in the original)
    int    cycleFreq = 0;         // grains per second; 0 = linked (41000 / cycle length)
    bool   stereoQuirk = true;    // process stereo interleaved, as the original does
};

// ---- the Akaizer routine itself (exact, on 16-bit interleaved samples)
int akaizerRound (double x);                 // round half to even (VB6 CInt)
int akaizerLinkedFreq (int cycleLength);     // 41000 / cycle length, rounded
std::vector<int16_t> akaizerStretch (const std::vector<int16_t>& interleaved, int channels, double sampleRate,
                                     double stretchPercent, int cycleLength, int cycleFreq);

// Process one or two channels with the same cycle boundaries.
std::vector<std::vector<float>> cyclicStretch (const std::vector<std::vector<float>>& channels, const StretchSettings&);

// Cycle length in the material's own samples
int cycleSamples (const StretchSettings&);
} // namespace prv
