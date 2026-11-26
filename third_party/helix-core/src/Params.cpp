#include "helix/Params.h"
#include "helix/Math.h"
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace helix {

namespace choices {
const std::vector<std::string> filterType = {"LP 12", "LP 24", "BP 12", "BP 24", "HP 12", "HP 24", "Notch 12",
                                             "Notch 24", "Ladder 24", "Comb +", "Comb -", "Formant", "Phaser"};
const std::vector<std::string> filterModel = {"HELIX", "Standard LP", "Sallen-Key", "Ladder", "Steiner-Parker",
                                              "State Var", "OTA", "Polivoks", "Sw Cap", "Acid", "MS-20",
                                              "Oberheim", "SEM", "TB-303", "Fat Cat", "Odyssey", "CS-15", "SH-2"};
const std::vector<std::string> filterKind = {"Low Pass", "High Pass", "Band Pass", "Notch", "Peak", "All Pass"};
const std::vector<std::string> filterSlope = {"Model", "6 dB", "12 dB", "18 dB", "24 dB"};
const std::vector<std::string> moOscTarget = {"Off", "Osc A", "Osc B", "A + B"};
const std::vector<std::string> moFilterTarget = {"Off", "F1", "F2", "F1 + F2"};
// (kept in step with FxLibrary.cpp's table: test_fx_library checks)
const std::vector<std::string> fxLibType = {"Off", "Ping-Pong", "Tape Echo", "BBD Echo", "Dub Delay", "Spring", "Tomb Room", "Mod Hall", "Celestial", "Dolmen", "Tumulus", "Plate", "Gated Verb", "Vastness", "Super-Massive", "Quadraverb", "Ensemble", "BBD Chorus", "Multi-Chorus", "Dimension", "Widener", "Flanger", "Script Phase", "Colour Phase", "Dual Phaser", "Barber-pole", "Vibe", "Filter", "Ring Mod", "Drive", "SP-1200", "MPC60", "S950", "S1000", "Amiga", "Beat Repeat", "Volume Shaper", "Stereo Tools",
                                             "HELIX Dist", "HELIX Chorus", "HELIX Phaser", "HELIX Delay", "HELIX Reverb", "OTT", "HELIX EQ"};
const std::vector<std::string> warpMode = {"Off", "Bend +", "Bend -", "Asym", "PWM", "Mirror", "Sync", "Formant",
                                           "FM Other", "FM Sub", "FM Noise", "AM", "RM", "Quantize", "Bit Red"};
const std::vector<std::string> subShape = {"Sine", "Triangle", "Square", "Saw"};
const std::vector<std::string> subOctave = {"-1 Oct", "-2 Oct"};
const std::vector<std::string> noiseType = {"White", "Pink", "Brown", "Vinyl", "Tape Hiss", "Crackle"};
const std::vector<std::string> polyMode = {"Poly", "Mono", "Legato"};
const std::vector<std::string> glideMode = {"Always", "Legato"};
const std::vector<std::string> stealMode = {"Oldest", "Quietest"};
const std::vector<std::string> lfoShape = {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H",
                                           "Smooth Rnd", "Breakpoint", "Steps"};
const std::vector<std::string> lfoMode = {"Retrig", "Free", "One-shot"};
const std::vector<std::string> syncDiv = {"8 Bar", "4 Bar", "2 Bar", "1 Bar", "1/2", "1/2T", "1/4D", "1/4", "1/4T",
                                          "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T", "1/32", "1/32T", "1/64"};
const std::vector<std::string> modSource = {"None", "Env 1", "Filter Env", "Env 3", "Env 4", "LFO 1", "LFO 2", "LFO 3",
                                            "LFO 4", "Perf 1", "Perf 2", "Velocity", "Pressure", "Chan AT",
                                            "Mod Wheel", "Pitch Bend", "Note", "Random", "Macro 1", "Macro 2",
                                            "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8"};
const std::vector<std::string> modCurve = {"Linear"};
const std::vector<std::string> printSource = {"Time", "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Growl", "Pos A", "Pos B",
                                              "Cutoff 1", "Warp A"};
const std::vector<std::string> perfStepShape = {"Hold", "Ramp Up", "Ramp Down", "Smooth", "Triangle", "Exp",
                                                "Sine", "Exp Up", "Log Up", "Log Down", "S Up", "S Down",
                                                "Gate 1/2", "Gate 1/4", "Gate 3/4", "Saw x2", "Saw Dn x2", "Tri x2",
                                                "Sine x2", "Pulse x2", "Stairs Up", "Stairs Dn", "Bounce", "Off",
                                                "Roll x2", "Roll x3", "Roll x4", "Roll x8"};
const std::vector<std::string> charType = {"Off", "Hoover", "Mentasm", "Hoover Reese", "Rave", "Dread", "Donk", "Growl",
                                          "Dread Hoover", "Rollers Sub", "Rollers Reese", "Rollers Sub+Reese",
                                          "Rollers Hoover"};
const std::vector<std::string> chordMemory = {"Off", "Octave", "Fifth", "Power", "Minor", "Major", "Minor 7", "Major 7",
                                             "Sus 4", "Minor 9", "Minor 11", "Rave Stab", "Dub Stab", "Hoover Oct"};
int chordIntervals(int c, int* out)
{
    static const int iv[14][6] = {{0}, {1, 12}, {1, 7}, {2, 7, 12}, {2, 3, 7}, {2, 4, 7}, {3, 3, 7, 10}, {3, 4, 7, 11},
                                  {2, 5, 7}, {4, 3, 7, 10, 14}, {5, 3, 7, 10, 14, 17}, {4, 3, 7, 12, 15}, {3, 7, 10, 15},
                                  {2, -12, 12}};
    if (c < 0 || c > 13) return 0;
    for (int k = 0; k < iv[c][0]; ++k) out[k] = iv[c][k + 1];
    return iv[c][0];
}
const std::vector<std::string> perfLoop = {"Off", "Forward", "Backward", "Ping-Pong", "Random", "Intro+Loop"};
const std::vector<std::string> perfFeel = {"Normal", "Half-time", "Double", "Triplet"};
const std::vector<std::string> perfFill = {"Off", "Every 2", "Every 4"};
const std::vector<std::string> lfoJump = {"Off", "x2", "x3", "x4", "x8", "x1/2"};
const std::vector<std::string> macroPick = {"Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8"};
const std::vector<std::string> fxType = {"Distortion", "Chorus", "Phaser", "Delay", "Reverb", "OTT", "EQ"};
const std::vector<std::string> distType = {"Tube", "Hard Clip", "Fold", "Crush", "Asym"};
const std::vector<std::string> delayMode = {"Stereo", "Ping-Pong"};
const std::vector<std::string> chorusMode = {"Chorus", "Ensemble"};
const std::vector<std::string> reverbType = {"Plate", "Hall"};
const std::vector<std::string> quality = {"Eco", "Normal", "High"};
const std::vector<std::string> onOff = {"Off", "On"};
extern const std::vector<std::string> modDest;
} // namespace choices

static const ParamDef kDefs[kNumParams] = {
#define HP(e, id, name, kind, mn, mx, df, unit, ch, mod) {id, name, kind, mn, mx, df, unit, ch, mod},
#include "helix/ParamList.inc"
#undef HP
};

// Special destinations come first; then every parameter flagged modulatable, in parameter order. Because the
// parameter list is append-only, this list is append-only too.
static std::vector<std::string> buildModDest()
{
    std::vector<std::string> d = {"None", "Pitch", "A Pitch", "B Pitch", "Amp", "Pan"};
    for (const ParamDef& p : kDefs) if (p.modulatable) d.push_back(p.name);
    return d;
}
namespace choices { const std::vector<std::string> modDest = buildModDest(); }

const ParamDef& paramDef(int index) { return kDefs[clamp(index, 0, kNumParams - 1)]; }

int paramIndexFromId(const std::string& id)
{
    static const std::unordered_map<std::string, int> map = [] {
        std::unordered_map<std::string, int> m;
        for (int i = 0; i < kNumParams; ++i) m.emplace(kDefs[i].id, i);
        return m;
    }();
    auto it = map.find(id);
    return it == map.end() ? -1 : it->second;
}

int ParamDef::numSteps() const
{
    switch (kind)
    {
        case ParamKind::Bool: return 2;
        case ParamKind::Choice: return choices ? (int)choices->size() : 0;
        case ParamKind::Int: return (int)(max - min) + 1;
        default: return 0;
    }
}

float ParamDef::clampPlain(float v) const
{
    if (!(v == v)) v = def;   // NaN guard
    switch (kind)
    {
        case ParamKind::Bool: return v >= 0.5f ? 1.0f : 0.0f;
        case ParamKind::Choice: return (float)clamp((int)std::lround(v), 0, std::max(0, numSteps() - 1));
        case ParamKind::Int: return clamp(std::round(v), min, max);
        default: return clamp(v, min, max);
    }
}

float ParamDef::toPlain(float n) const
{
    n = clamp(n, 0.0f, 1.0f);
    switch (kind)
    {
        case ParamKind::Bool: return n >= 0.5f ? 1.0f : 0.0f;
        case ParamKind::Choice: { int s = numSteps(); return s < 2 ? 0.0f : (float)std::lround(n * (float)(s - 1)); }
        case ParamKind::Int: return std::round(min + n * (max - min));
        case ParamKind::Log: return min * std::pow(max / min, n);
        case ParamKind::Squared: return min + (max - min) * n * n;
        default: return min + (max - min) * n;
    }
}

float ParamDef::toNormalised(float v) const
{
    v = clampPlain(v);
    switch (kind)
    {
        case ParamKind::Bool: return v;
        case ParamKind::Choice: { int s = numSteps(); return s < 2 ? 0.0f : v / (float)(s - 1); }
        case ParamKind::Log: return clamp(std::log(v / min) / std::log(max / min), 0.0f, 1.0f);
        case ParamKind::Squared: return clamp(std::sqrt((v - min) / (max - min)), 0.0f, 1.0f);
        default: return max > min ? clamp((v - min) / (max - min), 0.0f, 1.0f) : 0.0f;
    }
}

std::string ParamDef::format(float v) const
{
    v = clampPlain(v);
    char buf[48];
    switch (kind)
    {
        case ParamKind::Bool: return v >= 0.5f ? "On" : "Off";
        case ParamKind::Choice: return choices && !choices->empty() ? (*choices)[(size_t)v] : std::string();
        case ParamKind::Int: std::snprintf(buf, sizeof buf, "%d", (int)v); return buf;
        default: break;
    }
    const float a = std::fabs(v);
    if (unit[0] == 'H' && unit[1] == 'z' && a >= 1000.0f) { std::snprintf(buf, sizeof buf, "%.2fk", v / 1000.0f); return buf; }
    if (unit[0] == 'm' && unit[1] == 's' && a >= 1000.0f) { std::snprintf(buf, sizeof buf, "%.2fs", v / 1000.0f); return buf; }
    if (max - min <= 2.0f && unit[0] == '\0') { std::snprintf(buf, sizeof buf, "%.0f%%", v * 100.0f); return buf; }
    if (a < 10.0f) std::snprintf(buf, sizeof buf, "%.2f", v);
    else if (a < 100.0f) std::snprintf(buf, sizeof buf, "%.1f", v);
    else std::snprintf(buf, sizeof buf, "%.0f", v);
    return buf;
}

} // namespace helix
