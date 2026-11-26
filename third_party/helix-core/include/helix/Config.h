// HELIX core constants. Everything that sizes a buffer lives here so allocation can happen up front.
#pragma once
#include <cstddef>

namespace helix {

constexpr int kMaxVoices      = 8;     // full-spec polyphony
constexpr int kMaxUnison      = 7;     // per oscillator
constexpr int kMaxFrames      = 256;   // wavetable frames
constexpr int kFrameSize      = 2048;  // samples per frame after import (mip level 0)
constexpr int kNumMipLevels   = 11;    // level L keeps harmonics 1 .. (1024 >> L)
constexpr int kCtrlBlock      = 32;    // modulation / control-rate block, in samples
constexpr int kNumEnvs        = 4;
constexpr int kNumLfos        = 4;
constexpr int kNumPerformers  = 2;
constexpr int kNumModSlots    = 16;
constexpr int kNumMacros      = 8;
constexpr int kLfoMaxPoints   = 16;    // breakpoint LFO shape
constexpr int kMaxSteps       = 16;    // LFO step mode and performers
constexpr int kNumFxSlots     = 7;
constexpr double kMaxSampleRate = 192000.0;

} // namespace helix
