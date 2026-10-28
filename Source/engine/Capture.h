// Paula's Revenge - CAPTURE: turning what was played (MIDI keyboard, laptop keys) into pattern notes,
// like Ableton's Capture MIDI. Plain C++: the timing maths is unit tested.
#pragma once

#include "Song.h"
#include "ChordDetect.h"
#include <vector>

namespace prv
{
struct CapturedNote
{
    double timeMs = 0;
    int note = 0, velocity = 100;
    bool on = true;
    int instrument = 0;
    bool playing = false;        // played while the song / block was playing
    int seqPos = 0, block = 0;   // where (when playing)
    double line = 0;             // the line, with its fraction
    int speed = 6;
    int pass = 0;                // playback's pass (+1 at every wrap back to the start): one take each
};

struct CapturePlace
{
    int block = 0, line = 0, track = 0;
    uint8_t note = 0, instr = 0;   // note = kNoteOff for a release
    uint8_t micro = 0;             // Mxx (0 = on the line)
    uint8_t chordSlot = 0;         // #xy: notes played together that matched the chord table
    bool chordExact = true;
};

struct CaptureResult
{
    std::vector<CapturePlace> places;
    int skipped = 0;               // notes with no free cell
    bool played = false;           // placed where they were played (else from the cursor)
    int firstBlock = 0, firstLine = 0;
    int chords = 0;                // chords written as one note + #xy
};

// Which notes to write: the latest take - the notes since the last gap of more than 4 s while
// stopped, or, while playing, the latest pass of playback that had notes in it (silent passes
// after it don't count; a held note's release can fall in a later pass). `offsFor` says which instruments
// get note-offs (plug-ins / MIDI out: their notes would hang on otherwise). With `detectChords`,
// notes those instruments played together (kChordWindowMs) that match the song's chord table
// go in as the root + #xy on one track.
CaptureResult placeCapture (const Song& song, const std::vector<CapturedNote>& log,
                            int cursorBlock, int cursorLine, int cursorTrack, int bpm, int speed,
                            const std::vector<bool>& offsFor, bool detectChords = false);
} // namespace prv
