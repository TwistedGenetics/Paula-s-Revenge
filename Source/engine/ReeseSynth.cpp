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
    static const std::vector<SynthParam> ps {
        { "MODE",      "MODE",        0, 3, 0,       3, "",   "LEGATO|ALWAYS|OFF|POLY" },
        { "GLIDE",     "MODE",        1, 2000, 120,  1, "MS", "" },
        { "TYPE",      "OSCILLATORS", 0, 1, 0,       3, "",   "REESE|HOOVER" },
        { "VOICES",    "OSCILLATORS", 1, 7, 3,       2, "",   "" },
        { "DETUNE",    "OSCILLATORS", 0, 100, 25,    0, "C",  "" },
        { "SPREAD",    "OSCILLATORS", 0, 100, 60,    0, "%",  "" },
        { "OCTAVE",    "OSCILLATORS", -3, 2, -1,     2, "",   "" },
        { "MOVE RATE", "MOVEMENT",    0.03f, 5, 0.15f, 1, "HZ", "" },
        { "MOVE",      "MOVEMENT",    0, 100, 40,    0, "%",  "" },
        { "BEND",      "HOOVER",      0, 24, 0,      0, "ST", "" },
        { "BEND TIME", "HOOVER",      5, 2000, 180,  1, "MS", "" },
        { "PWM",       "HOOVER",      0, 100, 60,    0, "%",  "" },
        { "PWM RATE",  "HOOVER",      0.1f, 10, 0.8f, 1, "HZ", "" },
        { "SUB",       "OSCILLATORS", 0, 100, 30,    0, "%",  "" },
        { "CUTOFF",    "FILTER",      40, 18000, 900, 1, "HZ", "" },
        { "RES",       "FILTER",      0, 100, 20,    0, "%",  "" },
        { "ENV AMT",   "FILTER",      -100, 100, 35, 0, "%",  "" },
        { "ENV DECAY", "FILTER",      5, 3000, 350,  1, "MS", "" },
        { "WOB RATE",  "WOBBLE",      0.1f, 16, 2,   1, "HZ", "" },
        { "WOBBLE",    "WOBBLE",      0, 100, 0,     0, "%",  "" },
        { "DRIVE",     "DRIVE",       0, 100, 25,    0, "%",  "" },
        { "CHORUS",    "DRIVE",       0, 100, 0,     0, "%",  "" },
        { "ATTACK",    "ENVELOPE",    0.5f, 2000, 3, 1, "MS", "" },
        { "DECAY",     "ENVELOPE",    5, 5000, 400,  1, "MS", "" },
        { "SUSTAIN",   "ENVELOPE",    0, 100, 100,   0, "%",  "" },
        { "RELEASE",   "ENVELOPE",    5, 5000, 120,  1, "MS", "" },
        { "VELOCITY",  "OUTPUT",      0, 100, 40,    0, "%",  "" },
        { "LEVEL",     "OUTPUT",      0, 100, 75,    0, "%",  "" },
    };
    return ps;
}

ReeseSynth::ReeseSynth()
{
    const auto& ps = reeseParams();
    for (int i = 0; i < reese::Count; ++i) p[(size_t) i] = ps[(size_t) i].def;
    events.reserve (256);
    // the oscillators start spread through their cycle (as analogue ones would be): a fat sound from the first note
    uint32_t s = 4242;
    for (auto& v : voices) for (auto& x : v.ph) { s = s * 1664525u + 1013904223u; x = (s >> 8) / 16777216.0; }
}

void ReeseSynth::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& b : chBuf) b.assign ((size_t) (sr * 0.04) + 4, 0.0f);
    chPos = 0;
}

void ReeseSynth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }

void ReeseSynth::update (const Instrument& in)
{
    bendRange = std::clamp (in.bendRange, 1, 48);
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
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
    if (v.stage == 0 && ! v.fading) return;
    if (v.glideStep != 0.0f)
    {
        v.pitch += v.glideStep;
        if ((v.glideStep > 0 && v.pitch >= v.target) || (v.glideStep < 0 && v.pitch <= v.target)) { v.pitch = v.target; v.glideStep = 0; }
    }
    if (v.bendEnv < -0.0005f) v.bendEnv *= (float) std::exp (-1.0 / std::max (1.0, p[reese::BendTime] * 0.001 * sr * 0.3));
    else v.bendEnv = 0;
    const double semis = v.pitch + v.bendEnv + bend + p[reese::Octave] * 12.0f;
    const double hz = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
    const int n = std::clamp ((int) std::lround (p[reese::Voices]), 1, kOsc);
    const bool hoover = std::lround (p[reese::Type]) == reese::Hoover;
    // DETUNE spread evenly across the oscillators, MOVEMENT breathing it wider and narrower
    const float det = p[reese::Detune] * (1.0f + moveNow * p[reese::MoveDepth] * 0.01f * 0.8f);
    const float spread = p[reese::Spread] * 0.01f;
    const float pw = 0.5f + 0.42f * pwmNow * p[reese::Pwm] * 0.01f;
    float l = 0, r = 0;
    for (int o = 0; o < n; ++o)
    {
        const float pos = n > 1 ? (float) o / (float) (n - 1) * 2.0f - 1.0f : 0.0f;   // -1..1
        const double f = hz * std::pow (2.0, pos * det / 1200.0) * (1.0 + 0.0007 * o);  // (a hair apart even at 0)
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
        const float pan = pos * spread;   // -1..1
        l += x * (1.0f - std::max (0.0f, pan)); r += x * (1.0f + std::min (0.0f, pan));
    }
    const float norm = 1.0f / std::sqrt ((float) n);
    l *= norm; r *= norm;
    // SUB: an octave below (a sine on REESE, a square on HOOVER), in the middle - REESE's sine added after the filter
    // (v0.73), so the resonance and the filter's sweep never ring on it: the low end stays a plain, steady tone
    float subOut = 0;
    {
        const double dt = (hz * 0.5) / sr;
        v.phSub += dt; if (v.phSub >= 1.0) v.phSub -= 1.0;
        float s;
        if (hoover) { double t2 = v.phSub + 0.5; if (t2 >= 1.0) t2 -= 1.0; s = 0.5f * (saw (v.phSub, dt) - saw (t2, dt)); }
        else s = (float) std::sin (kTwoPiR * v.phSub);
        s *= p[reese::SubLevel] * 0.01f;
        if (hoover) { l += s; r += s; }   // (HOOVER's square stays in the filter: it's part of the buzz)
        else subOut = s;
    }
    // the amp envelope (from where it is), the filter envelope
    const float sus = p[reese::Sustain] * 0.01f;
    switch (v.stage)
    {
        case 1: v.env += (float) (1.0 / std::max (1.0, p[reese::Attack] * 0.001 * sr)); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; } break;
        case 2: v.env = sus + (v.env - sus) * (float) std::exp (-1.0 / std::max (1.0, p[reese::Decay] * 0.001 * sr * 0.3)); break;
        case 3: v.env *= (float) std::exp (-1.0 / std::max (1.0, p[reese::Release] * 0.001 * sr * 0.3));
                if (v.env < 0.00003f) { v.env = 0; v.stage = 0; } break;
        default: break;
    }
    if (v.tick < (int) (sr * 0.002)) { v.fenv += (1.0f - v.fenv) * 0.02f; ++v.tick; }
    else v.fenv *= (float) std::exp (-1.0 / std::max (1.0, p[reese::FilterDecay] * 0.001 * sr * 0.3));
    // the 24 dB low-pass: its cutoff from CUTOFF, the envelope, the wobble (worked out every 16 samples)
    if (v.upd-- <= 0 || v.a1 == 0)
    {
        const float oct = p[reese::EnvAmount] * 0.01f * 6.0f * v.fenv + wobNow * p[reese::WobbleDepth] * 0.01f * 3.0f;
        v.cutNow = std::clamp (p[reese::Cutoff] * std::pow (2.0f, oct), 30.0f, (float) (sr * 0.45));
        v.g = (float) std::tan (3.14159265358979 * v.cutNow / sr);
        v.k = 2.0f - 1.9f * std::clamp (p[reese::Resonance] * 0.01f, 0.0f, 1.0f);
        v.a1 = 1.0f / (1.0f + v.g * (v.g + v.k)); v.a2 = v.g * v.a1; v.a3 = v.g * v.a2;
        v.upd = 15;
    }
    auto svf = [] (float x, float& ic1, float& ic2, float a1, float a2, float a3)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; return v2;
    };
    // (the first stage without resonance: the peak comes from the second, as a ladder's would)
    const float b1 = 1.0f / (1.0f + v.g * (v.g + 1.414f)), b2 = v.g * b1, b3 = v.g * b2;
    l = svf (svf (l, v.s1[0][0], v.s1[0][1], b1, b2, b3), v.s2[0][0], v.s2[0][1], v.a1, v.a2, v.a3);
    r = svf (svf (r, v.s1[1][0], v.s1[1][1], b1, b2, b3), v.s2[1][0], v.s2[1][1], v.a1, v.a2, v.a3);
    l += subOut; r += subOut;
    const float vs = p[reese::VelSens] * 0.01f;
    float a = v.env * (1.0f - vs + vs * v.vel);
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
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    const float drive = p[reese::Drive] * 0.01f;
    const float dg = 1.0f + drive * 12.0f;
    const float dnorm = drive > 0 ? 1.0f / std::tanh (dg * 0.7f) : 1.0f;
    const float chorus = p[reese::Chorus] * 0.01f;
    const float level = p[reese::Level] * 0.01f * 0.55f;
    const double mStep = p[reese::MoveRate] / sr, wStep = p[reese::WobbleRate] / sr, pStep = p[reese::PwmRate] / sr;
    const int chLen = (int) chBuf[0].size();
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);
        if (pendingRelease >= 0 && --pendingRelease < 0) releaseVoice (voices[0]);
        move += mStep; if (move >= 1) move -= 1;
        wob += wStep; if (wob >= 1) wob -= 1;
        pwmPh += pStep; if (pwmPh >= 1) pwmPh -= 1;
        const float moveNow = (float) std::sin (kTwoPiR * move);
        const float wobNow = (float) std::sin (kTwoPiR * wob);
        const float pwmNow = (float) (pwmPh < 0.5 ? 4.0 * pwmPh - 1.0 : 3.0 - 4.0 * pwmPh);
        float l = 0, r = 0;
        for (auto& v : voices) renderVoice (v, l, r, moveNow, wobNow, pwmNow);
        if (drive > 0)
        {
            float* ch[2] = { &l, &r };
            for (int c = 0; c < 2; ++c)
            {
                float x = std::tanh (dg * 0.7f * *ch[c]) * dnorm;
                const float y = x - dcX[c] + 0.9995f * dcY[c];
                dcX[c] = x; dcY[c] = y; *ch[c] = y;
            }
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
        left[i] = l * level; right[i] = r * level;
    }
    while (ev < events.size()) handle (events[ev++]);
    events.clear();
}
} // namespace prv
