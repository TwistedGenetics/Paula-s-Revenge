// Paula's Revenge - GEAR maps (see GearMaps.h)
#include "GearMaps.h"
#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    GearParam cc (const char* g, const char* n, int num, int steps = 0)   { GearParam p; p.group = g; p.name = n; p.how = GearSend::Cc; p.num = num; p.steps = steps; return p; }
    GearParam sw (const char* g, const char* n, int num, int steps)      { GearParam p = cc (g, n, num, steps); p.direct = true; return p; }
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

    // the Dreadbox Artemis: its Product Manual's MIDI CC list (v1.20)
    GearMap artemis()
    {
        GearMap m;
        m.name = "DREADBOX ARTEMIS";
        m.note = "DREADBOX'S CC LIST - SET CC IN ON (MIDI MENU)";
        m.params = {
            cc ("VCO", "MIX", 8), cc ("VCO", "MORPH", 70), cc ("VCO", "SUB / NOISE", 76), cc ("VCO", "VCO 1 TUNE", 77), cc ("VCO", "VCO 2 TUNE", 78), cc ("VCO", "DETUNE", 79),
            cc ("VCO MOD", "FM", 84), cc ("VCO MOD", "FM MODE", 80), cc ("VCO MOD", "VCO 2 SYNC", 81), cc ("VCO MOD", "PULSE WIDTH", 85), cc ("VCO MOD", "GLIDE", 5), cc ("VCO MOD", "SPREAD", 10),
            cc ("LPF", "CUTOFF", 75), cc ("LPF", "RESONANCE", 71), cc ("LPF", "EG AMOUNT", 6), cc ("LPF", "FFM", 4), cc ("LPF", "FFM NOISE SRC", 3), cc ("LPF", "TRACK", 82), cc ("LPF", "POLES", 83),
            cc ("HPF", "CUTOFF", 16), cc ("HPF", "RESONANCE", 17),
            cc ("EG", "ATTACK", 12), cc ("EG", "DECAY", 13), cc ("EG", "SUSTAIN", 14), cc ("EG", "RELEASE", 15),
            cc ("VCA EG", "ATTACK", 73), cc ("VCA EG", "DECAY", 9), cc ("VCA EG", "SUSTAIN", 11), cc ("VCA EG", "RELEASE", 72),
            cc ("LFO 1", "RATE", 23), cc ("LFO 1", "WAVE", 24), cc ("LFO 1", "FADE", 25), cc ("LFO 1", "VCO TARGET", 18), cc ("LFO 1", "VCO AMOUNT", 19), cc ("LFO 1", "VCF AMOUNT", 20),
            cc ("LFO 2", "RATE", 26), cc ("LFO 2", "WAVE", 27), cc ("LFO 2", "MORPH AMOUNT", 21), cc ("LFO 2", "PW AMOUNT", 22), cc ("LFO 2", "XMOD", 28), cc ("LFO 2", "SYNC MODE", 29),
            cc ("DISTORTION", "TYPE", 38), cc ("DISTORTION", "PARAM 1", 33), cc ("DISTORTION", "PARAM 2", 34), cc ("DISTORTION", "PARAM 3", 35), cc ("DISTORTION", "PARAM 4", 36), cc ("DISTORTION", "PARAM 5", 37),
            cc ("MOD FX", "TYPE", 44), cc ("MOD FX", "PARAM 1", 39), cc ("MOD FX", "PARAM 2", 40), cc ("MOD FX", "PARAM 3", 41), cc ("MOD FX", "PARAM 4", 42), cc ("MOD FX", "PARAM 5", 43),
            cc ("DELAY", "TYPE", 50), cc ("DELAY", "PARAM 1", 45), cc ("DELAY", "PARAM 2", 46), cc ("DELAY", "PARAM 3", 47), cc ("DELAY", "PARAM 4", 48), cc ("DELAY", "PARAM 5", 49),
            cc ("REVERB", "TYPE", 56), cc ("REVERB", "PARAM 1", 51), cc ("REVERB", "PARAM 2", 52), cc ("REVERB", "PARAM 3", 53), cc ("REVERB", "PARAM 4", 54), cc ("REVERB", "PARAM 5", 55),
            cc ("SEQ / ARP", "PARAM TYPE", 91), cc ("SEQ / ARP", "PARAM 1", 86), cc ("SEQ / ARP", "PARAM 2", 87), cc ("SEQ / ARP", "PARAM 3", 88), cc ("SEQ / ARP", "PARAM 4", 89), cc ("SEQ / ARP", "PARAM 5", 90),
            cc ("VOICE", "PLAY MODE", 30), cc ("VOICE", "GLIDE MODE", 65), cc ("VOICE", "LEGATO", 68), cc ("VOICE", "AMP VELOCITY", 95), cc ("VOICE", "INSTABILITY", 96), cc ("VOICE", "CC74 MOD SOURCE", 74),
            cc ("MASTER", "VOLUME", 7), cc ("MASTER", "PRESET LEVEL", 94), cc ("MASTER", "DRIVE MODE", 93), cc ("MASTER", "BPM", 92),
        };
        return m;
    }

    // the Access Virus TI: its PAGE A parameters as controllers (the Virus's MIDI CONTROL: PAGE A = CONTROLLER DATA).
    // The switches (modes, waves, shapes) take their position as the value.
    GearMap virusTi()
    {
        GearMap m;
        m.name = "ACCESS VIRUS TI";
        m.note = "PAGE A - SET ITS PAGE A TO CONTROLLER DATA";
        m.params = {
            cc ("OSC 1", "SHAPE", 17), cc ("OSC 1", "PULSEWIDTH", 18), sw ("OSC 1", "WAVE", 19, 100), cc ("OSC 1", "SEMITONE", 20), cc ("OSC 1", "KEY FOLLOW", 21), cc ("OSC 1", "LFO 1 AMOUNT", 74),
            cc ("OSC 2", "SHAPE", 22), cc ("OSC 2", "PULSEWIDTH", 23), sw ("OSC 2", "WAVE", 24, 100), cc ("OSC 2", "SEMITONE", 25), cc ("OSC 2", "DETUNE", 26), cc ("OSC 2", "KEY FOLLOW", 31),
            cc ("OSC 2 MOD", "FM AMOUNT", 27), sw ("OSC 2 MOD", "SYNC", 28, 2), cc ("OSC 2 MOD", "FILTER EG AMOUNT", 29), cc ("OSC 2 MOD", "LFO 1 AMOUNT", 75),
            cc ("MIX", "OSC BALANCE", 33), cc ("MIX", "SUB VOLUME", 34), cc ("MIX", "SUB SHAPE", 35), cc ("MIX", "OSC VOLUME", 36), cc ("MIX", "NOISE VOLUME", 37), cc ("MIX", "RING MOD", 38), cc ("MIX", "NOISE COLOUR", 39),
            cc ("FILTER 1", "CUTOFF", 40), cc ("FILTER 1", "RESONANCE", 42), cc ("FILTER 1", "EG AMOUNT", 44), cc ("FILTER 1", "KEY FOLLOW", 46), sw ("FILTER 1", "MODE", 51, 8),
            cc ("FILTER 2", "CUTOFF", 41), cc ("FILTER 2", "RESONANCE", 43), cc ("FILTER 2", "EG AMOUNT", 45), cc ("FILTER 2", "KEY FOLLOW", 47), sw ("FILTER 2", "MODE", 52, 8),
            cc ("FILTERS", "BALANCE", 48), sw ("FILTERS", "ROUTING", 53, 4), cc ("FILTERS", "SATURATION", 49), cc ("FILTERS", "FM EG AMOUNT", 30),
            cc ("FILTER EG", "ATTACK", 54), cc ("FILTER EG", "DECAY", 55), cc ("FILTER EG", "SUSTAIN", 56), cc ("FILTER EG", "SUSTAIN TIME", 57), cc ("FILTER EG", "RELEASE", 58),
            cc ("AMP EG", "ATTACK", 59), cc ("AMP EG", "DECAY", 60), cc ("AMP EG", "SUSTAIN", 61), cc ("AMP EG", "SUSTAIN TIME", 62), cc ("AMP EG", "RELEASE", 63),
            cc ("LFO 1", "RATE", 67), sw ("LFO 1", "SHAPE", 68, 68), sw ("LFO 1", "ENV MODE", 69, 2), sw ("LFO 1", "MODE", 70, 2), cc ("LFO 1", "SYMMETRY", 71), cc ("LFO 1", "KEY FOLLOW", 72), cc ("LFO 1", "RESTART", 73),
            cc ("LFO 1 TO", "PULSEWIDTH", 76), cc ("LFO 1 TO", "RESONANCE", 77), cc ("LFO 1 TO", "FILTER GAIN", 78),
            cc ("LFO 2", "RATE", 79), sw ("LFO 2", "SHAPE", 80, 68), sw ("LFO 2", "ENV MODE", 81, 2), sw ("LFO 2", "MODE", 82, 2), cc ("LFO 2", "SYMMETRY", 83), cc ("LFO 2", "KEY FOLLOW", 84), cc ("LFO 2", "RESTART", 85),
            cc ("LFO 2 TO", "OSC SHAPE", 86), cc ("LFO 2 TO", "FM", 87), cc ("LFO 2 TO", "FILTER 1", 88), cc ("LFO 2 TO", "FILTER 2", 89), cc ("LFO 2 TO", "PAN", 90),
            cc ("VOICE", "GLIDE TIME", 5), cc ("VOICE", "TRANSPOSE", 93), cc ("VOICE", "KEY MODE", 94), cc ("VOICE", "UNISON MODE", 97), cc ("VOICE", "UNISON DETUNE", 98), cc ("VOICE", "UNISON SPREAD", 99), cc ("VOICE", "UNISON LFO PHASE", 100),
            sw ("CHORUS", "TYPE", 103, 7), cc ("CHORUS", "MIX", 104), cc ("CHORUS", "RATE", 106), cc ("CHORUS", "DEPTH", 107), cc ("CHORUS", "DELAY", 108), cc ("CHORUS", "FEEDBACK", 109), sw ("CHORUS", "LFO SHAPE", 110, 6),
            cc ("DELAY", "SEND", 113), cc ("DELAY", "MODE", 112), cc ("DELAY", "TIME", 114), cc ("DELAY", "FEEDBACK", 115), cc ("DELAY", "COLOUR", 119), cc ("DELAY", "LFO SHAPE", 118),
            cc ("REVERB", "TIME", 116), cc ("REVERB", "SIZE", 117),
            cc ("MASTER", "VOLUME", 91), cc ("MASTER", "PAN", 10), cc ("MASTER", "BALANCE", 8),
        };
        return m;
    }

    const std::vector<GearMap>& maps()
    {
        GearMap s1000;
        s1000.name = "AKAI S1000";
        s1000.note = "SYSEX EDITOR - S1000 EXCLUSIVE CH = CHANNEL - 1";   // (no controls by CC: S1000 EDITOR... instead)
        GearMap s950;
        s950.name = "AKAI S950";
        s950.note = "SYSEX EDITOR - S950 MIDI CHANNEL = CHANNEL";   // (the S900's protocol: S950 EDITOR...)
        static const std::vector<GearMap> all { microKorg2(), nts1(), nts1Rollers(), artemis(), virusTi(), s1000, s950 };
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
bool gearHasEditor (int id) { return id == kGearAkaiS1000 || id == kGearAkaiS950; }
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
            put (0, p.num, step >= 0 ? (p.direct ? step : gearStepValue (step, p.steps)) : (int) std::lround (v * 127.0f));
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
