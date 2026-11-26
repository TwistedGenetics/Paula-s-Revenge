// A band-limited wavetable: up to 256 frames, each stored as kNumMipLevels mip levels.
//
// Level L keeps harmonics 1 .. (1024 >> L). Each level is stored at its own power-of-two size, chosen so that
// linear-interpolation images stay near -72 dB (2048, 2048, 1024, 1024, 512, 512, 256 x 5), plus one guard
// sample. A full 256-frame table takes about 8.6 MB; a typical 64-frame table 2.2 MB.
//
// Building (fromFrames) runs FFTs and allocates: call it on a loader thread, never on the audio thread.
// A built table is immutable, so the audio thread can read it with no locking.
#pragma once
#include "helix/Config.h"
#include <memory>
#include <string>
#include <vector>

namespace helix {

class Wavetable
{
public:
    // frames: numFrames * frameLen samples, frame after frame. frameLen must be a power of two in [8, 65536];
    // other lengths are resampled to kFrameSize first. Removes DC, normalises the table's peak to 1.
    static std::shared_ptr<Wavetable> fromFrames(const float* frames, int numFrames, int frameLen,
                                                 const std::string& name, bool normalise = true);

    int numFrames() const { return numFrames_; }
    const std::string& name() const { return name_; }
    // Number of samples in one frame at mip level L (a power of two).
    int levelSize(int level) const { return levelSize_[level]; }
    int levelShift(int level) const { return levelShift_[level]; }   // 32 - log2(levelSize)
    // Frame f at level L: levelSize(L) + 1 samples (the last one repeats the first).
    const float* frame(int level, int f) const { return data_.data() + levelOffset_[level] + (size_t)f * (size_t)(levelSize_[level] + 1); }

    // Level-0 (full band) samples of frame f, kFrameSize long. Handy for drawing and tests.
    const float* fullFrame(int f) const { return frame(0, f); }

    size_t memoryBytes() const { return data_.size() * sizeof(float); }

private:
    Wavetable() = default;
    int numFrames_ = 0;
    std::string name_;
    int levelSize_[kNumMipLevels] = {};
    int levelShift_[kNumMipLevels] = {};
    size_t levelOffset_[kNumMipLevels] = {};
    std::vector<float> data_;
};

using WavetablePtr = std::shared_ptr<const Wavetable>;

// Highest harmonic kept at a mip level.
constexpr int mipMaxHarmonic(int level) { return 1024 >> level; }

// Resample one arbitrary-length cycle to dstLen samples (cubic, periodic). For importing odd-length cycles.
void resampleCycle(const float* src, int srcLen, float* dst, int dstLen);

} // namespace helix
