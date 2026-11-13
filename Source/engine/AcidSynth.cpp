// Paula's Revenge - ACID (see AcidSynth.h). Our own model of a 303-style bass line: not a copy of any circuit or code
#include "AcidSynth.h"
#include "BuiltinSynths.h"
#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    constexpr double kAcidPi = 3.141592653589793;
    inline float blep (double t, double dt)
    {
        if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }
    inline float sawAt (double t, double dt) { return (float) (2.0 * t - 1.0) - blep (t, dt); }
    // (a time constant: the per-sample factor that falls to 10% in `ms`)
    inline float fallIn (float ms, double sr) { return (float) std::exp (-2.302585 / std::max (1.0, ms * 0.001 * sr)); }
}

const std::vector<SynthParam>& acidSynthParams()
{
    static const std::vector<SynthParam> ps {
        // the 303's own
        { "WAVE",        "OSCILLATOR", 0, 1, 0,          3, "",   "SAW|SQUARE" },
        { "TUNE",        "OSCILLATOR", -12, 12, 0,       2, "ST", "" },
        { "FINE",        "OSCILLATOR", -50, 50, 0,       0, "C",  "" },
        { "SHAPE",       "OSCILLATOR", 50, 90, 50,       0, "%",  "" },
        { "CUTOFF",      "FILTER",     40, 8000, 380,    1, "HZ", "" },
        { "RESONANCE",   "FILTER",     0, 100, 72,       0, "%",  "" },
        { "ENV MOD",     "FILTER",     0, 100, 55,       0, "%",  "" },
        { "DECAY",       "ENVELOPE + ACCENT", 30, 3000, 350, 1, "MS", "" },
        { "ACCENT",      "ENVELOPE + ACCENT", 0, 100, 65, 0, "%", "" },
        { "ACCENT FROM", "ENVELOPE + ACCENT", 1, 64, 0x38, 2, "CX", "" },   // (a note this loud (Cxx) or louder is accented)
        // the Devil Fish's
        { "ACC DECAY",   "DEVIL FISH ENVELOPES", 30, 3000, 200, 1, "MS", "" },
        { "ATTACK",      "DEVIL FISH ENVELOPES", 0.3f, 30, 3, 1, "MS", "" },
        { "SWEEP",       "DEVIL FISH ENVELOPES", 0, 100, 45, 0, "%", "" },
        { "AMP DECAY",   "DEVIL FISH ENVELOPES", 16, 5000, 5000, 1, "MS", "" },
        { "RELEASE",     "DEVIL FISH ENVELOPES", 1, 1000, 8, 1, "MS", "" },
        { "TRACKING",    "DEVIL FISH FILTER", 0, 100, 0,  0, "%",  "" },
        { "FILTER FM",   "DEVIL FISH FILTER", 0, 100, 0,  0, "%",  "" },
        { "RES BASS",    "DEVIL FISH FILTER", 0, 100, 40, 0, "%",  "" },
        { "TIME",        "SLIDE",      10, 1000, 60,     1, "MS", "" },
        { "SLIDE",       "SLIDE",      0, 1, 0,          3, "",   "OVERLAP (KEYS)|TIED NOTES" },
        { "MUFFLER",     "MUFFLER + DRIVE", 0, 2, 0,     3, "",   "OFF|SOFT|HARD" },
        { "DRIVE",       "MUFFLER + DRIVE", 0, 100, 0,   0, "%",  "" },
        { "SUB",         "OSCILLATOR", 0, 100, 0,        0, "%",  "" },
        { "VELOCITY",    "OUTPUT",     0, 100, 0,        0, "%",  "" },
        { "LEVEL",       "OUTPUT",     0, 100, 75,       0, "%",  "" },
    };
    return ps;
}

const char* acidPresetName (int i)
{
    static const char* n[kAcidPresets] = { "CLASSIC", "SQUELCH", "RUBBER BASS", "SQUARE ACID", "DEVIL SCREAM",
                                           "SLOW SWEEP", "MUFFLED SUB", "SELF-OSC BLEEP", "TIED SLIDES", "HARDCORE ACID" };
    return n[std::clamp (i, 0, kAcidPresets - 1)];
}

std::vector<float> acidPreset (int i)
{
    using namespace acid;
    std::vector<float> v;
    for (const auto& q : acidSynthParams()) v.push_back (q.def);   // (CLASSIC's)
    auto set = [&] (std::initializer_list<std::pair<int, float>> l) { for (auto& [k, x] : l) v[(size_t) k] = x; };
    switch (std::clamp (i, 0, kAcidPresets - 1))
    {
        case 1: set ({ { Cutoff, 260 }, { Resonance, 88 }, { EnvMod, 80 }, { Decay, 220 }, { Accent, 85 }, { SweepSpeed, 25 } }); break;
        case 2: set ({ { Cutoff, 180 }, { Resonance, 45 }, { EnvMod, 40 }, { Decay, 600 }, { Accent, 40 }, { ResBass, 90 }, { SubOsc, 35 }, { SlideTime, 120 } }); break;
        case 3: set ({ { Wave, Square }, { PulseShape, 62 }, { Cutoff, 320 }, { Resonance, 78 }, { EnvMod, 65 }, { Decay, 300 } }); break;
        case 4: set ({ { Cutoff, 500 }, { Resonance, 97 }, { EnvMod, 90 }, { Decay, 180 }, { Accent, 100 }, { AccentDecay, 120 }, { SweepSpeed, 15 },
                       { FilterFm, 35 }, { Muffler, 1 }, { Drive, 45 } }); break;
        case 5: set ({ { Cutoff, 120 }, { Resonance, 82 }, { EnvMod, 70 }, { Decay, 2200 }, { AccentDecay, 1500 }, { SweepSpeed, 8 }, { SlideTime, 250 } }); break;
        case 6: set ({ { Wave, Square }, { Cutoff, 140 }, { Resonance, 30 }, { EnvMod, 25 }, { Decay, 900 }, { Accent, 30 }, { ResBass, 100 },
                       { SubOsc, 60 }, { Muffler, 1 }, { Drive, 20 }, { SoftAttack, 10 } }); break;
        case 7: set ({ { Cutoff, 900 }, { Resonance, 100 }, { EnvMod, 30 }, { Decay, 150 }, { AmpDecay, 250 }, { Release, 60 }, { FilterTrack, 100 },
                       { Level, 55 } }); break;
        case 8: set ({ { SlideMode, Tied }, { SlideTime, 90 }, { Cutoff, 300 }, { Resonance, 80 }, { EnvMod, 60 } }); break;
        case 9: set ({ { Cutoff, 420 }, { Resonance, 92 }, { EnvMod, 85 }, { Decay, 260 }, { Accent, 90 }, { Muffler, 2 }, { Drive, 70 },
                       { FilterFm, 15 }, { Level, 60 } }); break;
        default: break;
    }
    return v;
}

AcidSynth::AcidSynth()
{
    const auto& ps = acidSynthParams();
    for (int i = 0; i < acid::Count; ++i) p[(size_t) i] = ps[(size_t) i].def;
    events.reserve (256);
    held.reserve (32);
}

void AcidSynth::prepare (double sampleRate, int) { sr = sampleRate > 0 ? sampleRate : 48000.0; }

void AcidSynth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }

void AcidSynth::update (const Instrument& in)
{
    bendRange = std::clamp (in.bendRange, 1, 48);
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
}

void AcidSynth::setParameter (int i, float v01)
{
    const auto& ps = acidSynthParams();
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void AcidSynth::noteOn (int note, int vel, int offset) { if (events.size() < 1024) events.push_back ({ offset, vel > 0 ? 0 : 1, note, vel }); }
void AcidSynth::noteOff (int note, int offset)         { if (events.size() < 1024) events.push_back ({ offset, 1, note, 0 }); }
void AcidSynth::allNotesOff (int offset)               { if (events.size() < 1024) events.push_back ({ offset, 2, 0, 0 }); }
void AcidSynth::pitchBend (int v, int offset)          { if (events.size() < 1024) events.push_back ({ offset, 3, v, 0 }); }

void AcidSynth::releaseGate()
{
    if (! gate) return;
    gate = false;
    ampStage = 3;
    lastOffAt = sampleNow;
}

void AcidSynth::startNote (int note, int velocity, bool slide)
{
    key = note;
    target = (float) note;
    vel = std::clamp (velocity, 1, 127) / 127.0f;
    const bool acc = velocity >= (int) std::lround (p[acid::AccentAt] * 127.0f / 64.0f);
    if (slide && ampEnv > 1.0e-4f)
    {
        // SLIDE: the pitch glides there; the envelopes carry on (as a 303's slide step)
        gate = true;
        if (ampStage == 3) ampStage = 2;
        return;
    }
    pitch = target;
    accented = acc;
    meg = 1.0f;
    megDecay = fallIn (acc ? p[acid::AccentDecay] : p[acid::Decay], sr);
    if (acc) accEnv = 1.0f;
    ampStage = 1;
    attackRate = 1.0f / (float) std::max (1.0, (acc ? 0.3 : (double) p[acid::SoftAttack]) * 0.001 * sr);
    gate = true;
    ++attackCount;
}

void AcidSynth::handle (const Event& e)
{
    const bool tied = std::lround (p[acid::SlideMode]) == acid::Tied;
    switch (e.type)
    {
        case 0:
        {
            const bool overlap = gate && ! held.empty();
            const bool tiedNow = tied && ! gate && lastOffAt == sampleNow;   // (the last note ran right into this one)
            held.erase (std::remove (held.begin(), held.end(), e.note), held.end());
            held.push_back (e.note);
            startNote (e.note, e.vel, overlap || tiedNow);
            break;
        }
        case 1:
        {
            held.erase (std::remove (held.begin(), held.end(), e.note), held.end());
            if (e.note != key) break;
            if (! held.empty() && gate) { key = held.back(); target = (float) key; }   // (back to the key still down, sliding)
            else releaseGate();
            break;
        }
        case 2: held.clear(); releaseGate(); lastOffAt = -1; break;
        case 3: bend = (e.note - 8192) / 8192.0f * (float) bendRange; break;
        default: break;
    }
}

void AcidSynth::render (float* left, float* right, int n)
{
    using namespace acid;
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    const float glide = 1.0f - (float) std::exp (-3.0 / std::max (1.0, p[SlideTime] * 0.001 * sr));
    const float accAmt = p[Accent] * 0.01f;
    const float accFall = fallIn (p[AccentDecay], sr);
    // the sweep: charges fast and lets go slowly when SWEEP SPEED is up; slowly both ways (so a run of accents builds) when down
    const float chargeMs = 1.0f + (1.0f - p[SweepSpeed] * 0.01f) * 60.0f;
    const float charge = 1.0f - (float) std::exp (-1.0 / (chargeMs * 0.001 * sr));
    const float discharge = 1.0f - (float) std::exp (-1.0 / (chargeMs * 6.0 * 0.001 * sr));
    const bool holdAmp = p[AmpDecay] >= 4999.0f;
    const float ampFall = fallIn (p[AmpDecay], sr), relFall = fallIn (p[Release], sr);
    const float k = std::pow (p[Resonance] * 0.01f, 0.8f) * 4.15f;
    const float hpHz = 18.0f * std::pow (2.0f, p[ResBass] * 0.01f * 4.2f);
    const float gh = (float) std::tan (kAcidPi * std::min (hpHz, 2000.0f) / sr), Gh = gh / (1.0f + gh);
    const float drive = 1.0f + p[Drive] * 0.01f * 9.0f;
    const int muffler = (int) std::lround (p[Muffler]);
    const float level = p[Level] * 0.01f * 0.9f;
    const float pw = std::clamp (p[PulseShape] * 0.01f, 0.5f, 0.9f);
    const bool square = std::lround (p[Wave]) == Square;
    const float subAmt = p[SubOsc] * 0.01f;
    const float velSens = p[VelSens] * 0.01f;
    float G = 0, G2 = 0, G4 = 0;
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);
        float out = 0.0f;
        if (ampStage != 0 || ampEnv > 1.0e-6f)
        {
            // pitch, with the slide's glide
            pitch += (target - pitch) * glide;
            const double hz = 440.0 * std::pow (2.0, (pitch + p[Tune] + p[Fine] * 0.01 + bend - 69.0) / 12.0);
            const double dt = std::min (0.45, hz / sr);
            ph += dt; ph -= std::floor (ph);
            float x;
            if (square)
            {
                double ph2 = ph + pw; ph2 -= std::floor (ph2);
                x = 0.5f * (sawAt (ph, dt) - sawAt (ph2, dt));
            }
            else x = sawAt (ph, dt);
            if (subAmt > 0.0f)
            {
                subPh += dt * 0.5; subPh -= std::floor (subPh);
                x += subAmt * 0.7f * (subPh < 0.5 ? 1.0f : -1.0f);
            }
            // the envelopes: the MEG, the accent and its sweep
            meg *= megDecay;
            accEnv *= accFall;
            sweep += (accEnv - sweep) * (accEnv > sweep ? charge : discharge);
            // the cutoff (worked out every 4 samples)
            if ((i & 3) == 0)
            {
                const float oct = p[EnvMod] * 0.01f * meg * 4.5f + (accented ? accAmt * sweep * 4.5f : 0.0f)
                                + p[FilterTrack] * 0.01f * (pitch - 48.0f) / 12.0f + p[FilterFm] * 0.01f * lastOut * 3.0f;
                cutoffHz = std::clamp (p[Cutoff] * std::pow (2.0f, oct), 20.0f, (float) (sr * 0.42));
                const float g = (float) std::tan (kAcidPi * cutoffHz / sr);
                G = g / (1.0f + g); G2 = G * G; G4 = G2 * G2;
            }
            // the ladder: four one-poles, the feedback through a high-pass (RES BASS: how much of the bass it spares)
            const float kk = k * (1.0f - Gh);
            const float om = 1.0f - G;
            const float sig = G2 * G * om * s[0] + G2 * om * s[1] + G * om * s[2] + om * s[3];
            const float in = x * 0.55f;
            const float y4lin = (G4 * (in + kk * fbHp) + sig) / (1.0f + kk * G4);
            float u = std::tanh (in - kk * (y4lin - fbHp));
            for (auto& st : s)
            {
                const float v = (u - st) * G;
                const float y = v + st;
                st = y + v;
                u = y;
            }
            const float y4 = u;
            { const float v = (y4 - fbHp) * Gh; const float lp = v + fbHp; fbHp = lp + v; }
            // the amp
            switch (ampStage)
            {
                case 1: ampEnv += attackRate; if (ampEnv >= 1.0f) { ampEnv = 1.0f; ampStage = 2; } break;
                case 2: if (! holdAmp) ampEnv *= ampFall; if (! gate) ampStage = 3; break;
                case 3: ampEnv *= relFall; if (ampEnv < 1.0e-6f) { ampEnv = 0; ampStage = 0; } break;
                default: break;
            }
            float gain = ampEnv * (1.0f + (accented ? accAmt * sweep * 0.9f : 0.0f));
            gain *= 1.0f - velSens * (1.0f - vel);
            out = y4 * gain * (1.0f + k * 0.18f);
            lastOut = out;
            // MUFFLER and DRIVE
            out *= drive;
            if (muffler == 1) out = std::tanh (out * 1.3f) / 1.3f * 1.25f;
            else if (muffler == 2) out = std::clamp (out * 1.6f, -0.75f, 0.75f) + 0.12f * std::tanh (out * 4.0f);
            else if (drive > 1.0f) out = std::tanh (out);
        }
        else lastOut = 0.0f;
        // (no DC)
        const float d = out - dcX + 0.9995f * dcY; dcX = out; dcY = d;
        float o = d * level;
        const float ao = std::abs (o);
        if (ao > 1.5f) o = std::copysign (1.5f + 0.5f * std::tanh ((ao - 1.5f) * 2.0f), o);
        left[i] = o; right[i] = o;
        ++sampleNow;
    }
    // (events past the block's end: the next one's)
    events.erase (events.begin(), events.begin() + (long) ev);
    for (auto& e : events) e.offset = std::max (0, e.offset - n);
}
} // namespace prv
