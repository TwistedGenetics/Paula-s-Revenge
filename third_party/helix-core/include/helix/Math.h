// Small, fast math helpers. All inline, allocation free, safe on the audio thread.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace helix {

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

template <typename T> inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// 2^x, accurate to ~1e-4 relative over [-30, 30]. Used for pitch -> ratio at control rate and per sample.
inline float fastExp2(float x)
{
    x = clamp(x, -126.0f, 126.0f);
    float fl = std::floor(x);
    float f = x - fl;
    // 4th-order minimax polynomial for 2^f on [0,1)
    float p = 1.0f + f * (0.6931471806f + f * (0.2402265070f + f * (0.0555041087f + f * 0.0096181291f)));
    union { float f; int32_t i; } u;
    u.i = (int32_t)(fl + 127.0f) << 23;
    return p * u.f;
}

inline float semisToRatio(float semis) { return fastExp2(semis * (1.0f / 12.0f)); }
inline float noteToHz(float note) { return 440.0f * fastExp2((note - 69.0f) * (1.0f / 12.0f)); }
inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float gainToDb(float g) { return 20.0f * std::log10(std::max(g, 1e-9f)); }

// Rational tanh approximation: smooth, monotonic, exact limits of +-1. Cheap soft clipper.
inline float fastTanh(float x)
{
    x = clamp(x, -4.5f, 4.5f);
    float x2 = x * x;
    float n = x * (27.0f + x2);
    float d = 27.0f + 9.0f * x2;
    return clamp(n / d, -1.0f, 1.0f);
}

// tan(pi * fc / fs) prewarp for TPT filters, with fc clamped below Nyquist.
inline float prewarp(float fc, float invSr)
{
    float w = clamp(fc * invSr, 1e-5f, 0.49f);
    return std::tan(kPi * w);
}

inline float wrap01(float p) { return p - std::floor(p); }

// Deterministic per-instance random generator (xorshift32). Never uses global state.
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng(uint32_t seed = 0x9E3779B9u) : s(seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float bi()  { return uni() * 2.0f - 1.0f; }                            // [-1,1)
};

} // namespace helix
