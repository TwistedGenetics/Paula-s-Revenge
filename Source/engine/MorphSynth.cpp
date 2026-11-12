// Paula's Revenge - MORPH (see MorphSynth.h)
#include "MorphSynth.h"
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
    const std::vector<int>& chordOf (int c)
    {
        static const std::vector<int> chords[] = { { 0 }, { 0, 3, 7 }, { 0, 4, 7 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 },
                                                   { 0, 7, 12 }, { 0, 5, 7 }, { 0, 12 } };
        return chords[std::clamp (c, 0, 7)];
    }
    // (the MODEL choice: the models' names, | between)
    const char* modelChoices()
    {
        static const std::string s = []
        {
            std::string r;
            for (int i = 0; i < morph::kModels; ++i) { if (i) r += "|"; r += morph::modelName (i); }
            return r;
        }();
        return s.c_str();
    }
    inline float envStep (float& e, int& stage, float a, float d, float s, float r, double sr)
    {
        switch (stage)
        {
            case 1: e += (float) (1.0 / std::max (1.0, a * 0.001 * sr)); if (e >= 1.0f) { e = 1.0f; stage = 2; } break;
            case 2: e = s + (e - s) * (float) std::exp (-1.0 / std::max (1.0, d * 0.001 * sr * 0.3)); break;
            case 3: e *= (float) std::exp (-1.0 / std::max (1.0, r * 0.001 * sr * 0.3)); if (e < 0.00003f) { e = 0; stage = 0; } break;
            default: break;
        }
        return e;
    }
}

const std::vector<SynthParam>& morphSynthParams()
{
    static const char* waves = "SAW|PULSE|SQUARE|TRI|SINE|NOISE|SUPERSAW|ORGAN";
    static const std::vector<SynthParam> ps {
        { "WAVE 1",    "OSCILLATORS", 0, 7, 6,        3, "",   waves },
        { "WAVE 2",    "OSCILLATORS", 0, 7, 1,        3, "",   waves },
        { "OSC 2",     "OSCILLATORS", 0, 100, 40,     0, "%",  "" },
        { "DETUNE",    "OSCILLATORS", 0, 50, 12,      0, "C",  "" },
        { "OSC2 TUNE", "OSCILLATORS", -24, 24, 0,     2, "ST", "" },
        { "PW",        "OSCILLATORS", 5, 95, 30,      0, "%",  "" },
        { "MODEL",     "MORPH FILTER", 0, (float) (morph::kModels - 1), 0, 3, "", modelChoices() },
        { "MORPH",     "MORPH FILTER", 0, 100, 0,     0, "%",  "" },
        { "FREQ",      "MORPH FILTER", -36, 36, 0,    2, "ST", "" },
        { "EMPHASIS",  "MORPH FILTER", 0, 100, 55,    0, "%",  "" },
        { "KEY TRACK", "MORPH FILTER", 0, 100, 50,    0, "%",  "" },
        { "VEL>MORPH", "MORPH FILTER", -100, 100, 20, 0, "%",  "" },
        { "AMOUNT",    "MORPH ENV",   -100, 100, 80,  0, "%",  "" },
        { "ATTACK",    "MORPH ENV",   0.5f, 8000, 400, 1, "MS", "" },
        { "DECAY",     "MORPH ENV",   5, 8000, 1500,  1, "MS", "" },
        { "SUSTAIN",   "MORPH ENV",   0, 100, 60,     0, "%",  "" },
        { "RELEASE",   "MORPH ENV",   5, 8000, 800,   1, "MS", "" },
        { "RATE",      "MORPH LFO",   0.05f, 20, 0.4f, 1, "HZ", "" },
        { "DEPTH",     "MORPH LFO",   0, 100, 15,     0, "%",  "" },
        { "ATTACK",    "ENVELOPE",    0.5f, 8000, 30, 1, "MS", "" },
        { "DECAY",     "ENVELOPE",    5, 8000, 1500,  1, "MS", "" },
        { "SUSTAIN",   "ENVELOPE",    0, 100, 85,     0, "%",  "" },
        { "RELEASE",   "ENVELOPE",    5, 8000, 900,   1, "MS", "" },
        { "CHORD",     "SOUND",       0, 7, 0,        3, "",   "OFF|MINOR|MAJOR|MIN 7|MAJ 7|5TH|SUS 4|OCT" },
        { "OCTAVE",    "SOUND",       -3, 2, 0,       2, "",   "" },
        { "CHORUS",    "SPACE",       0, 100, 35,     0, "%",  "" },
        { "ECHO",      "SPACE",       0, 100, 15,     0, "%",  "" },
        { "ECHO TIME", "SPACE",       20, 1500, 375,  1, "MS", "" },
        { "VELOCITY",  "OUTPUT",      0, 100, 50,     0, "%",  "" },
        { "LEVEL",     "OUTPUT",      0, 100, 75,     0, "%",  "" },
    };
    return ps;
}

const char* morphPresetName (int i)
{
    static const char* n[kMorphPresets] = { "VOWEL PAD", "TALKING BASS", "PHASE STRINGS", "COMB BELLS", "ACID MORPH", "SCREAM LEAD" };
    return n[std::clamp (i, 0, kMorphPresets - 1)];
}

std::vector<float> morphPreset (int i)
{
    using namespace msyn;
    std::vector<float> v;
    for (const auto& q : morphSynthParams()) v.push_back (q.def);   // (VOWEL PAD's)
    auto set = [&] (std::initializer_list<std::pair<int, float>> l) { for (auto& [k, x] : l) v[(size_t) k] = x; };
    switch (std::clamp (i, 0, kMorphPresets - 1))
    {
        case 1: set ({ { Wave1, Saw }, { Wave2, Square }, { Mix, 50 }, { Octave, -1 }, { Model, 3 }, { Morph, 10 }, { EnvAmount, 70 }, { EnvAttack, 5 }, { EnvDecay, 350 }, { EnvSustain, 10 },
                       { LfoDepth, 0 }, { Attack, 2 }, { Decay, 800 }, { Sustain, 90 }, { Release, 120 }, { Chorus, 0 }, { Echo, 0 }, { KeyTrack, 30 }, { VelMorph, 40 } }); break;
        case 2: set ({ { Wave1, SuperSaw }, { Wave2, Saw }, { Detune, 20 }, { Model, 4 }, { Morph, 20 }, { EnvAmount, 30 }, { LfoRate, 0.15f }, { LfoDepth, 60 }, { Attack, 600 },
                       { Release, 1500 }, { Chorus, 60 }, { Echo, 20 } }); break;
        case 3: set ({ { Wave1, Organ }, { Wave2, Noise }, { Mix, 10 }, { Model, 9 }, { Morph, 70 }, { EnvAmount, -70 }, { EnvAttack, 1 }, { EnvDecay, 1200 }, { EnvSustain, 0 },
                       { Emphasis, 70 }, { Attack, 1 }, { Decay, 2500 }, { Sustain, 0 }, { Release, 1500 }, { Echo, 30 } }); break;
        case 4: set ({ { Wave1, Saw }, { Wave2, Pulse }, { Mix, 25 }, { Octave, -1 }, { Model, 7 }, { Morph, 5 }, { EnvAmount, 85 }, { EnvAttack, 1 }, { EnvDecay, 250 }, { EnvSustain, 0 },
                       { Emphasis, 75 }, { LfoDepth, 0 }, { Attack, 1 }, { Decay, 400 }, { Sustain, 70 }, { Release, 80 }, { Chorus, 0 }, { Echo, 15 }, { VelMorph, 50 } }); break;
        case 5: set ({ { Wave1, SuperSaw }, { Wave2, Square }, { Mix, 35 }, { Model, 10 }, { Morph, 30 }, { EnvAmount, 50 }, { EnvAttack, 200 }, { LfoRate, 5 }, { LfoDepth, 20 },
                       { Attack, 5 }, { Release, 300 }, { Chorus, 20 }, { Echo, 25 } }); break;
        default: break;
    }
    return v;
}

MorphSynth::MorphSynth()
{
    const auto& ps = morphSynthParams();
    for (int i = 0; i < msyn::Count; ++i) p[(size_t) i] = ps[(size_t) i].def;
}

void MorphSynth::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& b : chBuf) b.assign ((size_t) (sr * 0.04) + 4, 0.0f);
    for (auto& b : echoBuf) b.assign ((size_t) (sr * 1.6) + 4, 0.0f);
    chPos = echoPos = 0;
}

void MorphSynth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }

void MorphSynth::update (const Instrument& in)
{
    bendRange = std::clamp (in.bendRange, 1, 48);
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
}

void MorphSynth::setParameter (int i, float v01)
{
    const auto& ps = morphSynthParams();
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void MorphSynth::noteOn (int note, int vel, int offset) { if (events.size() < 1024) events.push_back ({ offset, vel > 0 ? 0 : 1, note, vel }); }
void MorphSynth::noteOff (int note, int offset)         { if (events.size() < 1024) events.push_back ({ offset, 1, note, 0 }); }
void MorphSynth::allNotesOff (int offset)               { if (events.size() < 1024) events.push_back ({ offset, 2, 0, 0 }); }
void MorphSynth::pitchBend (int v, int offset)          { if (events.size() < 1024) events.push_back ({ offset, 3, v, 0 }); }

int MorphSynth::voicesPlaying() const
{
    int n = 0;
    for (const auto& v : voices) if (v.stage != 0 || v.fading) ++n;
    return n;
}

float MorphSynth::morphOf (int i) const { return i >= 0 && i < kVoices ? voices[i].morphNow : 0.0f; }

void MorphSynth::startVoice (int key, float pitch, int vel)
{
    Voice* free = nullptr;
    for (auto& v : voices) if (v.stage == 0 && ! v.fading) { free = &v; break; }
    if (free == nullptr) for (auto& v : voices) if (v.fading && (free == nullptr || v.fade < free->fade)) free = &v;
    if (free == nullptr) for (auto& v : voices) if (free == nullptr || v.order < free->order) free = &v;
    *free = Voice {};
    Voice& v = *free;
    v.key = key; v.pitch = pitch; v.gate = true; v.stage = 1; v.mstage = 1; v.vel = (float) vel / 127.0f;
    for (size_t o = 0; o < v.ph1.size(); ++o)
    {
        seed = seed * 1664525u + 1013904223u; v.ph1[o] = (double) (seed >> 8) / 16777216.0;
        seed = seed * 1664525u + 1013904223u; v.ph2[o] = (double) (seed >> 8) / 16777216.0;
    }
    v.order = ++orderCount;
}

void MorphSynth::handle (const Event& e)
{
    if (e.type == 3) { bend = (float) (e.note - 8192) / 8192.0f * (float) bendRange; return; }
    if (e.type == 2) { for (auto& v : voices) if (v.stage != 0) { v.gate = false; v.stage = 3; v.mstage = 3; } return; }
    if (e.type == 1) { for (auto& v : voices) if (v.key == e.note && v.gate) { v.gate = false; if (v.stage != 0) { v.stage = 3; v.mstage = 3; } } return; }
    for (auto& v : voices) if (v.key == e.note && v.stage != 0 && ! v.fading) v.fading = true;
    const auto& chord = chordOf ((int) std::lround (p[msyn::Chord]));
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

float MorphSynth::osc (int wave, std::array<double, 5>& ph, double hz, float pw)
{
    const double dt = std::min (0.45, hz / sr);
    switch (wave)
    {
        case msyn::Saw: step (ph[0], dt); return saw (ph[0], dt);
        case msyn::Pulse: case msyn::Square:
        {
            step (ph[0], dt);
            double t2 = ph[0] + (wave == msyn::Square ? 0.5 : (double) pw); t2 -= std::floor (t2);
            return 0.5f * (saw (ph[0], dt) - saw (t2, dt));
        }
        case msyn::Tri: { step (ph[0], dt); double q = ph[0] + 0.25; q -= std::floor (q); return (float) (q < 0.5 ? 4.0 * q - 1.0 : 3.0 - 4.0 * q); }
        case msyn::Sine: step (ph[0], dt); return (float) std::sin (kTwoPi * ph[0]);
        case msyn::Noise: seed = seed * 1664525u + 1013904223u; return ((int) (seed >> 9) - 4194304) / 4194304.0f * 0.7f;
        case msyn::SuperSaw:
        {
            static const float off[5] = { -1.0f, -0.45f, 0.0f, 0.5f, 1.0f };
            float x = 0;
            for (int o = 0; o < 5; ++o)
            {
                const double d = std::min (0.45, hz * std::pow (2.0, off[o] * p[msyn::Detune] / 1200.0) / sr);
                step (ph[(size_t) o], d); x += saw (ph[(size_t) o], d);
            }
            return x * 0.38f;
        }
        default:   // ORGAN
        {
            step (ph[0], dt);
            float x = 0;
            static const float h[4] = { 1, 2, 3, 4 }, a[4] = { 1.0f, 0.6f, 0.35f, 0.25f };
            for (int k = 0; k < 4; ++k) if (hz * h[k] < sr * 0.45) x += a[k] * (float) std::sin (kTwoPi * ph[0] * h[k]);
            return x * 0.45f;
        }
    }
}

void MorphSynth::renderVoice (Voice& v, float lfo, float& outL, float& outR)
{
    if (v.stage == 0 && ! v.fading) return;
    const double semis = v.pitch + bend + p[msyn::Octave] * 12.0f;
    const double hz1 = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
    const double hz2 = 440.0 * std::pow (2.0, (semis + p[msyn::Osc2Tune] + p[msyn::Detune] * 0.01 - 69.0) / 12.0);
    const float pw = std::clamp (p[msyn::PulseWidth] * 0.01f, 0.05f, 0.95f);
    const float mix = p[msyn::Mix] * 0.01f;
    float x = osc ((int) std::lround (p[msyn::Wave1]), v.ph1, hz1, pw) * (1.0f - 0.5f * mix)
            + osc ((int) std::lround (p[msyn::Wave2]), v.ph2, hz2, pw) * mix;

    envStep (v.env, v.stage, p[msyn::Attack], p[msyn::Decay], p[msyn::Sustain] * 0.01f, p[msyn::Release], sr);
    int ms = v.mstage;
    envStep (v.menv, ms, p[msyn::EnvAttack], p[msyn::EnvDecay], p[msyn::EnvSustain] * 0.01f, p[msyn::EnvRelease], sr);
    v.mstage = ms == 0 ? 4 : ms;   // (the MORPH envelope rests at 0 when it's done; the amp decides the end)

    if (v.upd-- <= 0)
    {
        // where the filter is: MORPH, its envelope, the LFO, how hard it was played; FREQ with the keys
        const float m = p[msyn::Morph] * 0.01f + p[msyn::EnvAmount] * 0.01f * v.menv + p[msyn::LfoDepth] * 0.01f * 0.5f * lfo
                        + p[msyn::VelMorph] * 0.01f * (v.vel - 0.5f);
        v.morphNow = std::clamp (m, 0.0f, 1.0f);
        const float oct = p[msyn::Freq] / 12.0f + p[msyn::KeyTrack] * 0.01f * (v.pitch - 60.0f) / 12.0f;
        v.filter.set ((int) std::lround (p[msyn::Model]), v.morphNow, oct, p[msyn::Emphasis] * 0.01f, sr);
        v.upd = 31;
    }
    x = v.filter.process (x);

    const float vs = p[msyn::VelSens] * 0.01f;
    float amp = v.env * (1.0f - vs + vs * v.vel);
    if (v.fading)
    {
        v.fade -= (float) (1.0 / (0.004 * sr));
        if (v.fade <= 0.0f) { v = Voice {}; return; }
        amp *= v.fade;
    }
    const float share = 1.0f / std::sqrt ((float) chordOf ((int) std::lround (p[msyn::Chord])).size());
    outL += x * amp * share; outR += x * amp * share;
}

void MorphSynth::render (float* left, float* right, int n)
{
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    const float chorus = p[msyn::Chorus] * 0.01f, echo = p[msyn::Echo] * 0.01f;
    const float level = p[msyn::Level] * 0.01f * 0.6f;
    const int chLen = (int) chBuf[0].size(), ecLen = (int) echoBuf[0].size();
    const int ecDelay = std::clamp ((int) (p[msyn::EchoTime] * 0.001 * sr), 1, std::max (1, ecLen - 2));
    const float fb = 0.25f + 0.4f * echo;
    for (int i = 0; i < n; ++i)
    {
        while (ev < events.size() && events[ev].offset <= i) handle (events[ev++]);
        lfoPh += p[msyn::LfoRate] / sr; lfoPh -= std::floor (lfoPh);
        const float lfo = (float) std::sin (kTwoPi * lfoPh);
        float l = 0, r = 0;
        for (auto& v : voices) renderVoice (v, lfo, l, r);
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
