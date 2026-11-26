// Print to wavetable ("neuro resampling"): render the current patch offline, one held note, optionally sweeping
// one control from low to high, and slice the result into a new wavetable.
//
// The note is rendered at a sample rate chosen so that one cycle of the note is exactly kFrameSize samples, so
// each slice is one whole, phase-aligned cycle and the table plays back at any pitch with no resampling. Detune,
// unison, drift and effects all go into the slices as they sound at that moment, which is the point: a moving
// reese or a growl sweep becomes a scannable table you can push through the warps and filters again.
//
// Non-real-time: builds a private Engine, takes ~1-3 s of CPU on the MPC. Call it from a worker thread.
#pragma once
#include "helix/Preset.h"
#include "helix/Wavetable.h"
#include <atomic>
#include <string>
#include <vector>

namespace helix {

// What the print sweeps from 0 to 1 while the note plays (choices::printSource).
enum PrintSource : int
{
    kPrintTime = 0,          // nothing moves: the patch's own envelopes, LFOs and performers do the work
    kPrintMacro1, kPrintMacro2, kPrintMacro3, kPrintMacro4,
    kPrintGrowl, kPrintPosA, kPrintPosB, kPrintCutoff1, kPrintWarpA,
    kNumPrintSources
};

struct PrintOptions
{
    int source = kPrintGrowl;
    int frames = 64;          // 2..256
    float seconds = 2.0f;     // how long the note is held while the frames are taken (0.25..8)
    int note = 36;            // MIDI note held (24..42: one cycle must fit kFrameSize at <= 192 kHz)
    int velocity = 110;
    bool effects = true;      // print through the effects (distortion, OTT ...) or the dry voices only
};

// Parameter swept by a source, or -1 for kPrintTime.
int printSourceParam(int source);
// Note clamped to the printable range.
int printNote(int note);

// Renders and slices. tableA / tableB are the tables the patch plays (engine.uiTable(slot)). Returns null and
// sets *error when the print is silent or the options are bad. If framesOut is given it receives the raw slices
// (frames * kFrameSize samples, peak-normalised) ready for writing to a WAV.
WavetablePtr printToTable(const Preset& patch, WavetablePtr tableA, WavetablePtr tableB, const PrintOptions& opt,
                          const std::string& name, std::vector<float>* framesOut = nullptr,
                          std::string* error = nullptr, const std::atomic<bool>* cancel = nullptr);

// "<dir>/Print 001.wav", the first free number. Creates dir if needed. Empty on failure.
std::string nextPrintPath(const std::string& dir);
// Writes the slices as a 32-bit float mono WAV with a Serum "clm " chunk (2048 samples per frame).
bool writePrintWav(const std::string& path, const std::vector<float>& frames);

} // namespace helix
