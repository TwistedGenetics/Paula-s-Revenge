// Paula's Revenge - TG 101 (see Tg101Synth.h). Our own model of an SH-101-style monosynth, tuned by ear
#include "Tg101Synth.h"
#include "BuiltinSynths.h"
#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    constexpr double kTgPi = 3.141592653589793;
    inline float blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }
    inline float sawAt (double t, double dt) { return (float) (2.0 * t - 1.0) - blep (t, dt); }
    inline double frac (double x) { return x - std::floor (x); }
    // a pulse of duty `w` (its high part first), band-limited
    inline float pulseAt (double t, double dt, double w) { return 0.5f * (sawAt (t, dt) - sawAt (frac (t + 1.0 - w), dt)); }
    // (a time constant: the per-sample factor that falls to 10% in `ms`)
    inline float fallIn (float ms, double sr) { return (float) std::exp (-2.302585 / std::max (1.0, ms * 0.001 * sr)); }
    const double kStepsPerBeat[6] = { 1, 2, 4, 8, 3, 6 };          // ARP RATE: 1/4 1/8 1/16 1/32 1/8T 1/16T
    const double kLfoBeats[6] = { 0, 4, 2, 1, 0.5, 0.25 };          // LFO SYNC: FREE, 1 BAR, 1/2, 1/4, 1/8, 1/16 (beats a cycle)
}

const std::vector<SynthParam>& tg101Params()
{
    static const std::vector<SynthParam> ps {
        // VCO + MIXER
        { "RANGE",     "0:VCO + MIXER|VCO>VCO", 0, 3, 1,        3, "",   "16'|8'|4'|2'" },
        { "TUNE",      "0:VCO + MIXER|VCO>VCO", -12, 12, 0,     2, "ST", "" },
        { "FINE",      "0:VCO + MIXER|VCO>VCO", -50, 50, 0,     0, "C",  "" },
        { "VIBRATO",   "0:VCO + MIXER|VCO>VCO", 0, 100, 0,      0, "%",  "" },      // (the LFO into the pitch: vibrato)
        { "PW",        "0:VCO + MIXER|VCO>VCO", 0, 100, 0,      0, "%",  "" },      // (MAN: the width; LFO / ENV: how far they move it)
        { "PWM",       "0:VCO + MIXER|VCO>VCO", 0, 2, 1,        3, "",   "LFO|MAN|ENV" },
        { "PULSE",     "0:VCO + MIXER|VCO>SOURCE MIXER", 0, 100, 0,   0, "%", "" },
        { "SAW",       "0:VCO + MIXER|VCO>SOURCE MIXER", 0, 100, 100, 0, "%", "" },
        { "SUB",       "0:VCO + MIXER|VCO>SOURCE MIXER", 0, 100, 0,   0, "%", "" },
        { "SUB TYPE",  "0:VCO + MIXER|VCO>SOURCE MIXER", 0, 2, 0,     3, "",  "-1 OCT|-2 OCT|-2 PULSE" },
        { "NOISE",     "0:VCO + MIXER|VCO>SOURCE MIXER", 0, 100, 0,   0, "%", "" },
        // VCF + ENV
        { "FREQ",      "1:VCF + ENV|VCF>VCF", 20, 18000, 2400,  1, "HZ", "" },
        { "RES",       "1:VCF + ENV|VCF>VCF", 0, 100, 20,       0, "%",  "" },
        { "ENV",       "1:VCF + ENV|VCF>VCF", 0, 100, 35,       0, "%",  "" },
        { "LFO",       "1:VCF + ENV|VCF>VCF", 0, 100, 0,        0, "%",  "" },      // (the LFO into the cutoff)
        { "KYBD",      "1:VCF + ENV|VCF>VCF", 0, 100, 50,       0, "%",  "" },
        { "BENDER",    "1:VCF + ENV|VCF>VCF", 0, 100, 0,        0, "%",  "" },      // (the bender opens the filter too)
        { "ATTACK",    "1:VCF + ENV|VCF>ENV", 1.5f, 4000, 2,    1, "MS", "" },
        { "DECAY",     "1:VCF + ENV|VCF>ENV", 2, 10000, 400,    1, "MS", "" },
        { "SUSTAIN",   "1:VCF + ENV|VCF>ENV", 0, 100, 40,       0, "%",  "" },
        { "RELEASE",   "1:VCF + ENV|VCF>ENV", 2, 10000, 60,     1, "MS", "" },
        { "TRIGGER",   "1:VCF + ENV|VCF>ENV", 0, 2, 0,          3, "",   "GATE+TRIG|GATE|LFO" },
        { "VCA",       "1:VCF + ENV|VCF>ENV", 0, 1, 0,          3, "",   "ENV|GATE" },
        // LFO + PORTAMENTO + OUTPUT
        { "RATE",      "2:LFO + PORTA|LFO>LFO", 0.1f, 30, 4,    1, "HZ", "" },
        { "WAVE",      "2:LFO + PORTA|LFO>LFO", 0, 3, 0,        3, "",   "TRI|SQUARE|RANDOM|NOISE" },
        { "SYNC",      "2:LFO + PORTA|LFO>LFO", 0, 5, 0,        3, "",   "FREE|1 BAR|1/2|1/4|1/8|1/16" },
        { "GLIDE",     "2:LFO + PORTA|LFO>PORTAMENTO", 0, 2, 0, 3, "",   "OFF|ON|AUTO" },
        { "TIME",      "2:LFO + PORTA|LFO>PORTAMENTO", 5, 3000, 120, 1, "MS", "" },
        // ARPEGGIO
        { "MODE",      "3:ARPEGGIO|ARP>ARPEGGIO", 0, 3, 0,      3, "",   "OFF|UP|DOWN|UP+DOWN" },
        { "STEP",      "3:ARPEGGIO|ARP>ARPEGGIO", 0, 5, 2,      3, "",   "1/4|1/8|1/16|1/32|1/8T|1/16T" },
        { "OCTAVES",   "3:ARPEGGIO|ARP>ARPEGGIO", 1, 3, 1,      2, "",   "" },
        { "GATE",      "3:ARPEGGIO|ARP>ARPEGGIO", 10, 100, 60,  0, "%",  "" },
        { "HOLD",      "3:ARPEGGIO|ARP>ARPEGGIO", 0, 1, 0,      3, "",   "OFF|ON" },
        { "VELOCITY",  "2:LFO + PORTA|LFO>OUTPUT", 0, 100, 0,   0, "%",  "" },
        { "DRIVE",     "2:LFO + PORTA|LFO>OUTPUT", 0, 100, 0,   0, "%",  "" },
        { "LEVEL",     "2:LFO + PORTA|LFO>OUTPUT", 0, 100, 75,  0, "%",  "" },
    };
    return ps;
}

const char* tg101PresetName (int i)
{
    static const char* n[kTg101Presets] = { "TG BASS", "RUBBER SUB BASS", "RAVE STAB", "ACID 101", "BLEEP", "PWM LEAD",
                                            "SQUELCH LEAD", "SQUARE WOBBLE", "ZAP TOM", "NOISE SNARE", "ARP RAVE",
                                            "ORGAN GATE", "RES SWEEP", "MINI HOOVER" };
    return n[std::clamp (i, 0, kTg101Presets - 1)];
}

std::vector<float> tg101Preset (int i)
{
    using namespace tg101;
    std::vector<float> v;
    for (const auto& q : tg101Params()) v.push_back (q.def);   // (TG BASS's)
    auto set = [&] (std::initializer_list<std::pair<int, float>> l) { for (auto& [k, x] : l) v[(size_t) k] = x; };
    switch (std::clamp (i, 0, kTg101Presets - 1))
    {
        case 0: set ({ { Range, 0 }, { SawLvl, 80 }, { SubLvl, 60 }, { Freq, 420 }, { Res, 25 }, { EnvAmt, 45 }, { Decay, 260 }, { Sustain, 20 } }); break;
        case 1: set ({ { Range, 0 }, { SawLvl, 0 }, { PulseLvl, 70 }, { PwmSource, PwmManual }, { Pw, 30 }, { SubLvl, 100 }, { SubType, SubOct1 },
                       { Freq, 260 }, { Res, 10 }, { EnvAmt, 30 }, { Decay, 600 }, { Sustain, 50 }, { PortaMode, PortaAuto }, { PortaTime, 80 } }); break;
        case 2: set ({ { SawLvl, 100 }, { PulseLvl, 60 }, { PwmSource, PwmManual }, { Pw, 40 }, { Freq, 900 }, { Res, 45 }, { EnvAmt, 70 },
                       { Attack, 1.5f }, { Decay, 220 }, { Sustain, 0 }, { Release, 180 } }); break;
        case 3: set ({ { Range, 0 }, { SawLvl, 100 }, { Freq, 220 }, { Res, 85 }, { EnvAmt, 75 }, { Decay, 200 }, { Sustain, 0 }, { Release, 40 },
                       { PortaMode, PortaAuto }, { PortaTime, 60 }, { Kybd, 30 } }); break;
        case 4: set ({ { Range, 2 }, { SawLvl, 0 }, { PulseLvl, 100 }, { PwmSource, PwmManual }, { Pw, 0 }, { Freq, 3000 }, { Res, 30 }, { EnvAmt, 20 },
                       { Decay, 120 }, { Sustain, 0 }, { Release, 80 }, { Level, 95 } }); break;
        case 5: set ({ { Range, 1 }, { SawLvl, 0 }, { PulseLvl, 100 }, { PwmSource, PwmLfo }, { Pw, 70 }, { LfoRate, 3.5f }, { Freq, 2600 }, { Res, 20 },
                       { EnvAmt, 25 }, { Sustain, 80 }, { Release, 300 }, { VcoMod, 12 }, { PortaMode, PortaOn }, { PortaTime, 90 } }); break;
        case 6: set ({ { SawLvl, 100 }, { Freq, 700 }, { Res, 80 }, { EnvAmt, 60 }, { Decay, 500 }, { Sustain, 30 }, { VcfMod, 25 }, { LfoRate, 5 },
                       { Drive, 35 } }); break;
        case 7: set ({ { Range, 0 }, { SawLvl, 90 }, { SubLvl, 70 }, { Freq, 300 }, { Res, 55 }, { EnvAmt, 10 }, { VcfMod, 70 }, { LfoWave, LfoSquare },
                       { LfoSync, 4 }, { Sustain, 100 } }); break;
        case 8: set ({ { Range, 0 }, { SawLvl, 0 }, { PulseLvl, 0 }, { SubLvl, 0 }, { NoiseLvl, 3 }, { Freq, 180 }, { Res, 100 }, { EnvAmt, 90 }, { Kybd, 100 },
                       { Decay, 260 }, { Sustain, 0 }, { Release, 260 }, { Level, 70 } }); break;
        case 9: set ({ { SawLvl, 0 }, { NoiseLvl, 100 }, { SubLvl, 40 }, { SubType, SubOct2 }, { Freq, 3500 }, { Res, 30 }, { EnvAmt, 40 },
                       { Decay, 140 }, { Sustain, 0 }, { Release, 140 } }); break;
        case 10: set ({ { SawLvl, 90 }, { PulseLvl, 50 }, { PwmSource, PwmLfo }, { Pw, 40 }, { Freq, 1200 }, { Res, 50 }, { EnvAmt, 55 },
                        { Decay, 150 }, { Sustain, 0 }, { Release, 60 }, { ArpMode, ArpUp }, { ArpRate, 2 }, { ArpOct, 2 }, { ArpGate, 55 } }); break;
        case 11: set ({ { SawLvl, 0 }, { PulseLvl, 100 }, { PwmSource, PwmManual }, { Pw, 0 }, { SubLvl, 80 }, { SubType, SubOct1 }, { Freq, 4000 },
                        { Res, 0 }, { EnvAmt, 0 }, { VcaMode, VcaGate }, { Level, 55 } }); break;
        case 12: set ({ { SawLvl, 100 }, { Freq, 400 }, { Res, 90 }, { EnvAmt, 85 }, { Attack, 300 }, { Decay, 1500 }, { Sustain, 60 }, { Release, 800 } }); break;
        case 13: set ({ { SawLvl, 100 }, { PulseLvl, 100 }, { PwmSource, PwmLfo }, { Pw, 90 }, { LfoRate, 6 }, { SubLvl, 60 }, { Freq, 1600 }, { Res, 35 },
                        { EnvAmt, 30 }, { Sustain, 90 }, { PortaMode, PortaOn }, { PortaTime, 220 }, { Drive, 50 } }); break;
        default: break;
    }
    return v;
}

Tg101Synth::Tg101Synth()
{
    const auto& ps = tg101Params();
    for (int i = 0; i < tg101::Count; ++i) p[(size_t) i] = ps[(size_t) i].def;
    base = p;
    events.reserve (256);
    held.reserve (32);
    latched.reserve (32);
}

void Tg101Synth::prepare (double sampleRate, int)
{
    outSr = sampleRate > 0 ? sampleRate : 48000.0;
    sr = outSr * kOs;   // (at 4x: the screaming filter and the drive can't fold back)
    dec.reset();
}

void Tg101Synth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }

void Tg101Synth::update (const Instrument& in)
{
    bendRange = std::clamp (in.bendRange, 1, 48);
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
    base = p;
}

void Tg101Synth::setParameter (int i, float v01)
{
    const auto& ps = tg101Params();
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void Tg101Synth::noteOn (int note, int v, int offset) { if (events.size() < 1024) events.push_back ({ offset, v > 0 ? 0 : 1, note, v }); }
void Tg101Synth::noteOff (int note, int offset)       { if (events.size() < 1024) events.push_back ({ offset, 1, note, 0 }); }
void Tg101Synth::allNotesOff (int offset)             { if (events.size() < 1024) events.push_back ({ offset, 2, 0, 0 }); }
void Tg101Synth::pitchBend (int v, int offset)        { if (events.size() < 1024) events.push_back ({ offset, 3, v, 0 }); }

float Tg101Synth::noise()
{
    seed = seed * 1664525u + 1013904223u;
    return (float) ((seed >> 9) & 0x7FFFFF) / 4194304.0f - 1.0f;
}

void Tg101Synth::startEnv()
{
    envStage = 1;   // (from where it is: an analogue envelope doesn't snap to zero)
    ++envStartCount;
}

void Tg101Synth::play (int note, bool legato)
{
    using namespace tg101;
    const int porta = (int) std::lround (p[PortaMode]);
    target = (float) note;
    // PORTAMENTO: ON glides every note; AUTO only from a held note into the next (legato); OFF jumps
    if (porta == PortaOff || (porta == PortaAuto && ! legato) || playing < 0) pitch = target;
    playing = note;
    const int trig = (int) std::lround (p[EnvTrig]);
    if (! legato || trig == TrigGateTrig || ! gateOn) startEnv();
    gateOn = true;
}

void Tg101Synth::stop()
{
    gateOn = false;
    if (envStage != 0) envStage = 4;
}

void Tg101Synth::handle (const Event& e)
{
    using namespace tg101;
    const bool arp = std::lround (p[ArpMode]) != ArpOff;
    const bool hold = std::lround (p[ArpHold]) == 1;
    switch (e.type)
    {
        case 0:
        {
            const bool wasEmpty = held.empty();
            held.erase (std::remove (held.begin(), held.end(), e.note), held.end());
            held.push_back (e.note);
            vel = std::clamp (e.vel, 1, 127) / 127.0f;
            if (hold)
            {
                if (wasEmpty) latched.clear();   // (a fresh chord replaces the one held)
                if (std::find (latched.begin(), latched.end(), e.note) == latched.end()) latched.push_back (e.note);
            }
            if (arp)
            {
                if (arpIndex < 0) { arpPhase = 1.0; arpDir = 1; }   // (it starts at once, from its first step)
            }
            else play (e.note, ! wasEmpty && gateOn);
            break;
        }
        case 1:
        {
            held.erase (std::remove (held.begin(), held.end(), e.note), held.end());
            if (arp) break;
            if (e.note != playing) break;
            if (! held.empty()) play (held.back(), true);   // (back to the key still down: legato)
            else stop();
            break;
        }
        case 2: held.clear(); latched.clear(); stop(); arpIndex = -1; break;
        case 3: bend = (e.note - 8192) / 8192.0f * (float) bendRange; break;
        default: break;
    }
}

void Tg101Synth::render (float* left, float* right, int n)
{
    for (auto& e : events) e.offset *= kOs;
    int done = 0;
    while (done < n)
    {
        const int m = std::min (n - done, kChunk);
        renderHi (hi.data(), m * kOs);
        for (int i = 0; i < m; ++i)
        {
            float o = dec.push (hi.data() + i * kOs);
            const float ao = std::abs (o);
            if (ao > 1.5f) o = std::copysign (1.5f + 0.5f * std::tanh ((ao - 1.5f) * 2.0f), o);
            left[done + i] = o; right[done + i] = o;
        }
        done += m;
    }
    for (auto& e : events) e.offset /= kOs;
}

void Tg101Synth::renderHi (float* dst, int n)
{
    using namespace tg101;
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    // (the settings, once a block)
    static const float rangeSemis[4] = { -12, 0, 12, 24 };
    const float rangeOff = rangeSemis[std::clamp ((int) std::lround (p[Range]), 0, 3)];
    const float vcoModSemis = (p[VcoMod] * 0.01f) * (p[VcoMod] * 0.01f) * 12.0f;
    const int pwmSrc = (int) std::lround (p[PwmSource]);
    const float pwAmt = p[Pw] * 0.01f;
    const float lvPulse = p[PulseLvl] * 0.01f, lvSaw = p[SawLvl] * 0.01f, lvSub = p[SubLvl] * 0.01f, lvNoise = p[NoiseLvl] * 0.01f;
    const int subType = (int) std::lround (p[SubType]);
    const float envOct = p[EnvAmt] * 0.01f * 8.0f, lfoOct = p[VcfMod] * 0.01f * 3.0f, kybd = p[Kybd] * 0.01f, bendVcf = p[BendVcf] * 0.01f;
    const float k = std::pow (p[Res] * 0.01f, 0.9f) * 4.3f;
    const double tauA = std::max (1.5f, p[Attack]) * 0.001 * sr / 1.466;
    const float attK = (float) (1.0 - std::exp (-1.0 / tauA));
    const float decK = fallIn (p[Decay], sr), relK = fallIn (p[Release], sr);
    const float sus = p[Sustain] * 0.01f;
    const int trig = (int) std::lround (p[EnvTrig]);
    const bool vcaGateMode = std::lround (p[VcaMode]) == VcaGate;
    const float gateK = (float) (1.0 - std::exp (-1.0 / (0.001 * sr)));
    const int lfoSync = std::clamp ((int) std::lround (p[LfoSync]), 0, 5);
    const double lfoHz = lfoSync == 0 ? (double) p[LfoRate] : songBpm / 60.0 / kLfoBeats[lfoSync];
    const double lfoStep = lfoHz / sr;
    const int lfoWave = (int) std::lround (p[LfoWave]);
    const int porta = (int) std::lround (p[PortaMode]);
    const float glideK = porta == PortaOff ? 1.0f : (float) (1.0 - std::exp (-3.0 / std::max (1.0, p[PortaTime] * 0.001 * sr)));
    const int arpMode = (int) std::lround (p[ArpMode]);
    const bool hold = std::lround (p[ArpHold]) == 1;
    const double arpStep = songBpm / 60.0 * kStepsPerBeat[std::clamp ((int) std::lround (p[ArpRate]), 0, 5)] / sr;
    const int arpOct = std::clamp ((int) std::lround (p[ArpOct]), 1, 3);
    const double arpGate = std::clamp (p[ArpGate] * 0.01f, 0.1f, 1.0f);
    const float velSens = p[VelSens] * 0.01f;
    const float drive = 1.0f + p[Drive] * 0.01f * 8.0f;
    const float level = p[Level] * 0.01f * 0.9f;
    const float Gh = (float) std::tan (kTgPi * 14.0 / sr) / (1.0f + (float) std::tan (kTgPi * 14.0 / sr));
    float G = 0, G2 = 0, G4 = 0;
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);

        // ---- the LFO
        lfoPh += lfoStep;
        if (lfoPh >= 1.0)
        {
            lfoPh -= std::floor (lfoPh);
            lfoRand = noise();
            if (trig == TrigLfo && gateOn) startEnv();   // (TRIGGER: LFO - the envelope fires each cycle while a key is down)
        }
        lfoNoise += (noise() - lfoNoise) * 0.02f;
        switch (lfoWave)
        {
            case LfoSquare: lfoOut = lfoPh < 0.5 ? 1.0f : -1.0f; break;
            case LfoRandom: lfoOut = lfoRand; break;
            case LfoNoise:  lfoOut = std::clamp (lfoNoise * 4.0f, -1.0f, 1.0f); break;
            default:        lfoOut = (float) (1.0 - 4.0 * std::abs (lfoPh - 0.5)); break;
        }

        // ---- the arpeggio: a step every 1/n of a beat at the song's tempo
        if (arpMode != ArpOff)
        {
            const auto& src = hold && ! latched.empty() ? latched : held;
            if (src.empty())
            {
                if (arpIndex >= 0 || gateOn) { stop(); arpIndex = -1; playing = -1; }
            }
            else
            {
                arpPhase += arpStep;
                if (arpPhase >= 1.0)
                {
                    arpPhase -= std::floor (arpPhase);
                    // the notes: low to high, over the octaves
                    std::array<int, 96> seq {}; int count = 0;
                    std::array<int, 32> sorted {}; int sn = 0;
                    for (int nn : src) if (sn < 32) sorted[(size_t) sn++] = nn;
                    std::sort (sorted.begin(), sorted.begin() + sn);
                    for (int o = 0; o < arpOct; ++o)
                        for (int j = 0; j < sn && count < 96; ++j) seq[(size_t) count++] = sorted[(size_t) j] + 12 * o;
                    if (arpMode == ArpUp) arpIndex = arpIndex < 0 ? 0 : (arpIndex + 1) % count;
                    else if (arpMode == ArpDown) arpIndex = arpIndex < 0 ? count - 1 : (arpIndex - 1 + count) % count;
                    else
                    {
                        if (arpIndex < 0) { arpIndex = 0; arpDir = 1; }
                        else if (count > 1)
                        {
                            if (arpIndex + arpDir >= count || arpIndex + arpDir < 0) arpDir = -arpDir;
                            arpIndex = std::clamp (arpIndex + arpDir, 0, count - 1);
                        }
                        else arpIndex = 0;
                    }
                    arpIndex = std::clamp (arpIndex, 0, count - 1);
                    play (seq[(size_t) arpIndex], false);
                    arpNoteOn = true;
                    ++arpStepCount;
                }
                else if (arpNoteOn && arpPhase >= arpGate && arpGate < 0.999) { stop(); arpNoteOn = false; }
            }
        }

        // ---- the envelope
        switch (envStage)
        {
            case 1: env += (1.3f - env) * attK; if (env >= 1.0f) { env = 1.0f; envStage = 2; } break;
            case 2: env = sus + (env - sus) * decK; if (env - sus < 1.0e-4f) { env = sus; envStage = 3; } if (! gateOn) envStage = 4; break;
            case 3: env = sus; if (! gateOn) envStage = 4; break;
            case 4: env *= relK; if (env < 1.0e-6f) { env = 0; envStage = 0; } break;
            default: break;
        }
        vcaGate += ((gateOn ? 1.0f : 0.0f) - vcaGate) * gateK;
        const float amp = vcaGateMode ? vcaGate : env;
        float out = 0.0f;
        if (amp > 1.0e-6f || envStage != 0 || gateOn)
        {
            // ---- the VCO (with the portamento's glide, the LFO's vibrato and the bender)
            pitch += (target - pitch) * glideK;
            const double semis = pitch + rangeOff + p[Tune] + p[Fine] * 0.01 + bend + vcoModSemis * lfoOut;
            const double hz = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
            const double dt = std::min (0.45, hz / sr);
            ph += dt;
            if (ph >= 1.0) { ph -= std::floor (ph); flip = (flip + 1) & 3; }   // (the sub's flip-flops count the cycles)
            const float saw = sawAt (ph, dt);
            float x = lvSaw * saw;
            if (lvPulse > 0.0f)
            {
                float w;
                if (pwmSrc == PwmManual) w = 0.5f + 0.45f * pwAmt;
                else if (pwmSrc == PwmEnv) w = 0.5f + 0.45f * pwAmt * env;
                else w = 0.5f + 0.45f * pwAmt * (0.5f + 0.5f * lfoOut);
                x += lvPulse * pulseAt (ph, dt, std::clamp (w, 0.5f, 0.95f));
            }
            if (lvSub > 0.0f)
            {
                // the sub: from the VCO's own cycles - a square an octave down, two down, or a 25% pulse two down
                const double div = subType == SubOct1 ? 2.0 : 4.0;
                const double sp = ((subType == SubOct1 ? (flip & 1) : flip) + ph) / div, sdt = dt / div;
                x += lvSub * (subType == SubPulse2 ? pulseAt (sp, sdt, 0.25) : pulseAt (sp, sdt, 0.5));
            }
            if (lvNoise > 0.0f) x += lvNoise * noise() * 0.8f;

            // ---- the VCF: its cutoff every 4 samples
            if ((i & 3) == 0)
            {
                const float oct = envOct * env + lfoOct * lfoOut + kybd * (float) (pitch + rangeOff - 60.0f) / 12.0f
                                + bendVcf * bend / 12.0f * 2.0f + velSens * (vel - 1.0f) * 2.0f;
                cutoffHz = std::clamp (p[Freq] * std::pow (2.0f, oct), 16.0f, (float) (sr * 0.42));
                const float g = (float) std::tan (kTgPi * cutoffHz / sr);
                G = g / (1.0f + g); G2 = G * G; G4 = G2 * G2;
            }
            // four OTA stages, the resonance around them (a little bass kept), the mixer's drive into them
            const float kk = k * (1.0f - Gh);
            const float om = 1.0f - G;
            const float sig = G2 * G * om * s[0] + G2 * om * s[1] + G * om * s[2] + om * s[3];
            const float in = std::tanh (x * 0.5f);
            const float y4lin = (G4 * (in + kk * fbHp) + sig) / (1.0f + kk * G4);
            float u = std::tanh (in - kk * (y4lin - fbHp));
            for (auto& st : s)
            {
                const float v = (u - st) * G;
                const float y = v + st;
                st = y + v;
                u = y;
            }
            // (the DC taken out here, before the VCA: when the VCA shuts, nothing is left to drift back)
            const float y4 = u - dcX + 0.9995f * dcY; dcX = u; dcY = y4;
            { const float v = (y4 - fbHp) * Gh; const float lp = v + fbHp; fbHp = lp + v; }
            // ---- the VCA
            float gain = amp * (1.0f - velSens * (1.0f - vel));
            out = y4 * gain * (1.0f + k * 0.2f) * 1.6f;
            if (drive > 1.0f) out = std::tanh (out * drive) / std::sqrt (drive);
        }
        dst[i] = out * level;
    }
    events.erase (events.begin(), events.begin() + (long) ev);
    for (auto& e : events) e.offset = std::max (0, e.offset - n);
}
} // namespace prv
