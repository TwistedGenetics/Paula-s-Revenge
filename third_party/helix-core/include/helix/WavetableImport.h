// Wavetable import (non-real-time):
//  * Serum-style WAVs: frames of N samples back to back; N from the "clm " chunk ("<!>2048 ..."), default 2048.
//  * Plain single-cycle WAVs (any length; resampled to 2048).
//  * Folders of single cycles: each file is one frame, in alphabetical order.
//  * Sample -> Table: slice any sample into N frames with pitch detection and zero-crossing alignment.
// Every function validates its input and returns nullptr (with a reason) instead of crashing.
#pragma once
#include "helix/WavIO.h"
#include "helix/Wavetable.h"
#include <string>
#include <vector>

namespace helix {

struct SampleToTableOptions
{
    int frames = 64;            // 2..256
    float minHz = 30.0f;        // pitch search range
    float maxHz = 2000.0f;
    bool alignZeroCrossings = true;
};

struct SampleToTableInfo
{
    bool pitched = false;       // a clear pitch was found
    float detectedHz = 0.0f;
    int periodSamples = 0;      // cycle length used per frame
};

// From parsed WAV data. Decides between Serum table, single cycle and Sample -> Table (long, non-table audio).
WavetablePtr importWavetable(const WavData& wav, const std::string& name, std::string* error = nullptr);
WavetablePtr importWavetableFile(const std::string& path, std::string* error = nullptr);
// A folder of single-cycle WAVs (alphabetical). Non-WAV and unreadable files are skipped.
WavetablePtr importCycleFolder(const std::string& dir, std::string* error = nullptr);

WavetablePtr sampleToTable(const float* mono, size_t len, int sampleRate, const std::string& name,
                           const SampleToTableOptions& opt = {}, SampleToTableInfo* info = nullptr);

// Entries of a directory (files and folders), never throws; empty on any error. For non-real-time scanners.
std::vector<std::string> listDirectory(const std::string& dir);
// All regular files below dir (up to maxDepth folder levels), sorted per folder. Never throws.
void listFilesRecursive(const std::string& dir, std::vector<std::string>& out, int maxDepth = 6);

// YIN pitch detection on x[0..len). Returns the period in samples (fractional), or 0 if aperiodic.
float detectPeriod(const float* x, size_t len, int minPeriod, int maxPeriod, float threshold = 0.15f);

} // namespace helix
