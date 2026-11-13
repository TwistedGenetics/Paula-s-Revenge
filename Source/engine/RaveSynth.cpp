// Paula's Revenge - RAVE (see RaveSynth.h)
#include "RaveSynth.h"
#include "BuiltinSynths.h"
#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    inline float blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }
    inline float saw (double t, double dt) { return (float) (2.0 * t - 1.0) - blep (t, dt); }
    inline void step (double& ph, double dt) { ph += dt; ph -= std::floor (ph); }
    // CHORD: the notes one key plays (semitones from it)
    const std::vector<int>& chordOf (int c)
    {
        static const std::vector<int> chords[] = { { 0 }, { 0, 3, 7 }, { 0, 4, 7 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 },
                                                   { 0, 7, 12 }, { 0, 5, 7 }, { 0, 12 } };
        return chords[std::clamp (c, 0, 7)];
    }
}

const std::vector<SynthParam>& raveParams()
{
    // (v0.83: three tabs)
    static const char* sd = "0:SOUND>SOUND";
    static const char* ly = "0:SOUND>LAYER";
    static const char* cd = "0:SOUND>CHORD";
    static const char* fl = "1:SHAPE>FILTER";
    static const char* en = "1:SHAPE>ENVELOPE";
    static const char* pt = "1:SHAPE>PITCH";
    static const char* dr = "2:FX>DRIVE + CRUSH";
    static const char* sp = "2:FX>SPACE";
    static const char* ou = "2:FX>OUTPUT";
    static const char* types = "STAB|PAD|ORGAN|BLEEP|BELL|HOOVER|MENTASM|PIANO|BELGIAN";
    static const std::vector<SynthParam> ps {
        { "TYPE",      sd, 0, 8, 0,        3, "",   types },
        { "CHORD",     cd, 0, 7, 1,        3, "",   "OFF|MINOR|MAJOR|MIN 7|MAJ 7|5TH|SUS 4|OCT" },
        { "DETUNE",    sd, 0, 50, 14,      0, "C",  "" },
        { "OCTAVE",    sd, -3, 2, 0,       2, "",   "" },
        { "CUTOFF",    fl, 40, 18000, 1800, 1, "HZ", "" },
        { "RES",       fl, 0, 100, 30,     0, "%",  "" },
        { "ENV AMT",   fl, -100, 100, 60,  0, "%",  "" },
        { "ENV DECAY", fl, 5, 3000, 220,   1, "MS", "" },
        { "ATTACK",    en, 0.5f, 4000, 1,  1, "MS", "" },
        { "DECAY",     en, 5, 5000, 350,   1, "MS", "" },
        { "SUSTAIN",   en, 0, 100, 0,      0, "%",  "" },
        { "RELEASE",   en, 5, 8000, 200,   1, "MS", "" },
        { "CHORUS",    sp, 0, 100, 25,     0, "%",  "" },
        { "ECHO",      sp, 0, 100, 25,     0, "%",  "" },
        { "ECHO TIME", sp, 20, 1500, 375,  1, "MS", "" },
        { "VELOCITY",  ou, 0, 100, 50,     0, "%",  "" },
        { "LEVEL",     ou, 0, 100, 75,     0, "%",  "" },
        // (v0.83)
        { "LAYER",     ly, 0, 9, 0,        3, "",   "OFF|STAB|PAD|ORGAN|BLEEP|BELL|HOOVER|MENTASM|PIANO|BELGIAN" },
        { "LEVEL",     ly, 0, 100, 50,     0, "%",  "" },
        { "OCTAVE",    ly, -2, 2, 0,       2, "",   "" },
        { "INVERSION", cd, 0, 3, 0,        2, "",   "" },
        { "SPREAD",    cd, 0, 100, 0,      0, "%",  "" },
        { "STRUM",     cd, 0, 200, 0,      0, "MS", "" },
        { "TYPE",      fl, 0, 2, 0,        3, "",   "LOW|BAND|HIGH" },
        { "KEY TRACK", fl, 0, 100, 0,      0, "%",  "" },
        { "PITCH ENV", pt, -24, 24, 0,     0, "ST", "" },
        { "PITCH TIME", pt, 5, 2000, 80,   1, "MS", "" },
        { "GLIDE",     pt, 0, 1000, 0,     0, "MS", "" },
        { "VIB RATE",  pt, 0.5f, 12, 5.5f, 1, "HZ", "" },
        { "VIB DEPTH", pt, 0, 100, 0,      0, "%",  "" },
        { "VIB DELAY", pt, 0, 2000, 250,   0, "MS", "" },
        { "DRIVE",     dr, 0, 100, 0,      0, "%",  "" },
        { "CRUSH",     dr, 0, 100, 0,      0, "%",  "" },
        { "FEEDBACK",  sp, 0, 95, 0,       0, "AUTO%", "" },
        { "WIDTH",     sp, 0, 150, 100,    0, "%",  "" },
    };
    return ps;
}

const char* raveLayeredName (int i)
{
    static const char* n[kRaveLayered] = { "STAB + ORGAN", "PIANO + PAD", "HOOVER STACK", "CRUSHED STAB", "STRUM CHORDS" };
    return n[std::clamp (i, 0, kRaveLayered - 1)];
}

std::vector<float> raveLayered (int i)
{
    using namespace rave;
    std::vector<float> v;
    auto set = [&] (std::initializer_list<std::pair<int, float>> l) { for (auto& [k, x] : l) v[(size_t) k] = x; };
    switch (std::clamp (i, 0, kRaveLayered - 1))
    {
        case 0: v = ravePreset (Stab); set ({ { Layer, 1 + Organ }, { LayerLevel, 45 }, { LayerOctave, 1 } }); break;
        case 1: v = ravePreset (Piano); set ({ { Layer, 1 + Pad }, { LayerLevel, 40 }, { Sustain, 30 }, { Release, 1500 }, { Chord, 2 }, { ChordSpread, 60 } }); break;
        case 2: v = ravePreset (Hoover); set ({ { Layer, 1 + Mentasm }, { LayerLevel, 50 }, { LayerOctave, 1 }, { Drive, 30 }, { Width, 130 } }); break;
        case 3: v = ravePreset (Stab); set ({ { Crush, 65 }, { Drive, 25 }, { Cutoff, 2600 } }); break;
        default: v = ravePreset (Organ); set ({ { Chord, 3 }, { Strum, 35 }, { ChordSpread, 80 }, { Inversion, 1 }, { Layer, 1 + Bell }, { LayerLevel, 30 } }); break;
    }
    return v;
}

const char* raveTypeName (int t)
{
    static const char* n[] = { "STAB", "PAD", "ORGAN", "BLEEP", "BELL", "HOOVER", "MENTASM", "PIANO", "BELGIAN" };
    return n[std::clamp (t, 0, rave::Types - 1)];
}

std::vector<float> ravePreset (int type)
{
    using namespace rave;
    std::vector<float> v;
    for (const auto& q : raveParams()) v.push_back (q.def);   // (STAB's)
    auto set = [&] (std::initializer_list<std::pair<int, float>> l) { for (auto& [i, x] : l) v[(size_t) i] = x; };
    v[Type] = (float) std::clamp (type, 0, Types - 1);
    switch (type)
    {
        case Pad:   set ({ { Chord, 0 }, { Detune, 22 }, { Cutoff, 1400 }, { Resonance, 10 }, { EnvAmount, 20 }, { FilterDecay, 1500 },
                           { Attack, 900 }, { Decay, 2000 }, { Sustain, 85 }, { Release, 2500 }, { Chorus, 60 }, { Echo, 30 }, { EchoTime, 500 } }); break;
        case Organ: set ({ { Chord, 0 }, { Detune, 3 }, { Cutoff, 6000 }, { Resonance, 0 }, { EnvAmount, 10 }, { FilterDecay, 120 },
                           { Attack, 1 }, { Decay, 600 }, { Sustain, 70 }, { Release, 120 }, { Chorus, 15 }, { Echo, 10 } }); break;
        case Bleep: set ({ { Chord, 0 }, { Detune, 0 }, { Cutoff, 8000 }, { Resonance, 0 }, { EnvAmount, 0 }, { FilterDecay, 100 },
                           { Attack, 1 }, { Decay, 180 }, { Sustain, 0 }, { Release, 120 }, { Chorus, 0 }, { Echo, 35 }, { EchoTime, 250 } }); break;
        case Bell:  set ({ { Chord, 0 }, { Detune, 4 }, { Cutoff, 9000 }, { Resonance, 0 }, { EnvAmount, 0 }, { FilterDecay, 300 },
                           { Attack, 1 }, { Decay, 1800 }, { Sustain, 20 }, { Release, 1200 }, { Chorus, 35 }, { Echo, 25 }, { EchoTime, 430 } }); break;
        case Hoover:  set ({ { Chord, 0 }, { Detune, 25 }, { Octave, -1 }, { Cutoff, 5000 }, { Resonance, 10 }, { EnvAmount, 20 }, { FilterDecay, 400 },
                             { Attack, 2 }, { Decay, 900 }, { Sustain, 60 }, { Release, 350 }, { Chorus, 80 }, { Echo, 15 }, { EchoTime, 375 } }); break;
        case Mentasm: set ({ { Chord, 0 }, { Detune, 30 }, { Octave, 0 }, { Cutoff, 7000 }, { Resonance, 15 }, { EnvAmount, 15 }, { FilterDecay, 600 },
                             { Attack, 1 }, { Decay, 1200 }, { Sustain, 50 }, { Release, 400 }, { Chorus, 70 }, { Echo, 20 }, { EchoTime, 375 } }); break;
        case Piano:   set ({ { Chord, 0 }, { Detune, 6 }, { Octave, 0 }, { Cutoff, 9000 }, { Resonance, 0 }, { EnvAmount, 10 }, { FilterDecay, 300 },
                             { Attack, 1 }, { Decay, 2500 }, { Sustain, 0 }, { Release, 400 }, { Chorus, 15 }, { Echo, 15 }, { EchoTime, 375 } }); break;
        case Belgian: set ({ { Chord, 5 }, { Detune, 10 }, { Octave, 0 }, { Cutoff, 1200 }, { Resonance, 45 }, { EnvAmount, 70 }, { FilterDecay, 160 },
                             { Attack, 1 }, { Decay, 300 }, { Sustain, 0 }, { Release, 150 }, { Chorus, 10 }, { Echo, 20 }, { EchoTime, 250 } }); break;
        default: break;
    }
    return v;
}

RaveSynth::RaveSynth()
{
    const auto& ps = raveParams();
    for (int i = 0; i < rave::Count; ++i) p[(size_t) i] = ps[(size_t) i].def;
}

void RaveSynth::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& b : chBuf) b.assign ((size_t) (sr * 0.04) + 4, 0.0f);
    for (auto& b : echoBuf) b.assign ((size_t) (sr * 1.6) + 4, 0.0f);
    chPos = echoPos = 0;
}

void RaveSynth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }

void RaveSynth::update (const Instrument& in)
{
    bendRange = std::clamp (in.bendRange, 1, 48);
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
}

void RaveSynth::setParameter (int i, float v01)
{
    const auto& ps = raveParams();
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void RaveSynth::noteOn (int note, int vel, int offset) { if (events.size() < 1024) events.push_back ({ offset, vel > 0 ? 0 : 1, note, vel }); }
void RaveSynth::noteOff (int note, int offset)         { if (events.size() < 1024) events.push_back ({ offset, 1, note, 0 }); }
void RaveSynth::allNotesOff (int offset)               { if (events.size() < 1024) events.push_back ({ offset, 2, 0, 0 }); }
void RaveSynth::pitchBend (int v, int offset)          { if (events.size() < 1024) events.push_back ({ offset, 3, v, 0 }); }

int RaveSynth::voicesPlaying() const
{
    int n = 0;
    for (const auto& v : voices) if (v.stage != 0 || v.fading) ++n;
    return n;
}

void RaveSynth::startOsc (int type, Osc& o)
{
    o = Osc {};
    o.click = 1.0f; o.modEnv = 1.0f; o.pitchEnv = 1.0f;
    for (int k = 0; k < kOsc; ++k)
    {
        seed = seed * 1664525u + 1013904223u;
        o.ph[k] = (double) (seed >> 8) / 16777216.0;   // (the saws start apart, as free-running ones would be)
    }
    if (type == rave::Organ || type == rave::Bleep || type == rave::Bell || type == rave::Piano) for (auto& ph : o.ph) ph = 0;   // (sines: from zero)
}

void RaveSynth::startVoice (int key, float pitch, int vel)
{
    Voice* free = nullptr;
    for (auto& v : voices) if (v.stage == 0 && ! v.fading) { free = &v; break; }
    if (free == nullptr)
        for (auto& v : voices) if (v.fading && (free == nullptr || v.fade < free->fade)) free = &v;
    if (free == nullptr)
    {
        // (all busy and none fading yet: the oldest goes, now)
        for (auto& v : voices) if (free == nullptr || v.order < free->order) free = &v;
    }
    *free = Voice {};
    Voice& v = *free;
    v.key = key; v.pitch = pitch; v.gate = true; v.stage = 1; v.vel = (float) vel / 127.0f;
    v.fenv = 1.0f;
    startOsc ((int) std::lround (p[rave::Type]), v.osc[0]);
    startOsc ((int) std::lround (p[rave::Layer]) - 1, v.osc[1]);
    v.penv = 1.0f;
    // GLIDE: from the last note played
    if (p[rave::Glide] > 0.5f && lastNote >= 0) v.glideOff = lastNote - (float) key;
    v.order = ++orderCount;
}

void RaveSynth::handle (const Event& e)
{
    if (e.type == 3) { bend = (float) (e.note - 8192) / 8192.0f * (float) bendRange; return; }
    if (e.type == 2) { for (auto& v : voices) if (v.stage != 0) { v.gate = false; v.stage = 3; } return; }
    if (e.type == 1)
    {
        for (auto& v : voices) if (v.key == e.note && v.gate) { v.gate = false; if (v.stage != 0) v.stage = 3; }
        return;
    }
    // the same key again: its voices let go (fast) for the new ones
    for (auto& v : voices) if (v.key == e.note && (v.stage != 0) && ! v.fading) v.fading = true;
    const auto& chord = chordOf ((int) std::lround (p[rave::Chord]));
    // room for the chord: the oldest fade out (4 ms) rather than stopping dead
    int freeCount = 0;
    for (auto& v : voices) if (v.stage == 0 && ! v.fading) ++freeCount;
    while (freeCount < (int) chord.size())
    {
        Voice* oldest = nullptr;
        for (auto& v : voices) if (v.stage != 0 && ! v.fading && (oldest == nullptr || v.order < oldest->order)) oldest = &v;
        if (oldest == nullptr) break;
        oldest->fading = true; ++freeCount;
    }
    // INVERSION: the lowest notes up an octave; SPREAD: across the stereo; STRUM: one after another
    std::vector<int> notes (chord.begin(), chord.end());
    const int inv = std::clamp ((int) std::lround (p[rave::Inversion]), 0, 3);
    for (int k = 0; k < inv && k < (int) notes.size() - 1; ++k) notes[(size_t) k] += 12;
    std::sort (notes.begin(), notes.end());
    const int n = (int) notes.size();
    for (int k = 0; k < n; ++k)
    {
        startVoice (e.note, (float) (e.note + notes[(size_t) k]), e.vel);
        Voice* v = nullptr;
        for (auto& x : voices) if (x.order == orderCount) v = &x;
        if (v == nullptr) continue;
        v->pan = n > 1 ? ((float) k / (float) (n - 1) * 2.0f - 1.0f) * p[rave::ChordSpread] * 0.01f : 0.0f;
        v->wait = (int) (k * p[rave::Strum] * 0.001 * sr);
    }
    lastNote = (float) e.note;
}

void RaveSynth::oscillate (int type, Osc& os, double semis, float det, float& l, float& r)
{
    // HOOVER swoops up into the note (from 7 below, over about 80 ms); MENTASM falls into it (from 7 above, slower)
    if (type == rave::Hoover)  { semis -= 7.0 * os.pitchEnv; os.pitchEnv *= (float) std::exp (-1.0 / (0.03 * sr)); }
    if (type == rave::Mentasm) { semis += 7.0 * os.pitchEnv; os.pitchEnv *= (float) std::exp (-1.0 / (0.12 * sr)); }
    const double hz = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
    switch (type)
    {
        case rave::Stab:
        {
            // three saws, detuned and spread, over a square an octave down
            static const float pos[3] = { -1.0f, 0.0f, 1.0f };
            for (int o = 0; o < 3; ++o)
            {
                const double dt = std::min (0.45, hz * std::pow (2.0, pos[o] * det / 1200.0) / sr);
                step (os.ph[o], dt);
                const float x = saw (os.ph[o], dt);
                l += x * (1.0f - 0.5f * std::max (0.0f, pos[o])); r += x * (1.0f + 0.5f * std::min (0.0f, pos[o]));
            }
            const double dt = std::min (0.45, hz * 0.5 / sr);
            step (os.phLow, dt);
            double t2 = os.phLow + 0.5; t2 -= std::floor (t2);
            const float sq = 0.5f * (saw (os.phLow, dt) - saw (t2, dt)) * 0.45f;
            l = l * 0.5f + sq; r = r * 0.5f + sq;
            break;
        }
        case rave::Pad:
        {
            // five saws, wide and wandering slowly (each its own way), with a soft octave above
            for (int o = 0; o < kOsc; ++o)
            {
                seed = seed * 1664525u + 1013904223u;
                const float rnd = (float) (seed >> 9) / 4194304.0f - 1.0f;
                os.drift[o] = std::clamp (os.drift[o] * 0.99995f + rnd * 0.0004f, -1.0f, 1.0f);
                const float pos = (float) o / (kOsc - 1) * 2.0f - 1.0f;
                const double dt = std::min (0.45, hz * std::pow (2.0, (pos * det + os.drift[o] * det * 0.3f) / 1200.0) / sr);
                step (os.ph[o], dt);
                const float x = saw (os.ph[o], dt);
                l += x * (1.0f - std::max (0.0f, pos)); r += x * (1.0f + std::min (0.0f, pos));
            }
            step (os.phLow, hz * 2.0 / sr);
            const float up = (float) std::sin (kTwoPi * os.phLow) * 0.15f;
            l = l * 0.55f + up; r = r * 0.55f + up;
            break;
        }
        case rave::Organ:
        {
            // drawbar sines (16', 8', 5 1/3', 4', 2 2/3', 2'), and a click at the front from the fifth harmonic
            step (os.ph[0], hz / sr);
            static const float h[6] = { 1, 2, 3, 4, 6, 8 }, a[6] = { 1.0f, 0.7f, 0.45f, 0.35f, 0.2f, 0.12f };
            float x = 0;
            for (int k = 0; k < 6; ++k) if (hz * h[k] < sr * 0.45) x += a[k] * (float) std::sin (kTwoPi * os.ph[0] * h[k]);
            if (hz * 5 < sr * 0.45) x += 0.5f * os.click * (float) std::sin (kTwoPi * os.ph[0] * 5);
            os.click *= (float) std::exp (-1.0 / (0.012 * sr));
            // (DETUNE: a second organ a few cents off, on the other side)
            step (os.ph[1], hz * std::pow (2.0, det / 1200.0) / sr);
            const float y = (float) std::sin (kTwoPi * os.ph[1]) + 0.7f * (float) std::sin (kTwoPi * os.ph[1] * 2);
            const float d = std::min (1.0f, det / 20.0f) * 0.5f;
            l = (x + y * d) * 0.32f; r = (x - y * d * 0.2f) * 0.32f + y * d * 0.32f;
            break;
        }
        case rave::Bleep:
        {
            step (os.ph[0], hz / sr);
            float x = (float) std::sin (kTwoPi * os.ph[0]);
            x += 0.12f * (float) std::sin (kTwoPi * os.ph[0] * 3);
            step (os.ph[1], hz * std::pow (2.0, det / 1200.0) / sr);
            const float y = (float) std::sin (kTwoPi * os.ph[1]) * std::min (1.0f, det / 10.0f);
            l = (x + y * 0.5f) * 0.7f; r = (x + y * 0.5f) * 0.7f;
            break;
        }
        case rave::Hoover: case rave::Mentasm:
        {
            // pulse-saws: a saw less a shifted saw, the shift (pulse width) moving - three of them spread wide
            const bool scream = type == rave::Mentasm;
            os.pwmPh += (scream ? 6.0 : 1.3) / sr; os.pwmPh -= std::floor (os.pwmPh);
            const double pw = 0.5 + (scream ? 0.4 : 0.3) * std::sin (kTwoPi * os.pwmPh);
            static const float pos[3] = { -1.0f, 0.0f, 1.0f };
            for (int o = 0; o < 3; ++o)
            {
                const double f = hz * std::pow (2.0, pos[o] * det / 1200.0) * (scream && o == 1 ? 2.0 : 1.0);   // (MENTASM: the middle one an octave up)
                const double dt = std::min (0.45, f / sr);
                step (os.ph[o], dt);
                double t2 = os.ph[o] + pw; t2 -= std::floor (t2);
                const float x = saw (os.ph[o], dt) - 0.8f * saw (t2, dt);
                l += x * (1.0f - 0.7f * std::max (0.0f, pos[o])); r += x * (1.0f + 0.7f * std::min (0.0f, pos[o]));
            }
            // (a square an octave down under the hoover)
            const double dl = std::min (0.45, hz * 0.5 / sr);
            step (os.phLow, dl);
            double t3 = os.phLow + 0.5; t3 -= std::floor (t3);
            const float sq = scream ? 0.0f : 0.5f * (saw (os.phLow, dl) - saw (t3, dl)) * 0.5f;
            l = l * 0.32f + sq; r = r * 0.32f + sq;
            break;
        }
        case rave::Piano:
        {
            // two strings a few cents apart; eight partials, the higher ones dying sooner (so it mellows as it
            // rings); a little thump of the hammer at the front
            static const float amp[8] = { 1.0f, 0.55f, 0.35f, 0.25f, 0.16f, 0.1f, 0.07f, 0.05f };
            const double age = os.phMod;   // (seconds since the hammer)
            os.phMod += 1.0 / sr;
            float a = 0, b = 0;
            step (os.ph[0], hz / sr);
            step (os.ph[1], hz * std::pow (2.0, det / 1200.0) / sr);
            for (int k = 1; k <= 8; ++k)
            {
                if (hz * k > sr * 0.45) break;
                const float fall = (float) std::exp (-age * (0.6 + 0.9 * k));
                a += amp[k - 1] * fall * (float) std::sin (kTwoPi * os.ph[0] * k);
                b += amp[k - 1] * fall * (float) std::sin (kTwoPi * os.ph[1] * k);
            }
            seed = seed * 1664525u + 1013904223u;
            const float thump = ((float) (seed >> 9) / 4194304.0f - 1.0f) * 0.25f * os.click * os.click;
            os.click *= (float) std::exp (-1.0 / (0.02 * sr));
            l = (a * 0.6f + b * 0.4f) * 0.4f + thump; r = (a * 0.4f + b * 0.6f) * 0.4f + thump;
            break;
        }
        case rave::Belgian:
        {
            // a saw and a square an octave up, two of each a little apart, driven hard (and its offset taken out)
            for (int o = 0; o < 2; ++o)
            {
                const double d = (o == 0 ? -0.5 : 0.5) * det;
                const double dt = std::min (0.45, hz * std::pow (2.0, d / 1200.0) / sr), dt2 = std::min (0.45, dt * 2.0);
                step (os.ph[o], dt); step (os.ph[o + 2], dt2);
                double t2 = os.ph[o + 2] + 0.5; t2 -= std::floor (t2);
                const float x = saw (os.ph[o], dt) + 0.6f * (saw (os.ph[o + 2], dt2) - saw (t2, dt2)) * 0.5f;
                (o == 0 ? l : r) += x;
            }
            l = std::tanh (l * 1.6f) * 0.55f; r = std::tanh (r * 1.6f) * 0.55f;
            os.dc += 0.0005f * (0.5f * (l + r) - os.dc);
            l -= os.dc; r -= os.dc;
            break;
        }
        default:   // BELL
        {
            // two-operator FM: the modulator at the note (the electric piano's bark) plus a bell partial at 3.5 times,
            // both falling away so it goes from bright to mellow
            step (os.phMod, hz / sr);
            step (os.ph[2], hz * 3.5 / sr);
            const float index = 0.4f + 2.2f * os.modEnv;
            const float mod = index * (float) std::sin (kTwoPi * os.phMod) + 0.8f * os.modEnv * (float) std::sin (kTwoPi * os.ph[2]);
            os.modEnv *= (float) std::exp (-1.0 / (0.35 * sr));
            step (os.ph[0], hz / sr);
            step (os.ph[1], hz * std::pow (2.0, det / 1200.0) / sr);
            const float a = (float) std::sin (kTwoPi * os.ph[0] + mod), b = (float) std::sin (kTwoPi * os.ph[1] + mod);
            l = (a * 0.8f + b * 0.2f) * 0.6f; r = (a * 0.2f + b * 0.8f) * 0.6f;
            break;
        }
    }

}

float RaveSynth::renderVoice (Voice& v, float& outL, float& outR)
{
    using namespace rave;
    if (v.stage == 0 && ! v.fading) return 0.0f;
    if (v.wait > 0) { --v.wait; return 0.0f; }   // (STRUM: not yet)
    const int type = std::clamp ((int) std::lround (p[Type]), 0, Types - 1);
    // pitch: the PITCH envelope, GLIDE, VIBRATO (after VIB DELAY, eased in)
    v.penv *= (float) std::exp (-1.0 / std::max (1.0, p[PitchTime] * 0.001 * sr * 0.3));
    if (v.glideOff != 0.0f)
    {
        v.glideOff *= (float) std::exp (-1.0 / std::max (1.0, p[Glide] * 0.001 * sr * 0.3));
        if (std::abs (v.glideOff) < 0.002f) v.glideOff = 0;
    }
    ++v.age;
    const float vibIn = std::clamp ((float) (v.age - p[VibDelay] * 0.001 * sr) / (float) (0.3 * sr), 0.0f, 1.0f);
    const float vib = (float) std::sin (6.283185307179586 * vibPh) * p[VibDepth] * 0.01f * 0.5f * vibIn;
    const double semis = v.pitch + bend + p[Octave] * 12.0f + p[PitchAmount] * v.penv + v.glideOff + vib;
    const float det = p[Detune];
    float l = 0, r = 0;
    oscillate (type, v.osc[0], semis, det, l, r);
    const int layer = (int) std::lround (p[Layer]) - 1;
    if (layer >= 0)
    {
        float ll = 0, lr = 0;
        oscillate (std::clamp (layer, 0, Types - 1), v.osc[1], semis + p[LayerOctave] * 12.0f, det, ll, lr);
        const float g = p[LayerLevel] * 0.01f;
        l += ll * g; r += lr * g;
    }
    if (v.pan != 0.0f) { l *= std::min (1.0f, 1.0f - v.pan); r *= std::min (1.0f, 1.0f + v.pan); }

    // the envelopes
    const float sus = p[rave::Sustain] * 0.01f;
    switch (v.stage)
    {
        case 1: v.env += (float) (1.0 / std::max (1.0, p[rave::Attack] * 0.001 * sr)); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; } break;
        case 2: v.env = sus + (v.env - sus) * (float) std::exp (-1.0 / std::max (1.0, p[rave::Decay] * 0.001 * sr * 0.3));
                if (sus <= 0.0f && v.env < 0.00003f) { v.env = 0; v.stage = 0; } break;
        case 3: v.env *= (float) std::exp (-1.0 / std::max (1.0, p[rave::Release] * 0.001 * sr * 0.3));
                if (v.env < 0.00003f) { v.env = 0; v.stage = 0; } break;
        default: break;
    }
    v.fenv *= (float) std::exp (-1.0 / std::max (1.0, p[rave::FilterDecay] * 0.001 * sr * 0.3));

    // the 12 dB low-pass (worked out every 16 samples)
    if (v.upd-- <= 0)
    {
        const float oct = p[rave::EnvAmount] * 0.01f * 5.0f * v.fenv + p[rave::KeyTrack] * 0.01f * (v.pitch - 60.0f) / 12.0f;
        const float cut = std::clamp (p[rave::Cutoff] * std::pow (2.0f, oct), 30.0f, (float) (sr * 0.45));
        const float g = (float) std::tan (3.14159265358979 * cut / sr);
        const float k = 2.0f - 1.9f * std::clamp (p[rave::Resonance] * 0.01f, 0.0f, 1.0f);
        v.a1 = 1.0f / (1.0f + g * (g + k)); v.a2 = g * v.a1; v.a3 = g * v.a2; v.k = k;
        v.upd = 15;
    }
    const int ft = (int) std::lround (p[rave::FilterType]);
    auto svf = [&] (float x, float* ic)
    {
        const float v3 = x - ic[1], v1 = v.a1 * ic[0] + v.a2 * v3, v2 = ic[1] + v.a2 * ic[0] + v.a3 * v3;
        ic[0] = 2 * v1 - ic[0]; ic[1] = 2 * v2 - ic[1];
        return ft == 1 ? v1 * (0.5f + 0.5f * v.k) : ft == 2 ? x - v.k * v1 - v2 : v2;   // (LOW, BAND, HIGH)
    };
    l = svf (l, v.s1[0]); r = svf (r, v.s1[1]);

    const float vs = p[rave::VelSens] * 0.01f;
    float a = v.env * (1.0f - vs + vs * v.vel);
    if (v.fading)
    {
        v.fade -= (float) (1.0 / (0.004 * sr));
        if (v.fade <= 0.0f) { v = Voice {}; return 0.0f; }
        a *= v.fade;
    }
    // (a chord's notes share the level: a 4-note chord isn't four times as loud)
    const float share = 1.0f / std::sqrt ((float) chordOf ((int) std::lround (p[rave::Chord])).size());
    outL += l * a * share; outR += r * a * share;
    return a;
}

void RaveSynth::render (float* left, float* right, int n)
{
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    const float chorus = p[rave::Chorus] * 0.01f, echo = p[rave::Echo] * 0.01f;
    const float level = p[rave::Level] * 0.01f * 0.5f;
    const int chLen = (int) chBuf[0].size(), ecLen = (int) echoBuf[0].size();
    const int ecDelay = std::clamp ((int) (p[rave::EchoTime] * 0.001 * sr), 1, std::max (1, ecLen - 2));
    const float fb = p[rave::EchoFeedback] >= 0.5f ? p[rave::EchoFeedback] * 0.01f : 0.25f + 0.4f * echo;   // (0: AUTO, as before v0.83)
    const float drive = p[rave::Drive] * 0.01f, crush = p[rave::Crush] * 0.01f;
    const float dg = 1.0f + drive * 8.0f, dn = drive > 0 ? 1.0f / std::tanh (dg * 0.6f) : 1.0f;
    const int crushEvery = 1 + (int) (crush * 5.0f);
    const float steps = std::pow (2.0f, 14.0f - crush * 9.0f);
    const float width = p[rave::Width] * 0.01f;
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);
        vibPh += p[rave::VibRate] / sr; vibPh -= std::floor (vibPh);
        float l = 0, r = 0;
        for (auto& v : voices) renderVoice (v, l, r);
        // DRIVE, then CRUSH: an old sampler's fewer bits and lower rate
        if (drive > 0.0f) { l = std::tanh (l * dg * 0.6f) * dn; r = std::tanh (r * dg * 0.6f) * dn; }
        if (crush > 0.0f)
        {
            if ((crushCount++ % crushEvery) == 0) { crushHold[0] = std::round (l * steps) / steps; crushHold[1] = std::round (r * steps) / steps; }
            l = crushHold[0]; r = crushHold[1];
        }
        if (chorus > 0 && chLen > 8)
        {
            chBuf[0][(size_t) chPos] = l; chBuf[1][(size_t) chPos] = r;
            chPh += 0.43 / sr; if (chPh >= 1) chPh -= 1;
            float wet[2];
            for (int c = 0; c < 2; ++c)
            {
                const double d = (0.008 + 0.004 * std::sin (kTwoPi * (chPh + c * 0.25))) * sr;
                double rp = chPos - d; while (rp < 0) rp += chLen;
                const int i0 = (int) rp; const float fr = (float) (rp - i0);
                const float a = chBuf[(size_t) (1 - c)][(size_t) i0], b = chBuf[(size_t) (1 - c)][(size_t) ((i0 + 1) % chLen)];
                wet[c] = a + (b - a) * fr;
            }
            if (++chPos >= chLen) chPos = 0;
            l += wet[0] * chorus * 0.8f; r += wet[1] * chorus * 0.8f;
            const float g = 1.0f / (1.0f + chorus * 0.5f); l *= g; r *= g;
        }
        if (echo > 0 && ecLen > 8)
        {
            // ECHO: ping-pong, each repeat a little darker
            int rp = echoPos - ecDelay; if (rp < 0) rp += ecLen;
            const float eL = echoBuf[0][(size_t) rp], eR = echoBuf[1][(size_t) rp];
            echoLp[0] += 0.35f * (eR - echoLp[0]); echoLp[1] += 0.35f * (eL - echoLp[1]);
            echoBuf[0][(size_t) echoPos] = 0.5f * (l + r) + fb * echoLp[0];
            echoBuf[1][(size_t) echoPos] = fb * echoLp[1];
            if (++echoPos >= ecLen) echoPos = 0;
            l += eL * echo * 0.7f; r += eR * echo * 0.7f;
        }
        if (std::abs (width - 1.0f) > 0.001f) { const float m = (l + r) * 0.5f, sd = (l - r) * 0.5f * width; l = m + sd; r = m - sd; }
        float ol = l * level, orr = r * level;
        auto lim = [] (float x) { const float a = std::abs (x); return a < 1.5f ? x : std::copysign (1.5f + 0.5f * std::tanh ((a - 1.5f) * 2.0f), x); };
        left[i] = std::isfinite (ol) ? lim (ol) : 0.0f; right[i] = std::isfinite (orr) ? lim (orr) : 0.0f;
    }
    while (ev < events.size()) handle (events[ev++]);
    events.clear();
}
} // namespace prv
