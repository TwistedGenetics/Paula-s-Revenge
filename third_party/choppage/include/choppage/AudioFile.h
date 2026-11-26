#pragma once
/*
    AudioFile — WAV and AIFF in and out, no dependencies. Used by the MPC build
    (which has no JUCE) and by the tests; the desktop also decodes FLAC / MP3
    through JUCE and hands the result over as SampleData.

    Reads: WAV (PCM 8/16/24/32, float 32/64, WAVE_FORMAT_EXTENSIBLE), AIFF / AIFC (PCM 8/16/24/32, "sowt"),
           REX2 (.rx2 / .rex from ReCycle: the loop plus ReCycle's slice points, see RexFile.cpp).
    Writes: 24-bit PCM WAV.
*/
#include "choppage/Sample.h"
#include <string>

namespace bc::AudioFile
{
    enum class Status { ok, missing, unreadable, tooLong, empty };

    struct Info { bool ok = false; int channels = 0; double sampleRate = 0; int64_t frames = 0;
                  double bpm = 0; int bars = 0; };   // bpm / bars: known from the file (REX), else 0

    /** Header only (fast): for the browser. */
    Info readInfo (const std::string& path);

    /** Whole file, at most `maxSeconds`. Mono stays mono; more than two channels keep the first two. */
    std::shared_ptr<SampleData> load (const std::string& path, Status& status, double maxSeconds = 300.0);

    bool writeWav24 (const std::string& path, const float* const* channels, int numChannels, int numFrames, double sampleRate);

    bool isAudioFile (const std::string& path);          // .wav .aif .aiff .rx2 .rex (the core can read these)
    bool isRexFile (const std::string& path);            // .rx2 .rex
    std::shared_ptr<SampleData> loadRex (const std::string& path, Status& status, double maxSeconds = 300.0);
    Info readRexInfo (const std::string& path);
    std::string baseName (const std::string& path);      // file name without folder or extension
}
