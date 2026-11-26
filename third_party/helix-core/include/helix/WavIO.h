// WAV reading and writing (non-real-time). Reads PCM 8/16/24/32 and IEEE float 32/64, any channel count,
// and the Serum "clm " chunk that gives a wavetable's frame size.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace helix {

struct WavData
{
    int sampleRate = 44100;
    int channels = 1;
    std::vector<float> samples;      // interleaved
    int clmFrameSize = 0;            // from a Serum "clm " chunk ("<!>2048 ..."), 0 if absent
    size_t frames() const { return channels > 0 ? samples.size() / (size_t)channels : 0; }
    std::vector<float> mono() const; // channel average
};

// Parse from memory. Returns false (and an error message) on anything malformed; never reads out of bounds.
bool parseWav(const uint8_t* data, size_t size, WavData& out, std::string* error = nullptr);
bool readWavFile(const std::string& path, WavData& out, std::string* error = nullptr);
// 16-bit PCM or 32-bit float stereo/mono writer.
bool writeWavFile(const std::string& path, const float* interleaved, size_t frames, int channels, int sampleRate,
                  bool float32 = false);

} // namespace helix
