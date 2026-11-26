// Paula's Revenge - CRAFTY built in (see CraftySynth.h)
#include "CraftySynth.h"
#include "BuiltinSynths.h"
#include "Song.h"
extern "C" {
#include "cr_inst.h"
#include "cr_text.h"
}
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <string>

namespace prv
{
namespace
{
    struct Pick { int id; const char* section; };
    const Pick kPicks[] = {
        { CR_W1BANK, "0:OSC|OSC>WAVES" },   { CR_W1POS, "0:OSC|OSC>WAVES" },   { CR_W2BANK, "0:OSC|OSC>WAVES" },
        { CR_W2POS, "0:OSC|OSC>WAVES" },    { CR_MIX, "0:OSC|OSC>WAVES" },
        { CR_OMMODE, "0:OSC|OSC>MODIFIER" }, { CR_OMAMT, "0:OSC|OSC>MODIFIER" }, { CR_BMODE, "0:OSC|OSC>MODIFIER" },
        { CR_BAMT, "0:OSC|OSC>MODIFIER" },  { CR_SPREAD, "0:OSC|OSC>MODIFIER" },
        { CR_TUNE, "0:OSC|OSC>PITCH + SUB" }, { CR_FINE, "0:OSC|OSC>PITCH + SUB" }, { CR_OCT, "0:OSC|OSC>PITCH + SUB" },
        { CR_GLIDE, "0:OSC|OSC>PITCH + SUB" }, { CR_SUBLVL, "0:OSC|OSC>PITCH + SUB" }, { CR_SUBOCT, "0:OSC|OSC>PITCH + SUB" },
        { CR_SUBWAVE, "0:OSC|OSC>PITCH + SUB" },
        { CR_FTYPE, "1:FILTER + AMP|FLT>FILTER" }, { CR_CUT, "1:FILTER + AMP|FLT>FILTER" }, { CR_RES, "1:FILTER + AMP|FLT>FILTER" },
        { CR_MORPH, "1:FILTER + AMP|FLT>FILTER" }, { CR_FEGAMT, "1:FILTER + AMP|FLT>FILTER" },
        { CR_FA, "1:FILTER + AMP|FLT>FILTER EG" }, { CR_FD, "1:FILTER + AMP|FLT>FILTER EG" }, { CR_FS, "1:FILTER + AMP|FLT>FILTER EG" },
        { CR_FR, "1:FILTER + AMP|FLT>FILTER EG" },
        { CR_AA, "1:FILTER + AMP|FLT>AMP EG" }, { CR_AD, "1:FILTER + AMP|FLT>AMP EG" }, { CR_AS, "1:FILTER + AMP|FLT>AMP EG" },
        { CR_AR, "1:FILTER + AMP|FLT>AMP EG" }, { CR_AEGAMT, "1:FILTER + AMP|FLT>AMP EG" },
        { CR_MA, "2:MOD|MOD>MOD EG" }, { CR_MD, "2:MOD|MOD>MOD EG" }, { CR_MS, "2:MOD|MOD>MOD EG" }, { CR_MR, "2:MOD|MOD>MOD EG" },
        { CR_L1RATE, "2:MOD|MOD>LFO 1" }, { CR_L1SHAPE, "2:MOD|MOD>LFO 1" }, { CR_L1MODE, "2:MOD|MOD>LFO 1" }, { CR_L1SYNC, "2:MOD|MOD>LFO 1" },
        { CR_L2RATE, "2:MOD|MOD>LFO 2" }, { CR_L2SHAPE, "2:MOD|MOD>LFO 2" }, { CR_L2MODE, "2:MOD|MOD>LFO 2" }, { CR_L2SYNC, "2:MOD|MOD>LFO 2" },
        { CR_DIST, "3:FX|FX>DRIVE + DELAY" }, { CR_DMIX, "3:FX|FX>DRIVE + DELAY" }, { CR_DTIME, "3:FX|FX>DRIVE + DELAY" },
        { CR_DFB, "3:FX|FX>DRIVE + DELAY" }, { CR_DSYNC, "3:FX|FX>DRIVE + DELAY" },
        { CR_VMODE, "3:FX|FX>SPACE" }, { CR_VSIZE, "3:FX|FX>SPACE" }, { CR_VDECAY, "3:FX|FX>SPACE" }, { CR_VMIX, "3:FX|FX>SPACE" },
        { CR_VWARP, "3:FX|FX>SPACE" }, { CR_VDENS, "3:FX|FX>SPACE" }, { CR_VMOD, "3:FX|FX>SPACE" },
        { CR_CHMIX, "3:FX|FX>COLOUR" }, { CR_CHRATE, "3:FX|FX>COLOUR" }, { CR_PHMIX, "3:FX|FX>COLOUR" }, { CR_PHRATE, "3:FX|FX>COLOUR" },
        { CR_ERA, "3:FX|FX>COLOUR" }, { CR_CRUSH, "3:FX|FX>COLOUR" }, { CR_OTT, "3:FX|FX>COLOUR" },
        { CR_ARP, "4:ARP + OUT|ARP>ARP" }, { CR_ARPRATE, "4:ARP + OUT|ARP>ARP" },
        { CR_VOL, "4:ARP + OUT|ARP>OUTPUT" },
    };
    constexpr int kPickCount = (int) (sizeof (kPicks) / sizeof (kPicks[0]));

    // a value as the CRAFTsynth writes it ("2.1K", "1/8D", "LADDER"): the control's id rides in its unit ("#12")
    std::string craftyText (const SynthParam& q, float v)
    {
        const int id = q.unit != nullptr && q.unit[0] == '#' ? std::atoi (q.unit + 1) : -1;
        if (id < 0 || id >= (int) CR_NP) return std::to_string ((int) std::lround (v));
        cr_patch_t p;
        cr_patch_init (&p, "TEXT");
        p.p[id] = (uint8_t) std::clamp ((int) std::lround (v), 0, 127);
        char b[48] = {};
        cr_param_text (&p, (uint32_t) id, b, 0, nullptr, nullptr);
        return b;
    }
    std::string presetName (int slot)
    {
        cr_patch_t p;
        cr_preset_get (nullptr, (uint32_t) slot, &p);
        std::string n (p.name, strnlen (p.name, sizeof (p.name)));
        while (! n.empty() && n.back() == ' ') n.pop_back();
        for (auto& c : n) c = (char) std::toupper ((unsigned char) c);
        return n.empty() ? "INIT" : n;
    }
    struct Table
    {
        std::deque<std::string> store;
        std::vector<SynthParam> params;
        std::vector<int> crId;   // per page control: its patch byte (-1: PRESET)
        const char* keep (const std::string& s) { store.push_back (s); return store.back().c_str(); }
    };
    const Table& table()
    {
        static Table t = []
        {
            Table tb;
            std::string names;
            for (int s = 0; s < (int) CR_NFACTORY; ++s) names += (s ? "|" : "") + presetName (s);
            tb.params.push_back ({ "PRESET", "4:ARP + OUT|ARP>OUTPUT", 0, (float) CR_NFACTORY - 1, 0, 3, "", tb.keep (names) });
            tb.crId.push_back (-1);
            cr_patch_t p0;
            cr_preset_get (nullptr, 0, &p0);
            for (const auto& pk : kPicks)
            {
                SynthParam q { tb.keep (std::string (CR_P[pk.id].name)), pk.section, 0, 127, (float) p0.p[pk.id], 0,
                               tb.keep ("#" + std::to_string (pk.id)), "" };
                q.textFn = craftyText;
                tb.params.push_back (q);
                tb.crId.push_back (pk.id);
            }
            return tb;
        }();
        return t;
    }
}

const std::vector<SynthParam>& craftyParams() { return table().params; }
int craftyPresetCount() { return (int) CR_NFACTORY; }
const char* craftyPresetName (int i)
{
    static std::vector<std::string> names = [] { std::vector<std::string> n; for (int s = 0; s < (int) CR_NFACTORY; ++s) n.push_back (presetName (s)); return n; }();
    return names[(size_t) std::clamp (i, 0, (int) names.size() - 1)].c_str();
}
std::vector<float> craftyPreset (int i)
{
    const auto& t = table();
    cr_patch_t p;
    i = std::clamp (i, 0, (int) CR_NFACTORY - 1);
    cr_preset_get (nullptr, (uint32_t) i, &p);
    std::vector<float> v (t.params.size());
    v[0] = (float) i;
    for (size_t k = 1; k < v.size(); ++k) v[k] = (float) p.p[t.crId[k]];
    return v;
}

struct CraftySynth::Impl
{
    std::unique_ptr<cr_inst_t> inst { new cr_inst_t {} };
    std::vector<float> values, base;
    std::vector<int> sent;
    std::vector<uint8_t> lastState;
    int presetNow = -1;
    double outRate = 48000.0, step = 44100.0 / 48000.0, pos = 0;   // (44.1 kHz in, the song's rate out)
    float hist[4][2] {};                                            // the last 4 frames in (cubic interpolation)
    float chunk[CR_INST_FRAMES * 2] {};
    int chunkLen = 0, chunkAt = 0;
    double bpm = 125.0;
    struct Ev { int offset; int kind; int a, b; };
    std::vector<Ev> events;

    void apply()
    {
        const auto& t = table();
        if (values.size() != t.params.size()) return;
        const int preset = (int) std::lround (values[0]);
        if (preset != presetNow)
        {
            cr_inst_load (inst.get(), (uint32_t) std::clamp (preset, 0, (int) CR_NFACTORY - 1));
            presetNow = preset;
            sent.assign (t.params.size(), -1);
        }
        for (size_t k = 1; k < t.params.size(); ++k)
        {
            const int v = std::clamp ((int) std::lround (values[k]), 0, 127);
            if (sent[k] == v) continue;
            sent[k] = v;
            cr_inst_param (inst.get(), (uint32_t) t.crId[k], (uint32_t) v);
        }
    }
    void handle (const Ev& e)
    {
        switch (e.kind)
        {
            case 0: cr_inst_note (inst.get(), (uint32_t) e.a, (uint32_t) e.b); break;
            case 1: cr_inst_note (inst.get(), (uint32_t) e.a, 0); break;
            case 2: cr_inst_all_off (inst.get()); break;
            case 3: { const uint8_t m[3] = { 0xE0, (uint8_t) (e.a & 0x7F), (uint8_t) ((e.a >> 7) & 0x7F) }; cr_inst_midi (inst.get(), m, 3); break; }
            case 4: { const uint8_t m[3] = { 0xB0, (uint8_t) e.a, (uint8_t) e.b }; cr_inst_midi (inst.get(), m, 3); break; }
            default: break;
        }
    }
    // the next frame in, at 44.1 kHz
    void pull (float& l, float& r)
    {
        if (chunkAt >= chunkLen)
        {
            cr_inst_transport (inst.get(), -1.0, (float) bpm, 1);
            cr_inst_render (inst.get(), chunk, CR_INST_FRAMES);
            chunkLen = (int) CR_INST_FRAMES; chunkAt = 0;
        }
        l = chunk[chunkAt * 2]; r = chunk[chunkAt * 2 + 1];
        ++chunkAt;
    }
};

CraftySynth::CraftySynth() : d (std::make_unique<Impl>())
{
    cr_inst_init (d->inst.get());
    for (const auto& q : craftyParams()) d->values.push_back (q.def);
    d->base = d->values;
    d->apply();
    d->events.reserve (256);
}

CraftySynth::~CraftySynth() = default;

void CraftySynth::prepare (double sampleRate, int)
{
    d->outRate = sampleRate > 0 ? sampleRate : 48000.0;
    d->step = (double) CR_INST_RATE / d->outRate;
}

void CraftySynth::noteOn (int note, int vel, int offset)   { if (d->events.size() < 1024) d->events.push_back ({ offset, vel > 0 ? 0 : 1, note, vel }); }
void CraftySynth::noteOff (int note, int offset)           { if (d->events.size() < 1024) d->events.push_back ({ offset, 1, note, 0 }); }
void CraftySynth::allNotesOff (int offset)                 { if (d->events.size() < 1024) d->events.push_back ({ offset, 2, 0, 0 }); }
void CraftySynth::pitchBend (int v, int offset)            { if (d->events.size() < 1024) d->events.push_back ({ offset, 3, std::clamp (v, 0, 16383), 0 }); }
void CraftySynth::controller (int cc, int value, int offset) { if (d->events.size() < 1024) d->events.push_back ({ offset, 4, std::clamp (cc, 0, 127), std::clamp (value, 0, 127) }); }

void CraftySynth::setParameter (int i, float v01)
{
    const auto& ps = craftyParams();
    if (i <= 0 || i >= (int) ps.size()) return;
    d->values[(size_t) i] = ps[(size_t) i].toValue (v01);
    d->apply();
}

void CraftySynth::restoreParameter (int i)
{
    if (i > 0 && i < (int) d->values.size()) { d->values[(size_t) i] = d->base[(size_t) i]; d->apply(); }
}

void CraftySynth::setValues (const std::vector<float>& v)
{
    for (size_t i = 0; i < v.size() && i < d->values.size(); ++i) d->values[i] = v[i];
    d->base = d->values;
    d->apply();
}

void CraftySynth::update (const Instrument& in)
{
    if (in.pluginState == d->lastState) return;
    d->lastState = in.pluginState;
    setValues (readBuiltinState (in));
}

void CraftySynth::updateSong (const Song& s) { d->bpm = (double) s.tempo * 6.0 / std::max (1, s.speed); }

void CraftySynth::render (float* left, float* right, int n)
{
    std::stable_sort (d->events.begin(), d->events.end(), [] (const Impl::Ev& a, const Impl::Ev& b) { return a.offset < b.offset; });
    size_t ev = 0;
    for (int i = 0; i < n; ++i)
    {
        while (ev < d->events.size() && d->events[ev].offset <= i) d->handle (d->events[ev++]);
        // step through the 44.1 kHz frames; cubic (Hermite) between the middle two of the last four
        d->pos += d->step;
        while (d->pos >= 1.0)
        {
            d->pos -= 1.0;
            for (int k = 0; k < 3; ++k) { d->hist[k][0] = d->hist[k + 1][0]; d->hist[k][1] = d->hist[k + 1][1]; }
            d->pull (d->hist[3][0], d->hist[3][1]);
        }
        const float t = (float) d->pos;
        for (int c = 0; c < 2; ++c)
        {
            const float y0 = d->hist[0][c], y1 = d->hist[1][c], y2 = d->hist[2][c], y3 = d->hist[3][c];
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            const float y = ((c3 * t + c2) * t + c1) * t + y1;
            (c == 0 ? left : right)[i] = y;
        }
    }
    d->events.erase (d->events.begin(), d->events.begin() + (long) ev);
    for (auto& e : d->events) e.offset = std::max (0, e.offset - n);
}

bool CraftySynth::sounding() const { return cr_synth_sounding (&d->inst->syn) != 0; }
} // namespace prv
