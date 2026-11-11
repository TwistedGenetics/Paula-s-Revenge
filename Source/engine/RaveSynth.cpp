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
    static const std::vector<SynthParam> ps {
        { "TYPE",      "SOUND",    0, 4, 0,        3, "",   "STAB|PAD|ORGAN|BLEEP|BELL" },
        { "CHORD",     "SOUND",    0, 7, 1,        3, "",   "OFF|MINOR|MAJOR|MIN 7|MAJ 7|5TH|SUS 4|OCT" },
        { "DETUNE",    "SOUND",    0, 50, 14,      0, "C",  "" },
        { "OCTAVE",    "SOUND",    -3, 2, 0,       2, "",   "" },
        { "CUTOFF",    "FILTER",   40, 18000, 1800, 1, "HZ", "" },
        { "RES",       "FILTER",   0, 100, 30,     0, "%",  "" },
        { "ENV AMT",   "FILTER",   -100, 100, 60,  0, "%",  "" },
        { "ENV DECAY", "FILTER",   5, 3000, 220,   1, "MS", "" },
        { "ATTACK",    "ENVELOPE", 0.5f, 4000, 1,  1, "MS", "" },
        { "DECAY",     "ENVELOPE", 5, 5000, 350,   1, "MS", "" },
        { "SUSTAIN",   "ENVELOPE", 0, 100, 0,      0, "%",  "" },
        { "RELEASE",   "ENVELOPE", 5, 8000, 200,   1, "MS", "" },
        { "CHORUS",    "SPACE",    0, 100, 25,     0, "%",  "" },
        { "ECHO",      "SPACE",    0, 100, 25,     0, "%",  "" },
        { "ECHO TIME", "SPACE",    20, 1500, 375,  1, "MS", "" },
        { "VELOCITY",  "OUTPUT",   0, 100, 50,     0, "%",  "" },
        { "LEVEL",     "OUTPUT",   0, 100, 75,     0, "%",  "" },
    };
    return ps;
}

const char* raveTypeName (int t)
{
    static const char* n[] = { "STAB", "PAD", "ORGAN", "BLEEP", "BELL" };
    return n[std::clamp (t, 0, 4)];
}

std::vector<float> ravePreset (int type)
{
    using namespace rave;
    std::vector<float> v;
    for (const auto& q : raveParams()) v.push_back (q.def);   // (STAB's)
    auto set = [&] (std::initializer_list<std::pair<int, float>> l) { for (auto& [i, x] : l) v[(size_t) i] = x; };
    v[Type] = (float) std::clamp (type, 0, 4);
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
    v.fenv = 1.0f; v.click = 1.0f; v.modEnv = 1.0f;
    for (int o = 0; o < kOsc; ++o)
    {
        seed = seed * 1664525u + 1013904223u;
        v.ph[o] = (double) (seed >> 8) / 16777216.0;   // (the saws start apart, as free-running ones would be)
        v.drift[o] = 0;
    }
    const int type = (int) std::lround (p[rave::Type]);
    if (type == rave::Organ || type == rave::Bleep || type == rave::Bell) for (auto& ph : v.ph) ph = 0;   // (sines: from zero)
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
    for (int iv : chord) startVoice (e.note, (float) (e.note + iv), e.vel);
}

float RaveSynth::renderVoice (Voice& v, float& outL, float& outR)
{
    if (v.stage == 0 && ! v.fading) return 0.0f;
    const int type = std::clamp ((int) std::lround (p[rave::Type]), 0, 4);
    const double semis = v.pitch + bend + p[rave::Octave] * 12.0f;
    const double hz = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
    const float det = p[rave::Detune];
    float l = 0, r = 0;
    switch (type)
    {
        case rave::Stab:
        {
            // three saws, detuned and spread, over a square an octave down
            static const float pos[3] = { -1.0f, 0.0f, 1.0f };
            for (int o = 0; o < 3; ++o)
            {
                const double dt = std::min (0.45, hz * std::pow (2.0, pos[o] * det / 1200.0) / sr);
                step (v.ph[o], dt);
                const float x = saw (v.ph[o], dt);
                l += x * (1.0f - 0.5f * std::max (0.0f, pos[o])); r += x * (1.0f + 0.5f * std::min (0.0f, pos[o]));
            }
            const double dt = std::min (0.45, hz * 0.5 / sr);
            step (v.phLow, dt);
            double t2 = v.phLow + 0.5; t2 -= std::floor (t2);
            const float sq = 0.5f * (saw (v.phLow, dt) - saw (t2, dt)) * 0.45f;
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
                v.drift[o] = std::clamp (v.drift[o] * 0.99995f + rnd * 0.0004f, -1.0f, 1.0f);
                const float pos = (float) o / (kOsc - 1) * 2.0f - 1.0f;
                const double dt = std::min (0.45, hz * std::pow (2.0, (pos * det + v.drift[o] * det * 0.3f) / 1200.0) / sr);
                step (v.ph[o], dt);
                const float x = saw (v.ph[o], dt);
                l += x * (1.0f - std::max (0.0f, pos)); r += x * (1.0f + std::min (0.0f, pos));
            }
            step (v.phLow, hz * 2.0 / sr);
            const float up = (float) std::sin (kTwoPi * v.phLow) * 0.15f;
            l = l * 0.55f + up; r = r * 0.55f + up;
            break;
        }
        case rave::Organ:
        {
            // drawbar sines (16', 8', 5 1/3', 4', 2 2/3', 2'), and a click at the front from the fifth harmonic
            step (v.ph[0], hz / sr);
            static const float h[6] = { 1, 2, 3, 4, 6, 8 }, a[6] = { 1.0f, 0.7f, 0.45f, 0.35f, 0.2f, 0.12f };
            float x = 0;
            for (int k = 0; k < 6; ++k) if (hz * h[k] < sr * 0.45) x += a[k] * (float) std::sin (kTwoPi * v.ph[0] * h[k]);
            if (hz * 5 < sr * 0.45) x += 0.5f * v.click * (float) std::sin (kTwoPi * v.ph[0] * 5);
            v.click *= (float) std::exp (-1.0 / (0.012 * sr));
            // (DETUNE: a second organ a few cents off, on the other side)
            step (v.ph[1], hz * std::pow (2.0, det / 1200.0) / sr);
            const float y = (float) std::sin (kTwoPi * v.ph[1]) + 0.7f * (float) std::sin (kTwoPi * v.ph[1] * 2);
            const float d = std::min (1.0f, det / 20.0f) * 0.5f;
            l = (x + y * d) * 0.32f; r = (x - y * d * 0.2f) * 0.32f + y * d * 0.32f;
            break;
        }
        case rave::Bleep:
        {
            step (v.ph[0], hz / sr);
            float x = (float) std::sin (kTwoPi * v.ph[0]);
            x += 0.12f * (float) std::sin (kTwoPi * v.ph[0] * 3);
            step (v.ph[1], hz * std::pow (2.0, det / 1200.0) / sr);
            const float y = (float) std::sin (kTwoPi * v.ph[1]) * std::min (1.0f, det / 10.0f);
            l = (x + y * 0.5f) * 0.7f; r = (x + y * 0.5f) * 0.7f;
            break;
        }
        default:   // BELL
        {
            // two-operator FM: the modulator at the note (the electric piano's bark) plus a bell partial at 3.5 times,
            // both falling away so it goes from bright to mellow
            step (v.phMod, hz / sr);
            step (v.ph[2], hz * 3.5 / sr);
            const float index = 0.4f + 2.2f * v.modEnv;
            const float mod = index * (float) std::sin (kTwoPi * v.phMod) + 0.8f * v.modEnv * (float) std::sin (kTwoPi * v.ph[2]);
            v.modEnv *= (float) std::exp (-1.0 / (0.35 * sr));
            step (v.ph[0], hz / sr);
            step (v.ph[1], hz * std::pow (2.0, det / 1200.0) / sr);
            const float a = (float) std::sin (kTwoPi * v.ph[0] + mod), b = (float) std::sin (kTwoPi * v.ph[1] + mod);
            l = (a * 0.8f + b * 0.2f) * 0.6f; r = (a * 0.2f + b * 0.8f) * 0.6f;
            break;
        }
    }

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
        const float oct = p[rave::EnvAmount] * 0.01f * 5.0f * v.fenv;
        const float cut = std::clamp (p[rave::Cutoff] * std::pow (2.0f, oct), 30.0f, (float) (sr * 0.45));
        const float g = (float) std::tan (3.14159265358979 * cut / sr);
        const float k = 2.0f - 1.9f * std::clamp (p[rave::Resonance] * 0.01f, 0.0f, 1.0f);
        v.a1 = 1.0f / (1.0f + g * (g + k)); v.a2 = g * v.a1; v.a3 = g * v.a2;
        v.upd = 15;
    }
    auto svf = [&] (float x, float* ic)
    {
        const float v3 = x - ic[1], v1 = v.a1 * ic[0] + v.a2 * v3, v2 = ic[1] + v.a2 * ic[0] + v.a3 * v3;
        ic[0] = 2 * v1 - ic[0]; ic[1] = 2 * v2 - ic[1]; return v2;
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
    const float fb = 0.25f + 0.4f * echo;
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);
        float l = 0, r = 0;
        for (auto& v : voices) renderVoice (v, l, r);
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
        left[i] = l * level; right[i] = r * level;
    }
    while (ev < events.size()) handle (events[ev++]);
    events.clear();
}
} // namespace prv
