#pragma once
/*
    Analysis — runs on the loader thread, never the audio thread.

      classify()      gives every slice a badge (KICK / SNARE / GHOST / HAT / OPEN / OTHER)
                      from three band energies, its peak and how fast it decays.
      estimateTempo() works out the break's BPM and length in bars from its length
                      (and a number in the file name, when there is one).
      gridSlices()    equal slices on a 1/8, 1/16 or 1/32 grid.
*/
#include "choppage/Model.h"

namespace bc::Analysis
{
    struct SliceFeatures
    {
        float low = 0, mid = 0, high = 0;   // share of energy in each band, sums to ~1
        float peak = 0;                      // linear
        float decayMs = 0;                   // time for the envelope to fall 20 dB after the peak
    };

    SliceFeatures measure (const SampleData&, Range region);
    std::array<Badge, kMaxSlices> classify (const SampleData&, const SliceTable&);

    struct Tempo { double bpm = 170.0; int bars = 2; };
    Tempo estimateTempo (double lengthSeconds, const std::string& fileName);

    SliceTable gridSlices (int numSamples, int bars, int stepsPerBar);

    /** The pattern the break plays as recorded: each slice on the 16th nearest its start. */
    Pattern originalPattern (const SlotContent&);
}
