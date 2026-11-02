// Paula's Revenge - MODULATION settings (in the song): 16 modulators, each a source (LFO, a track's
// level, a 16-step sequence, random, the key or velocity of notes, or a MACRO knob) sending to up
// to 8 targets: any built-in effect control, a plug-in effect's parameter, or a track's VOLUME and
// SENDS, each by its own amount (negative turns it down, for ducking).
#pragma once

#include <cstdint>
#include <string>

namespace prv
{
constexpr int kMods = 16, kModRoutes = 8, kModSteps = 16, kModParams = 6;
enum class ModSource : uint8_t { Off = 0, Lfo, Follower, Stepper, Random, Key, Velocity, Macro, Count };

// what a route moves: a mixer slot's effect (target = slot, param = its parameter), or a track's
// strip (target = kModStripBase + track; param 0 VOLUME, 1 SEND A, 2 SEND B)
constexpr int kModStripBase = 2000;
constexpr int kModStripParams = 3;
// an automation knob (V / J) can turn a MACRO: AutoTarget { kModMacroBase + mod, 0 }
constexpr int kModMacroBase = 3000;

struct ModRoute
{
    int   target = -1, param = -1;
    float amount = 0.5f;          // -1 .. 1 of the control's whole range (VOLUME / SENDS: x0 .. x2)
    float base = 0.5f;            // a plug-in parameter's value when it was picked (it swings from there)
    std::string name;             // "TRACK 1 TAPE ECHO: FEEDBACK"
    bool valid() const { return target >= 0 && param >= 0; }
    bool operator== (const ModRoute& o) const { return target == o.target && param == o.param && amount == o.amount && base == o.base && name == o.name; }
};

struct Modulator
{
    ModSource source = ModSource::Off;
    float p[kModParams] {};
    float steps[kModSteps] {};
    ModRoute route[kModRoutes];
    bool on() const { return source != ModSource::Off; }
    int routes() const { int n = 0; for (const auto& r : route) n += r.valid(); return n; }
    bool operator== (const Modulator& o) const
    {
        if (source != o.source) return false;
        for (int i = 0; i < kModParams; ++i) if (p[i] != o.p[i]) return false;
        for (int i = 0; i < kModSteps; ++i) if (steps[i] != o.steps[i]) return false;
        for (int i = 0; i < kModRoutes; ++i) if (! (route[i] == o.route[i])) return false;
        return true;
    }
};

struct ModSettings
{
    Modulator mod[kMods];
    bool any() const { for (const auto& m : mod) if (m.on() && m.routes() > 0) return true; return false; }
    bool operator== (const ModSettings& o) const { for (int i = 0; i < kMods; ++i) if (! (mod[i] == o.mod[i])) return false; return true; }
};
} // namespace prv
