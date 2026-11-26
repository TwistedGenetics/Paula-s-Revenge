// Paula's Revenge - HELIX built in (see HelixSynth.h)
#include "HelixSynth.h"
#include "BuiltinSynths.h"
#include "Song.h"
#include "helix/Engine.h"
#include "helix/Preset.h"
#include "helix/TableLibrary.h"
#include "helix/WavetableGen.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <mutex>

namespace prv
{
namespace
{
    // the page: HELIX's own parameters (by id), in boxes on tabs, with the names they have here
    struct Pick { const char* id; const char* name; const char* section; };
    const Pick kPicks[] = {
        { "a_on",       "OSC A",      "0:OSC A|A>OSC A" },
        { "a_table",    "TABLE",      "0:OSC A|A>OSC A" },
        { "a_pos",      "WT POS",     "0:OSC A|A>OSC A" },
        { "a_oct",      "OCTAVE",     "0:OSC A|A>OSC A" },
        { "a_semi",     "SEMI",       "0:OSC A|A>OSC A" },
        { "a_fine",     "FINE",       "0:OSC A|A>OSC A" },
        { "a_level",    "LEVEL",      "0:OSC A|A>OSC A" },
        { "a_w1_mode",  "WARP",       "0:OSC A|A>WARP + UNISON" },
        { "a_w1_amt",   "WARP AMT",   "0:OSC A|A>WARP + UNISON" },
        { "a_unison",   "UNISON",     "0:OSC A|A>WARP + UNISON" },
        { "a_detune",   "DETUNE",     "0:OSC A|A>WARP + UNISON" },
        { "a_spread",   "SPREAD",     "0:OSC A|A>WARP + UNISON" },
        { "b_on",       "OSC B",      "1:OSC B|B>OSC B" },
        { "b_table",    "TABLE",      "1:OSC B|B>OSC B" },
        { "b_pos",      "WT POS",     "1:OSC B|B>OSC B" },
        { "b_oct",      "OCTAVE",     "1:OSC B|B>OSC B" },
        { "b_semi",     "SEMI",       "1:OSC B|B>OSC B" },
        { "b_fine",     "FINE",       "1:OSC B|B>OSC B" },
        { "b_level",    "LEVEL",      "1:OSC B|B>OSC B" },
        { "b_w1_mode",  "WARP",       "1:OSC B|B>WARP + UNISON" },
        { "b_w1_amt",   "WARP AMT",   "1:OSC B|B>WARP + UNISON" },
        { "b_unison",   "UNISON",     "1:OSC B|B>WARP + UNISON" },
        { "b_detune",   "DETUNE",     "1:OSC B|B>WARP + UNISON" },
        { "b_spread",   "SPREAD",     "1:OSC B|B>WARP + UNISON" },
        { "sub_on",     "SUB",        "2:SUB + NOISE|SUB>SUB" },
        { "sub_shape",  "SHAPE",      "2:SUB + NOISE|SUB>SUB" },
        { "sub_oct",    "OCTAVE",     "2:SUB + NOISE|SUB>SUB" },
        { "sub_level",  "LEVEL",      "2:SUB + NOISE|SUB>SUB" },
        { "noise_on",   "NOISE",      "2:SUB + NOISE|SUB>NOISE" },
        { "noise_type", "TYPE",       "2:SUB + NOISE|SUB>NOISE" },
        { "noise_level","LEVEL",      "2:SUB + NOISE|SUB>NOISE" },
        { "f1_on",      "FILTER 1",   "3:FILTERS|FLT>FILTER 1" },
        { "f1_type",    "TYPE",       "3:FILTERS|FLT>FILTER 1" },
        { "f1_cutoff",  "CUTOFF",     "3:FILTERS|FLT>FILTER 1" },
        { "f1_res",     "RES",        "3:FILTERS|FLT>FILTER 1" },
        { "f1_drive",   "DRIVE",      "3:FILTERS|FLT>FILTER 1" },
        { "f1_keytrk",  "KEY TRACK",  "3:FILTERS|FLT>FILTER 1" },
        { "f2_on",      "FILTER 2",   "3:FILTERS|FLT>FILTER 2" },
        { "f2_type",    "TYPE",       "3:FILTERS|FLT>FILTER 2" },
        { "f2_cutoff",  "CUTOFF",     "3:FILTERS|FLT>FILTER 2" },
        { "f2_res",     "RES",        "3:FILTERS|FLT>FILTER 2" },
        { "filter_blend","SER > PAR", "3:FILTERS|FLT>FILTER 2" },
        { "e1_a",       "ATTACK",     "4:ENVELOPES|ENV>AMP ENV" },
        { "e1_d",       "DECAY",      "4:ENVELOPES|ENV>AMP ENV" },
        { "e1_s",       "SUSTAIN",    "4:ENVELOPES|ENV>AMP ENV" },
        { "e1_r",       "RELEASE",    "4:ENVELOPES|ENV>AMP ENV" },
        { "e2_a",       "ATTACK",     "4:ENVELOPES|ENV>FILTER ENV" },
        { "e2_d",       "DECAY",      "4:ENVELOPES|ENV>FILTER ENV" },
        { "e2_s",       "SUSTAIN",    "4:ENVELOPES|ENV>FILTER ENV" },
        { "e2_r",       "RELEASE",    "4:ENVELOPES|ENV>FILTER ENV" },
        { "l1_shape",   "SHAPE",      "5:LFOS|LFO>LFO 1" },
        { "l1_sync",    "SYNC",       "5:LFOS|LFO>LFO 1" },
        { "l1_rate",    "RATE",       "5:LFOS|LFO>LFO 1" },
        { "l1_div",     "DIV",        "5:LFOS|LFO>LFO 1" },
        { "l2_shape",   "SHAPE",      "5:LFOS|LFO>LFO 2" },
        { "l2_sync",    "SYNC",       "5:LFOS|LFO>LFO 2" },
        { "l2_rate",    "RATE",       "5:LFOS|LFO>LFO 2" },
        { "l2_div",     "DIV",        "5:LFOS|LFO>LFO 2" },
        { "macro1",     "MACRO 1",    "6:MACROS + VOICE|MAC>MACROS" },
        { "macro2",     "MACRO 2",    "6:MACROS + VOICE|MAC>MACROS" },
        { "macro3",     "MACRO 3",    "6:MACROS + VOICE|MAC>MACROS" },
        { "macro4",     "MACRO 4",    "6:MACROS + VOICE|MAC>MACROS" },
        { "macro5",     "MACRO 5",    "6:MACROS + VOICE|MAC>MACROS" },
        { "macro6",     "MACRO 6",    "6:MACROS + VOICE|MAC>MACROS" },
        { "macro7",     "MACRO 7",    "6:MACROS + VOICE|MAC>MACROS" },
        { "macro8",     "MACRO 8",    "6:MACROS + VOICE|MAC>MACROS" },
        { "poly_mode",  "MODE",       "6:MACROS + VOICE|MAC>VOICE" },
        { "glide",      "GLIDE",      "6:MACROS + VOICE|MAC>VOICE" },
        { "vel_sens",   "VELOCITY",   "6:MACROS + VOICE|MAC>VOICE" },
        { "master",     "LEVEL",      "6:MACROS + VOICE|MAC>VOICE" },
    };
    constexpr int kPickCount = (int) (sizeof (kPicks) / sizeof (kPicks[0]));

    int helixIndexOf (const char* id)
    {
        for (int i = 0; i < helix::kNumParams; ++i) if (std::string (helix::paramDef (i).id) == id) return i;
        return -1;
    }
    std::string upper (std::string s) { for (auto& c : s) c = (char) std::toupper ((unsigned char) c); return s; }
    std::string joined (const std::vector<std::string>& v)
    {
        std::string out;
        for (size_t i = 0; i < v.size(); ++i) out += (i ? "|" : "") + upper (v[i]);
        return out;
    }
    struct Table
    {
        std::vector<int> helixIndex;               // per page control (from 1): which HELIX parameter
        std::vector<std::string> strings;          // (the names, units and choices the params point at)
        std::deque<std::string> store;
        std::vector<SynthParam> params;
        const char* keep (const std::string& s) { store.push_back (s); return store.back().c_str(); }
    };
    int builtinIndexOf (const std::string& id)
    {
        const auto& names = helix::builtinTableNames();
        const std::string n = id.rfind ("builtin:", 0) == 0 ? id.substr (8) : id;
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return (int) i;
        return 0;
    }
    std::vector<float> valuesOf (int presetIndex, const Table& t);
    const Table& table()
    {
        static Table t = []
        {
            Table tb;
            const auto& fp = helix::factoryPresets();
            std::vector<std::string> names;
            for (const auto& p : fp) names.push_back (p.name);
            tb.helixIndex.push_back (-1);
            tb.params.push_back ({ "PRESET", "6:MACROS + VOICE|MAC>VOICE", 0, (float) std::max (0, (int) fp.size() - 1), 0, 3, "", tb.keep (joined (names)) });
            for (const auto& pk : kPicks)
            {
                const int hi = helixIndexOf (pk.id);
                if (hi < 0) continue;
                const auto& def = helix::paramDef (hi);
                SynthParam q { pk.name, pk.section, def.min, def.max, def.def, 0, "", "" };
                std::string unit = upper (def.unit ? def.unit : "");
                if (unit == "CT") unit = "C";
                q.unit = tb.keep (unit);
                switch (def.kind)
                {
                    case helix::ParamKind::Log:     q.kind = def.min > 0.0f ? 1 : 0; break;
                    case helix::ParamKind::Int:     q.kind = 2; break;
                    case helix::ParamKind::Bool:    q.kind = 3; q.choices = "OFF|ON"; break;
                    case helix::ParamKind::Choice:  q.kind = 3; q.choices = def.choices ? tb.keep (joined (*def.choices)) : ""; break;
                    default:                        q.kind = 0; break;
                }
                if (std::string (pk.id) == "a_table" || std::string (pk.id) == "b_table")
                {
                    // the tables: HELIX's built-in ones, by name
                    const auto& tn = helix::builtinTableNames();
                    q.kind = 3; q.min = 0; q.max = (float) std::max (0, (int) tn.size() - 1); q.def = 0;
                    q.choices = tb.keep (joined (tn));
                }
                // (percent-like 0..1 controls read as %)
                if (q.kind == 0 && unit.empty() && def.min == 0.0f && def.max == 1.0f) { q.min = 0; q.max = 100; q.def = def.def * 100.0f; q.unit = "%"; }
                tb.helixIndex.push_back (hi);
                tb.params.push_back (q);
            }
            // the defaults: the first factory preset's own values
            const auto v = valuesOf (0, tb);
            for (size_t i = 0; i < tb.params.size() && i < v.size(); ++i) tb.params[i].def = v[i];
            return tb;
        }();
        return t;
    }
    // a page value -> HELIX's plain value (the % ones back to 0..1), and back
    float toHelix (const SynthParam& q, int hi, float v)
    {
        const auto& def = helix::paramDef (hi);
        if (q.unit != nullptr && std::string (q.unit) == "%" && def.max == 1.0f && def.min == 0.0f) return v * 0.01f;
        return v;
    }
    float fromHelix (const SynthParam& q, int hi, float v)
    {
        const auto& def = helix::paramDef (hi);
        if (q.unit != nullptr && std::string (q.unit) == "%" && def.max == 1.0f && def.min == 0.0f) return v * 100.0f;
        return v;
    }
    std::vector<float> valuesOf (int presetIndex, const Table& t)
    {
        const auto& fp = helix::factoryPresets();
        std::vector<float> v (t.params.size(), 0.0f);
        if (fp.empty()) return v;
        const auto& p = fp[(size_t) std::clamp (presetIndex, 0, (int) fp.size() - 1)];
        v[0] = (float) std::clamp (presetIndex, 0, (int) fp.size() - 1);
        for (size_t i = 1; i < t.params.size(); ++i)
        {
            const int hi = t.helixIndex[i];
            if (hi == helix::P_A_TABLE) v[i] = (float) builtinIndexOf (p.tables[0]);
            else if (hi == helix::P_B_TABLE) v[i] = (float) builtinIndexOf (p.tables[1]);
            else v[i] = fromHelix (t.params[i], hi, p.params[hi]);
        }
        return v;
    }
}

const std::vector<SynthParam>& helixParams() { return table().params; }
int helixPresetCount() { return (int) helix::factoryPresets().size(); }
const char* helixPresetName (int i)
{
    static std::vector<std::string> names = [] { std::vector<std::string> n; for (const auto& p : helix::factoryPresets()) n.push_back (upper (p.name)); return n; }();
    return names.empty() ? "" : names[(size_t) std::clamp (i, 0, (int) names.size() - 1)].c_str();
}
std::vector<float> helixPreset (int i) { return valuesOf (i, table()); }

struct HelixSynth::Impl
{
    helix::Engine engine;
    std::unique_ptr<helix::TableLibrary> library;
    std::vector<helix::MidiMsg> events;
    std::vector<float> values, base, applied;
    std::vector<uint8_t> lastState;
    int presetNow = -1;
    int tableNow[2] { -1, -1 };
    helix::Transport transport;
    Impl() { library = std::make_unique<helix::TableLibrary> (engine); events.reserve (512); transport.valid = true; transport.playing = true; }

    void push (int offset, uint8_t a, uint8_t b, uint8_t c)
    {
        if (events.size() < 1024) events.push_back (helix::MidiMsg::make (std::max (0, offset), a, b, c));
    }
    void apply()
    {
        const auto& t = table();
        if (values.size() != t.params.size()) return;
        const int preset = (int) std::lround (values[0]);
        if (preset != presetNow)
        {
            // another preset under it: everything it has (matrix, effects, tables), then the page's controls over it
            const auto& fp = helix::factoryPresets();
            if (! fp.empty()) helix::applyPreset (fp[(size_t) std::clamp (preset, 0, (int) fp.size() - 1)], engine, library.get());
            presetNow = preset;
            applied.assign (t.params.size(), std::numeric_limits<float>::quiet_NaN());
            for (int s = 0; s < 2; ++s) tableNow[s] = -1;
        }
        for (size_t i = 1; i < t.params.size(); ++i)
        {
            if (applied[i] == values[i]) continue;
            applied[i] = values[i];
            const int hi = t.helixIndex[i];
            if (hi == helix::P_A_TABLE || hi == helix::P_B_TABLE)
            {
                const int s = hi == helix::P_A_TABLE ? 0 : 1, idx = (int) std::lround (values[i]);
                if (idx != tableNow[s])
                {
                    tableNow[s] = idx;
                    engine.params().set (hi, (float) idx);
                    engine.collectGarbage();   // (the table it had before goes, now and then)
                    library->requestLoad (s, idx);
                }
                continue;
            }
            engine.params().set (hi, toHelix (t.params[i], hi, values[i]));
        }
    }
};

HelixSynth::HelixSynth() : d (std::make_unique<Impl>())
{
    for (const auto& q : helixParams()) d->values.push_back (q.def);
    d->base = d->values;
    d->apply();
}

HelixSynth::~HelixSynth() = default;

void HelixSynth::prepare (double sampleRate, int maxBlock)
{
    d->engine.prepare (sampleRate > 0 ? sampleRate : 48000.0, std::max (32, maxBlock));
    d->engine.setRealtime (true);
}

void HelixSynth::noteOn (int note, int vel, int offset)   { d->push (offset, vel > 0 ? 0x90 : 0x80, (uint8_t) std::clamp (note, 0, 127), (uint8_t) std::clamp (vel, 0, 127)); }
void HelixSynth::noteOff (int note, int offset)           { d->push (offset, 0x80, (uint8_t) std::clamp (note, 0, 127), 0); }
void HelixSynth::allNotesOff (int offset)                 { d->push (offset, 0xB0, 123, 0); }
void HelixSynth::pitchBend (int v, int offset)            { v = std::clamp (v, 0, 16383); d->push (offset, 0xE0, (uint8_t) (v & 0x7F), (uint8_t) (v >> 7)); }
void HelixSynth::controller (int cc, int value, int offset) { d->push (offset, 0xB0, (uint8_t) std::clamp (cc, 0, 127), (uint8_t) std::clamp (value, 0, 127)); }

void HelixSynth::setParameter (int i, float v01)
{
    const auto& ps = helixParams();
    if (i <= 0 || i >= (int) ps.size()) return;   // (PRESET isn't a knob)
    d->values[(size_t) i] = ps[(size_t) i].toValue (v01);
    d->apply();
}

void HelixSynth::restoreParameter (int i)
{
    if (i > 0 && i < (int) d->values.size()) { d->values[(size_t) i] = d->base[(size_t) i]; d->apply(); }
}

void HelixSynth::setValues (const std::vector<float>& v)
{
    for (size_t i = 0; i < v.size() && i < d->values.size(); ++i) d->values[i] = v[i];
    d->base = d->values;
    d->apply();
}

void HelixSynth::update (const Instrument& in)
{
    if (in.pluginState == d->lastState) return;
    d->lastState = in.pluginState;
    setValues (readBuiltinState (in));
}

void HelixSynth::updateSong (const Song& s) { d->transport.bpm = (double) s.tempo * 6.0 / std::max (1, s.speed); }

void HelixSynth::render (float* left, float* right, int n)
{
    std::stable_sort (d->events.begin(), d->events.end(), [] (const helix::MidiMsg& a, const helix::MidiMsg& b) { return a.offset < b.offset; });
    // (events past this block wait for the next)
    size_t k = 0;
    while (k < d->events.size() && d->events[k].offset < n) ++k;
    d->engine.process (left, right, n, d->events.data(), (int) k, d->transport);
    d->events.erase (d->events.begin(), d->events.begin() + (long) k);
    for (auto& e : d->events) e.offset -= n;
    d->transport.ppq += (double) n / std::max (1.0, d->engine.sampleRate()) * d->transport.bpm / 60.0;
}

int HelixSynth::activeVoices() const { return d->engine.activeVoices(); }
void HelixSynth::waitTables() { d->library->waitIdle(); }
} // namespace prv
