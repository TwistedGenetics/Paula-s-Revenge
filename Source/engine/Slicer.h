// Paula's Revenge - find slice points in a sample (plain C++, unit tested)
#pragma once

#include <cstdint>
#include <vector>

namespace prv
{
// Start points of hits (drum hits, notes) found by a jump in loudness.
// sensitivity 0..1 (higher finds quieter hits). The first point is always 0.
std::vector<int> detectSlices (const std::vector<int16_t>& left, const std::vector<int16_t>& right,
                               double sampleRate, float sensitivity, int maxSlices = 64);

// `count` equal slices
std::vector<int> evenSlices (int length, int count);

// Keep slice points sorted, unique, inside the sample, starting at 0
void tidySlices (std::vector<int>& slices, int length);
} // namespace prv
