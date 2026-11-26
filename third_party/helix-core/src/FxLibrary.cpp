// The effects library: see FxLibrary.h. Three engines behind one slot:
//   DN   DREADNOUGHT's effects (C, Q15 integer, wet only: send / return; Filter, Ring, Drive are in-line)
//   MEGA Mega Synth's rack effects (float, in place; each reads its settings from a full Mega Synth parameter array)
//   ERA  EraCrush's machines (float, in place)
#include "helix/FxLibrary.h"
#include "helix/Effects.h"
#include "helix/ModSystem.h"
#include "helix/Params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../third_party/dnfx/dn_fx.h"
#include "../third_party/megafx/Fx/ModFx.h"
#include "../third_party/megafx/Fx/RackFx.h"
#include "../third_party/megafx/Fx/SpaceFx.h"
#include "../third_party/megafx/Fx/VastFx.h"
#include "../third_party/eracrush/eracrush_dsp.h"

namespace helix {
namespace fxlib {

// ------------------------------------------------------------------------------------------- Mega Synth parameter table

namespace {

struct MInfo
{
    float mn = 0, mx = 1, df = 0, sk = 0;
    const tg::ChoiceList* list = nullptr;   // choice
    bool isBool = false;
};

const std::vector<MInfo>& megaInfo()
{
    static const std::vector<MInfo> t = [] {
        using namespace tg;
        std::vector<MInfo> v;
        v.reserve(tg::P_COUNT);
#define TG_F(id, name, mn, mx, df, st, sk) v.push_back(MInfo{(float)(mn), (float)(mx), (float)(df), (float)(sk), nullptr, false});
#define TG_C(id, name, list, df) v.push_back(MInfo{0.0f, (float)(list.size - 1), (float)(df), 0.0f, &list, false});
#define TG_B(id, name, df) v.push_back(MInfo{0.0f, 1.0f, (df) ? 1.0f : 0.0f, 0.0f, nullptr, true});
#define TG_A(id, name, df) v.push_back(MInfo{0.0f, 1.0f, (float)(df), 0.0f, nullptr, false});
        TG_PARAMS(TG_F, TG_C, TG_B, TG_A)
#undef TG_F
#undef TG_C
#undef TG_B
#undef TG_A
        return v;
    }();
    return t;
}

// 0..1 -> a Mega Synth parameter's value (its range, skew and steps)
float megaFrom01(int p, float n)
{
    const MInfo& m = megaInfo()[(size_t)p];
    n = std::min(1.0f, std::max(0.0f, n));
    if (m.isBool) return n >= 0.5f ? 1.0f : 0.0f;
    if (m.list) return std::round(n * (float)(m.list->size - 1));
    if (m.sk > 0.0f && m.mx > m.mn && m.sk > m.mn && m.sk < m.mx)
    {
        const double skew = std::log(0.5) / std::log((m.sk - m.mn) / (double)(m.mx - m.mn));
        return m.mn + (m.mx - m.mn) * (float)std::pow((double)n, 1.0 / skew);
    }
    if (m.sk < 0.0f && m.mn > 0.0f) return m.mn * std::pow(m.mx / m.mn, n);
    return m.mn + (m.mx - m.mn) * n;
}

float megaTo01(int p, float v)
{
    const MInfo& m = megaInfo()[(size_t)p];
    if (m.isBool) return v >= 0.5f ? 1.0f : 0.0f;
    if (m.list) return m.list->size > 1 ? v / (float)(m.list->size - 1) : 0.0f;
    if (m.mx <= m.mn) return 0.0f;
    if (m.sk > 0.0f && m.sk > m.mn && m.sk < m.mx)
    {
        const double skew = std::log(0.5) / std::log((m.sk - m.mn) / (double)(m.mx - m.mn));
        return (float)std::pow((v - m.mn) / (double)(m.mx - m.mn), skew);
    }
    if (m.sk < 0.0f && m.mn > 0.0f) return std::log(v / m.mn) / std::log(m.mx / m.mn);
    return (v - m.mn) / (m.mx - m.mn);
}

// ------------------------------------------------------------------------------------------- the effects

enum Eng { E_OFF, E_DN, E_MEGA, E_ERA, E_HELIX };
enum MegaFx { M_DUB, M_PLATE, M_GATED, M_VAST, M_ENS, M_BBD, M_MULTI, M_DIM, M_WIDEN, M_FLP, M_SCRIPT, M_COLOUR, M_DUAL,
              M_BARBER, M_VIBE, M_RPT, M_SHAPER, M_STEREO };
// knob kinds for the Mega effects that need more than "set this parameter"
enum : int { K_NONE = -1, K_DIMMODE = -2, K_SHAPECURVE = -3 };

struct KnobSpec { const char* name; int src; const char* unit; };   // MEGA: src = Mega parameter (or K_*); DN: value index
struct TypeSpec
{
    const char* name; const char* group; Eng eng; int sub;   // DN type, MegaFx or EraCrush machine
    float mix;                                                // default Mix
    bool wetOnly;                                             // output is the effect alone: Mix adds it to the dry
    KnobSpec k[kNumKnobs];
};

#define KN {nullptr, K_NONE, ""}
using namespace tg;
const TypeSpec kTypes[] = {
    {"Off", "", E_OFF, 0, 1.0f, false, {KN, KN, KN, KN, KN, KN}},
    // ---- delays
    {"Ping-Pong", "Delay", E_DN, DN_FX_DELAY, 0.35f, true, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, KN, KN, KN}},
    {"Tape Echo", "Delay", E_DN, DN_FX_TAPE, 0.35f, true, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"BBD Echo", "Delay", E_DN, DN_FX_BBD, 0.35f, true, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"Dub Delay", "Delay", E_MEGA, M_DUB, 0.4f, false,
     {{"Sync", P_sxDubSync, ""}, {"Time", P_sxDubTime, "s"}, {"Feedback", P_sxDubFb, "%"}, {"Low Cut", P_sxDubHP, "Hz"},
      {"High Cut", P_sxDubLP, "Hz"}, {"Drive", P_sxDubDrive, "%"}}},
    // ---- reverbs
    {"Spring", "Reverb", E_DN, DN_FX_SPRING, 0.3f, true, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"Tomb Room", "Reverb", E_DN, DN_FX_TOMB, 0.3f, true, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"Mod Hall", "Reverb", E_DN, DN_FX_MOD, 0.3f, true, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"Celestial", "Reverb", E_DN, DN_FX_CELEST, 0.3f, true, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"Dolmen", "Reverb", E_DN, DN_FX_DOLMEN, 0.3f, true, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"Tumulus", "Reverb", E_DN, DN_FX_TUMULUS, 0.3f, true, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"Plate", "Reverb", E_MEGA, M_PLATE, 0.3f, false,
     {{"Decay", P_sxPlDecay, "s"}, {"Damping", P_sxPlDamp, "%"}, {"Pre-delay", P_sxPlPre, "ms"}, {"Width", P_sxPlWidth, "%"},
      {"Mod", P_sxPlMod, "%"}, KN}},
    {"Gated Verb", "Reverb", E_MEGA, M_GATED, 0.35f, false,
     {{"Size", P_sxGateSize, "%"}, {"Pre-delay", P_sxGatePre, "ms"}, {"Tone", P_sxGateTone, "Hz"},
      {"Length", P_sxGateLen, "ms"}, {"Shape", P_sxGateShape, ""}, {"Threshold", P_sxGateThr, "dB"}}},
    {"Vastness", "Reverb", E_MEGA, M_VAST, 0.35f, false,
     {{"Mode", P_vsMode, ""}, {"Delay", P_vsDelay, "ms"}, {"Warp", P_vsWarp, "%"}, {"Feedback", P_vsFeedback, "%"},
      {"Density", P_vsDensity, "%"}, {"High Cut", P_vsHighCut, "Hz"}}},
    {"Super-Massive", "Reverb", E_DN, DN_FX_MASSIVE, 0.35f, true,
     {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, {"", 4, ""}, {"", 5, ""}}},
    {"Quadraverb", "Reverb", E_DN, DN_FX_QUADRA, 0.35f, true,
     {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, {"", 4, ""}, {"", 5, ""}}},
    // ---- choruses
    {"Ensemble", "Chorus", E_MEGA, M_ENS, 0.5f, false,
     {{"Depth", P_cpEnsDepth, "%"}, {"Speed", P_cpEnsSpeed, "%"}, {"Tone", P_cpEnsTone, "Hz"}, KN, KN, KN}},
    {"BBD Chorus", "Chorus", E_MEGA, M_BBD, 0.5f, false,
     {{"Rate", P_cpBbdRate, "Hz"}, {"Depth", P_cpBbdDepth, "%"}, {"Stereo", P_cpBbdStereo, "%"}, {"Bright", P_cpBbdBright, ""},
      {"Sync", P_cpBbdSync, ""}, KN}},
    {"Multi-Chorus", "Chorus", E_MEGA, M_MULTI, 0.5f, false,
     {{"Voices", P_cpMulVoices, ""}, {"Spread", P_cpMulSpread, "%"}, {"Rate", P_cpMulRate, "Hz"}, {"Depth", P_cpMulDepth, "%"},
      {"Feedback", P_cpMulFb, "%"}, {"Sync", P_cpMulSync, ""}}},
    {"Dimension", "Chorus", E_MEGA, M_DIM, 0.5f, false, {{"Mode", K_DIMMODE, ""}, KN, KN, KN, KN, KN}},
    {"Widener", "Chorus", E_MEGA, M_WIDEN, 1.0f, false,
     {{"Cents", P_sxWidCents, "ct"}, {"Delay", P_sxWidDelay, "ms"}, {"Low Cut", P_sxWidLowCut, "Hz"}, {"Width", P_sxWidWidth, "%"},
      KN, KN}},
    {"Flanger", "Chorus", E_MEGA, M_FLP, 0.5f, false,
     {{"Mode", P_flpMode, ""}, {"Rate", P_flpRate, "Hz"}, {"Depth", P_flpDepth, "%"}, {"Feedback", P_flpFeedback, "%"},
      {"Manual", P_flpManual, "%"}, {"Spread", P_flpSpread, "%"}}},
    // ---- phasers
    {"Script Phase", "Phaser", E_MEGA, M_SCRIPT, 1.0f, false,
     {{"Speed", P_cpScrSpeed, "Hz"}, {"Feedback", P_cpScrFb, ""}, {"Sync", P_cpScrSync, ""}, KN, KN, KN}},
    {"Colour Phase", "Phaser", E_MEGA, M_COLOUR, 1.0f, false,
     {{"Rate", P_cpColRate, "Hz"}, {"Colour", P_cpColColour, ""}, {"Sync", P_cpColSync, ""}, KN, KN, KN}},
    {"Dual Phaser", "Phaser", E_MEGA, M_DUAL, 1.0f, false,
     {{"Rate A", P_cpDualRateA, "Hz"}, {"Rate B", P_cpDualRateB, "Hz"}, {"Depth", P_cpDualDepth, "%"},
      {"Feedback", P_cpDualFb, "%"}, {"Routing", P_cpDualMode, ""}, {"Link", P_cpDualLink, ""}}},
    {"Barber-pole", "Phaser", E_MEGA, M_BARBER, 1.0f, false,
     {{"Speed", P_cpBarSpeed, "Hz"}, {"Direction", P_cpBarDir, ""}, {"Feedback", P_cpBarFb, "%"}, {"Sync", P_cpBarSync, ""},
      KN, KN}},
    {"Vibe", "Phaser", E_MEGA, M_VIBE, 1.0f, false,
     {{"Speed", P_cpVibeSpeed, "Hz"}, {"Intensity", P_cpVibeInt, "%"}, {"Mode", P_cpVibeMode, ""}, {"Sync", P_cpVibeSync, ""},
      KN, KN}},
    // ---- character
    {"Filter", "Character", E_DN, DN_FX_FILTER, 1.0f, false,
     {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, {"", 4, ""}, {"", 5, ""}}},
    {"Ring Mod", "Character", E_DN, DN_FX_RING, 1.0f, false, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"Drive", "Character", E_DN, DN_FX_DRIVE, 1.0f, false, {{"", 0, ""}, {"", 1, ""}, {"", 2, ""}, {"", 3, ""}, KN, KN}},
    {"SP-1200", "Sampler", E_ERA, eracrush::kSP1200, 1.0f, false, {}},
    {"MPC60", "Sampler", E_ERA, eracrush::kMPC60, 1.0f, false, {}},
    {"S950", "Sampler", E_ERA, eracrush::kS950, 1.0f, false, {}},
    {"S1000", "Sampler", E_ERA, eracrush::kS1000, 1.0f, false, {}},
    {"Amiga", "Sampler", E_ERA, eracrush::kAmiga, 1.0f, false, {}},
    // ---- rhythm / utility
    {"Beat Repeat", "Rhythm", E_MEGA, M_RPT, 1.0f, false,
     {{"Length", P_rptLength, ""}, {"Chance", P_rptChance, "%"}, {"Pitch Drop", P_rptPitch, "st"}, {"Gate", P_rptGate, "%"},
      {"Shrink", P_rptShrink, "%"}, {"Reverse", P_rptReverse, ""}}},
    {"Volume Shaper", "Rhythm", E_MEGA, M_SHAPER, 1.0f, false,
     {{"Curve", K_SHAPECURVE, ""}, {"Rate", P_vshRate, ""}, {"Depth", P_vshDepth, "%"}, {"Smooth", P_vshSmooth, "%"}, KN, KN}},
    {"Stereo Tools", "Utility", E_MEGA, M_STEREO, 1.0f, false,
     {{"Bass Mono", P_sttMono, ""}, {"Mono Below", P_sttMonoFreq, "Hz"}, {"Width", P_sttWidth, "%"}, {"Haas", P_sttHaas, "ms"},
      KN, KN}},
    // ---- HELIX's own effects (the old FX rack, folded into the slots in 0.3.6): knobs are the old parameters
    {"HELIX Dist", "HELIX", E_HELIX, 0, 1.0f, false, {}},
    {"HELIX Chorus", "HELIX", E_HELIX, 1, 0.4f, false, {}},
    {"HELIX Phaser", "HELIX", E_HELIX, 2, 0.5f, false, {}},
    {"HELIX Delay", "HELIX", E_HELIX, 3, 0.25f, false, {}},
    {"HELIX Reverb", "HELIX", E_HELIX, 4, 0.25f, false, {}},
    {"OTT", "HELIX", E_HELIX, 5, 1.0f, false, {}},
    {"HELIX EQ", "HELIX", E_HELIX, 6, 1.0f, false, {}},
};
#undef KN
constexpr int kTypeCount = (int)(sizeof(kTypes) / sizeof(kTypes[0]));

// The old rack effects: on switch, the parameter Mix stands for (-1: none, the slot crossfades), and the knobs' old
// parameters with short names. Order = FxType.
struct Legacy { int on; int mix; int knob[kNumKnobs]; const char* name[kNumKnobs]; };
const Legacy kLegacy[(int)FxType::Count] = {
    {P_DIST_ON, P_DIST_MIX, {P_DIST_TYPE, P_DIST_DRIVE, P_DIST_TONE, P_DIST_BITS, P_DIST_RATE, -1},
     {"Type", "Drive", "Tone", "Crush Bits", "Crush Rate", nullptr}},
    {P_CHO_ON, P_CHO_MIX, {P_CHO_MODE, P_CHO_RATE, P_CHO_DEPTH, -1, -1, -1}, {"Mode", "Rate", "Depth", nullptr, nullptr, nullptr}},
    {P_PH_ON, P_PH_MIX, {P_PH_RATE, P_PH_DEPTH, P_PH_FREQ, P_PH_FEEDBACK, -1, -1}, {"Rate", "Depth", "Freq", "Feedback", nullptr, nullptr}},
    {P_DLY_ON, P_DLY_MIX, {P_DLY_MODE, P_DLY_SYNC, P_DLY_DIV, P_DLY_TIME, P_DLY_FEEDBACK, P_DLY_TONE},
     {"Mode", "Sync", "Division", "Time", "Feedback", "Tone"}},
    {P_REV_ON, P_REV_MIX, {P_REV_TYPE, P_REV_SIZE, P_REV_DECAY, P_REV_DAMP, P_REV_PREDELAY, -1},
     {"Type", "Size", "Decay", "Damp", "Predelay", nullptr}},
    {P_OTT_ON, -1, {P_OTT_DEPTH, P_OTT_TIME, P_OTT_UP, P_OTT_DOWN, P_OTT_GAIN, -1}, {"Depth", "Time", "Upward", "Downward", "Output", nullptr}},
    // (EQ's Mid Q stays on its old parameter: six knobs, seven settings)
    {P_EQ_ON, -1, {P_EQ_LOW_FREQ, P_EQ_LOW_GAIN, P_EQ_MID_FREQ, P_EQ_MID_GAIN, P_EQ_HIGH_FREQ, P_EQ_HIGH_GAIN},
     {"Low Freq", "Low Gain", "Mid Freq", "Mid Gain", "High Freq", "High Gain"}},
};
constexpr int kFirstHelixType = kTypeCount - (int)FxType::Count;

const char* const kEraKnobs[kNumKnobs] = {"Rate", "Bits", "Drive", "Alias", "Tone", "Age"};
const char* const kDimModes[4] = {"Mode 1", "Mode 2", "Mode 3", "Mode 4"};
const char* const kShaperCurves[4] = {"Pump", "Gate", "1/8 Pump", "Triplet"};

const TypeSpec& spec(int t) { return kTypes[std::min(kTypeCount - 1, std::max(0, t))]; }

int dnMax(const dn_fx_p_t& p, int i) { return std::max(0, (int)dn_fx_pmax(&p, (uint32_t)i)); }

// a DN effect's values from the knobs (0..1 each), the ones past the knobs at their defaults
dn_fx_p_t dnParams(int dnType, const float* knobs)
{
    dn_fx_p_t p;
    dn_fx_default(&p, (uint32_t)dnType);
    if (knobs)
        for (int i = 0; i < kNumKnobs && i < (int)DN_FX_NP; ++i)
        {
            const int mx = dnMax(p, i);
            if (mx > 0) dn_fx_value(&p, (uint32_t)i, (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, knobs[i])) * (float)mx));
        }
    return p;
}

std::string titleCase(const char* s)
{
    std::string o(s ? s : "");
    bool start = true;
    for (char& c : o)
    {
        if (c >= 'A' && c <= 'Z' && !start) c = (char)(c - 'A' + 'a');
        start = c == ' ' || c == '-' || c == '/';
    }
    return o;
}

std::string fmtNum(float v, const char* unit)
{
    char b[48];
    const std::string u = unit ? unit : "";
    if (u == "%") std::snprintf(b, sizeof b, "%.0f%%", (double)v * 100.0);
    else if (u == "Hz") std::snprintf(b, sizeof b, v >= 1000.0f ? "%.1f kHz" : (v >= 100.0f ? "%.0f Hz" : "%.2f Hz"),
                                      (double)(v >= 1000.0f ? v / 1000.0f : v));
    else if (u == "s") std::snprintf(b, sizeof b, v < 1.0f ? "%.0f ms" : "%.2f s", (double)(v < 1.0f ? v * 1000.0f : v));
    else if (u == "ms") std::snprintf(b, sizeof b, "%.0f ms", (double)v);
    else if (u == "dB") std::snprintf(b, sizeof b, "%.0f dB", (double)v);
    else if (u == "st") std::snprintf(b, sizeof b, "%.1f st", (double)v);
    else if (u == "ct") std::snprintf(b, sizeof b, "%.1f ct", (double)v);
    else std::snprintf(b, sizeof b, "%.2f", (double)v);
    return b;
}

} // namespace

// ------------------------------------------------------------------------------------------- descriptions

int numTypes() { return kTypeCount; }
const char* typeName(int t) { return spec(t).name; }
const char* typeGroup(int t) { return spec(t).group; }
float mixDefault(int t) { return spec(t).mix; }

std::string knobName(int t, int k, const float* knobs)
{
    if (k < 0 || k >= kNumKnobs) return "";
    const TypeSpec& s = spec(t);
    switch (s.eng)
    {
        case E_OFF: return "";
        case E_ERA: return kEraKnobs[k];
        case E_DN:
        {
            if (!s.k[k].name) return "";
            const dn_fx_p_t p = dnParams(s.sub, knobs);
            const char* n = dn_fx_pname(&p, (uint32_t)s.k[k].src);
            return n[0] == '-' ? std::string() : titleCase(n);
        }
        case E_MEGA: return s.k[k].name ? s.k[k].name : "";
        case E_HELIX: return kLegacy[s.sub].name[k] ? kLegacy[s.sub].name[k] : "";
    }
    return "";
}

std::string knobText(int t, int k, float v, const float* knobs, double bpm)
{
    if (k < 0 || k >= kNumKnobs) return "";
    v = std::min(1.0f, std::max(0.0f, v));
    const TypeSpec& s = spec(t);
    char b[48];
    switch (s.eng)
    {
        case E_OFF: return "";
        case E_ERA:
            switch (k)
            {
                case 0: std::snprintf(b, sizeof b, "%.0f Hz", (double)(eracrush::kSpecs[s.sub].rate * std::exp2(-(1.0f - v) * 4.32f))); return b;
                case 1: { const int bits = (int)std::lround(v * 16.0f); if (!bits) return "Native"; std::snprintf(b, sizeof b, "%d bit", bits); return b; }
                default: std::snprintf(b, sizeof b, "%.0f%%", (double)v * 100.0); return b;
            }
        case E_DN:
        {
            if (!s.k[k].name) return "";
            float kn[kNumKnobs];
            for (int i = 0; i < kNumKnobs; ++i) kn[i] = knobs ? knobs[i] : knobDefault(t, i);
            kn[k] = v;
            const dn_fx_p_t p = dnParams(s.sub, kn);
            static const dn_fx_s_t blank {};   // (the text only needs the state for Super-Massive's live time)
            char out[8] = {0};
            dn_fx_ptext(&blank, &p, (uint32_t)s.k[k].src, (uint32_t)std::lround(bpm), out);
            out[7] = 0;
            return titleCase(out);
        }
        case E_MEGA:
        {
            const int src = s.k[k].src;
            if (src == K_DIMMODE) return kDimModes[std::min(3, (int)(v * 4.0f))];
            if (src == K_SHAPECURVE) return kShaperCurves[std::min(3, (int)(v * 4.0f))];
            if (src < 0) return "";
            const MInfo& m = megaInfo()[(size_t)src];
            const float x = megaFrom01(src, v);
            if (m.isBool) return x >= 0.5f ? "On" : "Off";
            if (m.list)
            {
                const std::string lab = m.list->labels[std::min(m.list->size - 1, std::max(0, (int)x))];
                return lab == "Off (Manual)" ? "Off" : lab;
            }
            // feedback / width style values with a range beyond 0..1 still read as %
            return fmtNum(x, s.k[k].unit);
        }
        case E_HELIX:
        {
            const int pi = kLegacy[s.sub].knob[k];
            if (pi < 0) return "";
            const ParamDef& d = paramDef(pi);
            return d.format(d.toPlain(v));
        }
    }
    return "";
}

float knobDefault(int t, int k)
{
    if (k < 0 || k >= kNumKnobs) return 0.0f;
    const TypeSpec& s = spec(t);
    switch (s.eng)
    {
        case E_OFF: return 0.5f;
        case E_ERA:
        {
            const eracrush::Params d;
            const float v[kNumKnobs] = {d.rate, 0.0f, d.drive, d.alias, d.tone, d.age};
            return v[k];
        }
        case E_DN:
        {
            if (!s.k[k].name) return 0.0f;
            dn_fx_p_t p;
            dn_fx_default(&p, (uint32_t)s.sub);
            const int i = s.k[k].src, mx = dnMax(p, i);
            return mx > 0 ? (float)p.p[i] / (float)mx : 0.0f;
        }
        case E_MEGA:
        {
            const int src = s.k[k].src;
            if (src == K_DIMMODE || src == K_SHAPECURVE) return 0.0f;
            if (src < 0) return 0.0f;
            // Sync off by default (the free Time / Rate knob then works)
            const MInfo& m = megaInfo()[(size_t)src];
            if (m.list && std::string(s.k[k].name) == "Sync") return 0.0f;
            return std::min(1.0f, std::max(0.0f, megaTo01(src, m.df)));
        }
        case E_HELIX:
        {
            const int pi = kLegacy[s.sub].knob[k];
            if (pi < 0) return 0.0f;
            const ParamDef& d = paramDef(pi);
            return d.toNormalised(d.def);
        }
    }
    return 0.0f;
}

// ------------------------------------------------------------------------------------------- the slot

namespace {
constexpr uint32_t kDnMassiveBytes = 512u * 1024u;
}

struct Slot::Impl
{
    double sr = 44100.0;
    int maxBlock = 32;
    std::vector<float> dryL, dryR;

    // DREADNOUGHT
    dn_fx_s_t dn {};
    std::vector<uint32_t> dnMem;   // (4-aligned)
    uint32_t dnBytes = 0;
    std::vector<int32_t> dnIn, dnOut;

    // Mega Synth
    std::vector<float> mv;         // a full Mega Synth parameter array (defaults, then the slot's knobs)
    tg::FxContext mctx;
    tg::DubDelay dub; tg::PlateReverb plate; tg::GatedReverb gated; tg::VastReverb vast;
    tg::EnsembleChorus ens; tg::BbdChorus bbd; tg::MultiChorus multi; tg::DimensionChorus dim; tg::MicroWidener widen;
    tg::FlangerPhaser flp; tg::ScriptPhase script; tg::ColourPhase colour; tg::DualPhaser dual; tg::BarberPhaser barber;
    tg::VibeFx vibe; tg::BeatRepeat rpt; tg::VolumeShaper shaper; tg::StereoTools stereo; tg::FxRackStore store;
    int shaperCurve = -1;
    std::vector<char> noHits;
    double beat = 0.0;

    // EraCrush
    eracrush::Crusher era;
    std::vector<float> eraBuf;

    // HELIX (the old rack's effects): one effect of a private chain, run with a copy of the engine's parameters
    FxChain chain;
    std::vector<float> hp;

    void megaReset(int which)
    {
        switch (which)
        {
            case M_DUB: dub.reset(); break; case M_PLATE: plate.reset(); break; case M_GATED: gated.reset(); break;
            case M_VAST: vast.reset(); break; case M_ENS: ens.reset(); break; case M_BBD: bbd.reset(); break;
            case M_MULTI: multi.reset(); break; case M_DIM: dim.reset(); break; case M_WIDEN: widen.reset(); break;
            case M_FLP: flp.reset(); break; case M_SCRIPT: script.reset(); break; case M_COLOUR: colour.reset(); break;
            case M_DUAL: dual.reset(); break; case M_BARBER: barber.reset(); break; case M_VIBE: vibe.reset(); break;
            case M_RPT: rpt.reset(); break; case M_SHAPER: shaper.reset(); break; case M_STEREO: stereo.reset(); break;
            default: break;
        }
    }
};

Slot::Slot() : d_(new Impl) {}
Slot::~Slot() = default;

void Slot::prepare(double sampleRate, int maxBlock)
{
    Impl& d = *d_;
    d.sr = sampleRate;
    d.maxBlock = std::max(1, maxBlock);
    d.dryL.assign((size_t)d.maxBlock, 0.0f);
    d.dryR.assign((size_t)d.maxBlock, 0.0f);
    // DN: one arena big enough for any of its effects
    uint32_t need = kDnMassiveBytes;
    for (uint32_t t = 1; t < DN_FX_N; ++t) need = std::max(need, dn_fx_ram(t));
    d.dnBytes = (need + 3u) & ~3u;
    d.dnMem.assign(d.dnBytes / 4u, 0u);
    d.dnIn.assign((size_t)d.maxBlock * 2u, 0);
    d.dnOut.assign((size_t)d.maxBlock * 2u, 0);
    dn_fx_init(&d.dn, DN_FX_OFF, nullptr, 0);
    // Mega
    d.mv.resize(tg::P_COUNT);
    for (int i = 0; i < tg::P_COUNT; ++i) d.mv[(size_t)i] = megaInfo()[(size_t)i].df;
    d.mctx.sr = sampleRate;
    d.dub.prepare(sampleRate); d.plate.prepare(sampleRate); d.gated.prepare(sampleRate); d.vast.prepare(sampleRate);
    d.ens.prepare(sampleRate); d.bbd.prepare(sampleRate); d.multi.prepare(sampleRate); d.dim.prepare(sampleRate);
    d.widen.prepare(sampleRate); d.flp.prepare(sampleRate); d.script.prepare(sampleRate); d.colour.prepare(sampleRate);
    d.dual.prepare(sampleRate); d.barber.prepare(sampleRate); d.vibe.prepare(sampleRate); d.rpt.prepare(sampleRate);
    d.stereo.prepare(sampleRate);
    d.shaper.reset();
    d.noHits.assign((size_t)d.maxBlock, 0);
    // EraCrush
    eracrush::kSampleRate = (float)sampleRate;
    d.era.reset();
    d.eraBuf.assign((size_t)d.maxBlock * 2u, 0.0f);
    d.chain.prepare((float)sampleRate);
    d.hp.assign((size_t)kNumParams, 0.0f);
    cur_ = -1;
}

void Slot::reset()
{
    Impl& d = *d_;
    if (d.dn.type != DN_FX_OFF) dn_fx_init(&d.dn, d.dn.type, d.dnMem.data(), d.dnBytes);
    for (int m = 0; m <= M_STEREO; ++m) d.megaReset(m);
    d.era.reset();
    d.chain.reset();
}

void Slot::process(float* l, float* r, int n, int type, float mix, const float* knobs, const Context& ctx)
{
    Impl& d = *d_;
    type = std::min(kTypeCount - 1, std::max(0, type));
    const TypeSpec& s = kTypes[type];
    n = std::min(n, d.maxBlock);
    if (type != cur_)
    {
        // a new effect starts from silence (its memory cleared here: no allocation)
        if (s.eng == E_DN) dn_fx_init(&d.dn, (uint32_t)s.sub, d.dnMem.data(), d.dnBytes);
        else if (d.dn.type != DN_FX_OFF) dn_fx_init(&d.dn, DN_FX_OFF, nullptr, 0);
        if (s.eng == E_MEGA) d.megaReset(s.sub);
        if (s.eng == E_ERA) d.era.reset();
        if (s.eng == E_HELIX) d.chain.reset();
        cur_ = type;
    }
    if (s.eng == E_OFF || mix <= 0.0f) return;
    mix = std::min(1.0f, mix);
    std::memcpy(d.dryL.data(), l, sizeof(float) * (size_t)n);
    std::memcpy(d.dryR.data(), r, sizeof(float) * (size_t)n);

    switch (s.eng)
    {
        case E_DN:
        {
            const dn_fx_p_t p = dnParams(s.sub, knobs);
            for (int i = 0; i < n; ++i)
            {
                d.dnIn[(size_t)(2 * i)] = (int32_t)std::lround(std::max(-4.0f, std::min(4.0f, l[i])) * 32768.0f);
                d.dnIn[(size_t)(2 * i + 1)] = (int32_t)std::lround(std::max(-4.0f, std::min(4.0f, r[i])) * 32768.0f);
            }
            std::fill(d.dnOut.begin(), d.dnOut.begin() + 2 * n, 0);
            dn_fx_run(&d.dn, &p, d.dnIn.data(), d.dnOut.data(), (uint32_t)n, (uint32_t)std::lround(ctx.bpm), 100u);
            for (int i = 0; i < n; ++i)
            {
                l[i] = (float)d.dnOut[(size_t)(2 * i)] * (1.0f / 32768.0f);
                r[i] = (float)d.dnOut[(size_t)(2 * i + 1)] * (1.0f / 32768.0f);
            }
            break;
        }
        case E_MEGA:
        {
            for (int k = 0; k < kNumKnobs; ++k)
            {
                const int src = s.k[k].src;
                if (!s.k[k].name || src == K_NONE) continue;
                if (src == K_DIMMODE)
                {
                    const int m = std::min(3, (int)(knobs[k] * 4.0f));
                    d.mv[tg::P_sxDim1] = m == 0; d.mv[tg::P_sxDim2] = m == 1; d.mv[tg::P_sxDim3] = m == 2; d.mv[tg::P_sxDim4] = m == 3;
                }
                else if (src == K_SHAPECURVE)
                {
                    const int c = std::min(3, (int)(knobs[k] * 4.0f));
                    if (c != d.shaperCurve) { d.store.setCurvePreset(c); d.shaperCurve = c; }
                }
                else d.mv[(size_t)src] = megaFrom01(src, knobs[k]);
            }
            d.mctx.tempo = ctx.bpm;
            d.mctx.beat = ctx.beat;
            d.mctx.beatInc = ctx.bpm / 60.0 / d.sr;
            const float* v = d.mv.data();
            const auto& c = d.mctx;
            switch (s.sub)
            {
                case M_DUB: d.dub.process(l, r, n, v, c, false, nullptr); break;
                case M_PLATE: d.plate.process(l, r, n, v, c, nullptr); break;
                case M_GATED: d.gated.process(l, r, n, v, c, 0u, reinterpret_cast<const bool*>(d.noHits.data()), nullptr); break;
                case M_VAST: d.vast.process(l, r, n, v, c, nullptr); break;
                case M_ENS: d.ens.process(l, r, n, v, c); break;
                case M_BBD: d.bbd.process(l, r, n, v, c); break;
                case M_MULTI: d.multi.process(l, r, n, v, c); break;
                case M_DIM: d.dim.process(l, r, n, v, c); break;
                case M_WIDEN: d.widen.process(l, r, n, v, c); break;
                case M_FLP: d.flp.process(l, r, n, v, c); break;
                case M_SCRIPT: d.script.process(l, r, n, v, c); break;
                case M_COLOUR: d.colour.process(l, r, n, v, c); break;
                case M_DUAL: d.dual.process(l, r, n, v, c); break;
                case M_BARBER: d.barber.process(l, r, n, v, c); break;
                case M_VIBE: d.vibe.process(l, r, n, v, c); break;
                case M_RPT: d.rpt.process(l, r, n, v, c, false); break;
                case M_SHAPER: d.shaper.process(l, r, n, v, c, d.store); break;
                case M_STEREO: d.stereo.process(l, r, n, v, c); break;
                default: break;
            }
            break;
        }
        case E_ERA:
        {
            eracrush::Params& ep = d.era.params;
            ep.machine = s.sub;
            ep.rate = knobs[0];
            ep.bits = (int)std::lround(std::min(1.0f, std::max(0.0f, knobs[1])) * 16.0f);
            ep.drive = knobs[2]; ep.alias = knobs[3]; ep.tone = knobs[4]; ep.age = knobs[5];
            ep.mix = 1.0f;
            for (int i = 0; i < n; ++i) { d.eraBuf[(size_t)(2 * i)] = l[i]; d.eraBuf[(size_t)(2 * i + 1)] = r[i]; }
            d.era.process(d.eraBuf.data(), d.eraBuf.data(), n);
            for (int i = 0; i < n; ++i) { l[i] = d.eraBuf[(size_t)(2 * i)]; r[i] = d.eraBuf[(size_t)(2 * i + 1)]; }
            break;
        }
        case E_HELIX:
        {
            const Legacy& L = kLegacy[s.sub];
            float* hp = d.hp.data();
            if (ctx.params) std::memcpy(hp, ctx.params, sizeof(float) * (size_t)kNumParams);
            else for (int i = 0; i < kNumParams; ++i) hp[i] = paramDef(i).def;
            for (const Legacy& o : kLegacy) hp[o.on] = 0.0f;
            hp[L.on] = 1.0f;
            for (int k = 0; k < kNumKnobs; ++k)
                if (L.knob[k] >= 0) hp[L.knob[k]] = paramDef(L.knob[k]).toPlain(std::min(1.0f, std::max(0.0f, knobs[k])));
            if (L.mix >= 0) hp[L.mix] = mix;
            d.chain.process(l, r, n, hp, ctx.bpm, ctx.oversample);
            if (L.mix >= 0)
            {
                // its own Mix did the blend (exactly as in the rack)
                for (int i = 0; i < n; ++i)
                    if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { std::fill(l, l + n, 0.0f); std::fill(r, r + n, 0.0f); reset(); break; }
                return;
            }
            if (mix >= 1.0f) return;
            break;
        }
        default: break;
    }

    // Mix: send / return effects add their wet to the dry; in-line ones crossfade
    for (int i = 0; i < n; ++i)
    {
        float wl = l[i], wr = r[i];
        if (!std::isfinite(wl) || !std::isfinite(wr)) { wl = wr = 0.0f; reset(); }
        if (s.wetOnly) { l[i] = d.dryL[(size_t)i] + wl * mix; r[i] = d.dryR[(size_t)i] + wr * mix; }
        else
        {
            l[i] = d.dryL[(size_t)i] + (wl - d.dryL[(size_t)i]) * mix;
            r[i] = d.dryR[(size_t)i] + (wr - d.dryR[(size_t)i]) * mix;
        }
    }
}

bool isHeavy(int type) { return type >= 0 && type < kTypeCount && kTypes[type].eng == E_HELIX && kTypes[type].sub == (int)FxType::Ott; }

int foldRack(float* p)
{
    // the rack's order (as FxChain: repeats skipped, missing effects appended)
    int order[(int)FxType::Count], count = 0;
    bool used[(int)FxType::Count] = {};
    for (int sl = 0; sl < kNumFxSlots; ++sl)
    {
        const int t = std::min((int)FxType::Count - 1, std::max(0, (int)p[P_FX_ORDER1 + sl]));
        if (!used[t]) { used[t] = true; order[count++] = t; }
    }
    for (int t = 0; t < (int)FxType::Count; ++t) if (!used[t]) order[count++] = t;

    // matrix destination index of a parameter (or -1)
    auto destOf = [](int param) {
        for (int dd = kNumSpecialDests; dd < numModDests(); ++dd) if (modDestParam(dd) == param) return dd;
        return -1;
    };
    const int stride = P_I2_TYPE - P_I1_TYPE;
    // Where each switched-on effect goes. Like Massive: what comes before the first delay / reverb goes in the
    // inserts, the delay / reverb and what follows in FX 1 / 2, when that fits; otherwise the free slots in order.
    // Either way the order is kept (Insert 1, Insert 2, FX 1, FX 2 run in that order).
    int on[(int)FxType::Count], nOn = 0, split = -1;
    for (int k = 0; k < count; ++k)
        if (p[kLegacy[order[k]].on] >= 0.5f)
        {
            if (split < 0 && (order[k] == (int)FxType::Delay || order[k] == (int)FxType::Reverb)) split = nOn;
            on[nOn++] = order[k];
        }
    if (split < 0) split = nOn;
    auto freeSlot = [&](int sl) { return (int)p[P_I1_TYPE + sl * stride] == 0; };
    int target[(int)FxType::Count];
    {
        int ins[2], fx[2], ni = 0, nf = 0;
        for (int sl = 0; sl < 2; ++sl) if (freeSlot(sl)) ins[ni++] = sl;
        for (int sl = 2; sl < 4; ++sl) if (freeSlot(sl)) fx[nf++] = sl;
        const bool massive = split <= ni && nOn - split <= nf;
        int next = 0;
        for (int k = 0; k < nOn; ++k)
        {
            if (massive) target[k] = k < split ? ins[k] : fx[k - split];
            else
            {
                while (next < kNumSlots && !freeSlot(next)) ++next;
                target[k] = next < kNumSlots ? next++ : -1;
            }
        }
    }
    int left = 0;
    for (int k = 0; k < nOn; ++k)
    {
        const Legacy& L = kLegacy[on[k]];
        const int slot = target[k];
        if (slot < 0) { ++left; continue; }
        const int b = P_I1_TYPE + slot * stride;
        p[b] = (float)(kFirstHelixType + on[k]);
        p[b + 1] = L.mix >= 0 ? p[L.mix] : 1.0f;
        for (int kn = 0; kn < kNumKnobs; ++kn)
            p[b + 2 + kn] = L.knob[kn] >= 0 ? paramDef(L.knob[kn]).toNormalised(p[L.knob[kn]]) : 0.0f;
        p[L.on] = 0.0f;
        // matrix routings aimed at its settings now aim at the slot (amounts were in the old parameter's
        // normalised units, as the slot knobs are, so they carry over)
        for (int m = 0; m < kNumModSlots; ++m)
        {
            float& dst = p[P_M1_DST + m * (P_M2_DST - P_M1_DST)];
            const int param = modDestParam((int)dst);
            int to = -1;
            if (param >= 0 && param == L.mix) to = b + 1;
            for (int kn = 0; kn < kNumKnobs; ++kn) if (param >= 0 && param == L.knob[kn]) to = b + 2 + kn;
            if (to >= 0) { const int nd = destOf(to); if (nd >= 0) dst = (float)nd; }
        }
    }
    return left;
}

} // namespace fxlib
} // namespace helix
