#pragma once
/*
    PerfFx — the 8 hold-to-play FX pads (and the FX lane's events), working on
    the dry break signal through a 4-second ring that records all the time.

      STUTTER    loops the last grid slice (1/8 .. 1/64) from the quantised start
      ROLL       loops 1/32 behind the play point, pitch rising while held
      REVERSE    plays backwards from the moment it was hit
      TAPE STOP  slows to a stop over TAPE STOP time
      HALF-TIME  (sequencer side: steps at half speed; nothing here)
      AKAIZE     Akai cyclic stretch of the live signal: the play head crawls at
                 1/ratio, each CYCLE restarts a grain there (the metallic smear)
      FILT DIVE  resonant low-pass sweeping down over a beat
      S950 DROP  (character side: 7.5 kHz / 8 bit while held; nothing here)

    Every effect fades in and out over 3 ms, and only one "read" effect owns
    the output at a time (tape > reverse > roll > stutter > akaize); the one
    losing fades out while the winner fades in. No allocation after prepare().
*/
#include "choppage/Model.h"
#include "choppage/AkaiGrains.h"

namespace bc
{
    class PerfFx
    {
    public:
        enum Pad { stutter = 0, roll, reverse, tapeStop, halfTime, akaize, filterDive, s950Drop };

        struct Settings
        {
            double samplesPerBeat = 22050.0;
            int    stutterDiv = 16, rollDiv = 32;
            double tapeMs = 700.0;
            int    akCycleLength = 800;      // Akaizer CYCLE LENGTH (samples)
            double akCycleFreq = 51.0;       // Akaizer CYCLE FREQ (grains per second)
            double akRatio = 2.0;
            bool   akInTime = false;         // AKAIZE MODE IN TIME: the read point keeps time, the grains ring
            double akPitch = 1.0;            // ... and their pitch (a ratio)
            float  diveRes = 0.7f;
            double quantSteps = 1.0;            // the pad quantise grid in 16ths: 0 off, 0.5 = 1/32, 1 = 1/16 ... 16 = a bar
            double samplesSinceGrid = 0.0;      // since the last 16th line (for quantised starts)
        };

        void prepare (double sampleRate, int maxBlock);
        void reset() noexcept;

        /** held[i]: pad i is down (from MIDI, UI or the FX lane). */
        void process (float* left, float* right, int numSamples, const std::array<bool, kFxPads>& held,
                      const Settings&) noexcept;

        bool isAudible (int pad) const noexcept { return pad >= 0 && pad < kFxPads && fx[(size_t) pad].gain > 0.001f; }

    private:
        struct Read
        {
            bool   on = false;           // wanted
            float  gain = 0.0f;          // 0..1, 3 ms ramps
            double pos = 0.0;            // absolute ring position (samples written so far)
            double speed = 1.0;
            double loopStart = 0.0, loopLen = 0.0, phase = 0.0;
            AkaiGrains grains;
            int    age = 0;
        };

        float ringAt (int ch, double absPos) const noexcept;
        void  engage (int pad, const Settings&) noexcept;
        float readEffect (int pad, int ch, const Settings&) const noexcept;
        int   akSetLength = -1; double akSetFreq = -1.0;   // the AKAIZE pad's grains are set up for these
        void  advance (int pad, const Settings&) noexcept;

        double sr = 44100.0;
        std::vector<float> ring[2];
        int ringMask = 0;
        int64_t written = 0;
        std::array<Read, kFxPads> fx {};
        std::array<bool, kFxPads> wasHeld {};
        float rampStep = 0.01f;

        Svf dive;
        float diveCutoff = 18000.0f, diveMix = 0.0f;
        int   diveAge = 0;
    };
}
