// Paula's Revenge - 808 and 909 drum synths (see DrumSynth.h)
#include "DrumSynth.h"
#include "BuiltinSynths.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace prv
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr drums::Kind k808[drums::kSlots] = { drums::Kick, drums::Rim, drums::Snare, drums::Clap, drums::LowTom, drums::ClHat,
                                                  drums::MidTom, drums::OpHat, drums::HiTom, drums::Cymbal, drums::Cowbell, drums::Clave };
    constexpr drums::Kind k909[drums::kSlots] = { drums::Kick, drums::Rim, drums::Snare, drums::Clap, drums::LowTom, drums::ClHat,
                                                  drums::MidTom, drums::OpHat, drums::HiTom, drums::Crash, drums::Ride, drums::None };
    const char* kindName (drums::Kind k)
    {
        static const char* n[] = { "KICK", "RIM SHOT", "SNARE", "CLAP", "LOW TOM", "CLOSED HAT", "MID TOM", "OPEN HAT", "HI TOM",
                                   "CYMBAL", "COWBELL", "CLAVE", "CRASH", "RIDE", "" };
        return n[(int) k];
    }
    // which tab a drum's box is on
    const char* tabFor (drums::Kind k)
    {
        switch (k)
        {
            case drums::Kick: case drums::Snare: case drums::Rim: case drums::Clap: return "0:KICK + SNARE|DRUMS";
            case drums::LowTom: case drums::MidTom: case drums::HiTom: case drums::Clave: case drums::Cowbell: return "1:TOMS + MORE|TOMS";
            default: return "2:HATS + CYMBALS|METAL";
        }
    }
    // (the strings live as long as the program: SynthParam keeps pointers)
    const char* keep (const std::string& s) { static std::vector<std::unique_ptr<std::string>> k; k.push_back (std::make_unique<std::string> (s)); return k.back()->c_str(); }

    std::vector<SynthParam> makeParams (bool is909)
    {
        std::vector<SynthParam> ps;
        const int n = drumSlots (is909);
        for (int s = 0; s < n; ++s)
        {
            const auto k = drumKind (is909, s);
            const char* sec = keep (std::string (tabFor (k)) + ">" + kindName (k));
            const char* toneName = k == drums::Kick ? (is909 ? "ATTACK" : "TONE") : k == drums::Snare ? "SNAPPY" : (k == drums::LowTom || k == drums::MidTom || k == drums::HiTom) ? "NOISE" : "TONE";
            ps.push_back ({ "LEVEL", sec, 0, 100, k == drums::Kick ? 90.0f : 80.0f, 0, "%", "" });
            ps.push_back ({ "TUNE",  sec, -12, 12, 0, 0, "ST", "" });
            ps.push_back ({ "DECAY", sec, 0, 100, (k == drums::Kick && ! is909) ? 55.0f : 50.0f, 0, "%", "" });
            ps.push_back ({ toneName, sec, 0, 100, k == drums::Snare ? 60.0f : 50.0f, 0, "%", "" });
            ps.push_back ({ "PAN",   sec, -50, 50, 0, 0, "LR", "" });
        }
        const char* out = "3:OUTPUT>OUTPUT";
        ps.push_back ({ "LEVEL",  out, 0, 100, 80, 0, "%", "" });
        ps.push_back ({ "DRIVE",  out, 0, 100, 0,  0, "%", "" });
        ps.push_back ({ "ACCENT", out, 0, 100, 60, 0, "%", "" });
        return ps;
    }
    inline float decayTime (float d01, float lo, float hi) { return lo * std::pow (hi / lo, std::clamp (d01, 0.0f, 1.0f)); }   // seconds
    inline float fall (double tau, double fs) { return (float) std::exp (-1.0 / std::max (1.0, tau * fs)); }
    inline float sq (double ph) { return ph < 0.5 ? 1.0f : -1.0f; }
    // the 808's six square oscillators (the 909's metal, here, the same ring)
    constexpr double kMetal[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
}

bool isDrumKey (const std::string& key) { return key == kDrum808Key || key == kDrum909Key; }
int  drumSlots (bool is909) { return is909 ? 11 : drums::kSlots; }
drums::Kind drumKind (bool is909, int slot) { return slot < 0 || slot >= drums::kSlots ? drums::None : (is909 ? k909 : k808)[slot]; }
const char* drumName (bool is909, int slot) { return kindName (drumKind (is909, slot)); }
int  drumParam (bool is909, int slot, drums::Ctl c) { return std::clamp (slot, 0, drumSlots (is909) - 1) * drums::PerDrum + (int) c; }
int  drumOutParam (bool is909, drums::Out o) { return drumSlots (is909) * drums::PerDrum + (int) o; }

const std::vector<SynthParam>& drumParams (bool is909)
{
    static const std::vector<SynthParam> a = makeParams (false), b = makeParams (true);
    return is909 ? b : a;
}

const char* drumPresetName (bool is909, int i)
{
    static const char* n8[kDrumPresets] = { "808 CLASSIC", "808 LONG BOOM", "808 TIGHT", "808 DIRTY" };
    static const char* n9[kDrumPresets] = { "909 CLASSIC", "909 HARD", "909 DEEP", "909 SHORT" };
    return (is909 ? n9 : n8)[std::clamp (i, 0, kDrumPresets - 1)];
}

std::vector<float> drumPreset (bool is909, int i)
{
    std::vector<float> v;
    for (const auto& q : drumParams (is909)) v.push_back (q.def);
    auto set = [&] (int slot, drums::Ctl c, float x) { v[(size_t) drumParam (is909, slot, c)] = x; };
    auto out = [&] (drums::Out o, float x) { v[(size_t) drumOutParam (is909, o)] = x; };
    switch (std::clamp (i, 0, kDrumPresets - 1))
    {
        case 1:   // a long, low kick (808: the sub boom; 909: a deep thump)
            set (0, drums::Decay, is909 ? 85.0f : 92.0f); set (0, drums::Tune, -3); set (0, drums::Level, 100);
            set (2, drums::Decay, 60); set (7, drums::Decay, 65); break;
        case 2:   // tight: short everything
            set (0, drums::Decay, 25); set (2, drums::Decay, 30); set (2, drums::Tone, 70); set (3, drums::Decay, 25);
            set (5, drums::Decay, 25); set (7, drums::Decay, 30); set (9, drums::Decay, 30); break;
        case 3:   // driven
            out (drums::OutDrive, 55); out (drums::OutLevel, 70); set (0, drums::Tone, 80); set (2, drums::Tone, 80); break;
        default: break;
    }
    return v;
}

// ============================================================================ the filter
void DrumSynth::Svf::set (double hz, double q, double sr)
{
    hz = std::clamp (hz, 20.0, sr * 0.45);
    g = (float) std::tan (3.141592653589793 * hz / sr);
    k = (float) (1.0 / std::max (0.3, q));
    a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
}

void DrumSynth::Svf::run (float x, float& lp, float& bp, float& hp)
{
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
    lp = v2; bp = v1; hp = x - k * v1 - v2;
}

// ============================================================================ the machine
DrumSynth::DrumSynth (bool nine) : is909 (nine)
{
    for (const auto& q : drumParams (is909)) p.push_back (q.def);
    base = p;
    events.reserve (256);
}

void DrumSynth::prepare (double sampleRate, int)
{
    outSr = sampleRate > 0 ? sampleRate : 48000.0;
    fs = outSr * kOs;
    decL.reset(); decR.reset();
    for (auto& w : v) w = Voice {};
}

void DrumSynth::setValues (const std::vector<float>& x) { for (size_t i = 0; i < x.size() && i < p.size(); ++i) p[i] = x[i]; }

void DrumSynth::update (const Instrument& in)
{
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
    base = p;
}

void DrumSynth::setParameter (int i, float v01)
{
    const auto& ps = drumParams (is909);
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void DrumSynth::noteOn (int note, int vel, int offset)
{
    if (vel > 0 && events.size() < 1024) events.push_back ({ offset, note, vel });
}

void DrumSynth::allNotesOff (int)
{
    for (auto& w : v) if (w.active) w.fadeStep = (float) (1.0 / (0.004 * fs));
}

void DrumSynth::trigger (int slot, int vel)
{
    using namespace drums;
    auto& w = v[(size_t) slot];
    const Kind k = drumKind (is909, slot);
    if (k == None) return;
    // the CLOSED HAT chokes the OPEN HAT (as on both machines)
    if (k == ClHat)
        for (int s = 0; s < kSlots; ++s)
            if (drumKind (is909, s) == OpHat && v[(size_t) s].active) v[(size_t) s].fadeStep = (float) (1.0 / (0.003 * fs));
    const float acc = p[(size_t) drumOutParam (is909, OutAccent)] * 0.01f;
    const float lvl = ctl (slot, Level) * 0.01f;
    w.kind = k;
    w.gain = lvl * lvl * (1.0f - acc * (1.0f - std::clamp (vel, 1, 127) / 127.0f));
    const float pan = std::clamp (ctl (slot, Pan) / 50.0f, -1.0f, 1.0f);
    w.panL = (float) std::cos ((pan + 1.0) * 0.25 * 3.141592653589793);
    w.panR = (float) std::sin ((pan + 1.0) * 0.25 * 3.141592653589793);
    w.t = 0; w.fade = 1; w.fadeStep = 0;
    w.env = 1; w.env2 = 1; w.penv = 1; w.click = 1; w.coefs = false;
    if (! w.active) { for (auto& ph : w.ph) ph = 0; w.f1.reset(); w.f2.reset(); w.f3.reset(); }
    w.active = true;
    ++hitCount;
}

// one drum, one sample at the high rate
float DrumSynth::voiceOut (int slot, Voice& w)
{
    using namespace drums;
    const float d = ctl (slot, Decay) * 0.01f, tone = ctl (slot, Tone) * 0.01f;
    const double tune = std::pow (2.0, ctl (slot, Tune) / 12.0);
    const double dt = 1.0 / fs;
    float y = 0, lp, bp, hp;
    auto osc = [&] (int i, double hz) { w.ph[i] += hz * dt; w.ph[i] -= std::floor (w.ph[i]); return w.ph[i]; };
    auto setf = [&] (Svf& f, double hz, double q) { if (! w.coefs) f.set (hz, q, fs); };
    auto sine = [&] (int i, double hz) { return (float) std::sin (kTwoPi * osc (i, hz)); };
    auto metal = [&] (double mul)
    {
        float m = 0;
        for (int i = 0; i < 6; ++i) m += sq (osc (i, kMetal[i] * tune * mul));
        return m * (1.0f / 6.0f);
    };
    switch (w.kind)
    {
        case Kick:
            if (! is909)
            {
                // 808: the bridged-T boom - a sine at ~49 Hz ringing down; a little pitch blip at the start; TONE: its edge
                w.penv *= fall (0.006, fs);
                w.env *= fall (decayTime (d, 0.08f, 1.6f) / 2.3, fs);
                const float s = sine (0, 49.0 * tune * (1.0 + 1.3 * w.penv));
                w.click *= fall (0.012, fs);
                const float drv = 1.0f + tone * 3.0f * w.click;
                y = std::tanh (s * drv) / std::tanh (drv) * w.env;
            }
            else
            {
                // 909: a sine swept down from high (the punch), a little squarer; ATTACK: the click (noise + pulse)
                w.penv *= fall (0.022, fs);
                w.env *= fall (decayTime (d, 0.12f, 1.3f) / 2.3, fs);
                const float s = sine (0, 50.0 * tune * (1.0 + 4.5 * w.penv * w.penv));
                y = std::tanh (s * 1.7f) / std::tanh (1.7f) * w.env;
                w.click *= fall (0.0015, fs);
                setf (w.f1, 3500, 0.7); w.f1.run (noise(), lp, bp, hp);
                y += tone * 0.9f * w.click * (lp * 2.0f + (w.t < 0.0006 ? 0.7f : 0.0f));
            }
            break;
        case Snare:
        {
            // two tones and the SNAPPY noise
            const float tauT = decayTime (d, 0.05f, 0.35f) / 2.3f, tauN = decayTime (d, 0.08f, 0.6f) / 2.3f;
            w.env *= fall (tauT, fs); w.env2 *= fall (tauN, fs);
            w.penv *= fall (0.012, fs);
            const double drop = is909 ? 1.0 + 0.35 * w.penv : 1.0 + 0.06 * w.penv;
            const float tones = (is909 ? 0.55f : 0.6f) * sine (0, (is909 ? 190.0 : 180.0) * tune * drop) + (is909 ? 0.4f : 0.4f) * sine (1, (is909 ? 340.0 : 330.0) * tune * drop);
            setf (w.f1, is909 ? 2200 : 1500, 0.7); w.f1.run (noise(), lp, bp, hp);
            setf (w.f2, is909 ? 9500 : 7000, 0.7); float l2, b2, h2; w.f2.run (hp, l2, b2, h2);
            y = tones * w.env * (1.0f - 0.35f * tone) + l2 * w.env2 * tone * 0.85f;
            break;
        }
        case Clap:
        {
            // noise through a band-pass, hit four times ~10 ms apart, then the tail
            const double sp = is909 ? 0.011 : 0.0095;
            const int burst = (int) (w.t / sp);
            const float be = burst < 4 ? (float) std::exp (-(w.t - burst * sp) / 0.0022) : 0.0f;
            w.env2 *= fall (decayTime (d, 0.06f, 0.6f) / 2.3, fs);
            setf (w.f1, (is909 ? 1300.0 : 1100.0) * std::pow (2.0, (tone - 0.5) * 1.6) * tune, 2.2);
            w.f1.run (noise(), lp, bp, hp);
            const float tail = w.t > 3 * sp ? w.env2 * 0.75f : 0.0f;
            y = bp * 2.2f * (be + tail);
            break;
        }
        case Rim:
        {
            w.env *= fall (decayTime (d, 0.006f, 0.04f) / 2.3, fs);
            const float s = 0.55f * sine (0, (is909 ? 500.0 : 455.0) * tune) + 0.45f * sine (1, (is909 ? 1750.0 : 1667.0) * tune);
            float x = s * w.env;
            if (is909) { w.click *= fall (0.001, fs); x += noise() * w.click * 0.5f * tone; }
            setf (w.f1, 300.0 + 500.0 * tone, 0.7); w.f1.run (x, lp, bp, hp);
            y = std::tanh (hp * 1.8f);
            break;
        }
        case LowTom: case MidTom: case HiTom:
        {
            const double f0 = (w.kind == LowTom ? (is909 ? 95.0 : 82.0) : w.kind == MidTom ? (is909 ? 135.0 : 120.0) : (is909 ? 195.0 : 165.0)) * tune;
            w.penv *= fall (is909 ? 0.03 : 0.04, fs);
            w.env *= fall (decayTime (d, 0.08f, 0.7f) / 2.3, fs);
            const double drop = 1.0 + (is909 ? 0.55 : 0.18) * w.penv;
            float s = sine (0, f0 * drop);
            if (is909) s = std::tanh (s * 1.4f) / std::tanh (1.4f);
            w.click *= fall (0.03, fs);
            setf (w.f1, f0 * 8.0, 0.8); w.f1.run (noise(), lp, bp, hp);
            y = (s + lp * tone * 0.6f * w.click) * w.env;
            break;
        }
        case ClHat: case OpHat:
        {
            const bool open = w.kind == OpHat;
            w.env *= fall ((open ? decayTime (d, 0.12f, 1.4f) : decayTime (d, 0.015f, 0.14f)) / 2.3, fs);
            float m = metal (1.0);
            if (is909) m = m * 0.65f + noise() * 0.35f;
            setf (w.f1, (is909 ? 10500.0 : 9000.0) * std::pow (2.0, (tone - 0.5) * 0.9), 1.4); w.f1.run (m, lp, bp, hp);
            setf (w.f2, is909 ? 7500.0 : 6500.0, 0.7); float l2, b2, h2; w.f2.run (bp, l2, b2, h2);
            y = h2 * 2.2f * w.env;
            break;
        }
        case Cymbal: case Crash:
        {
            const bool crash = w.kind == Crash;
            w.env *= fall (decayTime (d, 0.35f, crash ? 3.5f : 3.0f) / 2.3, fs);
            w.env2 *= fall (decayTime (d, 0.12f, 1.0f) / 2.3, fs);
            float m = metal (crash ? 1.12 : 1.0);
            if (crash) m = m * 0.5f + noise() * 0.5f;
            setf (w.f1, 3400.0 * std::pow (2.0, (tone - 0.5)), 1.6); w.f1.run (m, lp, bp, hp);
            const float low = bp;
            setf (w.f2, 8000.0 * std::pow (2.0, (tone - 0.5) * 0.7), 1.0); float l2, b2, h2; w.f2.run (m, l2, b2, h2);
            setf (w.f3, 6000.0, 0.7); float l3, b3, h3; w.f3.run (b2, l3, b3, h3);
            y = (low * 0.8f * w.env2 + h3 * 1.6f * w.env) * (crash ? 1.2f : 1.0f);
            break;
        }
        case Ride:
        {
            w.env *= fall (decayTime (d, 0.3f, 2.5f) / 2.3, fs);
            const float m = metal (1.55) * 0.85f + noise() * 0.15f;
            setf (w.f1, 8200.0 * std::pow (2.0, (tone - 0.5) * 0.8), 2.4); w.f1.run (m, lp, bp, hp);
            setf (w.f2, 5000.0, 0.7); float l2, b2, h2; w.f2.run (bp, l2, b2, h2);
            w.click *= fall (0.02, fs);
            y = h2 * 2.0f * w.env * (0.75f + 0.5f * w.click);
            break;
        }
        case Cowbell:
        {
            // two squares (540 / 800 Hz) through a band-pass: a fast drop, then a longer ring
            w.env *= fall (0.006, fs);
            w.env2 *= fall (decayTime (d, 0.05f, 0.6f) / 2.3, fs);
            const float m = 0.5f * sq (osc (0, 540.0 * tune)) + 0.5f * sq (osc (1, 800.0 * tune));
            setf (w.f1, 2640.0 * std::pow (2.0, (tone - 0.5) * 0.8), 2.5); w.f1.run (m, lp, bp, hp);
            y = bp * 1.0f * (0.55f * w.env + 0.45f * w.env2);
            break;
        }
        case Clave:
        {
            w.env *= fall (decayTime (d, 0.008f, 0.06f) / 2.3, fs);
            const float s = sine (0, 2500.0 * tune);
            y = std::tanh (s * (1.0f + 2.0f * tone)) * w.env;
            break;
        }
        default: break;
    }
    w.t += dt;
    w.coefs = true;
    if (w.fadeStep > 0) { w.fade -= w.fadeStep; if (w.fade <= 0) { w.fade = 0; w.active = false; } }
    // done: every envelope it uses has died away
    const bool two = w.kind == Snare || w.kind == Cymbal || w.kind == Crash || w.kind == Cowbell;
    const bool gone = w.kind == Clap ? w.env2 < 1.0e-5f : (w.env < 1.0e-5f && (! two || w.env2 < 1.0e-5f));
    if (w.t > 0.05 && gone) w.active = false;
    return y * w.gain * w.fade;
}

void DrumSynth::renderHi (int n)
{
    int ev = 0;
    const float drive = p[(size_t) drumOutParam (is909, drums::OutDrive)] * 0.01f;
    const float out = p[(size_t) drumOutParam (is909, drums::OutLevel)] * 0.01f;
    const float dg = 1.0f + drive * 5.0f, dn = 1.0f / std::tanh (dg);
    for (auto& w : v) w.coefs = false;   // (the filters follow the controls: set again once a chunk)
    for (int i = 0; i < n; ++i)
    {
        while (ev < (int) events.size() && events[(size_t) ev].offset <= i)
        {
            const auto& e = events[(size_t) ev++];
            int slot = ((e.note % 12) + 12) % 12;
            if (drumKind (is909, slot) == drums::None) slot = 0;   // (the 909's B: its kick again)
            trigger (slot, e.vel);
        }
        float l = 0, r = 0;
        for (int s = 0; s < drums::kSlots; ++s)
        {
            auto& w = v[(size_t) s];
            if (! w.active) continue;
            const float y = voiceOut (s, w);
            l += y * w.panL; r += y * w.panR;
        }
        l *= 1.41f * out; r *= 1.41f * out;
        if (drive > 0.0f) { l = std::tanh (l * dg) * dn; r = std::tanh (r * dg) * dn; }
        hiL[(size_t) i] = l; hiR[(size_t) i] = r;
    }
    events.erase (events.begin(), events.begin() + ev);
    for (auto& e : events) e.offset = std::max (0, e.offset - n);
}

void DrumSynth::render (float* left, float* right, int n)
{
    for (auto& e : events) e.offset *= kOs;
    int done = 0;
    while (done < n)
    {
        const int m = std::min (n - done, kChunk);
        renderHi (m * kOs);
        for (int i = 0; i < m; ++i)
        {
            float a = decL.push (hiL.data() + i * kOs), b = decR.push (hiR.data() + i * kOs);
            a = std::clamp (a, -2.0f, 2.0f); b = std::clamp (b, -2.0f, 2.0f);
            left[done + i] = a; right[done + i] = b;
        }
        done += m;
    }
    for (auto& e : events) e.offset /= kOs;
}
} // namespace prv
