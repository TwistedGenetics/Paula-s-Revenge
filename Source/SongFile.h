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
// keepRate (the default): don't resample; the instrument plays the file at its
// own rate on C-3. false converts it to the Amiga rate (16574 Hz on C-3).
bool importSample (const juce::File& file, Instrument& out, juce::String& error, bool keepRate = true);
constexpr double kSampleRateAtC3 = 16574.27;

// Decode an audio file at its own sample rate (for previews). `right` is left
// empty for mono files. `meta` gets the name, volume and any 8SVX loop.
bool decodeAudioFile (const juce::File& file, std::vector<float>& left, std::vector<float>& right,
                      double& rate, Instrument& meta, juce::String& error);

// Render the whole song once through to 24-bit stereo WAV, then its tail (until it has been
// quiet for a second, at most 20 s). `pluginFactory` makes plug-in slots' effects (none =
// plug-in slots pass audio through). stems = one file per used track, SEND A, SEND B and
// the master, in a folder next to `file` ("<name> STEMS"); all the same length. MIDI-out
// instruments can't be rendered: their names go in `leftOut`.
bool renderSongToWav (const Song& song, const PaulaOptions& options, double sampleRate,
                      const juce::File& file, juce::String& error,
                      std::function<std::unique_ptr<Effect> (int, const FxSlot&, double)> pluginFactory = nullptr,
                      std::function<std::unique_ptr<PluginSynth> (int, const Instrument&, double)> synthFactory = nullptr,
                      bool stems = false, juce::StringArray* leftOut = nullptr);
juce::File stemFolderFor (const juce::File& file);
juce::String stemFileName (int stem);          // "01 TRACK 1.wav", "09 SEND A.wav", "11 MASTER.wav"
std::vector<bool> usedTracks (const Song&);     // tracks with notes (or a plug-in instrument's output)
} // namespace prv
