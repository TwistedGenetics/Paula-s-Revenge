// Paula's Revenge - GEAR maps (see GearMaps.h)
#include "GearMaps.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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

    // the Dreadbox Typhon: its manual's CC list (firmware 3, Dec 2020 - the numbers moved from firmware 1's)
    GearMap typhon()
    {
        GearMap m;
        m.name = "DREADBOX TYPHON";
        m.note = "DREADBOX'S CCS (FIRMWARE 3) - MIDI CC: ENABLED";
        m.params = {
            cc ("OSC", "WAVE", 5), cc ("OSC", "OSC 2 TUNE", 6), cc ("OSC", "GLIDE", 7), cc ("OSC", "VCO LEVEL", 8),
            cc ("FILTER", "CUTOFF", 4), cc ("FILTER", "RESONANCE", 3), cc ("FILTER", "TRACKING", 9), cc ("FILTER", "FM AMOUNT", 11),
            cc ("FILTER", "HP CUTOFF", 12), cc ("FILTER", "HP RESONANCE", 13),
            cc ("VCF EG", "ATTACK", 27), cc ("VCF EG", "DECAY", 28), cc ("VCF EG", "SUSTAIN", 29), cc ("VCF EG", "RELEASE", 30),
            cc ("VCF EG", "TIME", 31), cc ("VCF EG", "LEVEL", 33),
            cc ("VCA EG", "ATTACK", 34), cc ("VCA EG", "DECAY", 35), cc ("VCA EG", "SUSTAIN", 36), cc ("VCA EG", "RELEASE", 37), cc ("VCA EG", "TIME", 38),
            cc ("FX 1", "PARAM 1", 14), cc ("FX 1", "PARAM 2", 15), cc ("FX 1", "MIX", 16),
            cc ("FX 2", "PARAM 1", 17), cc ("FX 2", "PARAM 2", 18), cc ("FX 2", "PARAM 3", 19), cc ("FX 2", "PARAM 4", 20), cc ("FX 2", "MIX", 21),
            cc ("FX 3", "PARAM 1", 22), cc ("FX 3", "PARAM 2", 23), cc ("FX 3", "PARAM 3", 24), cc ("FX 3", "PARAM 4", 25), cc ("FX 3", "MIX", 26),
            cc ("SEQ", "LENGTH", 89), cc ("SEQ", "DIVISION", 90), cc ("SEQ", "PROBABILITY", 91), cc ("SEQ", "SWING", 92),
        };
        // the three modulators: their own controls, where they go, their custom slots
        const char* mn[] = { "MOD 1", "MOD 2", "MOD 3" };
        const char* to[] = { "MOD 1 TO", "MOD 2 TO", "MOD 3 TO" };
        const char* cu[] = { "MOD 1 CUSTOM", "MOD 2 CUSTOM", "MOD 3 CUSTOM" };
        const int base[] = { 40, 56, 73 };
        for (int k = 0; k < 3; ++k)
        {
            int c = base[k];
            auto next = [&] { const int n = c++; return n == 64 ? c++ : n; };   // (64 is the sustain pedal)
            for (const char* n : { "PARAM 1", "PARAM 2", "PARAM 3", "CV", "CV 1", "CV 2" }) m.params.push_back (cc (mn[k], n, next()));
            for (const char* n : { "WAVE", "CUTOFF", "VCA", "FX 1", "FFM", "FX 2", "FX 3" }) m.params.push_back (cc (to[k], n, next()));
            for (const char* n : { "CUSTOM 1", "CUSTOM 2", "CUSTOM 3" }) m.params.push_back (cc (cu[k], n, next()));
        }
        return m;
    }

    // the Korg M1 / M1R: Korg's PARAMETER CHANGE (F0 42 3g 19 41 page position value-lo value-hi F7), pages and
    // positions from its Table 5. A SINGLE program and a DOUBLE one number their pages differently.
    GearParam m1 (const char* g, const char* n, int page, int pos, int lo, int hi)
    {
        GearParam p; p.group = g; p.name = n; p.how = GearSend::KorgM1; p.num = page; p.lsb = pos; p.lo = lo; p.hi = hi; return p;
    }
    GearMap korgM1 (bool dbl)
    {
        GearMap m;
        m.name = dbl ? "KORG M1 / M1R (DOUBLE)" : "KORG M1 / M1R (SINGLE)";
        m.note = "KORG'S PARAMETER CHANGE - M1 IN EDIT PROGRAM";
        struct Row { const char* g; const char* n; int sg, db, po, lo, hi; };
        const int X = -1;   // (no such page in a SINGLE program)
        const Row rows[] = {
            { "OSC 1", "MULTISOUND", 1, 1, 8, 0, 99 }, { "OSC 1", "LEVEL", 1, 1, 9, 0, 99 }, { "OSC 1", "OCTAVE", 1, 1, 10, -1, 1 },
            { "OSC 2", "MULTISOUND", X, 2, 8, 0, 99 }, { "OSC 2", "LEVEL", X, 2, 9, 0, 99 }, { "OSC 2", "OCTAVE", X, 2, 10, -1, 1 },
            { "OSC 2", "INTERVAL", X, 2, 11, -12, 12 }, { "OSC 2", "DETUNE", X, 2, 12, -50, 50 }, { "OSC 2", "DELAY START", X, 2, 13, 0, 99 },
            { "PITCH EG 1", "START LEVEL", 2, 3, 8, -99, 99 }, { "PITCH EG 1", "ATTACK TIME", 2, 3, 9, 0, 99 }, { "PITCH EG 1", "ATTACK LEVEL", 2, 3, 10, -99, 99 },
            { "PITCH EG 1", "DECAY TIME", 2, 3, 11, 0, 99 }, { "PITCH EG 1", "RELEASE TIME", 2, 3, 12, 0, 99 }, { "PITCH EG 1", "RELEASE LEVEL", 2, 3, 13, -99, 99 },
            { "PITCH EG 2", "START LEVEL", X, 4, 8, -99, 99 }, { "PITCH EG 2", "ATTACK TIME", X, 4, 9, 0, 99 }, { "PITCH EG 2", "ATTACK LEVEL", X, 4, 10, -99, 99 },
            { "PITCH EG 2", "DECAY TIME", X, 4, 11, 0, 99 }, { "PITCH EG 2", "RELEASE TIME", X, 4, 12, 0, 99 }, { "PITCH EG 2", "RELEASE LEVEL", X, 4, 13, -99, 99 },
            { "VDF 1", "CUTOFF", 3, 5, 8, 0, 99 }, { "VDF 1", "EG INTENSITY", 3, 5, 9, 0, 99 }, { "VDF 1", "EG INT VEL SENS", 5, 7, 8, -99, 99 },
            { "VDF 1", "EG TIME VEL SENS", 5, 7, 9, 0, 99 }, { "VDF 1", "KEY TRACK", 6, 8, 9, -99, 99 }, { "VDF 1", "EG TIME KEY TRACK", 6, 8, 10, 0, 99 },
            { "VDF 1 EG", "ATTACK", 4, 6, 8, 0, 99 }, { "VDF 1 EG", "ATTACK LEVEL", 4, 6, 9, -99, 99 }, { "VDF 1 EG", "DECAY", 4, 6, 10, 0, 99 },
            { "VDF 1 EG", "BREAK POINT", 4, 6, 11, -99, 99 }, { "VDF 1 EG", "SLOPE", 4, 6, 12, 0, 99 }, { "VDF 1 EG", "SUSTAIN", 4, 6, 13, -99, 99 },
            { "VDF 1 EG", "RELEASE", 4, 6, 14, 0, 99 },
            { "VDF 2", "CUTOFF", X, 9, 8, 0, 99 }, { "VDF 2", "EG INTENSITY", X, 9, 9, 0, 99 }, { "VDF 2", "EG INT VEL SENS", X, 11, 8, -99, 99 },
            { "VDF 2", "EG TIME VEL SENS", X, 11, 9, 0, 99 }, { "VDF 2", "KEY TRACK", X, 12, 9, -99, 99 }, { "VDF 2", "EG TIME KEY TRACK", X, 12, 10, 0, 99 },
            { "VDF 2 EG", "ATTACK", X, 10, 8, 0, 99 }, { "VDF 2 EG", "ATTACK LEVEL", X, 10, 9, -99, 99 }, { "VDF 2 EG", "DECAY", X, 10, 10, 0, 99 },
            { "VDF 2 EG", "BREAK POINT", X, 10, 11, -99, 99 }, { "VDF 2 EG", "SLOPE", X, 10, 12, 0, 99 }, { "VDF 2 EG", "SUSTAIN", X, 10, 13, -99, 99 },
            { "VDF 2 EG", "RELEASE", X, 10, 14, 0, 99 },
            { "VDA 1 EG", "ATTACK", 7, 13, 8, 0, 99 }, { "VDA 1 EG", "ATTACK LEVEL", 7, 13, 9, 0, 99 }, { "VDA 1 EG", "DECAY", 7, 13, 10, 0, 99 },
            { "VDA 1 EG", "BREAK POINT", 7, 13, 11, 0, 99 }, { "VDA 1 EG", "SLOPE", 7, 13, 12, 0, 99 }, { "VDA 1 EG", "SUSTAIN", 7, 13, 13, 0, 99 },
            { "VDA 1 EG", "RELEASE", 7, 13, 14, 0, 99 },
            { "VDA 1", "VELOCITY SENS", 8, 14, 8, -99, 99 }, { "VDA 1", "EG TIME VEL SENS", 8, 14, 9, 0, 99 }, { "VDA 1", "KEY TRACK", 9, 15, 9, -99, 99 },
            { "VDA 1", "EG TIME KEY TRACK", 9, 15, 10, 0, 99 },
            { "VDA 2 EG", "ATTACK", X, 16, 8, 0, 99 }, { "VDA 2 EG", "ATTACK LEVEL", X, 16, 9, 0, 99 }, { "VDA 2 EG", "DECAY", X, 16, 10, 0, 99 },
            { "VDA 2 EG", "BREAK POINT", X, 16, 11, 0, 99 }, { "VDA 2 EG", "SLOPE", X, 16, 12, 0, 99 }, { "VDA 2 EG", "SUSTAIN", X, 16, 13, 0, 99 },
            { "VDA 2 EG", "RELEASE", X, 16, 14, 0, 99 },
            { "VDA 2", "VELOCITY SENS", X, 17, 8, -99, 99 }, { "VDA 2", "EG TIME VEL SENS", X, 17, 9, 0, 99 }, { "VDA 2", "KEY TRACK", X, 18, 9, -99, 99 },
            { "VDA 2", "EG TIME KEY TRACK", X, 18, 10, 0, 99 },
            { "PITCH MG", "WAVE", 10, 19, 8, 0, 3 }, { "PITCH MG", "FREQUENCY", 10, 19, 9, 0, 99 }, { "PITCH MG", "DELAY", 10, 19, 10, 0, 99 },
            { "PITCH MG", "INTENSITY", 10, 19, 11, 0, 99 },
            { "VDF MG", "WAVE", 11, 20, 8, 0, 3 }, { "VDF MG", "FREQUENCY", 11, 20, 9, 0, 99 }, { "VDF MG", "DELAY", 11, 20, 10, 0, 99 },
            { "VDF MG", "INTENSITY", 11, 20, 11, 0, 99 },
            { "AFTERTOUCH", "PITCH", 12, 21, 8, -12, 12 }, { "AFTERTOUCH", "PITCH MG", 12, 21, 9, 0, 99 }, { "AFTERTOUCH", "VDF CUTOFF", 12, 21, 10, -99, 99 },
            { "AFTERTOUCH", "VDF MG", 12, 21, 11, 0, 99 }, { "AFTERTOUCH", "VDA AMP", 12, 21, 12, -99, 99 },
            { "JOYSTICK", "PITCH BEND", 13, 22, 8, -12, 12 }, { "JOYSTICK", "VDF SWEEP", 13, 22, 9, -99, 99 }, { "JOYSTICK", "PITCH MG", 13, 22, 10, 0, 99 },
            { "JOYSTICK", "VDF MG", 13, 22, 12, 0, 99 },
            { "EFFECTS", "EFFECT 1 TYPE", 14, 23, 8, 0, 33 }, { "EFFECTS", "EFFECT 2 TYPE", 16, 25, 8, 0, 33 },
        };
        for (const auto& r : rows)
        {
            const int page = dbl ? r.db : r.sg;
            if (page < 0) continue;
            auto p = m1 (r.g, r.n, page, r.po, r.lo, r.hi);
            if (r.hi - r.lo <= 3 || std::string (r.n) == "OCTAVE") { p.steps = r.hi - r.lo + 1; }
            if (std::string (r.n) == "OCTAVE") p.show = 0;
            m.params.push_back (p);
        }
        return m;
    }

    // the Roland JV-1080: DT1 (F0 41 10 6A 12 address data checksum F7) to the TEMPORARY PATCH - its own address map
    // (patch common at 03 00 00 00, tone n at 03 00 10+2n 00)
    GearParam jv (const char* g, const char* n, int off, int lo, int hi, int show = 0, int tones = 4, int tone = 0, bool nib = false)
    {
        GearParam p; p.group = g; p.name = n; p.how = GearSend::RolandDt1; p.lo = lo; p.hi = hi; p.show = show; p.nibbles = nib;
        const int base = tones == -1 ? 0x03000000 : 0x03001000 + tone * 0x200;   // (tone n: 7-bit digits, 02 00 apart)
        int a = base, carry = off;
        // (add the offset in 7-bit digits)
        int b0 = (a & 0x7F) + (carry & 0x7F), b1 = ((a >> 8) & 0x7F) + (carry >> 7) + (b0 >> 7);
        b0 &= 0x7F;
        int b2 = ((a >> 16) & 0x7F) + (b1 >> 7); b1 &= 0x7F;
        p.address = (a & 0x7F000000) | (b2 << 16) | (b1 << 8) | b0;
        p.tones = tones == -1 ? 0 : tones;
        return p;
    }
    GearMap jv1080()
    {
        GearMap m;
        m.name = "ROLAND JV-1080";
        m.note = "ROLAND'S DT1 - PATCH MODE, UNIT #17, RX SYSEX ON";
        const int C = -1;   // (patch common)
        m.params = {
            jv ("PATCH", "LEVEL", 46, 0, 127, 0, C), jv ("PATCH", "PAN", 47, 0, 127, -64, C), jv ("PATCH", "ANALOG FEEL", 48, 0, 127, 0, C),
            jv ("PATCH", "PORTAMENTO TIME", 57, 0, 127, 0, C), jv ("PATCH", "OCTAVE SHIFT", 65, 0, 6, -3, C),
            jv ("EFX", "TYPE", 12, 0, 39, 1, C), jv ("EFX", "PARAM 1", 13, 0, 127, 0, C), jv ("EFX", "PARAM 2", 14, 0, 127, 0, C),
            jv ("EFX", "PARAM 3", 15, 0, 127, 0, C), jv ("EFX", "PARAM 4", 16, 0, 127, 0, C), jv ("EFX", "MIX SEND", 26, 0, 127, 0, C),
            jv ("CHORUS + REVERB", "CHORUS LEVEL", 33, 0, 127, 0, C), jv ("CHORUS + REVERB", "CHORUS RATE", 34, 0, 127, 0, C),
            jv ("CHORUS + REVERB", "CHORUS DEPTH", 35, 0, 127, 0, C), jv ("CHORUS + REVERB", "REVERB TYPE", 39, 0, 7, 0, C),
            jv ("CHORUS + REVERB", "REVERB LEVEL", 40, 0, 127, 0, C), jv ("CHORUS + REVERB", "REVERB TIME", 41, 0, 127, 0, C),
            jv ("CHORUS + REVERB", "DELAY FEEDBACK", 43, 0, 127, 0, C),
            // every tone at once
            jv ("FILTER", "TYPE", 80, 0, 4), jv ("FILTER", "CUTOFF", 81, 0, 127), jv ("FILTER", "RESONANCE", 83, 0, 127),
            jv ("FILTER", "ENV DEPTH", 85, 0, 126, -63), jv ("FILTER", "KEYFOLLOW", 82, 0, 15), jv ("FILTER", "LFO 1 DEPTH", 99, 0, 126, -63),
            jv ("FILTER ENV", "TIME 1", 91, 0, 127), jv ("FILTER ENV", "TIME 2", 92, 0, 127), jv ("FILTER ENV", "TIME 3", 93, 0, 127),
            jv ("FILTER ENV", "TIME 4", 94, 0, 127), jv ("FILTER ENV", "LEVEL 1", 95, 0, 127), jv ("FILTER ENV", "LEVEL 2", 96, 0, 127),
            jv ("FILTER ENV", "LEVEL 3", 97, 0, 127),
            jv ("AMP ENV", "TIME 1", 110, 0, 127), jv ("AMP ENV", "TIME 2", 111, 0, 127), jv ("AMP ENV", "TIME 3", 112, 0, 127),
            jv ("AMP ENV", "TIME 4", 113, 0, 127), jv ("AMP ENV", "LEVEL 1", 114, 0, 127), jv ("AMP ENV", "LEVEL 2", 115, 0, 127),
            jv ("AMP ENV", "LEVEL 3", 116, 0, 127),
            jv ("PITCH", "COARSE", 61, 0, 96, -48), jv ("PITCH", "FINE", 62, 0, 100, -50), jv ("PITCH", "ENV DEPTH", 65, 0, 24, -12),
            jv ("PITCH", "LFO 1 DEPTH", 78, 0, 126, -63), jv ("PITCH", "LFO 2 DEPTH", 79, 0, 126, -63),
            jv ("LFO", "LFO 1 WAVE", 45, 0, 7), jv ("LFO", "LFO 1 RATE", 47, 0, 127), jv ("LFO", "LFO 2 WAVE", 53, 0, 7), jv ("LFO", "LFO 2 RATE", 55, 0, 127),
            jv ("LFO", "LEVEL LFO 1", 117, 0, 126, -63), jv ("LFO", "PAN LFO 1", 123, 0, 126, -63),
            jv ("LEVEL + PAN", "LEVEL", 101, 0, 127), jv ("LEVEL + PAN", "PAN", 119, 0, 127, -64), jv ("LEVEL + PAN", "MIX / EFX SEND", 126, 0, 127),
            jv ("LEVEL + PAN", "CHORUS SEND", 127, 0, 127), jv ("LEVEL + PAN", "REVERB SEND", 128, 0, 127),
        };
        // each tone on its own
        const char* tn[] = { "TONE 1", "TONE 2", "TONE 3", "TONE 4" };
        for (int t = 0; t < 4; ++t)
        {
            m.params.push_back (jv (tn[t], "ON", 0, 0, 1, 0, 1, t));
            m.params.push_back (jv (tn[t], "WAVE", 3, 0, 254, 1, 1, t, true));
            m.params.push_back (jv (tn[t], "CUTOFF", 81, 0, 127, 0, 1, t));
            m.params.push_back (jv (tn[t], "RESONANCE", 83, 0, 127, 0, 1, t));
            m.params.push_back (jv (tn[t], "LEVEL", 101, 0, 127, 0, 1, t));
            m.params.push_back (jv (tn[t], "PAN", 119, 0, 127, -64, 1, t));
            m.params.push_back (jv (tn[t], "COARSE", 61, 0, 96, -48, 1, t));
        }
        for (auto& p : m.params) if (p.hi - p.lo <= 8 && p.hi - p.lo >= 1) { p.steps = p.hi - p.lo + 1; p.direct = true; }
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
        static const std::vector<GearMap> all = [&]
        {
            std::vector<GearMap> v { microKorg2(), nts1(), nts1Rollers(), artemis(), virusTi(), s1000, s950, typhon(), korgM1 (false), korgM1 (true), jv1080() };
            // the knobs' usual four
            const std::vector<std::vector<std::string>> knobs {
                { "FILTER: CUTOFF", "FILTER: RESONANCE", "FILTER EG: INTENSITY", "AMP EG: RELEASE" },
                { "FILTER: CUTOFF", "FILTER: RESONANCE", "OSC: A", "OSC: B" },
                { "FILTER: CUTOFF", "ROLLERS: BLEND", "ROLLERS: DETUNE", "ROLLERS: PITCH DROP" },
                { "LPF: CUTOFF", "LPF: RESONANCE", "LPF: EG AMOUNT", "VCA EG: RELEASE" },
                { "FILTER 1: CUTOFF", "FILTER 1: RESONANCE", "FILTER 1: EG AMOUNT", "OSC 1: SHAPE" },
                {}, {},
                { "FILTER: CUTOFF", "FILTER: RESONANCE", "VCF EG: LEVEL", "VCA EG: RELEASE" },
                { "VDF 1: CUTOFF", "VDF 1: EG INTENSITY", "VDA 1 EG: RELEASE", "PITCH MG: INTENSITY" },
                { "VDF 1: CUTOFF", "VDF 2: CUTOFF", "VDA 1 EG: RELEASE", "OSC 2: DETUNE" },
                { "FILTER: CUTOFF", "FILTER: RESONANCE", "FILTER: ENV DEPTH", "LEVEL + PAN: LEVEL" } };
            for (size_t i = 0; i < v.size() && i < knobs.size(); ++i) v[i].knobs = knobs[i];
            return v;
        }();
        return all;
    }
}

std::string GearParam::where() const
{
    char b[32];
    if (how == GearSend::KorgM1) { std::snprintf (b, sizeof b, "PAGE %d:%d", num, lsb); return b; }
    if (how == GearSend::RolandDt1)
    {
        if (tones == 4) std::snprintf (b, sizeof b, "TONES 1-4 %02X", address & 127);   // (each tone's own address: 03 00 1x yy)
        else std::snprintf (b, sizeof b, "%02X %02X %02X %02X", (address >> 24) & 127, (address >> 16) & 127, (address >> 8) & 127, address & 127);
        return b;
    }
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

std::string GearParam::text (float v) const
{
    v = std::clamp (v, 0.0f, 1.0f);
    if (how == GearSend::KorgM1 || how == GearSend::RolandDt1)
    {
        const int raw = lo + (int) std::lround (v * (hi - lo));
        if (steps >= 2 && hi - lo <= 8 && show == 0 && lo == 0) return std::to_string (raw + 1) + "/" + std::to_string (steps);
        const int s = raw + show;
        return (lo + show < 0 && s > 0 ? "+" : "") + std::to_string (s);
    }
    if (steps >= 2) return std::to_string ((int) std::lround (v * (steps - 1)) + 1) + "/" + std::to_string (steps);
    return std::to_string ((int) std::lround (v * 127.0f));
}

bool gearIsSysex (const GearParam& p) { return p.how == GearSend::KorgM1 || p.how == GearSend::RolandDt1; }

std::vector<uint8_t> gearSysex (const GearParam& p, float v, int ch)
{
    std::vector<uint8_t> m;
    v = std::clamp (v, 0.0f, 1.0f);
    const int raw = p.lo + (int) std::lround (v * (p.hi - p.lo));
    if (p.how == GearSend::KorgM1)
    {
        // F0 42 3g 19 41 page position value (bits 6-0) value (bits 13-7) F7 - negatives as two's complement
        const int x = raw & 0x3FFF;
        m = { 0xF0, 0x42, (uint8_t) (0x30 | (ch & 15)), 0x19, 0x41, (uint8_t) (p.num & 127), (uint8_t) (p.lsb & 127),
              (uint8_t) (x & 127), (uint8_t) ((x >> 7) & 127), 0xF7 };
        return m;
    }
    if (p.how == GearSend::RolandDt1)
    {
        // F0 41 10 6A 12 address (4) data checksum F7, to each tone it covers (0x200 apart)
        const int count = p.tones == 4 ? 4 : 1;
        for (int t = 0; t < count; ++t)
        {
            int a = p.address;
            if (t > 0)
            {
                int b1 = ((a >> 8) & 127) + t * 2;   // (02 00 per tone)
                a = (a & 0x7F7F007F) | ((b1 & 127) << 8) | (((((a >> 16) & 127) + (b1 >> 7)) & 127) << 16);
            }
            std::vector<uint8_t> body { (uint8_t) ((a >> 24) & 127), (uint8_t) ((a >> 16) & 127), (uint8_t) ((a >> 8) & 127), (uint8_t) (a & 127) };
            if (p.nibbles) { body.push_back ((uint8_t) ((raw >> 4) & 15)); body.push_back ((uint8_t) (raw & 15)); }
            else body.push_back ((uint8_t) (raw & 127));
            int sum = 0;
            for (uint8_t x : body) sum += x;
            m.insert (m.end(), { 0xF0, 0x41, 0x10, 0x6A, 0x12 });
            m.insert (m.end(), body.begin(), body.end());
            m.push_back ((uint8_t) ((128 - (sum & 127)) & 127));
            m.push_back (0xF7);
        }
    }
    return m;
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
