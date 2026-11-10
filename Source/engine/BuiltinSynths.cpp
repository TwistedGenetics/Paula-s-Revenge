// Paula's Revenge - the built-in synths (SUB, GRANULAR)
#include "BuiltinSynths.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace prv
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }
}

// ============================================================================ the controls
float SynthParam::toValue (float v) const
{
    v = clamp01 (v);
    if (kind == 1) return min * std::pow (max / min, v);
    const float x = min + (max - min) * v;
    return kind >= 2 ? std::round (x) : x;
}

float SynthParam::to01 (float value) const
{
    if (max <= min) return 0;
    if (kind == 1) return clamp01 ((float) (std::log (std::max (min, value) / min) / std::log (max / min)));
    return clamp01 ((value - min) / (max - min));
}

std::string SynthParam::text (float v) const
{
    if (kind == 3)
    {
        std::string all = choices;
        int i = (int) std::lround (v), k = 0;
        size_t a = 0;
        while (k < i && a != std::string::npos) { a = all.find ('|', a); if (a != std::string::npos) ++a; ++k; }
        if (a == std::string::npos) return "?";
        const size_t b = all.find ('|', a);
        return all.substr (a, b == std::string::npos ? std::string::npos : b - a);
    }
    char buf[32];
    const std::string u = unit;
    if (u == "%") std::snprintf (buf, sizeof (buf), "%d%%", (int) std::lround (v));
    else if (u == "MS") { if (v >= 1000.0f) std::snprintf (buf, sizeof (buf), "%.2fS", v / 1000.0f); else std::snprintf (buf, sizeof (buf), "%dMS", (int) std::lround (v)); }
    else if (u == "HZ") { if (v >= 1000.0f) std::snprintf (buf, sizeof (buf), "%.1fK", v / 1000.0f); else if (v < 10.0f) std::snprintf (buf, sizeof (buf), "%.2fHZ", v); else std::snprintf (buf, sizeof (buf), "%dHZ", (int) std::lround (v)); }
    else if (u == "ST" || u == "C") std::snprintf (buf, sizeof (buf), "%+d%s", (int) std::lround (v), u.c_str());
    else if (kind == 2) std::snprintf (buf, sizeof (buf), "%+d", (int) std::lround (v));
    else std::snprintf (buf, sizeof (buf), "%.2f", v);
    return buf;
}

const std::vector<SynthParam>& builtinParams (const std::string& key)
{
    static const std::vector<SynthParam> none;
    static const std::vector<SynthParam> subP {
        { "MODE",      "MODE",       0, 3, 0,      3, "",   "LEGATO|ALWAYS|OFF|POLY" },
        { "GLIDE",     "MODE",       1, 2000, 80,  1, "MS", "" },
        { "SHAPE",     "OSCILLATOR", 0, 100, 0,    0, "%",  "" },
        { "OCTAVE",    "OSCILLATOR", -3, 2, 0,     2, "",   "" },
        { "FINE",      "OSCILLATOR", -100, 100, 0, 0, "C",  "" },
        { "OSC 2",     "OSC 2",      0, 100, 0,    0, "%",  "" },
        { "INTERVAL",  "OSC 2",      0, 3, 0,      3, "",   "+OCT|+5TH|+OCT+5TH|-OCT" },
        { "NOISE",     "OSC 2",      0, 100, 0,    0, "%",  "" },
        { "NOISE DEC", "OSC 2",      5, 500, 40,   1, "MS", "" },
        { "DROP",      "808 DROP",   0, 48, 0,     0, "ST", "" },
        { "DROP TIME", "808 DROP",   5, 1000, 60,  1, "MS", "" },
        { "DRIVE",     "DRIVE",      0, 100, 0,    0, "%",  "" },
        { "TYPE",      "DRIVE",      0, 1, 0,      3, "",   "TAPE|TUBE" },
        { "TONE",      "DRIVE",      40, 12000, 12000, 1, "HZ", "" },
        { "ATTACK",    "ENVELOPE",   0.5f, 2000, 2, 1, "MS", "" },
        { "DECAY",     "ENVELOPE",   5, 5000, 400, 1, "MS", "" },
        { "SUSTAIN",   "ENVELOPE",   0, 100, 100,  0, "%",  "" },
        { "RELEASE",   "ENVELOPE",   5, 5000, 60,  1, "MS", "" },
        { "VELOCITY",  "OUTPUT",     0, 100, 50,   0, "%",  "" },
        { "LEVEL",     "OUTPUT",     0, 100, 80,   0, "%",  "" },
    };
    static const std::vector<SynthParam> grainP {
        { "SOURCE",    "SOURCE",     0, 2, 0,      3, "",   "TABLE|SAMPLE|RECORDING" },
        { "TABLE",     "SOURCE",     0, 7, 0,      3, "",   "SINE|SAW|SQUARE|VOCAL|BELL|GLASS|NOISE|CHOIR" },
        { "SAMPLE",    "SOURCE",     1, 63, 1,     2, "",   "" },
        { "POSITION",  "GRAINS",     0, 100, 30,   0, "%",  "" },
        { "SCAN",      "GRAINS",     -100, 100, 0, 0, "%",  "" },
        { "SIZE",      "GRAINS",     5, 1000, 90,  1, "MS", "" },
        { "DENSITY",   "GRAINS",     1, 120, 20,   1, "HZ", "" },
        { "SPRAY",     "GRAINS",     0, 100, 10,   0, "%",  "" },
        { "PITCH",     "PITCH",      -24, 24, 0,   2, "ST", "" },
        { "SPREAD",    "PITCH",      0, 24, 0,     0, "ST", "" },
        { "REVERSE",   "PITCH",      0, 100, 0,    0, "%",  "" },
        { "STEREO",    "PITCH",      0, 100, 50,   0, "%",  "" },
        { "FREEZE",    "GRAINS",     0, 1, 0,      3, "",   "OFF|ON" },
        { "ATTACK",    "ENVELOPE",   1, 4000, 40,  1, "MS", "" },
        { "DECAY",     "ENVELOPE",   5, 5000, 500, 1, "MS", "" },
        { "SUSTAIN",   "ENVELOPE",   0, 100, 80,   0, "%",  "" },
        { "RELEASE",   "ENVELOPE",   5, 8000, 800, 1, "MS", "" },
        { "LFO1 RATE", "LFO 1",      0.02f, 20, 0.3f, 1, "HZ", "" },
        { "LFO1 AMT",  "LFO 1",      0, 100, 0,    0, "%",  "" },
        { "LFO1 TO",   "LFO 1",      0, 7, 0,      3, "",   "POSITION|SIZE|DENSITY|SPRAY|PITCH|STEREO|CUTOFF|LEVEL" },
        { "LFO2 RATE", "LFO 2",      0.02f, 20, 2, 1, "HZ", "" },
        { "LFO2 AMT",  "LFO 2",      0, 100, 0,    0, "%",  "" },
        { "LFO2 TO",   "LFO 2",      0, 7, 6,      3, "",   "POSITION|SIZE|DENSITY|SPRAY|PITCH|STEREO|CUTOFF|LEVEL" },
        { "ENV AMT",   "ENV MOD",    -100, 100, 0, 0, "%",  "" },
        { "ENV TO",    "ENV MOD",    0, 7, 0,      3, "",   "POSITION|SIZE|DENSITY|SPRAY|PITCH|STEREO|CUTOFF|LEVEL" },
        { "CUTOFF",    "FILTER",     40, 18000, 18000, 1, "HZ", "" },
        { "RES",       "FILTER",     0, 100, 0,    0, "%",  "" },
        { "DIFFUSE",   "FILTER",     0, 100, 0,    0, "%",  "" },
        { "LEVEL",     "OUTPUT",     0, 100, 80,   0, "%",  "" },
    };
    if (key == kSubKey) return subP;
    if (key == kGrainKey) return grainP;
    return none;
}

std::vector<float> builtinDefaults (const std::string& key)
{
    std::vector<float> v;
    for (const auto& q : builtinParams (key)) v.push_back (q.def);
    return v;
}

std::vector<float> readBuiltinState (const Instrument& in)
{
    auto v = builtinDefaults (in.pluginKey);
    const auto& s = in.pluginState;
    if (s.size() >= 8 && std::memcmp (s.data(), "SYN1", 4) == 0)
    {
        uint32_t n = 0;
        std::memcpy (&n, s.data() + 4, 4);
        for (size_t i = 0; i < v.size() && i < n && 8 + (i + 1) * 4 <= s.size(); ++i)
        {
            float f;
            std::memcpy (&f, s.data() + 8 + i * 4, 4);
            if (std::isfinite (f)) v[i] = f;
        }
    }
    return v;
}

void writeBuiltinState (Instrument& in, const std::vector<float>& v)
{
    std::vector<uint8_t> s (8 + v.size() * 4);
    std::memcpy (s.data(), "SYN1", 4);
    const uint32_t n = (uint32_t) v.size();
    std::memcpy (s.data() + 4, &n, 4);
    for (size_t i = 0; i < v.size(); ++i) std::memcpy (s.data() + 8 + i * 4, &v[i], 4);
    in.pluginState = std::move (s);
}

void makeBuiltin (Instrument& in, const std::string& key)
{
    const auto vol = in.volume;
    in = Instrument {};
    in.volume = vol;
    in.plugin = true;
    in.pluginKey = key;
    in.pluginName = key == kSubKey ? "SUB" : "GRANULAR";
    in.name = in.pluginName;
    in.bendRange = 12;
    writeBuiltinState (in, builtinDefaults (key));
}

std::unique_ptr<PluginSynth> createBuiltinSynth (const Instrument& in)
{
    if (in.pluginKey == kSubKey) return std::make_unique<SubSynth>();
    if (in.pluginKey == kGrainKey) return std::make_unique<GrainSynth>();
    return nullptr;
}

// ============================================================================ SUB
SubSynth::SubSynth()
{
    const auto d = builtinDefaults (kSubKey);
    for (int i = 0; i < sub::Count; ++i) p[(size_t) i] = d[(size_t) i];
    events.reserve (256);
    held.reserve (32);
}

void SubSynth::prepare (double sampleRate, int) { sr = sampleRate > 0 ? sampleRate : 48000.0; }

void SubSynth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }

void SubSynth::update (const Instrument& in)
{
    bendRange = std::clamp (in.bendRange, 1, 48);
    if (in.pluginState == lastState) return;   // (a knob's change holds until the page changes something)
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
}

void SubSynth::setParameter (int i, float v01)
{
    const auto& ps = builtinParams (kSubKey);
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void SubSynth::noteOn (int note, int vel, int offset)  { if (events.size() < 1024) events.push_back ({ offset, vel > 0 ? 0 : 1, note, vel }); }
void SubSynth::noteOff (int note, int offset)          { if (events.size() < 1024) events.push_back ({ offset, 1, note, 0 }); }
void SubSynth::allNotesOff (int offset)                { if (events.size() < 1024) events.push_back ({ offset, 2, 0, 0 }); }
void SubSynth::pitchBend (int v, int offset)           { if (events.size() < 1024) events.push_back ({ offset, 3, v, 0 }); }

float SubSynth::shapeWave (double ph) const
{
    // SHAPE: sine (0) to triangle (50%) to a soft square (100%) - all start at zero, so a new voice never jumps
    const float s = (float) std::sin (kTwoPi * ph);
    const float sh = p[sub::Shape] * 0.01f;
    if (sh <= 0.0f) return s;
    const float tri = (float) (std::asin (std::clamp (s, -1.0f, 1.0f)) * (2.0 / 3.141592653589793));
    if (sh <= 0.5f) return s + (tri - s) * (sh * 2.0f);
    const float sq = std::tanh (4.0f * s) / std::tanh (4.0f);
    return tri + (sq - tri) * ((sh - 0.5f) * 2.0f);
}

void SubSynth::startVoice (Voice& v, int note, int vel, bool retrigger, bool glide)
{
    const float glideMs = p[sub::Glide];
    if (v.stage == 0 && ! v.fading) { v.pitch = (float) note; glide = false; }   // (from silence: no slide from nowhere)
    v.target = (float) note;
    if (glide && glideMs > 1.5f) v.glideStep = (v.target - v.pitch) / (float) std::max (1.0, glideMs * 0.001 * sr);
    else { v.pitch = v.target; v.glideStep = 0; }
    v.note = note;
    v.gate = true;
    if (retrigger)
    {
        // a fresh attack - from the level it's at now, never from 0 (the wave keeps going: no click)
        v.stage = 1;
        v.vel = (float) vel / 127.0f;
        v.drop = p[sub::DropAmount];          // (the 808 drop: the pitch starts up there and falls; the wave never jumps)
        v.noiseEnv = 1.0f;
    }
    else if (v.stage == 0 || v.stage == 3) v.stage = 2;   // (legato into a voice that was letting go: back to sustain)
    v.order = ++orderCount;
}

void SubSynth::releaseVoice (Voice& v)
{
    v.gate = false;
    if (v.stage != 0) v.stage = 3;
}

void SubSynth::handle (const Event& e)
{
    const int mode = (int) std::lround (p[sub::Mode]);
    if (e.type == 3) { bend = (float) (e.note - 8192) / 8192.0f * (float) bendRange; return; }
    if (e.type == 2)
    {
        held.clear(); pendingRelease = -1;
        for (auto& v : voices) releaseVoice (v);
        return;
    }
    if (mode == sub::Poly)
    {
        if (e.type == 1) { for (auto& v : voices) if (v.gate && v.note == e.note && ! v.fading) releaseVoice (v); return; }
        // the same note again: that voice, from where it is
        for (auto& v : voices)
            if (v.note == e.note && v.stage != 0 && ! v.fading) { startVoice (v, e.note, e.vel, true, false); return; }
        // seven at most: the oldest fades out fast (a few ms) instead of stopping dead
        int busy = 0, oldest = -1;
        for (int i = 0; i < kVoices; ++i)
            if (voices[i].stage != 0 && ! voices[i].fading)
            {
                ++busy;
                if (oldest < 0 || voices[i].order < voices[oldest].order) oldest = i;
            }
        if (busy >= kVoices - 1 && oldest >= 0) voices[oldest].fading = true;
        Voice* free = nullptr;
        for (auto& v : voices) if (v.stage == 0 && ! v.fading) { free = &v; break; }
        if (free == nullptr)
            for (auto& v : voices) if (v.fading && (free == nullptr || v.fade < free->fade)) free = &v;
        if (free == nullptr) return;
        *free = Voice {};             // (a new voice starts at the wave's zero: no jump)
        startVoice (*free, e.note, e.vel, true, false);
        return;
    }

    // MONO: one voice, one wave that never restarts
    Voice& v = voices[0];
    if (e.type == 1)
    {
        held.erase (std::remove (held.begin(), held.end(), e.note), held.end());
        if (e.note != v.note || ! v.gate) return;
        if (! held.empty())
        {
            // back to the note still held: it slides (or jumps) there without a new attack
            startVoice (v, held.back(), (int) (v.vel * 127.0f), false, mode != sub::Retrigger);
            return;
        }
        pendingRelease = std::max (1, (int) (sr * 0.003));   // (a note in the next 3 ms is legato, not a new attack)
        return;
    }
    const bool overlapping = ! held.empty() || pendingRelease >= 0;
    held.erase (std::remove (held.begin(), held.end(), e.note), held.end());
    held.push_back (e.note);
    pendingRelease = -1;
    const bool sounding = v.stage != 0;
    const bool legato = overlapping && sounding;
    const bool glide = (mode == sub::Legato && legato) || (mode == sub::Always && sounding);
    const bool retrigger = mode == sub::Retrigger || ! legato;
    startVoice (v, e.note, e.vel, retrigger, glide);
}

float SubSynth::renderVoice (Voice& v)
{
    if (v.stage == 0 && ! v.fading) return 0.0f;
    // pitch: the glide, the 808 drop falling away, bend, octave and fine
    if (v.glideStep != 0.0f)
    {
        v.pitch += v.glideStep;
        if ((v.glideStep > 0 && v.pitch >= v.target) || (v.glideStep < 0 && v.pitch <= v.target)) { v.pitch = v.target; v.glideStep = 0; }
    }
    if (v.drop > 0.0005f) v.drop *= (float) std::exp (-1.0 / std::max (1.0, p[sub::DropTime] * 0.001 * sr * 0.4));
    else v.drop = 0;
    const double semis = v.pitch + v.drop + bend + p[sub::Octave] * 12.0f + p[sub::Fine] * 0.01f;
    const double hz = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
    v.phase += hz / sr; v.phase -= std::floor (v.phase);
    static const float iv[4] = { 12.0f, 7.0f, 19.0f, -12.0f };
    const int ii = std::clamp ((int) std::lround (p[sub::Osc2Interval]), 0, 3);
    v.phase2 += hz * std::pow (2.0, iv[ii] / 12.0) / sr; v.phase2 -= std::floor (v.phase2);

    // the envelope: a straight attack from where it is, decay to sustain, release (exponential)
    const float sus = p[sub::Sustain] * 0.01f;
    switch (v.stage)
    {
        case 1: v.env += (float) (1.0 / std::max (1.0, p[sub::Attack] * 0.001 * sr)); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; } break;
        case 2: v.env = sus + (v.env - sus) * (float) std::exp (-1.0 / std::max (1.0, p[sub::Decay] * 0.001 * sr * 0.3)); break;
        case 3: v.env *= (float) std::exp (-1.0 / std::max (1.0, p[sub::Release] * 0.001 * sr * 0.3));
                if (v.env < 0.00003f) { v.env = 0; v.stage = 0; } break;
        default: break;
    }
    float x = shapeWave (v.phase) + shapeWave (v.phase2) * p[sub::Osc2Level] * 0.01f;
    if (p[sub::Noise] > 0.0f && v.noiseEnv > 0.0001f)
    {
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        x += ((float) (noiseSeed >> 9) / 4194304.0f - 1.0f) * v.noiseEnv * p[sub::Noise] * 0.01f;
        v.noiseEnv *= (float) std::exp (-1.0 / std::max (1.0, p[sub::NoiseDecay] * 0.001 * sr * 0.3));
    }
    const float vs = p[sub::VelSens] * 0.01f;
    float y = x * v.env * (1.0f - vs + vs * v.vel);
    if (v.fading)
    {
        v.fade -= (float) (1.0 / (0.004 * sr));   // (4 ms)
        if (v.fade <= 0.0f) { v = Voice {}; return 0.0f; }
        y *= v.fade;
    }
    return y;
}

void SubSynth::render (float* left, float* right, int n)
{
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    const float drive = p[sub::Drive] * 0.01f;
    const float g = 1.0f + drive * 24.0f;
    const float norm = drive > 0.0f ? 1.0f / std::tanh (g) : 1.0f;
    const bool tube = std::lround (p[sub::DriveType]) == 1;
    const float tone = p[sub::Tone];
    const float a = tone >= 11990.0f ? 1.0f : (float) (1.0 - std::exp (-kTwoPi * tone / sr));
    const float level = p[sub::Level] * 0.01f * 0.6f;
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);
        if (pendingRelease >= 0 && --pendingRelease < 0) releaseVoice (voices[0]);
        float x = 0;
        for (auto& v : voices) x += renderVoice (v);
        if (drive > 0.0f)
        {
            // TAPE: a smooth, even squash; TUBE: lopsided (warmer, more second harmonic) - then the DC it makes is taken off
            x = tube ? std::tanh (g * (x + 0.3f * x * x)) * norm : std::tanh (g * x) * norm;
            const float y = x - dcX + 0.9995f * dcY;
            dcX = x; dcY = y; x = y;
        }
        if (a < 1.0f) { lp1 += a * (x - lp1); lp2 += a * (lp1 - lp2); x = lp2; }
        x *= level;
        left[i] = x; right[i] = x;
    }
    while (ev < events.size()) handle (events[ev++]);
    events.clear();
}

// ============================================================================ GRANULAR
namespace
{
    constexpr double kC3 = 261.6255653005986;   // MIDI 60, the pitch every source plays at on C-3
    constexpr int kTableLen = 72000;             // 1.5 s at 48 kHz

    void normalise (std::vector<float>& v, float peak = 0.9f)
    {
        float m = 0;
        for (float x : v) m = std::max (m, std::abs (x));
        if (m > 0) for (auto& x : v) x *= peak / m;
    }

    // a harmonic series whose levels change across the table (u = 0..1): amp (k, u)
    template <typename F> std::vector<float> additive (F amp, double f0 = kC3, float vibrato = 0)
    {
        std::vector<float> out ((size_t) kTableLen);
        const int kmax = (int) (18000.0 / f0);
        double th = 0;
        for (int t = 0; t < kTableLen; ++t)
        {
            const float u = (float) t / (float) kTableLen;
            const double s1 = std::sin (th), c2 = 2.0 * std::cos (th);
            double sPrev = 0, sCur = s1, x = 0;
            for (int k = 1; k <= kmax; ++k)
            {
                x += amp (k, u) * sCur;
                const double sNext = c2 * sCur - sPrev;
                sPrev = sCur; sCur = sNext;
            }
            out[(size_t) t] = (float) x;
            th += kTwoPi * f0 * (1.0 + vibrato * std::sin (kTwoPi * 5.1 * t / GrainSynth::kTableRate)) / GrainSynth::kTableRate;
            if (th > kTwoPi) th -= kTwoPi;
        }
        return out;
    }

    // the level a vowel's formants give a harmonic at f Hz; v = 0..4 morphs A, E, I, O, U
    float formant (double f, float v)
    {
        static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 280, 2150, 2950 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        static const float G[3] = { 1.0f, 0.5f, 0.22f }, B[3] = { 90, 110, 160 };
        v = std::clamp (v, 0.0f, 3.999f);
        const int a = (int) v; const float w = v - (float) a;
        float g = 0.02f;
        for (int j = 0; j < 3; ++j)
        {
            const float fj = F[a][j] + (F[a + 1][j] - F[a][j]) * w;
            const float d = (float) (f - fj) / B[j];
            g += G[j] / (1.0f + d * d);
        }
        return g;
    }

    std::vector<float> partials (const std::vector<float>& ratio, const std::vector<float>& level, const std::vector<float>& decay, float beat = 0)
    {
        std::vector<float> out ((size_t) kTableLen);
        std::vector<double> ph (ratio.size() * 2, 0.0);
        for (int t = 0; t < kTableLen; ++t)
        {
            const double secs = t / GrainSynth::kTableRate;
            double x = 0;
            for (size_t j = 0; j < ratio.size(); ++j)
            {
                const double f = kC3 * ratio[j];
                if (f > 18000) continue;
                const double a = level[j] * std::exp (-decay[j] * secs);
                x += a * std::sin (ph[j * 2]);
                ph[j * 2] += kTwoPi * f / GrainSynth::kTableRate;
                if (beat > 0)   // a detuned twin: a slow shimmer
                {
                    x += a * 0.7 * std::sin (ph[j * 2 + 1]);
                    ph[j * 2 + 1] += kTwoPi * f * (1.0 + beat * (1.0 + 0.37 * j)) / GrainSynth::kTableRate;
                }
            }
            out[(size_t) t] = (float) x;
        }
        return out;
    }

    std::vector<std::vector<float>> makeTables()
    {
        std::vector<std::vector<float>> t (8);
        // SINE: pure, a touch of the octave coming in across the table
        t[0] = additive ([] (int k, float u) { return k == 1 ? 1.0f : k == 2 ? 0.12f * u : k == 3 ? 0.04f * u * u : 0.0f; });
        // SAW: dark to bright
        t[1] = additive ([] (int k, float u) { return (1.0f / (float) k) * std::exp (-(float) k * (1.0f - u) * 0.18f); });
        // SQUARE: a square narrowing to a thin pulse
        t[2] = additive ([] (int k, float u) { const float d = 0.5f - 0.44f * u; return std::sin (3.14159265f * (float) k * d) / (float) k; });
        // VOCAL: a voice going through A E I O U
        t[3] = additive ([] (int k, float u) { return formant (k * kC3, u * 4.0f) / std::sqrt ((float) k); }, kC3, 0.004f);
        // BELL: struck, ringing away
        t[4] = partials ({ 1.0f, 2.0f, 2.76f, 3.0f, 4.07f, 5.4f, 6.9f, 8.93f, 11.3f },
                         { 1.0f, 0.6f, 0.5f, 0.3f, 0.35f, 0.25f, 0.18f, 0.12f, 0.08f },
                         { 1.2f, 1.8f, 2.4f, 2.0f, 3.5f, 4.5f, 6.0f, 8.0f, 10.0f });
        // GLASS: high, shimmering partials
        t[5] = partials ({ 1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.1f, 15.2f },
                         { 0.7f, 0.5f, 0.45f, 0.35f, 0.3f, 0.2f, 0.15f },
                         { 0.4f, 0.5f, 0.7f, 0.9f, 1.2f, 1.5f, 2.0f }, 0.0035f);
        // NOISE: dark rumble to bright hiss
        {
            std::vector<float> v ((size_t) kTableLen);
            uint32_t seed = 987654321u;
            float a1 = 0, a2 = 0;
            for (int i = 0; i < kTableLen; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                const float w = (float) (int32_t) seed / 2147483648.0f;
                const float u = (float) i / kTableLen;
                const float fc = 150.0f * std::pow (80.0f, u);
                const float a = 1.0f - std::exp (-6.2831853f * fc / 48000.0f);
                a1 += a * (w - a1); a2 += a * (a1 - a2);
                v[(size_t) i] = a2 / std::sqrt (std::max (0.01f, a));
            }
            t[6] = v;
        }
        // CHOIR: five voices a little out of tune with each other, AH to OO
        {
            std::vector<float> v ((size_t) kTableLen, 0.0f);
            const float cents[5] = { -13, -6, 0, 5, 12 };
            for (float c : cents)
            {
                const double f0 = kC3 * std::pow (2.0, c / 1200.0);
                auto one = additive ([f0] (int k, float u) { return k > 40 ? 0.0f : formant (k * f0, 3.0f * u * u + 0.0f) / std::sqrt ((float) k); }, f0, 0.006f);
                for (size_t i = 0; i < v.size(); ++i) v[i] += one[i];
            }
            t[7] = v;
        }
        for (auto& v : t) normalise (v);
        return t;
    }

    const float* hann()
    {
        static const std::vector<float> w = []
        {
            std::vector<float> h (1025);
            for (int i = 0; i <= 1024; ++i) h[(size_t) i] = 0.5f - 0.5f * (float) std::cos (kTwoPi * i / 1024.0);
            return h;
        }();
        return w.data();
    }

    constexpr float kRingSeconds = 8.0f;
}

const std::vector<float>& GrainSynth::table (int which)
{
    static const std::vector<std::vector<float>> all = makeTables();
    return all[(size_t) std::clamp (which, 0, (int) all.size() - 1)];
}

GrainSynth::GrainSynth()
{
    const auto d = builtinDefaults (kGrainKey);
    for (int i = 0; i < grain::Count; ++i) p[(size_t) i] = d[(size_t) i];
    table (0);   // (made here, on the message thread - never while playing)
    hann();
    events.reserve (256);
}

void GrainSynth::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    ring.assign ((size_t) (sr * kRingSeconds), 0.0f);
    ringPos = ringFill = 0;
    static const float msL[4] = { 4.7f, 7.9f, 12.3f, 19.1f }, msR[4] = { 5.3f, 8.6f, 13.1f, 20.4f };
    for (int i = 0; i < 8; ++i)
    {
        ap[i].assign ((size_t) std::max (1.0, (i < 4 ? msL[i] : msR[i - 4]) * 0.001 * sr), 0.0f);
        apPos[i] = 0;
    }
}

void GrainSynth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }
void GrainSynth::setTestSource (std::vector<float> mono, double rate) { testSrc = std::move (mono); testRate = rate; }

void GrainSynth::update (const Instrument& in)
{
    bendRange = std::clamp (in.bendRange, 1, 48);
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
}

void GrainSynth::setParameter (int i, float v01)
{
    const auto& ps = builtinParams (kGrainKey);
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void GrainSynth::noteOn (int note, int vel, int offset)  { if (events.size() < 1024) events.push_back ({ offset, vel > 0 ? 0 : 1, note, vel }); }
void GrainSynth::noteOff (int note, int offset)          { if (events.size() < 1024) events.push_back ({ offset, 1, note, 0 }); }
void GrainSynth::allNotesOff (int offset)                { if (events.size() < 1024) events.push_back ({ offset, 2, 0, 0 }); }
void GrainSynth::pitchBend (int v, int offset)           { if (events.size() < 1024) events.push_back ({ offset, 3, v, 0 }); }

void GrainSynth::liveInput (const float* l, const float* r, int n)
{
    if (ring.empty() || std::lround (p[grain::Source]) != 2 || std::lround (p[grain::Freeze]) == 1) return;   // FREEZE: hold what's there
    for (int i = 0; i < n; ++i)
    {
        ring[ringPos] = 0.5f * (l[i] + r[i]);
        if (++ringPos >= ring.size()) ringPos = 0;
    }
    ringFill = std::min (ring.size(), ringFill + (size_t) n);
}

GrainSynth::Src GrainSynth::source() const
{
    Src s;
    const int kind = (int) std::lround (p[grain::Source]);
    if (! testSrc.empty() && kind == 1) { s.f = testSrc.data(); s.len = testSrc.size(); s.rate = testRate; return s; }
    if (kind == 0)
    {
        const auto& t = table ((int) std::lround (p[grain::Table]));
        s.f = t.data(); s.len = t.size(); s.rate = kTableRate;
    }
    else if (kind == 1)
    {
        const int slot = (int) std::lround (p[grain::SampleSlot]) - 1;
        if (song != nullptr && slot >= 0 && slot < kMaxInstruments)
        {
            const auto& in = song->instruments[(size_t) slot];
            if (! in.isPlugin() && ! in.data.empty())
            {
                const size_t n = in.data.size();
                const size_t a = (size_t) std::clamp (in.playStart, 0, (int) n - 1);
                const size_t b = in.playEnd > (int) a ? std::min (n, (size_t) in.playEnd) : n;
                s.s = in.data.data() + a; s.len = b - a; s.rate = in.rate;
            }
        }
    }
    else if (! ring.empty() && ringFill > 0)
    {
        s.f = ring.data(); s.len = ringFill; s.rate = sr;
        s.ringStart = ringFill < ring.size() ? 0 : ringPos;   // (the oldest)
    }
    return s;
}

bool GrainSynth::sourceOverview (std::vector<float>& peaks, int count) const
{
    peaks.assign ((size_t) std::max (0, count), 0.0f);
    const Src s = source();
    if (s.len < 2 || count <= 0) return false;
    const size_t wrap = s.ringStart > 0 || s.len == ring.size() ? ring.size() : s.len;
    for (int c = 0; c < count; ++c)
    {
        const size_t a = s.len * (size_t) c / (size_t) count, b = std::max (a + 1, s.len * (size_t) (c + 1) / (size_t) count);
        const size_t step = std::max<size_t> (1, (b - a) / 64);
        float m = 0;
        for (size_t i = a; i < b; i += step)
        {
            const size_t k = s.f != nullptr && s.f == ring.data() ? (s.ringStart + i) % wrap : i;
            const float x = s.f != nullptr ? s.f[k] : s.s[k] / 32768.0f;
            m = std::max (m, std::abs (x));
        }
        peaks[(size_t) c] = std::min (1.0f, m);
    }
    return true;
}

int GrainSynth::activeGrains() const
{
    int n = 0;
    for (const auto& g : grains) n += g.on ? 1 : 0;
    return n;
}

float GrainSynth::rnd()
{
    seed = seed * 1664525u + 1013904223u;
    return (float) (int32_t) seed / 2147483648.0f;
}

float GrainSynth::envNow() const
{
    const Note* best = nullptr;
    for (const auto& n : notes) if (n.stage > 0 && (best == nullptr || n.order > best->order)) best = &n;
    return best != nullptr ? best->env : 0.0f;
}

void GrainSynth::handle (const Event& e)
{
    if (e.type == 3) { bend = (float) (e.note - 8192) / 8192.0f * (float) bendRange; return; }
    if (e.type == 2) { for (auto& n : notes) if (n.stage > 0) { n.gate = false; n.stage = 3; } return; }
    if (e.type == 1)
    {
        for (auto& n : notes) if (n.note == e.note && n.gate) { n.gate = false; n.stage = 3; }
        return;
    }
    // a note: the same note again, a free one, the quietest releasing one, the oldest - its envelope goes on from
    // where it is (no click), and it starts reading at POSITION again
    Note* use = nullptr;
    for (auto& n : notes) if (n.note == e.note && n.stage > 0) use = &n;
    if (use == nullptr) for (auto& n : notes) if (n.stage == 0) { use = &n; break; }
    if (use == nullptr)
        for (auto& n : notes) if (! n.gate && (use == nullptr || n.env < use->env)) use = &n;
    if (use == nullptr)
        for (auto& n : notes) if (use == nullptr || n.order < use->order) use = &n;
    if (use->stage == 0) use->env = 0;
    use->note = e.note; use->gate = true; use->stage = 1;
    use->vel = 0.35f + 0.65f * (float) std::clamp (e.vel, 1, 127) / 127.0f;
    use->spawn = 1.0; use->scan = 0; use->age = 0; use->order = ++orderCount;
}

void GrainSynth::spawn (Note& n, const Src& s, const float mod[8])
{
    Grain* g = nullptr;
    for (auto& x : grains) if (! x.on) { g = &x; break; }
    if (g == nullptr || s.len < 16) return;
    const float sizeMs = p[grain::Size] * std::pow (2.0f, 2.0f * mod[1]);
    const float spray = std::clamp (p[grain::Spray] * 0.01f + mod[3], 0.0f, 1.0f);
    const float stereo = std::clamp (p[grain::Stereo] * 0.01f + mod[5], 0.0f, 1.0f);
    const float semis = (float) (n.note - 60) + p[grain::Pitch] + 12.0f * mod[4] + bend + p[grain::PitchSpread] * rnd();
    const bool rev = rnd() * 0.5f + 0.5f < p[grain::Reverse] * 0.01f;
    const double step = (s.rate / sr) * std::pow (2.0, semis / 12.0) * (rev ? -1.0 : 1.0);
    const int len = std::max (16, (int) (std::clamp (sizeMs, 2.0f, 4000.0f) * 0.001f * (float) sr));
    // where: POSITION, plus SCAN's travel, the modulation and SPRAY
    double pos = p[grain::Position] * 0.01 + n.scan + mod[0] * 0.5 + spray * 0.15 * rnd();
    pos -= std::floor (pos);
    const bool live = s.f != nullptr && s.f == ring.data();
    if (live)   // (the recording: never read past what has just come in)
    {
        const double reach = (double) len * std::abs (step) + sr * 0.02;
        const double room = std::max (0.0, 1.0 - reach / (double) s.len);
        pos *= room;
    }
    double start = pos * (double) s.len;
    if (s.f != nullptr && ! live && s.rate == kTableRate && ! rev)
    {
        // a table is one pitch (C-3) all through: each grain starts in step with the ones before it (as if one
        // oscillator ran on under them), so overlapping grains add up instead of cancelling - a clean note
        const double period = kTableRate / kC3;
        start = std::floor (start / period) * period + std::fmod (n.age * step, period);
    }
    if (rev) start += (double) len * std::abs (step);   // (reversed: it ends where a forward grain would start)
    const float density = std::max (1.0f, p[grain::Density] * std::pow (2.0f, 2.0f * mod[2]));
    const float overlap = density * (float) len / (float) sr;
    const float pan = stereo * rnd();
    g->on = true;
    g->note = (int) (&n - notes);
    g->readPos = live ? (double) s.ringStart + start : start;
    g->step = step; g->age = 0; g->len = len;
    g->amp = 0.8f / std::sqrt (std::max (1.0f, overlap));
    g->panL = std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f;
    g->panR = std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;
    posNow = (float) pos;
}

void GrainSynth::render (float* left, float* right, int n)
{
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    const Src s = source();
    const size_t wrap = s.f != nullptr && s.f == ring.data() ? ring.size() : s.len;
    const float* win = hann();
    const bool freeze = std::lround (p[grain::Freeze]) == 1;
    const double scanStep = freeze || s.len == 0 ? 0.0 : p[grain::Scan] * 0.01 * (s.rate / sr) / (double) s.len;
    const float sus = p[grain::Sustain] * 0.01f;
    const float atk = (float) (1.0 / std::max (1.0, p[grain::Attack] * 0.001 * sr));
    const float dec = (float) (1.0 - std::exp (-4.6 / std::max (1.0, p[grain::Decay] * 0.001 * sr)));
    const float rel = (float) std::exp (-4.6 / std::max (1.0, p[grain::Release] * 0.001 * sr));
    const double l1 = p[grain::Lfo1Rate] / sr, l2 = p[grain::Lfo2Rate] / sr;
    const int t1 = (int) std::lround (p[grain::Lfo1Target]), t2 = (int) std::lround (p[grain::Lfo2Target]), te = (int) std::lround (p[grain::EnvTarget]);
    const float d1 = p[grain::Lfo1Depth] * 0.01f, d2 = p[grain::Lfo2Depth] * 0.01f, de = p[grain::EnvDepth] * 0.01f;
    const bool cutMod = (d1 > 0 && t1 == 6) || (d2 > 0 && t2 == 6) || (de != 0 && te == 6);
    const bool filterOn = p[grain::Cutoff] < 17990.0f || cutMod;
    const float k = 2.0f - 1.94f * std::clamp (p[grain::Resonance] * 0.01f, 0.0f, 1.0f);
    const float diffuse = p[grain::Diffuse] * 0.01f;
    const float level = p[grain::Level] * 0.01f * 0.8f;
    float a1 = 0, a2 = 0, a3 = 0;
    float shown = -1;
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);
        // the LFOs (1: a sine, 2: a triangle) into their targets
        lfo1 += l1; if (lfo1 >= 1) lfo1 -= 1;
        lfo2 += l2; if (lfo2 >= 1) lfo2 -= 1;
        float mod[8] {};
        mod[std::clamp (t1, 0, 7)] += d1 * (float) std::sin (kTwoPi * lfo1);
        mod[std::clamp (t2, 0, 7)] += d2 * (float) (lfo2 < 0.5 ? 4.0 * lfo2 - 1.0 : 3.0 - 4.0 * lfo2);
        // the notes: envelopes, travel, new grains
        for (auto& nt : notes)
        {
            if (nt.stage == 0) continue;
            if (nt.stage == 1) { nt.env += atk; if (nt.env >= 1) { nt.env = 1; nt.stage = 2; } }
            else if (nt.stage == 2) nt.env += (sus - nt.env) * dec;
            else { nt.env *= rel; if (nt.env < 0.0002f) { nt.env = 0; nt.stage = 0; nt.note = -1; continue; } }
            nt.scan += scanStep;
            nt.age += 1.0;
            if (nt.scan > 1e6 || nt.scan < -1e6) nt.scan = 0;
            if (! nt.gate && nt.env < 0.001f) continue;   // (releasing: the grains already playing carry it out)
            float m[8];
            for (int j = 0; j < 8; ++j) m[j] = mod[j];
            m[std::clamp (te, 0, 7)] += de * nt.env;
            const float density = std::max (0.5f, p[grain::Density] * std::pow (2.0f, 2.0f * m[2]));
            nt.spawn += density / sr;
            if (nt.spawn >= 1.0)
            {
                const float spray = std::clamp (p[grain::Spray] * 0.01f + m[3], 0.0f, 1.0f);
                nt.spawn -= 1.0 + 0.5 * spray * rnd();   // (SPRAY scatters the timing too)
                spawn (nt, s, m);
                if (nt.order >= (int) shown) shown = (float) nt.order;
            }
        }
        // the grains
        float L = 0, R = 0;
        if (s.len > 0)
        {
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const auto& nt = notes[g.note];
                const float w = win[(size_t) ((int64_t) g.age * 1024 / g.len)];
                double rp = g.readPos;
                if (rp < 0 || rp >= (double) wrap) { rp = std::fmod (rp, (double) wrap); if (rp < 0) rp += (double) wrap; }
                const size_t i0 = (size_t) rp;
                const float fr = (float) (rp - (double) i0);
                size_t i1 = i0 + 1; if (i1 >= wrap) i1 = 0;
                float x;
                if (s.f != nullptr) x = s.f[i0] + (s.f[i1] - s.f[i0]) * fr;
                else x = ((float) s.s[i0] + (float) (s.s[i1] - s.s[i0]) * fr) * (1.0f / 32768.0f);
                x *= w * g.amp * nt.env * nt.vel;
                L += x * g.panL; R += x * g.panR;
                g.readPos = rp + g.step;
                if (++g.age >= g.len) g.on = false;
            }
        }
        else for (auto& g : grains) g.on = false;
        // the filter (its cutoff moved by the modulation every 16 samples)
        if (filterOn)
        {
            if ((i & 15) == 0)
            {
                float cm = mod[6] + (te == 6 ? de * envNow() : 0.0f);
                cutNow = std::clamp (p[grain::Cutoff] * std::pow (2.0f, 4.0f * cm), 30.0f, (float) (sr * 0.45));
                const float g = (float) std::tan (3.14159265 * cutNow / sr);
                a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
            }
            float v3 = L - ic2L, v1 = a1 * ic1L + a2 * v3, v2 = ic2L + a2 * ic1L + a3 * v3;
            ic1L = 2 * v1 - ic1L; ic2L = 2 * v2 - ic2L; L = v2;
            v3 = R - ic2R; v1 = a1 * ic1R + a2 * v3; v2 = ic2R + a2 * ic1R + a3 * v3;
            ic1R = 2 * v1 - ic1R; ic2R = 2 * v2 - ic2R; R = v2;
        }
        // DIFFUSE: four all-passes a side smear the grains into a wash
        if (diffuse > 0 && ! ap[0].empty())
        {
            float wl = L, wr = R;
            for (int j = 0; j < 4; ++j)
            {
                auto& b = ap[j]; int& q = apPos[j];
                const float d = b[(size_t) q], y = d - 0.62f * wl;
                b[(size_t) q] = wl + 0.62f * y; wl = y; if (++q >= (int) b.size()) q = 0;
                auto& c = ap[j + 4]; int& r2 = apPos[j + 4];
                const float e = c[(size_t) r2], z = e - 0.62f * wr;
                c[(size_t) r2] = wr + 0.62f * z; wr = z; if (++r2 >= (int) c.size()) r2 = 0;
            }
            L += (wl - L) * diffuse; R += (wr - R) * diffuse;
        }
        float lv = level;
        const float lm = mod[7] + (te == 7 ? de * envNow() : 0.0f);
        if (lm != 0) lv *= std::max (0.0f, 1.0f + lm);
        left[i] = L * lv; right[i] = R * lv;
    }
    while (ev < events.size()) handle (events[ev++]);
    events.clear();
    bool any = false;
    for (const auto& nt : notes) any = any || nt.stage > 0;
    if (! any) posNow = (float) (p[grain::Position] * 0.01);
}
} // namespace prv
