// Paula's Revenge - native song format, sample import and WAV rendering
#pragma once

#include <JuceHeader.h>
#include "engine/Song.h"
#include "engine/Player.h"

namespace prv
{
// Native song file (.prv): our own compact binary format, gzip-compressed
juce::MemoryBlock writeSong (const Song& song);
bool readSong (const void* data, size_t size, Song& out, juce::String& error);

// Load a song from disk: .prv, .mod, MED (MMD0/MMD1). Warnings go into `notes`.
bool loadSongFile (const juce::File& file, Song& out, juce::String& error, juce::StringArray& notes);

// Load a WAV/AIFF/FLAC/MP3/8SVX sample. The audio is resampled so that
// note C-3 plays it back at its original pitch (16574 Hz, Amiga convention).
bool importSample (const juce::File& file, Instrument& out, juce::String& error);
constexpr double kSampleRateAtC3 = 16574.27;

// Decode an audio file at its own sample rate (for previews). `right` is left
// empty for mono files. `meta` gets the name, volume and any 8SVX loop.
bool decodeAudioFile (const juce::File& file, std::vector<float>& left, std::vector<float>& right,
                      double& rate, Instrument& meta, juce::String& error);

// Render the whole song once through to a 24-bit stereo WAV file.
bool renderSongToWav (const Song& song, const PaulaOptions& options, double sampleRate,
                      const juce::File& file, juce::String& error);
} // namespace prv
