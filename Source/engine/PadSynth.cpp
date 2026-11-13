// Paula's Revenge - PAD (see PadSynth.h)
#include "PadSynth.h"
#include "BuiltinSynths.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>

namespace prv
{
namespace
{
    constexpr double kTau = 6.283185307179586;
    constexpr double kPadPi = 3.141592653589793;
    inline float blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }
    inline float sawAt (double t, double dt) { return (float) (2.0 * t - 1.0) - blep (t, dt); }
    inline double wrap (double x) { return x - std::floor (x); }
    // a sine of a phase in cycles - quick (a parabola, refined: within 0.1%), for the oscillators
    inline float fsin (double cycles)
    {
        float x = (float) (cycles - std::floor (cycles)) * 2.0f - 1.0f;   // -1..1 for -pi..pi
        float y = 4.0f * x * (1.0f - std::abs (x));
        y = 0.225f * (y * std::abs (y) - y) + y;
        return -y;
    }
    // a state-variable filter step (g = tan(pi f / sr), k = 1/Q): low, band and high at once
    struct SvfOut { float lp, bp, hp; };
    template <typename S> inline SvfOut svf (S& s, float x, float g, float k)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1; s.ic2 = 2.0f * v2 - s.ic2;
        return { v2, v1, x - k * v1 - v2 };
    }
    // an ADSR step (its steps worked out for the block); stage 1 attack, 2 decay, 3 sustain, 4 release, 0 done
    template <typename E> inline void adsr (float& e, int& st, const E& k)
    {
        switch (st)
        {
            case 1: e += k.a; if (e >= 1.0f) { e = 1.0f; st = 2; } break;
            case 2: e = k.s + (e - k.s) * k.d; if (e - k.s < 1.0e-4f) { e = k.s; st = 3; } break;
            case 3: e = k.s; break;
            case 4: e *= k.r; if (e < 1.0e-5f) { e = 0; st = 0; } break;
            default: break;
        }
    }
    template <typename E> inline void envSteps (E& k, float a, float d, float s, float r, double sr)
    {
        k.a = 1.0f / (float) std::max (1.0, a * 0.001 * sr);
        k.d = (float) std::exp (-4.6 / std::max (1.0, d * 0.001 * sr));
        k.s = s;
        k.r = (float) std::exp (-6.9 / std::max (1.0, r * 0.001 * sr));
    }
    const char* str (const std::string& s)
    {
        static std::deque<std::string> keep;   // (the names live as long as the program)
        keep.push_back (s);
        return keep.back().c_str();
    }
}

const std::vector<SynthParam>& padSynthParams()
{
    static const std::vector<SynthParam> ps = []
    {
        std::vector<SynthParam> v;
        const char* waves = "SAW|SQUARE|PWM|TRI|SINE|ORGAN|VOX|GLASS|NOISE|AIR|SAMPLE";
        for (int l = 0; l < pad::kLayers; ++l)
        {
            const std::string L (1, (char) ('A' + l));
            // (tabs: LAYER A-D, then MIX - every layer's level, pan, keys and velocity zone side by side - MOD, SPACE)
            const std::string tab = std::to_string (l) + ":LAYER " + L + "|" + L + ">";
            const char* src = str (tab + "SOURCE");
            const char* uni = str (tab + "UNISON");
            const char* fil = str (tab + "FILTER");
            const char* env = str (tab + "ENVELOPE");
            const char* lev = str ("4:MIX>LAYER " + L);
            const bool on = l < 2;
            v.push_back ({ "LAYER",     src, 0, 1, on ? 1.0f : 0.0f, 3, "", "OFF|ON" });
            v.push_back ({ "WAVE",      src, 0, 10, l == 0 ? 0.0f : l == 1 ? 2.0f : l == 2 ? 7.0f : 9.0f, 3, "", waves });
            v.push_back ({ "OCTAVE",    src, -3, 2, l == 1 ? -1.0f : 0.0f, 2, "", "" });
            v.push_back ({ "TUNE",      src, -24, 24, l == 2 ? 12.0f : 0.0f, 2, "ST", "" });
            v.push_back ({ "FINE",      src, -50, 50, 0, 0, "C", "" });
            v.push_back ({ "VOICES",    uni, 1, 7, l == 0 ? 5.0f : 1.0f, 2, "#", "" });
            v.push_back ({ "DETUNE",    uni, 0, 100, 18, 0, "C", "" });
            v.push_back ({ "SPREAD",    uni, 0, 100, 70, 0, "%", "" });
            v.push_back ({ "DRIFT",     uni, 0, 100, 25, 0, "%", "" });
            v.push_back ({ "FILTER",    fil, 0, 3, 1, 3, "", "OFF|LOW|BAND|HIGH" });
            v.push_back ({ "CUTOFF",    fil, 40, 18000, l == 0 ? 3500.0f : 2500.0f, 1, "HZ", "" });
            v.push_back ({ "RES",       fil, 0, 100, 15, 0, "%", "" });
            v.push_back ({ "ENV>CUT",   fil, -100, 100, 0, 0, "%", "" });
            v.push_back ({ "ATTACK",    env, 0.5f, 10000, l == 2 ? 2.0f : 900.0f, 1, "MS", "" });
            v.push_back ({ "DECAY",     env, 5, 10000, l == 2 ? 1800.0f : 2000.0f, 1, "MS", "" });
            v.push_back ({ "SUSTAIN",   env, 0, 100, l == 2 ? 0.0f : 85.0f, 0, "%", "" });
            v.push_back ({ "RELEASE",   env, 5, 15000, 1800, 1, "MS", "" });
            v.push_back ({ "DELAY",     env, 0, 5000, 0, 0, "MS", "" });
            v.push_back ({ "LEVEL",     lev, 0, 100, l == 1 ? 55.0f : 70.0f, 0, "%", "" });
            v.push_back ({ "PAN",       lev, -50, 50, 0, 2, "LR", "" });
            v.push_back ({ "KEY LOW",   lev, 24, 119, 24, 2, "NOTE", "" });
            v.push_back ({ "KEY HIGH",  lev, 24, 119, 119, 2, "NOTE", "" });
            v.push_back ({ "VELOCITY",  lev, 0, 2, 0, 3, "", "ALL|SOFT|HARD" });
        }
        const char* mf = "5:MOD>MAIN FILTER";
        const char* fe = "5:MOD>FILTER ENV";
        const char* l1 = "5:MOD>LFO 1";
        const char* l2 = "5:MOD>LFO 2";
        const char* sp = "6:SPACE>CHORUS + ECHO";
        const char* rv = "6:SPACE>REVERB";
        const char* out = "6:SPACE>OUTPUT";
        const char* shapes = "SINE|TRI|SAW|SQUARE|S&H";
        const char* targets = "CUTOFF|PITCH|LEVEL|PAN|A<>B|C<>D|WIDTH";
        v.push_back ({ "TYPE",      mf, 0, 4, 2, 3, "", "OFF|LOW 12|LOW 24|BAND|HIGH" });
        v.push_back ({ "CUTOFF",    mf, 40, 18000, 6000, 1, "HZ", "" });
        v.push_back ({ "RES",       mf, 0, 100, 10, 0, "%", "" });
        v.push_back ({ "ENV AMT",   mf, -100, 100, 20, 0, "%", "" });
        v.push_back ({ "KEY TRACK", mf, 0, 100, 30, 0, "%", "" });
        v.push_back ({ "VEL>CUT",   mf, 0, 100, 25, 0, "%", "" });
        v.push_back ({ "ATTACK",    fe, 0.5f, 10000, 1500, 1, "MS", "" });
        v.push_back ({ "DECAY",     fe, 5, 10000, 3000, 1, "MS", "" });
        v.push_back ({ "SUSTAIN",   fe, 0, 100, 40, 0, "%", "" });
        v.push_back ({ "RELEASE",   fe, 5, 15000, 2000, 1, "MS", "" });
        v.push_back ({ "SHAPE",     l1, 0, 4, 0, 3, "", shapes });
        v.push_back ({ "RATE",      l1, 0.01f, 20, 0.12f, 1, "HZ", "" });
        v.push_back ({ "DEPTH",     l1, 0, 100, 25, 0, "%", "" });
        v.push_back ({ "TARGET",    l1, 0, 6, 0, 3, "", targets });
        v.push_back ({ "SHAPE",     l2, 0, 4, 1, 3, "", shapes });
        v.push_back ({ "RATE",      l2, 0.01f, 20, 0.07f, 1, "HZ", "" });
        v.push_back ({ "DEPTH",     l2, 0, 100, 0, 0, "%", "" });
        v.push_back ({ "TARGET",    l2, 0, 6, 4, 3, "", targets });
        v.push_back ({ "CHORUS",    sp, 0, 100, 40, 0, "%", "" });
        v.push_back ({ "RATE",      sp, 0.05f, 5, 0.4f, 1, "HZ", "" });
        v.push_back ({ "ECHO",      sp, 0, 100, 15, 0, "%", "" });
        v.push_back ({ "TIME",      sp, 20, 1500, 450, 1, "MS", "" });
        v.push_back ({ "FEEDBACK",  sp, 0, 95, 45, 0, "%", "" });
        v.push_back ({ "REVERB",    rv, 0, 100, 35, 0, "%", "" });
        v.push_back ({ "SIZE",      rv, 0, 100, 70, 0, "%", "" });
        v.push_back ({ "TONE",      rv, 0, 100, 55, 0, "%", "" });
        v.push_back ({ "GLIDE",     out, 0, 2000, 0, 0, "MS", "" });
        v.push_back ({ "VEL SENS",  out, 0, 100, 40, 0, "%", "" });
        v.push_back ({ "WIDTH",     out, 0, 150, 100, 0, "%", "" });
        v.push_back ({ "LEVEL",     out, 0, 100, 70, 0, "%", "" });
        // (v0.85) each layer's SAMPLE wave: the SAMPLES tab
        for (int l = 0; l < pad::kLayers; ++l)
        {
            const std::string L (1, (char) ('A' + l));
            const char* sm = str ("7:SAMPLES>LAYER " + L);   // (a tab of their own; LOAD SAMPLE... is on each layer's too)
            v.push_back ({ "SAMPLE",    sm, 0, (float) kMaxInstruments, 0, 2, "SLOT", "" });
            v.push_back ({ "ROOT",      sm, 24, 119, 60, 2, "NOTE", "" });
            v.push_back ({ "START",     sm, 0, 100, 0, 0, "%", "" });
            v.push_back ({ "LOOP",      sm, 0, 2, 0, 3, "", "AUTO|WHOLE|ONE SHOT" });
            v.push_back ({ "XFADE",     sm, 0, 100, 25, 0, "%", "" });
        }
        return v;
    }();
    return ps;
}

const char* padPresetName (int i)
{
    static const char* n[kPadPresets] = { "WARM ANALOG", "JUNGLE ATMOS", "GLASS CHOIR", "STRING ENSEMBLE", "AIR PAD",
                                          "ORGAN SWELL", "PWM DREAM", "BELL + PAD", "SPLIT BASS / PAD", "SOFT / HARD" };
    return n[std::clamp (i, 0, kPadPresets - 1)];
}

std::vector<float> padPreset (int i)
{
    using namespace pad;
    std::vector<float> v;
    for (const auto& q : padSynthParams()) v.push_back (q.def);   // (WARM ANALOG's)
    auto set = [&] (std::initializer_list<std::pair<int, float>> l) { for (auto& [k, x] : l) v[(size_t) k] = x; };
    auto lay = [&] (int layer, std::initializer_list<std::pair<int, float>> l) { for (auto& [k, x] : l) v[(size_t) pad::p (layer, k)] = x; };
    switch (std::clamp (i, 0, kPadPresets - 1))
    {
        case 1:   // dark, slow, wide: the rolling-jungle atmosphere
            lay (0, { { Wave, Saw }, { Unison, 7 }, { Detune, 25 }, { Cutoff, 1400 }, { Attack, 2500 }, { Release, 4000 } });
            lay (1, { { On, 1 }, { Wave, Air }, { Octave, 0 }, { Level, 45 }, { Attack, 4000 }, { Release, 5000 }, { pad::FilterType, 2 }, { Cutoff, 2200 } });
            set ({ { MainCutoff, 1800 }, { MainRes, 25 }, { Lfo1Depth, 55 }, { Lfo1Rate, 0.05f }, { Reverb, 65 }, { ReverbSize, 92 }, { ReverbTone, 35 },
                   { Echo, 25 }, { EchoTime, 600 }, { Chorus, 50 } });
            break;
        case 2:   // VOX choir with a GLASS shimmer an octave up
            lay (0, { { Wave, Vox }, { Unison, 5 }, { Detune, 15 }, { pad::FilterType, 0 }, { Attack, 1200 } });
            lay (1, { { Wave, Glass }, { Octave, 1 }, { Unison, 2 }, { Detune, 8 }, { Level, 30 }, { Attack, 600 }, { pad::FilterType, 0 } });
            set ({ { Reverb, 55 }, { ReverbSize, 85 }, { Chorus, 30 }, { Lfo2Depth, 30 }, { Lfo2Target, TAB } });
            break;
        case 3:   // strings: saws through the ensemble chorus, a slower octave below
            lay (0, { { Wave, Saw }, { Unison, 6 }, { Detune, 12 }, { Cutoff, 5000 }, { Attack, 500 }, { Release, 1200 } });
            lay (1, { { Wave, Saw }, { Octave, -1 }, { Unison, 3 }, { Detune, 10 }, { Attack, 900 }, { Level, 45 } });
            set ({ { Chorus, 80 }, { ChorusRate, 0.7f }, { MainCutoff, 4500 }, { MainEnv, 0 }, { Reverb, 40 } });
            break;
        case 4:   // breathy air over a sine
            lay (0, { { Wave, Sine }, { Unison, 1 }, { pad::FilterType, 0 }, { Attack, 1500 }, { Level, 60 } });
            lay (1, { { Wave, Air }, { Octave, 0 }, { Unison, 3 }, { Detune, 30 }, { Level, 60 }, { Attack, 2000 } });
            lay (2, { { On, 1 }, { Wave, Air }, { Octave, 1 }, { Tune, 7 }, { Unison, 2 }, { Level, 30 }, { Attack, 3000 }, { Sustain, 70 }, { Decay, 4000 }, { Release, 3000 } });
            set ({ { Reverb, 60 }, { ReverbSize, 80 }, { Lfo1Target, TPan }, { Lfo1Depth, 40 }, { Lfo1Rate, 0.2f } });
            break;
        case 5:   // organ that swells in, a rotary-ish wobble
            lay (0, { { Wave, Organ }, { Unison, 2 }, { Detune, 6 }, { pad::FilterType, 0 }, { Attack, 1800 }, { Release, 600 } });
            lay (1, { { On, 0 } });
            set ({ { Lfo1Target, TPitch }, { Lfo1Rate, 5.5f }, { Lfo1Depth, 8 }, { Lfo2Target, TLevel }, { Lfo2Rate, 6 }, { Lfo2Depth, 15 }, { Chorus, 50 }, { MainEnv, 0 } });
            break;
        case 6:   // PWM, slow and lush
            lay (0, { { Wave, Pwm }, { Unison, 4 }, { Detune, 14 }, { Cutoff, 3000 }, { Attack, 1500 } });
            lay (1, { { Wave, Pwm }, { Octave, -1 }, { Unison, 2 }, { Detune, 9 }, { Level, 50 } });
            set ({ { Chorus, 70 }, { Lfo1Depth, 35 }, { Lfo1Rate, 0.09f }, { Reverb, 45 } });
            break;
        case 7:   // a GLASS bell on the attack, a saw pad swelling in after it
            lay (0, { { Wave, Saw }, { Unison, 5 }, { Attack, 1800 }, { Delay, 250 }, { Level, 60 } });
            lay (1, { { On, 0 } });
            lay (2, { { On, 1 }, { Wave, Glass }, { Tune, 12 }, { Attack, 1 }, { Decay, 1600 }, { Sustain, 0 }, { Release, 1600 }, { pad::FilterType, 0 }, { Level, 65 } });
            set ({ { Reverb, 45 }, { Echo, 25 } });
            break;
        case 8:   // below C-3 a sub-ish bass, above it the pad
            lay (0, { { Wave, Saw }, { Unison, 5 }, { KeyLow, 60 } });
            lay (1, { { Wave, Sine }, { Octave, -1 }, { Unison, 1 }, { pad::FilterType, 0 }, { Attack, 3 }, { Release, 200 }, { Level, 80 }, { KeyHigh, 59 } });
            break;
        case 9:   // soft notes a sine pad, hard ones a bright saw stack
            lay (0, { { Wave, Saw }, { Unison, 7 }, { Detune, 22 }, { VelZone, 2 }, { Cutoff, 7000 }, { Attack, 300 } });
            lay (1, { { Wave, Sine }, { Octave, 0 }, { Unison, 3 }, { Detune, 8 }, { VelZone, 1 }, { pad::FilterType, 0 }, { Level, 70 } });
            set ({ { VelSens, 20 } });
            break;
        default: break;
    }
    return v;
}

bool PadSynth::Voice::active() const
{
    if (fading) return true;
    for (const auto& l : layers) if (l.on && (l.stage != 0 || l.delayLeft > 0)) return true;
    return false;
}

PadSynth::PadSynth()
{
    const auto& ps = padSynthParams();
    for (int i = 0; i < pad::Count; ++i) p[(size_t) i] = ps[(size_t) i].def;
    events.reserve (256);
}

void PadSynth::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& b : chBuf) b.assign ((size_t) (sr * 0.05) + 4, 0.0f);
    for (auto& b : echoBuf) b.assign ((size_t) (sr * 1.6) + 4, 0.0f);
    static const int lens[kRev] = { 1031, 1327, 1523, 1801, 2063, 2339, 2593, 2879 };
    for (int k = 0; k < kRev; ++k) { rev[k].assign ((size_t) (lens[k] * 1.6 * sr / 48000.0) + 8, 0.0f); revPos[k] = 0; revLp[k] = 0; }
    chPos = echoPos = 0;
}

void PadSynth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }

void PadSynth::updateSong (const Song& s)
{
    song = &s;
    self = me != nullptr && me >= s.instruments.data() && me < s.instruments.data() + kMaxInstruments ? (int) (me - s.instruments.data()) : -1;
}

void PadSynth::setTestSample (std::vector<int16_t> mono, double rate, int loopStart, int loopLength)
{
    testSmp = Instrument {};
    testSmp.data = std::move (mono); testSmp.rate = rate; testSmp.loopStart = loopStart; testSmp.loopLength = loopLength;
    hasTestSmp = true;
}

PadSynth::Smp PadSynth::sampleFor (int layer) const
{
    // which sample a layer's SAMPLE wave plays: 0 = the PAD's own (loaded into it), else that instrument's
    Smp s;
    const int slot = (int) std::lround (p[(size_t) pad::s (layer, pad::SmpSlot)]);
    const Instrument* in = nullptr;
    if (slot == 0 && hasTestSmp) in = &testSmp;
    else if (song != nullptr)
    {
        const int i = slot == 0 ? self : slot - 1;
        if (i >= 0 && i < kMaxInstruments) in = &song->instruments[(size_t) i];
    }
    if (in == nullptr || in->data.size() < 4) return s;
    const int n = (int) in->data.size();
    const int a = std::clamp (in->playStart, 0, n - 4);
    const int b = in->playEnd > a + 4 ? std::min (n, in->playEnd) : n;
    s.l = in->data.data(); s.r = in->dataR.size() == in->data.size() ? in->dataR.data() : nullptr;
    s.len = b; s.rate = in->rate > 1 ? in->rate : 16574.0;
    s.loopA = a;   // (where it starts: its START)
    if (in->loopLength >= 4 && in->loopStart >= a && in->loopStart + 4 <= b)
    { s.loopB = std::min (b, in->loopStart + in->loopLength); s.loopA = -in->loopStart - 1; }   // (its own loop, marked by a minus)
    return s;
}

void PadSynth::update (const Instrument& in)
{
    me = &in;
    bendRange = std::clamp (in.bendRange, 1, 48);
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
}

void PadSynth::setParameter (int i, float v01)
{
    const auto& ps = padSynthParams();
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void PadSynth::noteOn (int note, int vel, int offset) { if (events.size() < 1024) events.push_back ({ offset, vel > 0 ? 0 : 1, note, vel }); }
void PadSynth::noteOff (int note, int offset)         { if (events.size() < 1024) events.push_back ({ offset, 1, note, 0 }); }
void PadSynth::allNotesOff (int offset)               { if (events.size() < 1024) events.push_back ({ offset, 2, 0, 0 }); }
void PadSynth::pitchBend (int v, int offset)          { if (events.size() < 1024) events.push_back ({ offset, 3, v, 0 }); }

int PadSynth::voicesPlaying() const
{
    int n = 0;
    for (const auto& v : voices) if (v.active()) ++n;
    return n;
}

float PadSynth::layerLevel (int layer) const
{
    float m = 0;
    for (const auto& v : voices) if (v.active() && layer >= 0 && layer < pad::kLayers) m = std::max (m, v.layers[(size_t) layer].level);
    return m;
}

float PadSynth::rnd()
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (float) (seed & 0xffffff) / (float) 0x800000 - 1.0f;
}

void PadSynth::startVoice (int key, int vel)
{
    using namespace pad;
    // a free voice, else the oldest (faded out quickly so it doesn't click)
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active()) { v = &x; break; }
    if (v == nullptr)
    {
        // (all busy: the oldest one already let go, else the oldest)
        for (auto& x : voices) if (! x.gate && (v == nullptr || x.order < v->order)) v = &x;
        if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.order < v->order) v = &x; }
    }
    const float from = (p[Glide] > 0.5f && lastPitch >= 0) ? lastPitch : (float) key;
    *v = Voice {};
    v->key = key; v->gate = true; v->vel = std::clamp (vel, 1, 127) / 127.0f;
    v->pitch = from; v->target = (float) key;
    v->order = ++orderCount;
    v->fenv = 0; v->fstage = 1;
    for (int l = 0; l < kLayers; ++l)
    {
        auto& L = v->layers[(size_t) l];
        const bool zone = std::lround (p[(size_t) pad::p (l, VelZone)]) == 0 || (std::lround (p[(size_t) pad::p (l, VelZone)]) == 1) == (v->vel < 0.62f);
        L.on = p[(size_t) pad::p (l, On)] > 0.5f && key >= (int) p[(size_t) pad::p (l, KeyLow)] && key <= (int) p[(size_t) pad::p (l, KeyHigh)] && zone;
        L.stage = 1; L.env = 0;
        L.delayLeft = (int) (p[(size_t) pad::p (l, Delay)] * 0.001 * sr);
        for (int u = 0; u < kUni; ++u) { L.ph[(size_t) u] = 0.5 * (rnd() + 1.0f); L.ph2[(size_t) u] = 0.5 * (rnd() + 1.0f); L.drift[(size_t) u] = rnd(); L.driftTo[(size_t) u] = rnd(); }
        if (std::lround (p[(size_t) pad::p (l, Wave)]) == Sample)
        {
            // (SAMPLE: every copy from START, so the attack is the sample's own)
            const Smp sm = sampleFor (l);
            const int a = sm.loopA < 0 ? 0 : sm.loopA;
            const double from = sm.len > 0 ? a + (sm.len - a) * std::clamp (p[(size_t) pad::s (l, SmpStart)], 0.0f, 100.0f) * 0.01 : 0.0;
            for (int u = 0; u < kUni; ++u) { L.pos[(size_t) u] = std::min (from, std::max (0.0, sm.len - 2.0)); L.ended[(size_t) u] = false; }
        }
    }
    lastPitch = (float) key;
}

void PadSynth::handle (const Event& e)
{
    switch (e.type)
    {
        case 0: startVoice (e.note, e.vel); break;
        case 1:
            for (auto& v : voices)
                if (v.gate && v.key == e.note)
                {
                    v.gate = false;
                    for (auto& L : v.layers) if (L.stage != 0) L.stage = 4;
                    if (v.fstage != 0) v.fstage = 4;
                }
            break;
        case 2:
            for (auto& v : voices) { v.gate = false; for (auto& L : v.layers) if (L.stage != 0) L.stage = 4; if (v.fstage != 0) v.fstage = 4; }
            break;
        case 3: bend = (e.note - 8192) / 8192.0f * (float) bendRange; break;
        default: break;
    }
}

void PadSynth::renderVoice (Voice& v, const float* lfo, float& outL, float& outR)
{
    using namespace pad;
    if (! v.active()) return;
    // glide
    if (p[Glide] > 0.5f) v.pitch += (v.target - v.pitch) * (1.0f - (float) std::exp (-4.6 / (p[Glide] * 0.001 * sr)));
    else v.pitch = v.target;
    adsr (v.fenv, v.fstage, filterEnv);
    float sl = 0, sr_ = 0;
    const float velAmp = 1.0f - p[VelSens] * 0.01f * (1.0f - v.vel);
    const float ab = lfo[TAB], cd = lfo[TCD];
    for (int l = 0; l < kLayers; ++l)
    {
        auto& L = v.layers[(size_t) l];
        if (! L.on) continue;
        if (L.delayLeft > 0) { --L.delayLeft; continue; }
        auto q = [&] (int what) { return p[(size_t) pad::p (l, what)]; };
        adsr (L.env, L.stage, layerEnv[l]);
        if (L.stage == 0) { L.level = 0; continue; }
        const int wave = (int) std::lround (q (Wave));
        const int U = std::clamp ((int) std::lround (q (Unison)), 1, kUni);
        const int ft = (int) std::lround (q (pad::FilterType));
        const Smp sm = wave == Sample ? sampleFor (l) : Smp {};
        if (L.upd-- <= 0)
        {
            // the pitches of its unison voices (with their drift) and its filter: every 16 samples
            L.upd = 15;
            const double semis = v.pitch + q (Octave) * 12.0f + q (Tune) + q (Fine) * 0.01f + bend + lfo[TPitch];
            L.baseHz = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
            const float spread = q (Spread) * 0.01f, drift = q (Drift) * 0.01f;
            for (int u = 0; u < U; ++u)
            {
                const float pos = U > 1 ? (float) u / (float) (U - 1) * 2.0f - 1.0f : 0.0f;
                auto& d = L.drift[(size_t) u];
                d += (L.driftTo[(size_t) u] - d) * 0.0005f;
                if (std::abs (L.driftTo[(size_t) u] - d) < 0.02f) L.driftTo[(size_t) u] = rnd();
                const double cents = pos * q (Detune) + d * drift * 14.0;
                L.dts[(size_t) u] = std::min (0.45, L.baseHz * std::pow (2.0, cents / 1200.0) / sr);
                L.panOf[(size_t) u] = std::clamp (pos * spread, -1.0f, 1.0f);
                if (wave == Sample)
                {
                    // (SAMPLE: at ROOT it plays at the sample's own rate)
                    const double rootHz = 440.0 * std::pow (2.0, (p[(size_t) pad::s (l, SmpRoot)] - 69.0) / 12.0);
                    L.sstep[(size_t) u] = L.baseHz * std::pow (2.0, cents / 1200.0) / rootHz * sm.rate / sr;
                }
            }
            if (wave == Vox)
            {
                L.g1 = (float) std::tan (kPadPi * 730.0 / sr); L.g2 = (float) std::tan (kPadPi * 1090.0 / sr); L.g3 = (float) std::tan (kPadPi * std::min (2440.0, sr * 0.4) / sr);
            }
            else if (wave == Air)
            {
                L.g1 = (float) std::tan (kPadPi * std::min (L.baseHz, sr * 0.4) / sr); L.g2 = (float) std::tan (kPadPi * std::min (L.baseHz * 2.0, sr * 0.4) / sr);
            }
            if (ft != 0)
            {
                const float cut = std::clamp (q (Cutoff) * std::pow (2.0f, q (EnvCut) * 0.01f * 5.0f * L.env + (v.pitch - 60.0f) / 24.0f), 20.0f, (float) (sr * 0.45));
                L.fg = (float) std::tan (kPadPi * cut / sr); L.fk = 2.0f - 1.9f * q (Resonance) * 0.01f;
            }
        }
        const double base = L.baseHz;
        float lL = 0, lR = 0, mono = 0;
        for (int u = 0; u < U; ++u)
        {
            const double dt = L.dts[(size_t) u];
            const double hz = dt * sr;
            if (wave == Sample)
            {
                // SAMPLE: read it at this copy's speed, looping (with a crossfade over the join) or once through
                if (sm.len < 4 || L.ended[(size_t) u]) continue;
                const int mode = (int) std::lround (p[(size_t) pad::s (l, SmpLoop)]);
                int A = sm.loopA < 0 ? -sm.loopA - 1 : sm.loopA, B = sm.loopA < 0 ? sm.loopB : sm.len;
                if (mode == 1 || (mode == 0 && sm.loopA >= 0))   // (WHOLE - or AUTO with no loop of its own: from START to the end)
                {
                    const int a0 = sm.loopA < 0 ? 0 : sm.loopA;
                    A = a0 + (int) ((sm.len - a0) * std::clamp (p[(size_t) pad::s (l, SmpStart)], 0.0f, 100.0f) * 0.01f);
                    B = sm.len;
                }
                const bool loop = mode != 2 && B - A >= 4;
                const double F = loop ? std::floor ((B - A) * 0.5 * std::clamp (p[(size_t) pad::s (l, SmpFade)], 0.0f, 100.0f) * 0.01) : 0.0;
                auto rd = [&] (const int16_t* d, double x)
                {
                    const int i = std::clamp ((int) x, 0, sm.len - 2);
                    const float f = (float) (x - i);
                    return (d[i] + (d[i + 1] - d[i]) * f) * (1.0f / 32768.0f);
                };
                double& at = L.pos[(size_t) u];
                if (! loop && at >= sm.len - 2) { L.ended[(size_t) u] = true; continue; }
                float xl = rd (sm.l, at), xr = sm.r != nullptr ? rd (sm.r, at) : xl;
                if (loop && F >= 2 && at >= B - F)
                {
                    // (the join: the loop's start fades in as its end fades out - equal power, no click)
                    const double into = at - (B - F);
                    const float tt = (float) std::clamp (into / F, 0.0, 1.0);
                    const float go = std::sqrt (1.0f - tt), come = std::sqrt (tt);
                    xl = xl * go + rd (sm.l, A + into) * come;
                    xr = xr * go + (sm.r != nullptr ? rd (sm.r, A + into) : rd (sm.l, A + into)) * come;
                }
                at += L.sstep[(size_t) u];
                if (loop && at >= B) at = A + F + (at - B);
                if (loop && at >= B) at = A;   // (a step longer than the loop)
                const float pan = L.panOf[(size_t) u];
                lL += xl * (1.0f - pan) * 0.5f;
                lR += xr * (1.0f + pan) * 0.5f;
                continue;
            }
            auto& ph = L.ph[(size_t) u];
            ph = wrap (ph + dt);
            float x = 0;
            switch (wave)
            {
                case Saw: case Vox: x = sawAt (ph, dt); break;
                case Square: x = 0.7f * (sawAt (ph, dt) - sawAt (wrap (ph + 0.5), dt)); break;
                case Pwm:
                {
                    auto& lph = L.ph2[(size_t) u];
                    lph = wrap (lph + (0.17 + 0.05 * u) / sr);
                    const double pw = 0.5 + 0.38 * fsin (lph);
                    x = 0.7f * (sawAt (ph, dt) - sawAt (wrap (ph + pw), dt));
                    break;
                }
                case Tri: x = (float) (1.0 - 4.0 * std::abs (ph - 0.5)); break;
                case Sine: x = fsin (ph); break;
                case Organ:
                {
                    static const float w[6] = { 1.0f, 0.7f, 0.45f, 0.35f, 0.2f, 0.15f };
                    static const double h[6] = { 1, 2, 3, 4, 6, 8 };
                    for (int k = 0; k < 6; ++k) if (hz * h[k] < sr * 0.45) x += w[k] * fsin (ph * h[k]);
                    x *= 0.4f;
                    break;
                }
                case Glass:
                {
                    auto& mph = L.ph2[(size_t) u];
                    mph = wrap (mph + std::min (0.45, hz * 3.5 / sr));
                    const float index = 0.8f + 1.4f * L.env;
                    x = fsin (ph + index * 0.15915494 * fsin (mph));
                    break;
                }
                case Noise: case Air: x = rnd() * 0.6f; break;
                default: break;
            }
            if (wave == Vox || wave == Air) { mono += x; continue; }
            const float pan = L.panOf[(size_t) u];
            lL += x * (1.0f - pan) * 0.5f;
            lR += x * (1.0f + pan) * 0.5f;
        }
        const float norm = 1.0f / std::sqrt ((float) U);
        if (wave == Vox)
        {
            // a sung "AH": three formants
            mono *= norm;
            const float f = svf (L.f1, mono, L.g1, 0.12f).bp * 1.0f + svf (L.f2, mono, L.g2, 0.1f).bp * 0.6f + svf (L.f3, mono, L.g3, 0.08f).bp * 0.35f;
            lL += f * 0.6f; lR += f * 0.6f;
        }
        else if (wave == Air)
        {
            // breath: noise through a narrow band on the note (and an octave up)
            mono *= norm;
            const float f = svf (L.f1, mono, L.g1, 0.03f).bp * 2.2f + svf (L.f2, mono, L.g2, 0.05f).bp * 1.0f;
            lL += f; lR += f;
        }
        else { lL *= norm; lR *= norm; }
        // its own filter, moved by its envelope
        if (ft != 0)
        {
            const auto a = svf (L.fl, lL, L.fg, L.fk), b = svf (L.fr, lR, L.fg, L.fk);
            lL = ft == 1 ? a.lp : ft == 2 ? a.bp : a.hp;
            lR = ft == 1 ? b.lp : ft == 2 ? b.bp : b.hp;
        }
        // its level: the envelope, LEVEL, the A<>B / C<>D crossfades, PAN
        float gain = L.env * q (Level) * 0.01f;
        if (l == 0) gain *= std::clamp (1.0f - ab, 0.0f, 1.0f);
        if (l == 1) gain *= std::clamp (1.0f + ab, 0.0f, 1.0f);
        if (l == 2) gain *= std::clamp (1.0f - cd, 0.0f, 1.0f);
        if (l == 3) gain *= std::clamp (1.0f + cd, 0.0f, 1.0f);
        L.level = gain;
        const float pan = q (Pan) / 50.0f;
        sl += lL * gain * std::min (1.0f, 1.0f - pan);
        sr_ += lR * gain * std::min (1.0f, 1.0f + pan);
    }
    // the MAIN FILTER (worked out each sample - it's the voice's, not each layer's)
    const int mt = (int) std::lround (p[MainType]);
    if (mt != 0)
    {
        if (v.mupd-- <= 0)
        {
            v.mupd = 15;
            const float oct = p[MainEnv] * 0.01f * 5.0f * v.fenv + p[MainKey] * 0.01f * (v.pitch - 60.0f) / 12.0f + p[MainVel] * 0.01f * (v.vel - 0.7f) * 2.0f + lfo[TCutoff];
            const float cut = std::clamp (p[MainCutoff] * std::pow (2.0f, oct), 20.0f, (float) (sr * 0.45));
            v.mg = (float) std::tan (kPadPi * cut / sr); v.mk = 2.0f - 1.9f * p[MainRes] * 0.01f;
        }
        const float g = v.mg, k = v.mk;
        auto run = [&] (Svf* st, float x)
        {
            const auto a = svf (st[0], x, g, k);
            if (mt == 1) return a.lp;
            if (mt == 2) return svf (st[1], a.lp, g, 1.4f).lp;
            return mt == 3 ? a.bp : a.hp;
        };
        sl = run (v.ml, sl); sr_ = run (v.mr, sr_);
    }
    float amp = velAmp * (1.0f + lfo[TLevel]);
    if (v.fading) { v.fade -= 1.0f / (float) (0.005 * sr); if (v.fade <= 0) { v.fading = false; v.fade = 0; for (auto& L : v.layers) L.stage = 0; } amp *= std::max (0.0f, v.fade); }
    outL += sl * amp; outR += sr_ * amp;
}

void PadSynth::render (float* left, float* right, int n)
{
    using namespace pad;
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    const float chorus = p[Chorus] * 0.01f, echo = p[Echo] * 0.01f, reverb = p[Reverb] * 0.01f;
    const int chLen = (int) chBuf[0].size(), ecLen = (int) echoBuf[0].size();
    const int ecDelay = std::clamp ((int) (p[EchoTime] * 0.001 * sr), 1, std::max (1, ecLen - 2));
    const float efb = p[EchoFeedback] * 0.01f;
    const float revFb = 0.72f + 0.27f * p[ReverbSize] * 0.01f;
    const float revDamp = 0.15f + 0.8f * p[ReverbTone] * 0.01f;
    const float revScale = 0.55f + 0.45f * p[ReverbSize] * 0.01f;
    const float level = p[Level_] * 0.01f * 0.5f;
    envSteps (filterEnv, p[FAttack], p[FDecay], p[FSustain] * 0.01f, p[FRelease], sr);
    for (int l = 0; l < kLayers; ++l)
        envSteps (layerEnv[l], p[(size_t) pad::p (l, Attack)], p[(size_t) pad::p (l, Decay)], p[(size_t) pad::p (l, Sustain)] * 0.01f, p[(size_t) pad::p (l, Release)], sr);
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);
        // the two LFOs, into their targets
        float lfo[7] {};
        for (int k = 0; k < 2; ++k)
        {
            const int base = k == 0 ? Lfo1Shape : Lfo2Shape;
            const double before = lfoPh[k];
            lfoPh[k] = wrap (lfoPh[k] + p[(size_t) base + 1] / sr);
            if (lfoPh[k] < before) lfoHold[k] = rnd();
            const double t = lfoPh[k];
            float x = 0;
            switch ((int) std::lround (p[(size_t) base]))
            {
                case 0: x = (float) std::sin (kTau * t); break;
                case 1: x = (float) (1.0 - 4.0 * std::abs (t - 0.5)); break;
                case 2: x = (float) (2.0 * t - 1.0); break;
                case 3: x = t < 0.5 ? 1.0f : -1.0f; break;
                default: x = lfoHold[k]; break;
            }
            lfoVal[k] += (x - lfoVal[k]) * 0.01f;   // (smoothed: no clicks from SQUARE and S&H)
            const float d = p[(size_t) base + 2] * 0.01f;
            switch ((int) std::lround (p[(size_t) base + 3]))
            {
                case TCutoff: lfo[TCutoff] += lfoVal[k] * d * 3.0f; break;            // (octaves)
                case TPitch:  lfo[TPitch] += lfoVal[k] * d * 1.0f; break;             // (semitones)
                case TLevel:  lfo[TLevel] += lfoVal[k] * d * 0.5f; break;
                case TPan:    lfo[TPan] += lfoVal[k] * d; break;
                case TAB:     lfo[TAB] += lfoVal[k] * d; break;
                case TCD:     lfo[TCD] += lfoVal[k] * d; break;
                default:      lfo[TWidth] += lfoVal[k] * d; break;
            }
        }
        float l = 0, r = 0;
        for (auto& v : voices) renderVoice (v, lfo, l, r);
        // CHORUS: two slowly moving taps, opposite in each side
        if (chorus > 0.001f)
        {
            chBuf[0][(size_t) chPos] = l; chBuf[1][(size_t) chPos] = r;
            chPh = wrap (chPh + p[ChorusRate] / sr);
            auto tap = [&] (int c, double ph)
            {
                const double dly = (0.007 + 0.005 * (0.5 + 0.5 * std::sin (kTau * ph))) * sr;
                double pos = chPos - dly; while (pos < 0) pos += chLen;
                const int a = (int) pos; const float f = (float) (pos - a);
                return chBuf[(size_t) c][(size_t) a] * (1.0f - f) + chBuf[(size_t) c][(size_t) ((a + 1) % chLen)] * f;
            };
            const float cl = tap (0, chPh), cr = tap (1, chPh + 0.5);
            l = l * (1.0f - 0.4f * chorus) + cl * 0.7f * chorus;
            r = r * (1.0f - 0.4f * chorus) + cr * 0.7f * chorus;
            chPos = (chPos + 1) % chLen;
        }
        // ECHO: a ping-pong, darker each time round
        if (echo > 0.001f)
        {
            const int rp = (echoPos - ecDelay + ecLen) % ecLen;
            const float el = echoBuf[0][(size_t) rp], er = echoBuf[1][(size_t) rp];
            echoLp[0] += (er - echoLp[0]) * 0.35f; echoLp[1] += (el - echoLp[1]) * 0.35f;
            echoBuf[0][(size_t) echoPos] = l + echoLp[0] * efb;
            echoBuf[1][(size_t) echoPos] = r + echoLp[1] * efb;
            l += el * echo * 0.6f; r += er * echo * 0.6f;
            echoPos = (echoPos + 1) % ecLen;
        }
        // REVERB: eight delays fed back through a mixing matrix, each a little darker
        if (reverb > 0.001f)
        {
            static const int lens[kRev] = { 1031, 1327, 1523, 1801, 2063, 2339, 2593, 2879 };
            float o[kRev];
            for (int k = 0; k < kRev; ++k)
            {
                const int len = std::max (8, std::min ((int) rev[k].size() - 2, (int) (lens[k] * revScale * sr / 48000.0)));
                const int rp = (revPos[k] - len + (int) rev[k].size()) % (int) rev[k].size();
                o[k] = rev[k][(size_t) rp];
            }
            // (a Hadamard-style mix: the sum, less twice each)
            float sum = 0; for (float x : o) sum += x;
            const float in = (l + r) * 0.25f;
            float wl = 0, wr = 0;
            for (int k = 0; k < kRev; ++k)
            {
                float x = (o[k] - sum * 0.25f) * revFb;
                revLp[k] += (x - revLp[k]) * revDamp;
                const int sz = (int) rev[k].size();
                rev[k][(size_t) revPos[k]] = revLp[k] + in * (k % 2 == 0 ? 1.0f : -1.0f);
                revPos[k] = (revPos[k] + 1) % sz;
                if (k % 2 == 0) wl += o[k]; else wr += o[k];
            }
            l = l * (1.0f - 0.5f * reverb) + wl * 0.35f * reverb;
            r = r * (1.0f - 0.5f * reverb) + wr * 0.35f * reverb;
        }
        // WIDTH (and the LFO's), the auto-PAN, LEVEL
        const float width = std::clamp (p[Width] * 0.01f + lfo[TWidth], 0.0f, 1.8f);
        const float mid = (l + r) * 0.5f, side = (l - r) * 0.5f * width;
        l = mid + side; r = mid - side;
        const float pan = std::clamp (lfo[TPan], -1.0f, 1.0f);
        l *= std::min (1.0f, 1.0f - pan); r *= std::min (1.0f, 1.0f + pan);
        float ol = l * level, orr = r * level;
        auto lim = [] (float x) { const float a = std::abs (x); return a < 1.5f ? x : std::copysign (1.5f + 0.5f * std::tanh ((a - 1.5f) * 2.0f), x); };
        left[i] = std::isfinite (ol) ? lim (ol) : 0.0f;
        right[i] = std::isfinite (orr) ? lim (orr) : 0.0f;
    }
    events.erase (events.begin(), events.begin() + (long) ev);
    for (auto& e : events) e.offset = std::max (0, e.offset - n);
}
} // namespace prv
