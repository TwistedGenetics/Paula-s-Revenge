// Paula's Revenge - ROM (see RomSynth.h)
#include "RomSynth.h"
#include "BuiltinSynths.h"
#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    const std::vector<int>& chordOf (int c)
    {
        static const std::vector<int> chords[] = { { 0 }, { 0, 3, 7 }, { 0, 4, 7 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 },
                                                   { 0, 7, 12 }, { 0, 5, 7 }, { 0, 12 } };
        return chords[std::clamp (c, 0, 7)];
    }
}

const std::vector<SynthParam>& romParams()
{
    static const std::vector<SynthParam> ps = []
    {
        std::vector<SynthParam> v;
        static const char* boxes[kRomTones] = { "TONE 1", "TONE 2", "TONE 3", "TONE 4" };
        for (int t = 0; t < kRomTones; ++t)
        {
            v.push_back ({ "LEVEL", boxes[t], 0, 100, 100, 0, "%", "" });
            v.push_back ({ "PAN",   boxes[t], -100, 100, 0, 0, "LR", "" });
            v.push_back ({ "TUNE",  boxes[t], -24, 24, 0,  2, "ST", "" });
            v.push_back ({ "FINE",  boxes[t], -50, 50, 0,  0, "C",  "" });
        }
        const std::vector<SynthParam> rest {
            { "CHORD",     "SOUND",    0, 7, 0,        3, "",   "OFF|MINOR|MAJOR|MIN 7|MAJ 7|5TH|SUS 4|OCT" },
            { "OCTAVE",    "SOUND",    -3, 2, 0,       2, "",   "" },
            { "VIBRATO",   "SOUND",    0, 100, 0,      0, "C",  "" },
            { "VIB RATE",  "SOUND",    0.5f, 10, 5,    1, "HZ", "" },
            { "CUTOFF",    "FILTER",   40, 18000, 18000, 1, "HZ", "" },
            { "RES",       "FILTER",   0, 100, 0,      0, "%",  "" },
            { "ENV AMT",   "FILTER",   -100, 100, 0,   0, "%",  "" },
            { "ENV DECAY", "FILTER",   5, 3000, 300,   1, "MS", "" },
            { "ATTACK",    "ENVELOPE", 0.5f, 4000, 2,  1, "MS", "" },
            { "DECAY",     "ENVELOPE", 5, 5000, 1000,  1, "MS", "" },
            { "SUSTAIN",   "ENVELOPE", 0, 100, 100,    0, "%",  "" },
            { "RELEASE",   "ENVELOPE", 5, 8000, 300,   1, "MS", "" },
            { "CHORUS",    "SPACE",    0, 100, 20,     0, "%",  "" },
            { "ECHO",      "SPACE",    0, 100, 0,      0, "%",  "" },
            { "ECHO TIME", "SPACE",    20, 1500, 375,  1, "MS", "" },
            { "VELOCITY",  "OUTPUT",   0, 100, 50,     0, "%",  "" },
            { "LEVEL",     "OUTPUT",   0, 100, 75,     0, "%",  "" },
        };
        v.insert (v.end(), rest.begin(), rest.end());
        return v;
    }();
    return ps;
}

RomSynth::RomSynth()
{
    const auto& ps = romParams();
    for (int i = 0; i < rom::Count; ++i) p[(size_t) i] = ps[(size_t) i].def;
}

void RomSynth::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& b : chBuf) b.assign ((size_t) (sr * 0.04) + 4, 0.0f);
    for (auto& b : echoBuf) b.assign ((size_t) (sr * 1.6) + 4, 0.0f);
    chPos = echoPos = 0;
}

void RomSynth::setValues (const std::vector<float>& v) { for (size_t i = 0; i < v.size() && i < p.size(); ++i) p[i] = v[i]; }

void RomSynth::setBank (std::shared_ptr<const RomBank> b)
{
    if (b == bank) return;
    bank = std::move (b);
    for (auto& v : voices) if (v.stage != 0 && ! v.fading) v.fading = true;   // (the old samples' notes fade out)
}

void RomSynth::update (const Instrument& in)
{
    bendRange = std::clamp (in.bendRange, 1, 48);
    setBank (in.rom);
    if (in.pluginState == lastState) return;
    lastState = in.pluginState;
    setValues (readBuiltinState (in));
}

void RomSynth::setParameter (int i, float v01)
{
    const auto& ps = romParams();
    if (i >= 0 && i < (int) ps.size()) p[(size_t) i] = ps[(size_t) i].toValue (v01);
}

void RomSynth::noteOn (int note, int vel, int offset) { if (events.size() < 1024) events.push_back ({ offset, vel > 0 ? 0 : 1, note, vel }); }
void RomSynth::noteOff (int note, int offset)         { if (events.size() < 1024) events.push_back ({ offset, 1, note, 0 }); }
void RomSynth::allNotesOff (int offset)               { if (events.size() < 1024) events.push_back ({ offset, 2, 0, 0 }); }
void RomSynth::pitchBend (int v, int offset)          { if (events.size() < 1024) events.push_back ({ offset, 3, v, 0 }); }

int RomSynth::voicesPlaying() const
{
    int n = 0;
    for (const auto& v : voices) if (v.stage != 0 || v.fading) ++n;
    return n;
}

void RomSynth::startVoice (int key, float pitch, int vel)
{
    Voice* free = nullptr;
    for (auto& v : voices) if (v.stage == 0 && ! v.fading) { free = &v; break; }
    if (free == nullptr) for (auto& v : voices) if (v.fading && (free == nullptr || v.fade < free->fade)) free = &v;
    if (free == nullptr) for (auto& v : voices) if (free == nullptr || v.order < free->order) free = &v;
    *free = Voice {};
    Voice& v = *free;
    v.key = key; v.pitch = pitch; v.gate = true; v.stage = 1; v.vel = (float) vel / 127.0f; v.fenv = 1.0f;
    v.bank = bank;
    v.order = ++orderCount;
    // each tone: the zone this note plays (by the note with the tone's TUNE: the sample nearest the pitch it plays at)
    if (bank != nullptr)
        for (int t = 0; t < kRomTones; ++t)
        {
            const int n = (int) std::lround (pitch + p[(size_t) (t * rom::kPerTone + 2)] + p[rom::Octave] * 12.0f);
            v.tones[(size_t) t].zone = romZoneFor (*bank, t, std::clamp (n, 0, 127));
            v.tones[(size_t) t].pos = 0;
            v.tones[(size_t) t].done = v.tones[(size_t) t].zone == nullptr;
        }
}

void RomSynth::handle (const Event& e)
{
    if (e.type == 3) { bend = (float) (e.note - 8192) / 8192.0f * (float) bendRange; return; }
    if (e.type == 2) { for (auto& v : voices) if (v.stage != 0) { v.gate = false; v.stage = 3; } return; }
    if (e.type == 1) { for (auto& v : voices) if (v.key == e.note && v.gate) { v.gate = false; if (v.stage != 0) v.stage = 3; } return; }
    for (auto& v : voices) if (v.key == e.note && v.stage != 0 && ! v.fading) v.fading = true;
    const auto& chord = chordOf ((int) std::lround (p[rom::Chord]));
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

void RomSynth::renderVoice (Voice& v, float& outL, float& outR)
{
    if (v.stage == 0 && ! v.fading) return;
    // vibrato
    v.vibPh += p[rom::VibRate] / sr; v.vibPh -= std::floor (v.vibPh);
    const double vib = p[rom::Vibrato] * 0.01 * std::sin (kTwoPi * v.vibPh);
    float l = 0, r = 0;
    bool any = false;
    for (int t = 0; t < kRomTones; ++t)
    {
        auto& tn = v.tones[(size_t) t];
        if (tn.done || tn.zone == nullptr) continue;
        const float level = p[(size_t) (t * rom::kPerTone)] * 0.01f;
        const RomZone& z = *tn.zone;
        const int len = (int) z.data.size();
        // its pitch: the note against the sample's root, with TUNE, FINE, OCTAVE, bend and vibrato
        const double semis = v.pitch + p[(size_t) (t * rom::kPerTone + 2)] + p[rom::Octave] * 12.0f + bend + vib
                             + p[(size_t) (t * rom::kPerTone + 3)] * 0.01 - z.root - z.rootCents * 0.01;
        const double inc = z.rate / sr * std::pow (2.0, semis / 12.0);
        // the sample at pos (linear between two; across the loop's join when it loops)
        const int i0 = (int) tn.pos;
        const double fr = tn.pos - i0;
        int i1 = i0 + 1;
        if (z.loops() && i1 > z.loopEnd) i1 = z.loopStart;
        const float a = z.data[(size_t) std::min (i0, len - 1)] / 32768.0f, b = i1 < len ? z.data[(size_t) i1] / 32768.0f : 0.0f;
        const float x = (a + (b - a) * (float) fr) * level;
        const float pan = std::clamp (p[(size_t) (t * rom::kPerTone + 1)] * 0.01f, -1.0f, 1.0f);
        l += x * std::min (1.0f, 1.0f - pan); r += x * std::min (1.0f, 1.0f + pan);
        any = true;
        tn.pos += inc;
        if (z.loops()) { const double ll = z.loopEnd - z.loopStart + 1; while (tn.pos > z.loopEnd + 1) tn.pos -= ll; }
        else if (tn.pos >= len - 1) tn.done = true;
    }
    if (! any && ! v.fading) { v.stage = 0; return; }

    // the envelopes
    const float sus = p[rom::Sustain] * 0.01f;
    switch (v.stage)
    {
        case 1: v.env += (float) (1.0 / std::max (1.0, p[rom::Attack] * 0.001 * sr)); if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; } break;
        case 2: v.env = sus + (v.env - sus) * (float) std::exp (-1.0 / std::max (1.0, p[rom::Decay] * 0.001 * sr * 0.3));
                if (sus <= 0.0f && v.env < 0.00003f) { v.env = 0; v.stage = 0; } break;
        case 3: v.env *= (float) std::exp (-1.0 / std::max (1.0, p[rom::Release] * 0.001 * sr * 0.3));
                if (v.env < 0.00003f) { v.env = 0; v.stage = 0; } break;
        default: break;
    }
    v.fenv *= (float) std::exp (-1.0 / std::max (1.0, p[rom::FilterDecay] * 0.001 * sr * 0.3));

    // the 12 dB low-pass (worked out every 16 samples; wide open it's left out)
    const bool open = p[rom::Cutoff] >= 17999.0f && std::abs (p[rom::EnvAmount]) < 0.5f && p[rom::Resonance] < 0.5f;
    if (! open)
    {
        if (v.upd-- <= 0)
        {
            const float oct = p[rom::EnvAmount] * 0.01f * 5.0f * v.fenv;
            const float cut = std::clamp (p[rom::Cutoff] * std::pow (2.0f, oct), 30.0f, (float) (sr * 0.45));
            const float g = (float) std::tan (3.14159265358979 * cut / sr);
            const float k = 2.0f - 1.9f * std::clamp (p[rom::Resonance] * 0.01f, 0.0f, 1.0f);
            v.a1 = 1.0f / (1.0f + g * (g + k)); v.a2 = g * v.a1; v.a3 = g * v.a2;
            v.upd = 15;
        }
        auto svf = [&] (float x, float* ic)
        {
            const float v3 = x - ic[1], v1 = v.a1 * ic[0] + v.a2 * v3, v2 = ic[1] + v.a2 * ic[0] + v.a3 * v3;
            ic[0] = 2 * v1 - ic[0]; ic[1] = 2 * v2 - ic[1]; return v2;
        };
        l = svf (l, v.s1[0]); r = svf (r, v.s1[1]);
    }
    const float vs = p[rom::VelSens] * 0.01f;
    float amp = v.env * (1.0f - vs + vs * v.vel);
    if (v.fading)
    {
        v.fade -= (float) (1.0 / (0.004 * sr));
        if (v.fade <= 0.0f) { v = Voice {}; return; }
        amp *= v.fade;
    }
    const float share = 1.0f / std::sqrt ((float) chordOf ((int) std::lround (p[rom::Chord])).size());
    outL += l * amp * share; outR += r * amp * share;
}

void RomSynth::render (float* left, float* right, int n)
{
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.offset < b.offset; });
    size_t ev = 0;
    const float chorus = p[rom::Chorus] * 0.01f, echo = p[rom::Echo] * 0.01f;
    const float level = p[rom::Level] * 0.01f;
    const int chLen = (int) chBuf[0].size(), ecLen = (int) echoBuf[0].size();
    const int ecDelay = std::clamp ((int) (p[rom::EchoTime] * 0.001 * sr), 1, std::max (1, ecLen - 2));
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
