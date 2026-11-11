// Paula's Revenge - GEAR maps (see GearMaps.h)
#include "GearMaps.h"
#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    GearParam cc (const char* g, const char* n, int num, int steps = 0)   { GearParam p; p.group = g; p.name = n; p.how = GearSend::Cc; p.num = num; p.steps = steps; return p; }
    GearParam cc14 (const char* g, const char* n, int msb, int lsb)       { GearParam p; p.group = g; p.name = n; p.how = GearSend::Cc14; p.num = msb; p.lsb = lsb; return p; }
    GearParam nrpn (const char* g, const char* n, int msb, int lsb, int steps = 0) { GearParam p; p.group = g; p.name = n; p.how = GearSend::Nrpn; p.num = msb; p.lsb = lsb; p.steps = steps; return p; }

    // the microKORG 2: its Owner's Manual's MIDI implementation chart (all 7-bit CCs)
    GearMap microKorg2()
    {
        GearMap m;
        m.name = "MICROKORG 2";
        m.note = "KORG'S CCS - LOADED UNITS: ON THE SYNTH ONLY";
        m.params = {
            cc ("OSC 1", "WAVE", 8), cc ("OSC 1", "SHAPE", 9), cc ("OSC 1", "OSC MOD", 15), cc ("OSC 1", "SEMITONE", 16), cc ("OSC 1", "FINE", 17),
            cc ("OSC 2", "WAVE", 18), cc ("OSC 2", "SHAPE", 19), cc ("OSC 2", "OSC MOD", 20), cc ("OSC 2", "SEMITONE", 21), cc ("OSC 2", "FINE", 22),
            cc ("OSC 3", "WAVE", 48), cc ("OSC 3", "SHAPE", 49), cc ("OSC 3", "SEMITONE", 51), cc ("OSC 3", "FINE", 52),
            cc ("MIXER", "OSC 1 LEVEL", 23), cc ("MIXER", "OSC 2 LEVEL", 24), cc ("MIXER", "OSC 3 LEVEL", 25), cc ("MIXER", "NOISE LEVEL", 26),
            cc ("MIXER", "NOISE TYPE", 29), cc ("MIXER", "NOISE COLOUR", 30),
            cc ("FILTER", "TYPE", 27), cc ("FILTER", "CUTOFF", 74), cc ("FILTER", "RESONANCE", 71), cc ("FILTER", "DRIVE", 83), cc ("FILTER", "KEY TRACK", 28),
            cc ("FILTER EG", "INTENSITY", 84), cc ("FILTER EG", "ATTACK", 85), cc ("FILTER EG", "DECAY", 86), cc ("FILTER EG", "SUSTAIN", 87), cc ("FILTER EG", "RELEASE", 88),
            cc ("AMP EG", "ATTACK", 73), cc ("AMP EG", "DECAY", 75), cc ("AMP EG", "SUSTAIN", 70), cc ("AMP EG", "RELEASE", 72), cc ("AMP EG", "VELOCITY", 79),
            cc ("LFO 1", "WAVE", 89), cc ("LFO 1", "FREQ", 90), cc ("LFO 1", "SMOOTH", 91),
            cc ("LFO 2", "WAVE", 102), cc ("LFO 2", "FREQ", 76), cc ("LFO 2", "DELAY", 92),
            cc ("PATCH", "PATCH 1", 103), cc ("PATCH", "PATCH 2", 104), cc ("PATCH", "PATCH 3", 105), cc ("PATCH", "PATCH 4", 106), cc ("PATCH", "PATCH 5", 107), cc ("PATCH", "PATCH 6", 108),
            cc ("VOICE", "PORTAMENTO", 5), cc ("VOICE", "PORTA MODE", 65), cc ("VOICE", "UNISON DETUNE", 33), cc ("VOICE", "UNISON SPREAD", 34),
            cc ("VOICE", "TRANSPOSE", 35), cc ("VOICE", "FINE TUNE", 36),
            cc ("MOD FX", "SPEED", 12), cc ("MOD FX", "DEPTH", 111), cc ("MOD FX", "OUTPUT", 112),
            cc ("DELAY", "TIME", 13), cc ("DELAY", "DEPTH", 113), cc ("DELAY", "LOW CUT", 114), cc ("DELAY", "CC 115", 115),
            cc ("REVERB", "TIME", 14), cc ("REVERB", "DEPTH", 116), cc ("REVERB", "DAMP", 117), cc ("REVERB", "CC 118", 118),
            cc ("EQ", "LOW FREQ", 95), cc ("EQ", "LOW GAIN", 110), cc ("EQ", "HIGH FREQ", 94), cc ("EQ", "HIGH GAIN", 109), cc ("EQ", "CC 93", 93),
        };
        return m;
    }

    // the NTS-1 mkII: its MIDI Implementation (most are 14-bit pairs; the TYPEs are switches)
    std::vector<GearParam> nts1Core()
    {
        return {
            cc ("OSC", "TYPE", 53, 7), cc14 ("OSC", "A", 54, 102), cc14 ("OSC", "B", 55, 103),
            cc14 ("OSC", "LFO RATE", 24, 56), cc14 ("OSC", "LFO DEPTH", 26, 58),
            cc ("FILTER", "TYPE", 42, 7), cc14 ("FILTER", "CUTOFF", 43, 107), cc14 ("FILTER", "RESONANCE", 44, 108),
            cc14 ("FILTER", "SWEEP DEPTH", 45, 109), cc14 ("FILTER", "SWEEP RATE", 46, 110),
            cc ("EG", "TYPE", 14, 5), cc14 ("EG", "ATTACK", 16, 48), cc14 ("EG", "RELEASE", 19, 49),
            cc14 ("TREMOLO", "DEPTH", 20, 50), cc14 ("TREMOLO", "RATE", 21, 51),
            cc ("MOD FX", "TYPE", 88, 9), cc14 ("MOD FX", "A", 28, 60), cc14 ("MOD FX", "B", 29, 61),
            cc ("DELAY", "TYPE", 89, 13), cc14 ("DELAY", "A", 30, 62), cc14 ("DELAY", "B", 31, 63), cc14 ("DELAY", "MIX", 33, 113),
            cc ("REVERB", "TYPE", 90, 11), cc14 ("REVERB", "A", 34, 114), cc14 ("REVERB", "B", 35, 115), cc14 ("REVERB", "MIX", 36, 116),
            cc ("ARP", "PATTERN", 117, 10), cc ("ARP", "INTERVALS", 118, 6), cc ("ARP", "LENGTH", 119),
            cc14 ("MASTER", "VOLUME", 7, 39),
        };
    }

    GearMap nts1()
    {
        GearMap m;
        m.name = "NTS-1 MKII";
        m.note = "KORG'S CCS - SET ITS MIDI RX SHORT MSG ON";
        m.params = nts1Core();
        return m;
    }

    // the NTS-1 mkII with ROLLERS (your oscillator) loaded: OSC A / B are its first two controls (BLEND, DETUNE);
    // its other eight are the oscillator's edit parameters 1 - 8, which the NTS-1 takes as NRPN 0:0 - 0:7
    GearMap nts1Rollers()
    {
        GearMap m;
        m.name = "NTS-1 MKII + ROLLERS";
        m.note = "ROLLERS BY NAME - SET ITS MIDI NRPN ON";
        m.params = nts1Core();
        for (auto& p : m.params)
            if (p.group == "OSC" && (p.name == "A" || p.name == "B")) { p.group = "ROLLERS"; p.name = p.name == "A" ? "BLEND" : "DETUNE"; }
        const std::vector<GearParam> rollers {
            nrpn ("ROLLERS", "SUB SHAPE", 0, 0, 3), nrpn ("ROLLERS", "PITCH DROP", 0, 1), nrpn ("ROLLERS", "DROP TIME", 0, 2),
            nrpn ("ROLLERS 2", "VOICES", 0, 3, 3), nrpn ("ROLLERS 2", "DRIFT RATE", 0, 4), nrpn ("ROLLERS 2", "DRIVE", 0, 5),
            nrpn ("ROLLERS 2", "CRUNCH", 0, 6), nrpn ("ROLLERS 2", "MODE", 0, 7, 4) };
        // (ROLLERS first: BLEND, DETUNE, then the rest)
        std::vector<GearParam> out;
        for (const auto& p : m.params) if (p.group == "ROLLERS") out.push_back (p);
        out.insert (out.end(), rollers.begin(), rollers.end());
        for (const auto& p : m.params) if (p.group != "ROLLERS") out.push_back (p);
        m.params = std::move (out);
        return m;
    }

    const std::vector<GearMap>& maps()
    {
        static const std::vector<GearMap> all { microKorg2(), nts1(), nts1Rollers() };
        return all;
    }
}

std::string GearParam::where() const
{
    if (how == GearSend::Cc14) return "CC " + std::to_string (num) + "+" + std::to_string (lsb);
    if (how == GearSend::Nrpn) return "NRPN " + std::to_string (num) + ":" + std::to_string (lsb);
    return "CC " + std::to_string (num);
}

std::vector<std::string> GearMap::groups() const
{
    std::vector<std::string> g;
    for (const auto& p : params) if (std::find (g.begin(), g.end(), p.group) == g.end()) g.push_back (p.group);
    return g;
}

int gearCount() { return (int) maps().size(); }
const GearMap* gearMap (int id) { return id >= 1 && id <= gearCount() ? &maps()[(size_t) id - 1] : nullptr; }
std::string gearName (int id) { const auto* m = gearMap (id); return m != nullptr ? m->name : std::string ("NONE"); }

int gearStepValue (int i, int n)
{
    if (n < 2) return 0;
    i = std::clamp (i, 0, n - 1);
    return i == n - 1 ? 127 : i * (128 / n);
}

int gearMessages (const GearParam& p, float v, int ch, uint8_t out[4][3])
{
    v = std::clamp (v, 0.0f, 1.0f);
    const uint8_t st = (uint8_t) (0xB0 | (ch & 15));
    auto put = [&] (int k, int a, int b) { out[k][0] = st; out[k][1] = (uint8_t) (a & 127); out[k][2] = (uint8_t) (b & 127); };
    const int step = p.steps >= 2 ? (int) std::lround (v * (p.steps - 1)) : -1;
    switch (p.how)
    {
        case GearSend::Cc:
            put (0, p.num, step >= 0 ? gearStepValue (step, p.steps) : (int) std::lround (v * 127.0f));
            return 1;
        case GearSend::Cc14:
        {
            const int x = (int) std::lround (v * 16383.0f);
            put (0, p.num, x >> 7); put (1, p.lsb, x & 127);
            return 2;
        }
        case GearSend::Nrpn:
        {
            const int x = step >= 0 ? (int) std::lround (step * 16383.0 / (p.steps - 1)) : (int) std::lround (v * 16383.0f);
            // the parameter (99 MSB, 98 LSB), then the value: LSB (38) first, then MSB (6) - as the NTS-1 asks
            put (0, 99, p.num); put (1, 98, p.lsb); put (2, 38, x & 127); put (3, 6, x >> 7);
            return 4;
        }
    }
    return 0;
}
} // namespace prv
