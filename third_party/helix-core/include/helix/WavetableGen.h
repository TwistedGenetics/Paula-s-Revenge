// Built-in wavetable generators. Run on a loader thread (they allocate and FFT).
#pragma once
#include "helix/Wavetable.h"
#include <string>
#include <vector>

namespace helix {

enum class BuiltinTable : int
{
    BasicShapes = 0,   // sine -> triangle -> saw -> square
    HarmonicSweep,     // additive: 1 harmonic -> 256 harmonics (saw spectrum)
    PwmSweep,          // pulse width 50% -> 2%
    Vowels,            // formant vowels A-E-I-O-U
    Hoover,            // phase-modulated saw, the "Mentasm" family
    Growl,             // vowels through rising sync
    SyncSweep,         // hard-sync saw, ratio 1 -> 8
    DigitalGrit,       // sine -> saw at falling bit depth (S950 / SP-1200 flavour)
    Count
};

const std::vector<std::string>& builtinTableNames();
WavetablePtr makeBuiltinTable(BuiltinTable which);

// One frame from harmonic amplitudes (index 1 = fundamental) and phases, via inverse FFT. out: n samples.
void additiveFrame(const float* amps, const float* phases, int numHarmonics, float* out, int n);

// Formant spectrum for a vowel position 0..4 (A, E, I, O, U, fractional = morph), fundamental f0 Hz.
void vowelFrame(float vowelPos, float f0, float* out, int n);

} // namespace helix
