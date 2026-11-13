// Paula's Revenge - REESE (see ReeseSynth.h)
#include "ReeseSynth.h"
#include "BuiltinSynths.h"
#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    constexpr double kTwoPiR = 6.283185307179586;
    // a band-limited step's correction (polyBLEP): t the phase 0..1, dt the step a sample
    inline float blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }
    inline float saw (double t, double dt) { return (float) (2.0 * t - 1.0) - blep (t, dt); }
}

const std::vector<SynthParam>& reeseParams()
{
    // (v0.82: five tabs - "N:TAB>BOX")
    static const char* md = "0:OSC>MODE";
    static const char* os = "0:OSC>OSCILLATORS";
    static const char* sb = "0:OSC>SUB";
    static const char* mv = "1:MOVEMENT|MOVE>MOVEMENT";
    static const char* ho = "1:MOVEMENT|MOVE>HOOVER";
    static const char* wb = "1:MOVEMENT|MOVE>WOBBLE";
    static const char* fl = "2:FILTER>FILTER";
    static const char* fe = "2:FILTER>FILTER ENV";
    static const char* dr = "3:FX>DRIVE";
    static const char* ph = "3:FX>PHASER";
    static const char* fg = "3:FX>FLANGER";
    static const char* st = "3:FX>STEREO";
    static const char* en = "4:AMP>ENVELOPE";
    static const char* ou = "4:AMP>OUTPUT";
    static const std::vector<SynthParam> ps {
        { "MODE",      md, 0, 3, 0,       3, "",   "LEGATO|ALWAYS|OFF|POLY" },
        { "GLIDE",     md, 1, 2000, 120,  1, "MS", "" },
        { "TYPE",      os, 0, 1, 0,       3, "",   "REESE|HOOVER" },
        { "VOICES",    os, 1, 7, 3,       2, "",   "" },
        { "DETUNE",    os, 0, 100, 25,    0, "C",  "" },
        { "SPREAD",    os, 0, 100, 60,    0, "%",  "" },
        { "OCTAVE",    os, -3, 2, -1,     2, "",   "" },
        { "MOVE RATE", mv, 0.03f, 5, 0.15f, 1, "HZ", "" },
        { "MOVE",      mv, 0, 100, 40,    0, "%",  "" },
        { "BEND",      ho, 0, 24, 0,      0, "ST", "" },
        { "BEND TIME", ho, 5, 2000, 180,  1, "MS", "" },
        { "PWM",       ho, 0, 100, 60,    0, "%",  "" },
        { "PWM RATE",  ho, 0.1f, 10, 0.8f, 1, "HZ", "" },
        { "SUB",       sb, 0, 100, 30,    0, "%",  "" },
        { "CUTOFF",    fl, 40, 18000, 900, 1, "HZ", "" },
        { "RES",       fl, 0, 100, 20,    0, "%",  "" },
        { "ENV AMT",   fe, -100, 100, 35, 0, "%",  "" },
        { "ENV DECAY", fe, 5, 3000, 350,  1, "MS", "" },
        { "WOB RATE",  wb, 0.1f, 16, 2,   1, "HZ", "" },
        { "WOBBLE",    wb, 0, 100, 0,     0, "%",  "" },
        { "DRIVE",     dr, 0, 100, 25,    0, "%",  "" },
        { "CHORUS",    st, 0, 100, 0,     0, "%",  "" },
        { "ATTACK",    en, 0.5f, 2000, 3, 1, "MS", "" },
        { "DECAY",     en, 5, 5000, 400,  1, "MS", "" },
        { "SUSTAIN",   en, 0, 100, 100,   0, "%",  "" },
        { "RELEASE",   en, 5, 5000, 120,  1, "MS", "" },
        { "VELOCITY",  ou, 0, 100, 40,    0, "%",  "" },
        { "LEVEL",     ou, 0, 100, 75,    0, "%",  "" },
        { "SUB OCT",   sb, 0, 1, 0,       3, "",   "-1 OCT|-2 OCT" },
        { "SUB SHAPE", sb, 0, 3, 0,       3, "",   "AUTO|SINE|TRI|SQUARE" },
        // (v0.82)
        { "WAVE",      os, 0, 3, 0,       3, "",   "SAW|SQUARE|SAW+SQ|TRI" },
        { "SPREAD BY", os, 0, 2, 0,       3, "",   "EVEN|OUTSIDE|RANDOM" },
        { "STACK",     os, 0, 3, 0,       3, "",   "OFF|+OCT|-OCT|+5TH" },
        { "NOISE",     os, 0, 100, 0,     0, "%",  "" },
        { "DRIFT",     mv, 0, 100, 0,     0, "%",  "" },
        { "TYPE",      fl, 0, 4, 0,       3, "",   "LOW 24|LOW 12|BAND|HIGH|NOTCH" },
        { "KEY TRACK", fl, 0, 100, 0,     0, "%",  "" },
        { "DRIVE",     fl, 0, 100, 0,     0, "%",  "" },
        { "ENV ATK",   fe, 0.5f, 2000, 2, 1, "MS", "" },
        { "SHAPE",     wb, 0, 5, 0,       3, "",   "SINE|TRI|SAW|RAMP|SQUARE|S&H" },
        { "SYNC",      wb, 0, 7, 0,       3, "",   "FREE|1 BAR|1/2|1/4|1/8|1/16|1/4 T|1/8 T" },
        { "TARGET",    wb, 0, 5, 0,       3, "",   "CUTOFF|PITCH|LEVEL|DETUNE|PHASER|FLANGER" },
        { "RESTART",   wb, 0, 1, 0,       3, "",   "FREE|EACH NOTE" },
        { "TYPE",      dr, 0, 4, 0,       3, "",   "WARM|HARD|FOLD|RECTIFY|CRUSH" },
        { "SQUASH",    dr, 0, 100, 0,     0, "%",  "" },
        { "MIX",       ph, 0, 100, 0,     0, "%",  "" },
        { "RATE",      ph, 0.02f, 10, 0.3f, 1, "HZ", "" },
        { "DEPTH",     ph, 0, 100, 70,    0, "%",  "" },
        { "FEEDBACK",  ph, 0, 95, 50,     0, "%",  "" },
        { "MIX",       fg, 0, 100, 0,     0, "%",  "" },
        { "RATE",      fg, 0.02f, 10, 0.2f, 1, "HZ", "" },
        { "DEPTH",     fg, 0, 100, 60,    0, "%",  "" },
        { "FEEDBACK",  fg, -95, 95, 60,   0, "%",  "" },
        { "WIDTH",     st, 0, 150, 100,   0, "%",  "" },
        { "MONO BASS", st, 0, 300, 0,     0, "HZ", "" },
    };
    return ps;
}

const char* reesePresetName (int i)
{
    static const char* n[kReesePresets] = { "CLASSIC REESE", "DARK ROLLER", "NEURO GROWL", "HOOVER", "WOBBLE REESE",
                                            "FOLDED", "LIQUID REESE", "CRUSHED", "TECHSTEP", "SUB REESE" };
    return n[std::clamp (i, 0, kReesePresets - 1)];
}

std::vector<float> reesePreset (int i)
{
    using namespace reese;
    std::vector<float> v;
    for (const auto& q : reeseParams()) v.push_back (q.def);   // (CLASSIC REESE: as it starts)
    auto set = [&] (std::initializer_list<std::pair<int, float>> l) { for (auto& [k, x] : l) v[(size_t) k] = x; };
    switch (std::clamp (i, 0, kReesePresets - 1))
    {
        case 1: set ({ { Voices, 5 }, { Detune, 18 }, { Cutoff, 380 }, { Resonance, 30 }, { EnvAmount, 20 }, { MoveDepth, 60 }, { MoveRate, 0.08f },
                       { SubLevel, 45 }, { Drive, 35 }, { Drift, 30 }, { MonoBass, 120 } }); break;
        case 2: set ({ { Voices, 4 }, { Detune, 30 }, { OscWave, WSawSquare }, { Cutoff, 700 }, { Resonance, 45 }, { FilterDrive, 50 },
                       { WobbleDepth, 45 }, { WobSync, 4 }, { WobTarget, TPhaser }, { WobShape, 4 }, { PhaserMix, 70 }, { PhaserFb, 75 },
                       { FlangeMix, 35 }, { FlangeFb, 80 }, { DriveType, Fold }, { Drive, 45 }, { Squash, 50 }, { MonoBass, 100 } }); break;
        case 3: set ({ { Type, Hoover }, { Voices, 5 }, { Detune, 35 }, { Octave, 0 }, { Bend, 12 }, { BendTime, 220 }, { Pwm, 70 }, { Cutoff, 4000 },
                       { EnvAmount, 10 }, { Chorus, 70 }, { Drive, 20 }, { SubLevel, 25 } }); break;
        case 4: set ({ { Voices, 4 }, { Cutoff, 250 }, { Resonance, 55 }, { EnvAmount, 0 }, { WobbleDepth, 70 }, { WobSync, 3 }, { WobShape, 0 },
                       { WobRetrig, 1 }, { Drive, 40 }, { SubLevel, 40 }, { MonoBass, 110 } }); break;
        case 5: set ({ { Voices, 3 }, { Detune, 22 }, { Cutoff, 1200 }, { DriveType, Fold }, { Drive, 60 }, { Squash, 40 }, { MoveDepth, 50 } }); break;
        case 6: set ({ { Voices, 6 }, { Detune, 14 }, { Spread, 85 }, { Cutoff, 520 }, { Resonance, 10 }, { EnvAmount, 15 }, { Drive, 10 },
                       { Chorus, 45 }, { Drift, 40 }, { MoveRate, 0.06f }, { Attack, 15 }, { Release, 300 }, { MonoBass, 150 } }); break;
        case 7: set ({ { Voices, 3 }, { Detune, 28 }, { OscWave, WSquare }, { Cutoff, 1500 }, { DriveType, Crush }, { Drive, 55 }, { Squash, 30 } }); break;
        case 8: set ({ { Voices, 5 }, { Detune, 40 }, { DetuneShape, 1 }, { Stack, 1 }, { Cutoff, 900 }, { Resonance, 35 }, { KeyTrack, 50 },
                       { DriveType, Hard }, { Drive, 50 }, { Squash, 70 }, { FlangeMix, 25 }, { FlangeFb, -70 }, { MonoBass, 90 } }); break;
        case 9: set ({ { Voices, 2 }, { Detune, 10 }, { Cutoff, 300 }, { EnvAmount, 10 }, { SubLevel, 80 }, { SubOctave, 0 }, { SubShape, 1 },
                       { Drive, 15 }, { MonoBass, 200 }, { Width, 60 } }); break;
        default: break;
    }
    return v;
}

ReeseSynth::ReeseSynth()
{
    const auto& ps = reeseParams();
    for (int i = 0; i < reese::Count; ++i) p[(size_t) i] = ps[(size_t) i].def;
    events.reserve (256);
    // the oscillators start spread through their cycle (as analogue ones would be): a fat sound from the first note
    uint32_t s = 4242;
    for (auto& v : voices) for (auto& x : v.ph) { s = s * 1664525u + 1013904223u; x = (s >> 8) / 16777216.0; }
    // (SPREAD BY: RANDOM - each oscillator's own fixed place, the same every time)
    for (int o = 0; o < kOsc; ++o) randomDetune[o] = o == 0 ? 0.0f : rnd();
}

float ReeseSynth::rnd()
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (float) (seed & 0xffffff) / (float) 0x800000 - 1.0f;
}

void ReeseSynth::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& b : chBuf) b.assign ((size_t) (sr * 0.04) + 4, 0.0f);
    for (auto& b : flBuf) b.assign ((size_t) (sr * 0.02) + 4, 0.0f);
    chPos = flPos = 0;
}

void ReeseSynth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }

void ReeseSynth::update (const Instrument& in)
{
    bendRange = std::clamp (in.bendRange, 1, 48);
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
    base = p;   // (what an envelope goes back to when it lets go)
}

void ReeseSynth::setParameter (int i, float v01)
{
    const auto& ps = reeseParams();
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void ReeseSynth::noteOn (int note, int vel, int offset) { if (events.size() < 1024) events.push_back ({ offset, vel > 0 ? 0 : 1, note, vel }); }
void ReeseSynth::noteOff (int note, int offset)         { if (events.size() < 1024) events.push_back ({ offset, 1, note, 0 }); }
void ReeseSynth::allNotesOff (int offset)               { if (events.size() < 1024) events.push_back ({ offset, 2, 0, 0 }); }
void ReeseSynth::pitchBend (int v, int offset)          { if (events.size() < 1024) events.push_back ({ offset, 3, v, 0 }); }

void ReeseSynth::startVoice (Voice& v, int note, int vel, bool retrigger, bool glide)
{
    if (v.stage == 0 && ! v.fading) { v.pitch = (float) note; glide = false; }
    v.target = (float) note;
    if (glide && p[reese::Glide] > 1.5f) v.glideStep = (v.target - v.pitch) / (float) std::max (1.0, p[reese::Glide] * 0.001 * sr);
    else { v.pitch = v.target; v.glideStep = 0; }
    v.note = note; v.gate = true;
    if (retrigger)
    {
        v.stage = 1;                       // from the level it's at (the oscillators run on: no click)
        v.vel = (float) vel / 127.0f;
        v.bendEnv = -p[reese::Bend];       // HOOVER's swoop up into the note
        v.fenv = std::max (v.fenv, 0.0f);
        v.tick = 0; v.upd = 0;             // (the filter envelope's attack starts again)
        if (std::lround (p[reese::WobRetrig]) == 1) { wob = 0; wobHold = rnd(); }   // (WOBBLE: RESTART EACH NOTE)
    }
    else if (v.stage == 0 || v.stage == 3) v.stage = 2;
    v.order = ++orderCount;
}

void ReeseSynth::handle (const Event& e)
{
    const int mode = (int) std::lround (p[reese::Mode]);
    if (e.type == 3) { bend = (float) (e.note - 8192) / 8192.0f * (float) bendRange; return; }
    if (e.type == 2) { held.clear(); pendingRelease = -1; for (auto& v : voices) releaseVoice (v); return; }
    if (mode == reese::Poly)
    {
        if (e.type == 1) { for (auto& v : voices) if (v.gate && v.note == e.note && ! v.fading) releaseVoice (v); return; }
        for (auto& v : voices) if (v.note == e.note && v.stage != 0 && ! v.fading) { startVoice (v, e.note, e.vel, true, false); return; }
        int busy = 0, oldest = -1;
        for (int i = 0; i < kVoices; ++i)
            if (voices[i].stage != 0 && ! voices[i].fading) { ++busy; if (oldest < 0 || voices[i].order < voices[oldest].order) oldest = i; }
        if (busy >= kVoices - 1 && oldest >= 0) voices[oldest].fading = true;
        Voice* free = nullptr;
        for (auto& v : voices) if (v.stage == 0 && ! v.fading) { free = &v; break; }
        if (free == nullptr) for (auto& v : voices) if (v.fading && (free == nullptr || v.fade < free->fade)) free = &v;
        if (free == nullptr) return;
        // a fresh voice: silent filter, the envelope from 0 (its oscillators keep their phases - nothing to jump from)
        free->stage = 0; free->env = 0; free->fade = 1; free->fading = false; free->fenv = 0;
        for (auto& a : free->s1) a[0] = a[1] = 0;
        for (auto& a : free->s2) a[0] = a[1] = 0;
        startVoice (*free, e.note, e.vel, true, false);
        return;
    }
    Voice& v = voices[0];
    if (e.type == 1)
    {
        held.erase (std::remove (held.begin(), held.end(), e.note), held.end());
        if (e.note != v.note || ! v.gate) return;
        if (! held.empty()) { startVoice (v, held.back(), (int) (v.vel * 127.0f), false, mode != reese::Retrigger); return; }
        pendingRelease = std::max (1, (int) (sr * 0.003));
        return;
    }
    const bool overlapping = ! held.empty() || pendingRelease >= 0;
    held.erase (std::remove (held.begin(), held.end(), e.note), held.end());
    held.push_back (e.note);
    pendingRelease = -1;
    const bool sounding = v.stage != 0;
    const bool legato = overlapping && sounding;
    const bool glide = (mode == reese::Legato && legato) || (mode == reese::Always && sounding);
    startVoice (v, e.note, e.vel, mode == reese::Retrigger || ! legato, glide);
}

void ReeseSynth::renderVoice (Voice& v, float& outL, float& outR, float moveNow, float wobNow, float pwmNow)
{
    using namespace reese;
    if (v.stage == 0 && ! v.fading) return;
    if (v.glideStep != 0.0f)
    {
        v.pitch += v.glideStep;
        if ((v.glideStep > 0 && v.pitch >= v.target) || (v.glideStep < 0 && v.pitch <= v.target)) { v.pitch = v.target; v.glideStep = 0; }
    }
    if (v.bendEnv < -0.0005f) v.bendEnv *= (float) std::exp (-1.0 / std::max (1.0, p[BendTime] * 0.001 * sr * 0.3));
    else v.bendEnv = 0;
    const int wobTarget = (int) std::lround (p[WobTarget]);
    const float wobAmt = wobNow * p[WobbleDepth] * 0.01f;
    const double semis = v.pitch + v.bendEnv + bend + p[Octave] * 12.0f + (wobTarget == TPitch ? wobAmt * 12.0f : 0.0f);
    const double hz = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
    const int n = std::clamp ((int) std::lround (p[Voices]), 1, kOsc);
    const bool hoover = std::lround (p[Type]) == Hoover;
    const int wave = (int) std::lround (p[OscWave]);
    const int spreadBy = (int) std::lround (p[DetuneShape]);
    const int stack = (int) std::lround (p[Stack]);
    // DETUNE spread across the oscillators, MOVEMENT breathing it wider and narrower (and the WOBBLE, on DETUNE)
    float det = p[Detune] * (1.0f + moveNow * p[MoveDepth] * 0.01f * 0.8f);
    if (wobTarget == TDetune) det *= std::max (0.0f, 1.0f + wobAmt * 1.5f);
    const float spread = p[Spread] * 0.01f;
    const float pw = 0.5f + 0.42f * pwmNow * p[Pwm] * 0.01f;
    const float drift = p[Drift] * 0.01f;
    float l = 0, r = 0;
    for (int o = 0; o < n; ++o)
    {
        const float pos = n > 1 ? (float) o / (float) (n - 1) * 2.0f - 1.0f : 0.0f;   // -1..1
        // where its detune sits: EVEN, bunched in the middle with the OUTSIDE far out, or RANDOM
        const float at = spreadBy == 1 ? pos * std::abs (pos) * 1.4f : spreadBy == 2 ? randomDetune[o] : pos;
        float cents = at * det;
        if (drift > 0.0f && (v.upd & 7) == 0)
        {
            v.drift[o] += (v.driftTo[o] - v.drift[o]) * 0.004f;
            if (std::abs (v.driftTo[o] - v.drift[o]) < 0.02f) v.driftTo[o] = rnd();
        }
        cents += v.drift[o] * drift * 25.0f;
        double f = hz * std::pow (2.0, cents / 1200.0) * (1.0 + 0.0007 * o);   // (a hair apart even at 0)
        // STACK: every other one an octave up, down, or a fifth up
        if ((o & 1) == 1 && stack != 0) f *= stack == 1 ? 2.0 : stack == 2 ? 0.5 : 1.4983;
        const double dt = std::min (0.45, f / sr);
        double& ph = v.ph[o];
        ph += dt; if (ph >= 1.0) ph -= 1.0;
        float x = saw (ph, dt);
        if (hoover)
        {
            // a pulse-saw: the saw less itself shifted by the pulse width (PWM moves it) - the hoover's hollow buzz
            double t2 = ph + pw; if (t2 >= 1.0) t2 -= 1.0;
            x = 0.6f * x + 0.7f * (x - saw (t2, dt));
        }
        else if (wave != WSaw)
        {
            double t2 = ph + 0.5; if (t2 >= 1.0) t2 -= 1.0;
            const float sq = 0.7f * (x - saw (t2, dt));
            if (wave == WSquare) x = sq;
            else if (wave == WSawSquare) x = 0.6f * x + 0.6f * sq;
            else x = (float) (1.0 - 4.0 * std::abs (ph - 0.5));
        }
        const float pan = pos * spread;   // -1..1
        l += x * (1.0f - std::max (0.0f, pan)); r += x * (1.0f + std::min (0.0f, pan));
    }
    const float norm = 1.0f / std::sqrt ((float) n);
    l *= norm; r *= norm;
    if (p[Noise] > 0.0f) { const float nz = rnd() * p[Noise] * 0.006f; l += nz; r += nz; }
    // SUB: one or two octaves below. AUTO: a sine on REESE, a square on HOOVER. A sine or triangle is added after
    // the filter (v0.73), so the resonance and the filter's sweep never ring on it - the low end stays a plain,
    // steady tone; a square goes through the filter with the rest (it's part of the buzz)
    float subOut = 0;
    {
        const double subHz = 440.0 * std::pow (2.0, (v.pitch + v.bendEnv + bend + p[Octave] * 12.0f - 69.0) / 12.0);
        const double dt = (subHz * (std::lround (p[SubOctave]) >= 1 ? 0.25 : 0.5)) / sr;
        v.phSub += dt; if (v.phSub >= 1.0) v.phSub -= 1.0;
        int shape = (int) std::lround (p[SubShape]);
        if (shape == 0) shape = hoover ? 3 : 1;
        float s;
        if (shape == 3) { double t2 = v.phSub + 0.5; if (t2 >= 1.0) t2 -= 1.0; s = 0.5f * (saw (v.phSub, dt) - saw (t2, dt)); }
        else if (shape == 2) { double q = v.phSub + 0.25; if (q >= 1.0) q -= 1.0; s = (float) (q < 0.5 ? 4.0 * q - 1.0 : 3.0 - 4.0 * q); }   // (from 0, like the sine)
        else s = (float) std::sin (kTwoPiR * v.phSub);
        s *= p[SubLevel] * 0.01f;
        if (shape == 3) { l += s; r += s; }
        else subOut = s;
    }
    // the amp envelope (from where it is), the filter envelope (ENV ATK up, ENV DECAY down)
    const float sus = p[Sustain] * 0.01f;
    switch (v.stage)
    {
        case 1: v.env += (float) (1.0 / std::max (1.0, p[Attack] * 0.001 * sr)); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; } break;
        case 2: v.env = sus + (v.env - sus) * (float) std::exp (-1.0 / std::max (1.0, p[Decay] * 0.001 * sr * 0.3)); break;
        case 3: v.env *= (float) std::exp (-1.0 / std::max (1.0, p[Release] * 0.001 * sr * 0.3));
                if (v.env < 0.00003f) { v.env = 0; v.stage = 0; } break;
        default: break;
    }
    const int atk = std::max (1, (int) (sr * p[EnvAttack] * 0.001));
    if (v.tick < atk) { v.fenv += (1.0f - v.fenv) * std::min (1.0f, 1.92f / (float) atk); ++v.tick; }   // (2 MS: as before v0.82)
    else v.fenv *= (float) std::exp (-1.0 / std::max (1.0, p[FilterDecay] * 0.001 * sr * 0.3));
    // the filter: its cutoff from CUTOFF, the envelope, KEY TRACK, the wobble (worked out every 16 samples)
    const int mode = (int) std::lround (p[FilterMode]);
    if (v.upd-- <= 0 || v.a1 == 0 || v.mode != mode)
    {
        const float oct = p[EnvAmount] * 0.01f * 6.0f * v.fenv + (wobTarget == TCutoff ? wobAmt * 3.0f : 0.0f)
                        + p[KeyTrack] * 0.01f * (v.pitch - 48.0f) / 12.0f;
        v.cutNow = std::clamp (p[Cutoff] * std::pow (2.0f, oct), 30.0f, (float) (sr * 0.45));
        v.g = (float) std::tan (3.14159265358979 * v.cutNow / sr);
        v.k = 2.0f - 1.9f * std::clamp (p[Resonance] * 0.01f, 0.0f, 1.0f);
        v.a1 = 1.0f / (1.0f + v.g * (v.g + v.k)); v.a2 = v.g * v.a1; v.a3 = v.g * v.a2;
        v.upd = 15; v.mode = mode;
    }
    struct Out { float lp, bp, hp; };
    auto svf = [] (float x, float& ic1, float& ic2, float a1, float a2, float a3, float k)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return Out { v2, v1, x - k * v1 - v2 };
    };
    // FILTER DRIVE: into the filter, so the resonance growls
    if (p[FilterDrive] > 0.0f)
    {
        const float fd = 1.0f + p[FilterDrive] * 0.04f;
        l = std::tanh (l * fd) / std::sqrt (fd); r = std::tanh (r * fd) / std::sqrt (fd);
    }
    auto run = [&] (float x, int c)
    {
        if (mode == Low24)
        {
            // (the first stage without resonance: the peak comes from the second, as a ladder's would)
            const float b1 = 1.0f / (1.0f + v.g * (v.g + 1.414f)), b2 = v.g * b1, b3 = v.g * b2;
            return svf (svf (x, v.s1[c][0], v.s1[c][1], b1, b2, b3, 1.414f).lp, v.s2[c][0], v.s2[c][1], v.a1, v.a2, v.a3, v.k).lp;
        }
        const auto o = svf (x, v.s2[c][0], v.s2[c][1], v.a1, v.a2, v.a3, v.k);
        return mode == Low12 ? o.lp : mode == Band ? o.bp * (0.6f + v.k * 0.4f) : mode == High ? o.hp : o.lp + o.hp;
    };
    l = run (l, 0); r = run (r, 1);
    l += subOut; r += subOut;
    const float vs = p[VelSens] * 0.01f;
    float a = v.env * (1.0f - vs + vs * v.vel);
    if (wobTarget == TLevel) a *= 1.0f - p[WobbleDepth] * 0.01f * (0.5f - 0.5f * wobNow);
    if (v.fading)
    {
        v.fade -= (float) (1.0 / (0.004 * sr));
        if (v.fade <= 0.0f) { v.fading = false; v.stage = 0; v.env = 0; v.note = -1; v.gate = false; return; }
        a *= v.fade;
    }
    outL += l * a; outR += r * a;
}

void ReeseSynth::render (float* left, float* right, int n)
{
    using namespace reese;
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    const float drive = p[Drive] * 0.01f;
    const float dg = 1.0f + drive * 12.0f;
    const float dnorm = drive > 0 ? 1.0f / std::tanh (dg * 0.7f) : 1.0f;
    const int dtype = (int) std::lround (p[DriveType]);
    const float chorus = p[Chorus] * 0.01f;
    const float level = p[Level] * 0.01f * 0.55f;
    // the WOBBLE's rate: its own, or the song's tempo (beats a cycle)
    static const double perBeat[8] = { 0, 0.25, 0.5, 1, 2, 4, 1.5, 3 };
    const int sync = std::clamp ((int) std::lround (p[WobSync]), 0, 7);
    const double wRate = sync == 0 ? p[WobbleRate] : songBpm / 60.0 * perBeat[sync];
    const double mStep = p[MoveRate] / sr, wStep = wRate / sr, pStep = p[PwmRate] / sr;
    const int wShape = (int) std::lround (p[WobShape]);
    const int wobTarget = (int) std::lround (p[WobTarget]);
    const int chLen = (int) chBuf[0].size(), flLen = (int) flBuf[0].size();
    const float phMix = p[PhaserMix] * 0.01f, flMix = p[FlangeMix] * 0.01f, squash = p[Squash] * 0.01f;
    const float phFbAmt = p[PhaserFb] * 0.01f, flFbAmt = p[FlangeFb] * 0.01f;
    const float width = p[Width] * 0.01f;
    const float monoHz = p[MonoBass];
    const float mbA = monoHz > 1.0f ? 1.0f - (float) std::exp (-kTwoPiR * monoHz / sr) : 0.0f;
    const float sqAtt = 1.0f - (float) std::exp (-1.0 / (0.005 * sr)), sqRel = 1.0f - (float) std::exp (-1.0 / (0.08 * sr));
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);
        if (pendingRelease >= 0 && --pendingRelease < 0) releaseVoice (voices[0]);
        move += mStep; if (move >= 1) move -= 1;
        const double wBefore = wob;
        wob += wStep; if (wob >= 1) wob -= 1;
        if (wob < wBefore) wobHold = rnd();
        pwmPh += pStep; if (pwmPh >= 1) pwmPh -= 1;
        const float moveNow = (float) std::sin (kTwoPiR * move);
        float w;
        switch (wShape)
        {
            case 1: w = (float) (wob < 0.5 ? 4.0 * wob - 1.0 : 3.0 - 4.0 * wob); break;
            case 2: w = (float) (1.0 - 2.0 * wob); break;          // SAW: down
            case 3: w = (float) (2.0 * wob - 1.0); break;          // RAMP: up
            case 4: w = wob < 0.5 ? 1.0f : -1.0f; break;
            case 5: w = wobHold; break;
            default: w = (float) std::sin (kTwoPiR * wob); break;
        }
        // (the hard shapes softened a hair: no clicks)
        wobSmooth += (w - wobSmooth) * (wShape >= 2 ? 0.02f : 1.0f);
        const float wobNow = wobSmooth;
        wobNowValue = wobNow;
        const float pwmNow = (float) (pwmPh < 0.5 ? 4.0 * pwmPh - 1.0 : 3.0 - 4.0 * pwmPh);
        float l = 0, r = 0;
        for (auto& v : voices) renderVoice (v, l, r, moveNow, wobNow, pwmNow);
        float* ch[2] = { &l, &r };
        if (drive > 0)
        {
            const bool crushNow = dtype == Crush && (crushCount++ % std::max (1, 1 + (int) (drive * 10.0f))) == 0;
            for (int c = 0; c < 2; ++c)
            {
                float x = *ch[c];
                switch (dtype)
                {
                    case Hard:    x = std::clamp (x * dg * 0.7f, -1.0f, 1.0f) * 0.9f; break;
                    case Fold:    x = std::sin (x * dg * 1.2f) * 0.9f; break;
                    case Rectify: { const float y = std::tanh (dg * 0.7f * x) * dnorm; x = 0.55f * y + 0.65f * std::abs (y); break; }
                    case Crush:
                    {
                        const float steps = std::pow (2.0f, 12.0f - drive * 9.0f);
                        if (crushNow) crushHold[c] = std::round (std::tanh (x * (1.0f + drive * 3.0f)) * steps) / steps;
                        x = crushHold[c];
                        break;
                    }
                    default:      x = std::tanh (dg * 0.7f * x) * dnorm; break;
                }
                const float y = x - dcX[c] + 0.9995f * dcY[c];
                dcX[c] = x; dcY[c] = y; *ch[c] = y;
            }
        }
        // PHASER: six all-passes swept by its own LFO (or the WOBBLE), with feedback
        if (phMix > 0.001f)
        {
            phPh += p[PhaserRate] / sr; if (phPh >= 1) phPh -= 1;
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = wobTarget == TPhaser ? wobNow * p[WobbleDepth] * 0.01f
                                                       : (float) std::sin (kTwoPiR * (phPh + c * 0.25)) * p[PhaserDepth] * 0.01f;
                const float hz = 900.0f * std::pow (2.0f, lfo * 2.5f);
                const float t = (float) std::tan (3.14159265358979 * std::min (hz, (float) (sr * 0.45)) / sr);
                const float coef = (t - 1.0f) / (t + 1.0f);
                float x = *ch[c] + phFb[c] * phFbAmt;
                for (auto& a : phs[c]) { const float y = coef * x + a.z; a.z = x - coef * y; x = y; }
                phFb[c] = std::clamp (x, -2.0f, 2.0f);
                *ch[c] = *ch[c] * (1.0f - phMix * 0.5f) + x * phMix * 0.7f;
            }
        }
        // FLANGER: a short delay sweeping, fed back (negative FEEDBACK: the hollow one)
        if (flMix > 0.001f && flLen > 8)
        {
            flPh += p[FlangeRate] / sr; if (flPh >= 1) flPh -= 1;
            float wet[2];
            for (int c = 0; c < 2; ++c)
            {
                const float m = wobTarget == TFlanger ? 0.5f + 0.5f * wobNow * p[WobbleDepth] * 0.01f
                                                      : 0.5f + 0.5f * (float) std::sin (kTwoPiR * (flPh + c * 0.25)) * p[FlangeDepth] * 0.01f;
                const double d = (0.0006 + 0.008 * m) * sr;
                double rp = flPos - d; while (rp < 0) rp += flLen;
                const int i0 = (int) rp; const float fr = (float) (rp - i0);
                const float a = flBuf[(size_t) c][(size_t) i0], b = flBuf[(size_t) c][(size_t) ((i0 + 1) % flLen)];
                wet[c] = a + (b - a) * fr;
            }
            for (int c = 0; c < 2; ++c)
            {
                flBuf[(size_t) c][(size_t) flPos] = std::clamp (*ch[c] + wet[c] * flFbAmt, -3.0f, 3.0f);
                *ch[c] = *ch[c] * (1.0f - flMix * 0.4f) + wet[c] * flMix * 0.8f;
            }
            if (++flPos >= flLen) flPos = 0;
        }
        if (chorus > 0 && chLen > 8)
        {
            // two delays, 7 ms +/- 3 ms, moving at different rates: the wide, swirling hoover / reese chorus
            chBuf[0][(size_t) chPos] = l; chBuf[1][(size_t) chPos] = r;
            chPh += 0.31 / sr; if (chPh >= 1) chPh -= 1;
            float wet[2];
            for (int c = 0; c < 2; ++c)
            {
                const double d = (0.007 + 0.003 * std::sin (kTwoPiR * (chPh + c * 0.25))) * sr;
                double rp = chPos - d; while (rp < 0) rp += chLen;
                const int i0 = (int) rp; const float fr = (float) (rp - i0);
                const float a = chBuf[(size_t) (1 - c)][(size_t) i0], b = chBuf[(size_t) (1 - c)][(size_t) ((i0 + 1) % chLen)];
                wet[c] = a + (b - a) * fr;
            }
            if (++chPos >= chLen) chPos = 0;
            l += wet[0] * chorus * 0.8f; r += wet[1] * chorus * 0.8f;
            const float g = 1.0f / (1.0f + chorus * 0.5f); l *= g; r *= g;
        }
        // SQUASH: quiet parts up, loud parts down - everything in your face
        if (squash > 0.001f)
        {
            const float lv = std::max (std::abs (l), std::abs (r));
            sqEnv += (lv - sqEnv) * (lv > sqEnv ? sqAtt : sqRel);
            const float want = std::clamp (std::pow (0.35f / std::max (sqEnv, 0.003f), squash * 0.8f), 0.3f, 6.0f);
            sqGain += (want - sqGain) * 0.002f;
            l *= sqGain; r *= sqGain;
        }
        // WIDTH, and MONO BASS: below it, the same in both sides (a club's low end stays in the middle)
        if (std::abs (width - 1.0f) > 0.001f)
        {
            const float mid = (l + r) * 0.5f, side = (l - r) * 0.5f * width;
            l = mid + side; r = mid - side;
        }
        if (mbA > 0.0f)
        {
            mbL[0] += (l - mbL[0]) * mbA; mbL[1] += (mbL[0] - mbL[1]) * mbA;
            mbR[0] += (r - mbR[0]) * mbA; mbR[1] += (mbR[0] - mbR[1]) * mbA;
            const float low = (mbL[1] + mbR[1]) * 0.5f;
            l = low + (l - mbL[1]); r = low + (r - mbR[1]);
        }
        float ol = l * level, orr = r * level;
        auto lim = [] (float x) { const float a = std::abs (x); return a < 1.5f ? x : std::copysign (1.5f + 0.5f * std::tanh ((a - 1.5f) * 2.0f), x); };
        left[i] = std::isfinite (ol) ? lim (ol) : 0.0f;
        right[i] = std::isfinite (orr) ? lim (orr) : 0.0f;
    }
    while (ev < events.size()) handle (events[ev++]);
    events.clear();
}
} // namespace prv
