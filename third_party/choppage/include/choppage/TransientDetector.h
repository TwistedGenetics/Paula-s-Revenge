#pragma once

#include <vector>
#include "choppage/Sample.h"


namespace bc
{
    /*
        TransientDetector
        -----------------
        Finds drum hits in a break. Runs on the loader thread, never the
        audio thread, and never changes the audio.

          1. Mono mix -> 1024-point FFT every 256 samples (Hann window)
          2. Spectral flux on log-compressed magnitudes: how much louder each
             frequency got since the last frame (only increases count), with
             the high band weighted up so snares / hats register as clearly
             as kicks
          3. Adaptive threshold: local median + an offset set by SENSITIVITY
          4. Peak picking with a 40 ms minimum gap (no double hits on flams)
          5. Each onset refined in the time domain to the start of the
             attack, then moved back to the preceding zero crossing so the
             slice starts clean
          6. Keep the strongest `maxSlices` (sample start always included)
    */
    namespace TransientDetector
    {
        struct Onset { int position; float strength; };

        /** All candidate onsets, strongest first not guaranteed (time order). */
        std::vector<Onset> findOnsets (const SampleData&, float sensitivity);

        /** sensitivity 0..1 (higher = more slices). Result is normalised. */
        SliceTable detect (const SampleData&, float sensitivity, int maxSlices);
    }
}
