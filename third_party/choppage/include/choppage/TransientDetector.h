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

          1. Mono, and a high-passed copy (snares, ghosts, hats)
          2. A 24-band log spectrogram, frames every 128 samples (2.9 ms)
          3. SuperFlux in three groups (lows, mids, highs) plus a 6 ms
             burst detector on the high-passed signal, each normalised by
             its own big hits and summed
          4. Peaks above the local average by an offset set by SENSITIVITY
          5. Each peak refined to the sample its hit starts on (where the
             energy just after most outweighs just before, then the first
             sample above what was there before), checked to really be a
             hit, and moved to the zero crossing just before (<= 0.3 ms)
          6. One per hit (40 ms: flams are one hit)
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
