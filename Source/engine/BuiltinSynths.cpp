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
    // (GRANULAR: next)
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
} // namespace prv
