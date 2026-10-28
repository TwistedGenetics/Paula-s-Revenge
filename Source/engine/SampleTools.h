// Paula's Revenge - the sample editor's tools (plain C++, unit tested): gain, DC, trimming,
// silence, loop crossfades, pitch, swapping sides. They work on 16-bit sample data.
#pragma once

#include <cstdint>
#include <vector>

namespace prv
{
struct Instrument;

// [a, b) of one channel by a gain in dB (clipped at 16 bits)
void sampleGain (std::vector<int16_t>& d, int a, int b, double db);
// the average taken away (the whole of [a, b) at once)
void removeDc (std::vector<int16_t>& d, int a, int b);
// [start, end) of the frames louder than floorDb (both channels counted); false = all silence
bool findSoundBounds (const Instrument&, double floorDb, int& start, int& end);
// the loop's end made to flow into its start: the last fade frames before the loop end blended
// with the frames before the loop start (so the jump back is smooth). false = no room.
bool crossfadeLoop (Instrument&, int fadeFrames);
// edits that change the length move the loop, START / END and the slices with the audio
void removeFrames (Instrument&, int a, int b);          // [a, b) taken out
void insertSilence (Instrument&, int at, int frames);
void cropTo (Instrument&, int a, int b);
// the sample (or [a, b) of it) up or down in pitch by resampling: shorter when higher
void pitchBy (Instrument&, double semitones, int a = 0, int b = -1);
void swapSides (Instrument&);

// the pitch of [a, b) in Hz (YIN, on the loudest part; 30 Hz - 2 kHz); 0 = no clear pitch
double detectPitch (const std::vector<int16_t>& d, int a, int b, double rate);
// TUNE TO C: the transpose (semitones) and cents that make C-3 play the nearest C
bool tuneToC (double pitchHz, int& semitones, int& cents);
// BEAT SYNC: a sensible number of lines for a sample this long (seconds) at a line length
int suggestSyncLines (double seconds, double lineSeconds);
} // namespace prv
