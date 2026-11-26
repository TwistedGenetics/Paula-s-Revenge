#pragma once
/*
    SoundChain — everything after the FX pads, in signal order:

      CHARACTER   the machine: sample-and-hold at its rate (or the SAMPLE RATE
                  fader, whichever is lower), bit depth, its output low-pass.
                  DIRTY like the hardware: no anti-alias filter in front.
      FILTER      LP / HP state-variable, knob = cutoff 20 Hz .. 20 kHz
      DISTORTION  tanh drive up to +30 dB, level matched, TONE tilt
      CRUSH       extra bits (16 -> 4) and rate (x1 -> x8) reduction
      ECHO        tempo-synced, filtered feedback, MONO or stereo
      REVERB      pre-delay + JUCE reverb
      VOLUME      and the peak meter
*/
#include "choppage/Model.h"
#include <atomic>

namespace bc
{
    class SoundChain
    {
    public:
        struct Settings
        {
            int    character = 1;
            double faderRate = 44100.0;
            bool   dropHeld = false;  // S950 DROP pad
            double dropRate = 7500.0;
            float  filter = 1.0f; bool highPass = false; float filterRes = 0.2f;
            float  dist = 0.0f, distTone = 0.0f;
            float  crush = 0.0f;
            float  reverb = 0.0f, revDecay = 0.33f, revDamp = 0.5f, revWidth = 0.8f, revPreMs = 10.0f;
            float  echoMix = 0.0f, echoFb = 0.4f; double echoSamples = 11025.0; bool echoMono = true;
            float  volumeDb = 0.0f;
        };

        void prepare (double sampleRate, int maxBlock);
        void reset();
        void process (float* left, float* right, int numSamples, const Settings&) noexcept;

        float getPeak() noexcept { return peak.exchange (0.0f); }

    private:
        double sr = 44100.0;
        // character
        double holdPhase = 1.0; float held[2] {};
        Svf charLp;
        // filter
        Svf filt;
        Smoothed cutoff;
        // distortion tone
        float toneState[2] {};
        // crush
        double crushPhase = 1.0; float crushHeld[2] {};
        // echo
        std::vector<float> echo[2]; int echoW = 0; float echoLp[2] {};
        // reverb
        Reverb verb;
        std::vector<float> pre[2], wet[2]; int preW = 0;
        Smoothed gain;
        std::atomic<float> peak { 0.0f };
    };
}
