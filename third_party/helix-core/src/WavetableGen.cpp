#include "helix/WavetableGen.h"
#include "helix/FFT.h"
#include "helix/Math.h"
#include <cmath>
#include <complex>

namespace helix {

const std::vector<std::string>& builtinTableNames()
{
    static const std::vector<std::string> names = {
        "Basic Shapes", "Harmonic Sweep", "PWM Sweep", "Vowels", "Hoover", "Growl", "Sync Sweep", "Digital Grit"};
    return names;
}

void additiveFrame(const float* amps, const float* phases, int numHarmonics, float* out, int n)
{
    std::vector<std::complex<double>> spec((size_t)n, std::complex<double>(0, 0));
    const int maxH = std::min(numHarmonics, n / 2 - 1);
    for (int h = 1; h <= maxH; ++h)
    {
        const double a = amps[h] * 0.5, ph = phases ? phases[h] : 0.0;
        // sin(h w t + ph) = Im(e^{i(...)}) -> place -i/2 a e^{i ph} at +h and conjugate at -h
        const std::complex<double> c = std::polar(a, ph - M_PI / 2.0);
        spec[(size_t)h] = c;
        spec[(size_t)(n - h)] = std::conj(c);
    }
    fft(spec, true);
    for (int i = 0; i < n; ++i) out[i] = (float)spec[(size_t)i].real();
}

// Formant data (Hz, dB, bandwidth Hz) for a bass/baritone voice: classic Peterson-Barney style values.
static const float kFormants[5][3][3] = {
    {{800, 0, 80}, {1150, -6, 90}, {2900, -32, 120}},   // A
    {{400, 0, 70}, {1600, -24, 80}, {2700, -30, 100}},  // E
    {{250, 0, 60}, {1750, -30, 90}, {2600, -16, 100}},  // I
    {{400, 0, 40}, {750, -11, 80}, {2400, -21, 100}},   // O
    {{350, 0, 40}, {600, -20, 80}, {2400, -32, 100}},   // U
};

void vowelFrame(float vowelPos, float f0, float* out, int n)
{
    vowelPos = clamp(vowelPos, 0.0f, 4.0f);
    const int v0 = std::min(3, (int)vowelPos);
    const float t = vowelPos - v0;
    std::vector<float> amps((size_t)n / 2, 0.0f);
    const int maxH = n / 2 - 1;
    for (int h = 1; h < maxH; ++h)
    {
        const float hz = f0 * h;
        if (hz > 16000.0f) break;
        float a = 0.0f;
        for (int k = 0; k < 3; ++k)
        {
            const float fc = lerp(kFormants[v0][k][0], kFormants[v0 + 1][k][0], t);
            const float db = lerp(kFormants[v0][k][1], kFormants[v0 + 1][k][1], t);
            const float bw = lerp(kFormants[v0][k][2], kFormants[v0 + 1][k][2], t);
            const float x = (hz - fc) / bw;
            a += dbToGain(db) / (1.0f + x * x);
        }
        amps[(size_t)h] = a / (float)h * 6.0f + 0.002f / (float)h;   // glottal tilt
    }
    additiveFrame(amps.data(), nullptr, maxH - 1, out, n);
}

static float naiveShape(int shape, float p)
{
    switch (shape)
    {
        case 0: return std::sin(kTwoPi * p);
        case 1: return p < 0.25f ? 4 * p : (p < 0.75f ? 2 - 4 * p : 4 * p - 4);
        case 2: return 2.0f * p - 1.0f;
        default: return p < 0.5f ? 1.0f : -1.0f;
    }
}

WavetablePtr makeBuiltinTable(BuiltinTable which)
{
    const int N = kFrameSize;
    int frames = 64;
    std::vector<float> buf;
    auto alloc = [&](int nf) { frames = nf; buf.assign((size_t)nf * (size_t)N, 0.0f); };
    auto fr = [&](int f) { return buf.data() + (size_t)f * (size_t)N; };

    switch (which)
    {
        case BuiltinTable::BasicShapes:
        {
            alloc(64);
            for (int f = 0; f < frames; ++f)
            {
                const float pos = 3.0f * f / (frames - 1);
                const int s0 = std::min(2, (int)pos);
                const float t = pos - s0;
                for (int i = 0; i < N; ++i)
                {
                    const float p = (float)i / N;
                    fr(f)[i] = lerp(naiveShape(s0, p), naiveShape(s0 + 1, p), t);
                }
            }
            break;
        }
        case BuiltinTable::HarmonicSweep:
        {
            alloc(64);
            std::vector<float> amps(N / 2, 0.0f);
            for (int f = 0; f < frames; ++f)
            {
                const float count = std::pow(256.0f, (float)f / (frames - 1));
                for (int h = 1; h < N / 2; ++h)
                {
                    const float edge = clamp(count - (float)(h - 1), 0.0f, 1.0f);
                    amps[(size_t)h] = edge / (float)h;
                }
                additiveFrame(amps.data(), nullptr, N / 2 - 1, fr(f), N);
            }
            break;
        }
        case BuiltinTable::PwmSweep:
        {
            alloc(64);
            for (int f = 0; f < frames; ++f)
            {
                const float w = lerp(0.5f, 0.02f, (float)f / (frames - 1));
                for (int i = 0; i < N; ++i) fr(f)[i] = ((float)i / N) < w ? 1.0f : -1.0f;
            }
            break;
        }
        case BuiltinTable::Vowels:
        {
            alloc(64);
            for (int f = 0; f < frames; ++f) vowelFrame(4.0f * f / (frames - 1), 65.4f, fr(f), N);
            break;
        }
        case BuiltinTable::Hoover:
        {
            alloc(64);
            for (int f = 0; f < frames; ++f)
            {
                const float depth = 0.45f * (float)f / (frames - 1);
                for (int i = 0; i < N; ++i)
                {
                    const float p = (float)i / N;
                    // saw read through a sine-bent phase, plus a sub-octave saw for the hoover's dip
                    const float ph = wrap01(p + depth * std::sin(kTwoPi * p) / kTwoPi * 2.0f);
                    const float sub = wrap01(p * 0.5f + 0.25f) * 2.0f - 1.0f;   // half rate within cycle
                    fr(f)[i] = (2.0f * ph - 1.0f) * 0.75f + 0.25f * sub * (float)f / (frames - 1);
                }
            }
            break;
        }
        case BuiltinTable::Growl:
        {
            alloc(64);
            std::vector<float> v(N);
            for (int f = 0; f < frames; ++f)
            {
                const float t = (float)f / (frames - 1);
                vowelFrame(4.0f * std::fabs(std::sin(kPi * t * 1.5f)), 55.0f, v.data(), N);
                const float ratio = 1.0f + 3.0f * t;
                for (int i = 0; i < N; ++i)
                {
                    const float p = (float)i / N;
                    const float sp = wrap01(p * ratio);
                    const float win = 0.5f - 0.5f * std::cos(kTwoPi * p);
                    const float s = v[(size_t)((int)(sp * N) & (N - 1))];
                    fr(f)[i] = fastTanh(3.0f * lerp(s, s * win, t)) ;
                }
            }
            break;
        }
        case BuiltinTable::SyncSweep:
        {
            alloc(64);
            for (int f = 0; f < frames; ++f)
            {
                const float ratio = 1.0f + 7.0f * (float)f / (frames - 1);
                for (int i = 0; i < N; ++i) fr(f)[i] = 2.0f * wrap01((float)i / N * ratio) - 1.0f;
            }
            break;
        }
        case BuiltinTable::DigitalGrit:
        {
            alloc(32);
            for (int f = 0; f < frames; ++f)
            {
                const float t = (float)f / (frames - 1);
                const float levels = std::pow(2.0f, lerp(8.0f, 2.0f, t));
                const int steps = (int)std::pow(2.0f, lerp(9.0f, 4.0f, t));
                for (int i = 0; i < N; ++i)
                {
                    const float p = std::floor((float)i / N * steps) / steps;
                    const float s = lerp(std::sin(kTwoPi * p), 2.0f * p - 1.0f, t);
                    fr(f)[i] = std::round(s * levels) / levels;
                }
            }
            break;
        }
        default: alloc(1); for (int i = 0; i < N; ++i) fr(0)[i] = std::sin(kTwoPi * i / N); break;
    }
    const int idx = (int)which;
    const std::string name = idx >= 0 && idx < (int)builtinTableNames().size() ? builtinTableNames()[(size_t)idx] : "Sine";
    return Wavetable::fromFrames(buf.data(), frames, N, name);
}

} // namespace helix
