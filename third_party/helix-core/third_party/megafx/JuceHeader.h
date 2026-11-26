// Minimal stand-in for the few JUCE helpers the Mega Synth effects use, so they compile into HELIX (and the MPC
// plugin) without JUCE.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace juce {
template <typename T> inline T jlimit(T lo, T hi, T v) { return v < lo ? lo : (hi < v ? hi : v); }
template <typename T> inline T jmin(T a, T b) { return b < a ? b : a; }
template <typename T> inline T jmax(T a, T b) { return a < b ? b : a; }
template <typename T> struct MathConstants { static constexpr T pi = (T)3.141592653589793238; static constexpr T twoPi = (T)6.283185307179586476; };
using uint32 = uint32_t;
using uint = unsigned int;
}
