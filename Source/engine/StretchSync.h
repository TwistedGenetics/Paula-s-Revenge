// Paula's Revenge - STRETCH: BEAT SYNC that keeps the pitch (plain C++, unit tested).
// The sample is time-stretched with the old-sampler stretch (CLASSIC or AKAIZER) to the length BEAT
// SYNC asks for at the current tempo, and again when the tempo or LINES change. The original stays
// in the instrument (stretchSrc), so switching STRETCH off gives it back untouched; the song file
// keeps the original too. Rendering happens off the audio thread: make() then apply() (a swap).
#pragma once

#include "Song.h"
#include <optional>

namespace prv::stretch
{
const char* modeName (int mode);                      // "OFF", "CLASSIC", "AKAIZER"
bool wanted (const Instrument&);                      // STRETCH on, BEAT SYNC on, a sample
bool active (const Instrument&);                      // the sample playing is a stretch of the source
double factorFor (const Instrument&, double bpm, int speed);   // output length / source length
uint64_t fingerprint (const Instrument&);             // (of every sample)
bool edited (const Instrument&);                      // the data isn't the stretch we made (it was edited)

void begin (Instrument&);                             // keep the source (data, slices, START / END, loop)
void restore (Instrument&);                           // the source back, untouched
void bake (Instrument&);                              // keep what plays now as the sample; STRETCH off
Instrument original (const Instrument&);              // a copy as it was before stretching (for saving)

// re-render when the factor is off by more than 0.3%, or the PITCH / mode changed (or forced)
bool needsRender (const Instrument&, double bpm, int speed);
struct Render
{
    std::vector<int16_t> data, dataR;
    std::vector<int> slices;
    int playStart = 0, playEnd = 0, loopStart = 0, loopLength = 0;
    double factor = 1;
    int semis = 0, mode = 0;
};
std::optional<Render> make (const Instrument&, double bpm, int speed);
void apply (Instrument&, Render&&);                   // cheap: swaps; do it under the audio lock
} // namespace prv::stretch
