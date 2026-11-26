#include "helix/FxLibrary.h"
// The 24 HELIX factory presets: jungle, drum & bass and dark music. Built in code so every target (desktop,
// MPC, render CLI) has them without files; tools write them out as JSON into Presets/<Category>/.
#include "helix/Filter.h"
#include "helix/ModSystem.h"
#include "helix/Oscillator.h"
#include "helix/Preset.h"
#include "helix/WavetableGen.h"
#include "helix/CharOsc.h"
#include <string>

namespace helix {

namespace {

// Basic Shapes positions
constexpr float kSine = 0.0f, kTri = 1.0f / 3.0f, kSaw = 2.0f / 3.0f, kSquare = 1.0f;

int destOf(int param)
{
    for (int d = 0; d < numModDests(); ++d) if (modDestParam(d) == param) return d;
    return 0;
}

struct B
{
    Preset p;
    int slot = 0;
    B(const char* name, const char* cat, const char* comment)
    {
        p.name = name; p.category = cat; p.comment = comment; p.author = "Twisted Genetics";
    }
    B& set(int param, float v) { p.params[param] = paramDef(param).clampPlain(v); return *this; }
    B& table(int s, BuiltinTable t)
    {
        p.tables[s] = "builtin:" + builtinTableNames()[(size_t)t];
        p.params[s == 0 ? P_A_TABLE : P_B_TABLE] = (float)(int)t;
        return *this;
    }
    // dst: a parameter id, or -DST_* for the special destinations
    B& mod(int src, int dst, float amt, int aux = SRC_NONE, float curve = 0.0f)
    {
        const int b = P_M1_SRC + slot * (P_M2_SRC - P_M1_SRC);
        p.params[b] = (float)src;
        p.params[b + 1] = (float)(dst < 0 ? -dst : destOf(dst));
        p.params[b + 2] = amt;
        p.params[b + 3] = (float)aux;
        p.params[b + 4] = curve;
        ++slot;
        return *this;
    }
    B& env(int n, float a, float d, float s, float r, float dc = 0.5f)
    {
        const int b = P_E1_A + (n - 1) * (P_E2_A - P_E1_A);
        return set(b, a).set(b + 1, d).set(b + 2, s).set(b + 3, r).set(b + 5, dc);
    }
    B& lfo(int n, int shape, bool sync, float rateOrDiv, int mode = 0)
    {
        const int b = P_L1_SHAPE + (n - 1) * (P_L2_SHAPE - P_L1_SHAPE);
        set(b, (float)shape).set(b + 1, (float)mode).set(b + 2, sync ? 1.0f : 0.0f);
        return sync ? set(b + 4, rateOrDiv) : set(b + 3, rateOrDiv);
    }
    B& perf(int n, int div, std::initializer_list<float> steps, std::initializer_list<int> shapes = {})
    {
        const int b = n == 1 ? P_P1_DIV : P_P2_DIV;
        set(b, (float)div).set(b + 1, (float)steps.size()).set(b + 2, 1.0f);
        int k = 0; for (float v : steps) set(b + 3 + k++, v);
        k = 0; for (int s : shapes) set(b + 19 + k++, (float)s);
        return *this;
    }
    B& mono(float glideMs, bool legato = true) { return set(P_POLY_MODE, legato ? 2.0f : 1.0f).set(P_GLIDE, glideMs).set(P_GLIDE_MODE, 1.0f); }
    B& unison(int osc, int n, float detune, float blend, float spread)
    {
        const int o = osc == 0 ? 0 : P_B_ON - P_A_ON;
        return set(P_A_UNISON + o, (float)n).set(P_A_DETUNE + o, detune).set(P_A_BLEND + o, blend).set(P_A_SPREAD + o, spread);
    }
    B& filter(int f, FilterType t, float cutoff, float res, float drive = 0.0f, float keytrk = 0.0f)
    {
        const int o = f == 1 ? 0 : P_F2_ON - P_F1_ON;
        return set(P_F1_ON + o, 1).set(P_F1_TYPE + o, (float)t).set(P_F1_CUTOFF + o, cutoff).set(P_F1_RES + o, res)
               .set(P_F1_DRIVE + o, drive).set(P_F1_KEYTRK + o, keytrk);
    }
    B& subOsc(int shape, float level, bool direct = true, int oct = 0)
    {
        return set(P_SUB_ON, 1).set(P_SUB_SHAPE, (float)shape).set(P_SUB_LEVEL, level).set(P_SUB_DIRECT, direct ? 1.0f : 0.0f).set(P_SUB_OCT, (float)oct);
    }
    B& reverb(int type, float mix, float decay, float size = 0.6f) { return set(P_REV_ON, 1).set(P_REV_TYPE, (float)type).set(P_REV_MIX, mix).set(P_REV_DECAY, decay).set(P_REV_SIZE, size); }
    B& delay(int div, float fb, float mix, bool ping = true, float tone = 6000.0f) { return set(P_DLY_ON, 1).set(P_DLY_SYNC, 1).set(P_DLY_DIV, (float)div).set(P_DLY_FEEDBACK, fb).set(P_DLY_MIX, mix).set(P_DLY_MODE, ping ? 1.0f : 0.0f).set(P_DLY_TONE, tone); }
    B& dist(int type, float drive, float mix, float tone = 12000.0f) { return set(P_DIST_ON, 1).set(P_DIST_TYPE, (float)type).set(P_DIST_DRIVE, drive).set(P_DIST_MIX, mix).set(P_DIST_TONE, tone); }
    B& chorus(int mode, float mix, float depth = 0.5f, float rate = 0.4f) { return set(P_CHO_ON, 1).set(P_CHO_MODE, (float)mode).set(P_CHO_MIX, mix).set(P_CHO_DEPTH, depth).set(P_CHO_RATE, rate); }
    B& ott(float depth) { return set(P_OTT_ON, 1).set(P_OTT_DEPTH, depth); }
    B& macros(std::initializer_list<float> v) { int k = 0; for (float x : v) set(P_MACRO1 + k++, x); return *this; }

    // ---- 0.4.6 pack helpers
    B& co(int type, float level, float timbre, float chr, float drive, float sub, float env = 0.0f, float time = 0.35f,
          float toF1 = 1.0f)
    {
        return set(P_CO_TYPE, (float)type).set(P_CO_LEVEL, level).set(P_CO_TIMBRE, timbre).set(P_CO_CHAR, chr)
              .set(P_CO_DRIVE, drive).set(P_CO_SUB, sub).set(P_CO_ENV, env).set(P_CO_TIME, time).set(P_CO_TO_F1, toF1);
    }
    B& guard(float freq = 120.0f, float level = 0.0f, float warmth = 0.2f)
    {
        return set(P_SG_ON, 1).set(P_SG_FREQ, freq).set(P_SG_LEVEL, level).set(P_SG_WARMTH, warmth);
    }
    B& chord(int c) { return set(P_CHORD, (float)c); }
    // effect library slot 0..3 (Insert 1, Insert 2, FX 1, FX 2) by type name; knobs A.. (0..1), the rest default
    B& fx(int sl, const char* type, float mix, std::initializer_list<float> knobs = {})
    {
        int t = 0;
        for (int i = 1; i < fxlib::numTypes(); ++i) if (std::string(fxlib::typeName(i)) == type) t = i;
        const int b = P_I1_TYPE + sl * (P_I2_TYPE - P_I1_TYPE);
        set(b, (float)t).set(b + 1, mix);
        for (int k = 0; k < fxlib::kNumKnobs; ++k) set(b + 2 + k, fxlib::knobDefault(t, k));
        int k = 0; for (float v : knobs) set(b + 2 + k++, v);
        return *this;
    }
    B& model(int f, int mdl, int kind, int slope = 0)
    {
        const int o = f == 1 ? 0 : P_F2_MODEL - P_F1_MODEL;
        const int fo = f == 1 ? 0 : P_F2_ON - P_F1_ON;
        return set(P_F1_ON + fo, 1).set(P_F1_MODEL + o, (float)mdl).set(P_F1_KIND + o, (float)kind).set(P_F1_SLOPE + o, (float)slope);
    }
    B& reese(float drift, float rate, float growl, bool lock = true)
    {
        return set(P_RS_DRIFT, drift).set(P_RS_DRIFT_RATE, rate).set(P_RS_GROWL, growl).set(P_RS_LOCK, lock ? 1.0f : 0.0f);
    }
    B& groove(int n, float swing, int feel = 0, int fill = 0)
    {
        return set(n == 1 ? P_P1_SWING : P_P2_SWING, swing).set(n == 1 ? P_P1_FEEL : P_P2_FEEL, (float)feel)
              .set(n == 1 ? P_P1_FILL : P_P2_FILL, (float)fill);
    }
    B& oscOff(int s) { return set(s == 0 ? P_A_ON : P_B_ON, 0.0f); }
};

// Divisions (choices::syncDiv)
enum { D1BAR = 3, D1_2 = 4, D1_4D = 6, D1_4 = 7, D1_8D = 9, D1_8 = 10, D1_8T = 11, D1_16 = 13 };
// Sources shorthand
enum { E2 = SRC_ENV2, E3 = SRC_ENV3, L1 = SRC_LFO1, L2 = SRC_LFO2, L3 = SRC_LFO3, P1 = SRC_PERF1, VEL = SRC_VELOCITY,
       PRS = SRC_PRESSURE, MW = SRC_MODWHEEL, M1 = SRC_MACRO1, M2 = SRC_MACRO2, M3 = SRC_MACRO3, M4 = SRC_MACRO4 };

// Shared Reese core: two detuned saws, clean sine sub direct-out, mono legato.
B reeseCore(const char* name, const char* comment)
{
    B b(name, "Bass", comment);
    b.table(0, BuiltinTable::BasicShapes).set(P_A_POS, kSaw).unison(0, 3, 0.16f, 0.8f, 0.35f).set(P_A_LEVEL, 0.7f)
     .table(1, BuiltinTable::BasicShapes).set(P_B_ON, 1).set(P_B_POS, kSaw).set(P_B_FINE, -9.0f).set(P_B_LEVEL, 0.6f)
     .set(P_B_TO_F1, 1.0f).set(P_B_TO_F2, 0.0f)
     .subOsc(0, 0.55f).filter(1, FilterType::LP24, 900.0f, 0.15f, 0.25f, 0.3f)
     .env(1, 2.0f, 300.0f, 1.0f, 120.0f).mono(35.0f)
     .mod(M1, P_F1_CUTOFF, 0.35f).mod(M2, P_A_DETUNE, 0.4f)
     .macros({0.0f, 0.0f, 0.0f, 0.0f});
    return b;
}

// Mega filter models (choices::filterModel) and kinds, slopes
enum { M_LADDER = 3, M_STEINER = 4, M_SVF = 5, M_POLIVOKS = 7, M_MS20 = 10, M_SEM = 12, M_303 = 13 };
enum { K_LP = 0, K_HP = 1, K_BP = 2 };
enum { S_MODEL = 0, S_12 = 2, S_18 = 3, S_24 = 4 };
// Chords (choices::chordMemory)
enum { CH_OCT = 1, CH_MINOR = 4, CH_MIN7 = 6, CH_MIN9 = 9, CH_RAVE = 11, CH_DUB = 12, CH_HOOVER_OCT = 13 };

// 0.4.6 jungle / drum & bass pack: 10 reese, 6 sub, 6 hoover / stab, 4 pads, 4 FX. Uses the newer engine parts
// (character oscillator, Reese drift and growl, sub guardian, Mega filters, performer swing / feel / fills, LFO
// jump, chord memory, pad scenes, filth). Masters set in buildPack's table below.
void buildPack(std::vector<Preset>& v)
{
    std::vector<Preset> pk;
    auto add = [&](B& b) { pk.push_back(b.p); };
    auto rs = [](const char* name, const char* comment) {
        B b = reeseCore(name, comment);
        b.p.category = "Reese";
        return b;
    };

    // ------------------------------------------------------------------ REESE
    { B b = rs("Drift Reese", "Reese whose detune never sits still (Drift), sub guardian keeping the low end mono and "
                              "clean. Macro 1 filter, Macro 2 detune, mod wheel = filth.");
      b.reese(0.6f, 0.35f, 0.0f).guard(110.0f, 0.0f, 0.3f).set(P_FILTH, 0.7f).dist(0, 0.3f, 0.5f); add(b); }
    { B b = rs("Growl Reese", "Growl on a 1/8 LFO: cutoff, drive, positions and warps talk together. Macro 3 = growl depth.");
      b.table(0, BuiltinTable::Growl).set(P_A_POS, 0.3f).reese(0.3f, 0.3f, 0.0f).set(P_RS_G_CUT, 2.5f)
       .lfo(1, 0, true, D1_8, 1).mod(L1, P_RS_GROWL, 0.6f, M3).guard(120.0f).macros({0.0f, 0.0f, 0.8f}).ott(0.4f); add(b); }
    { B b = rs("Neuro Jump", "Formant neuro bass; the wobble LFO jumps x4 (1/4 to 1/16) while Macro 4 is up. Filth on the wheel.");
      b.table(0, BuiltinTable::Vowels).set(P_A_POS, 0.2f).filter(2, FilterType::Formant, 900.0f, 0.5f).set(P_FILTER_BLEND, 1.0f)
       .lfo(1, 0, true, D1_4, 1).set(P_L1_JUMP, 3.0f).set(P_LJ_MACRO, 3.0f).mod(L1, P_A_POS, 0.45f).mod(L1, P_F2_CUTOFF, 0.3f)
       .reese(0.3f, 0.4f, 0.3f).set(P_FILTH, 0.9f).guard(130.0f).ott(0.6f).dist(2, 0.25f, 0.4f); add(b); }
    { B b = rs("Hoover Reese Stack", "Wavetable Reese over the Hoover unit's two beating saws, drifting apart.");
      b.co(kCharHooverReese, 0.6f, 0.5f, 0.3f, 0.2f, 0.0f).reese(0.45f, 0.25f, 0.0f).guard(110.0f).chorus(1, 0.2f); add(b); }
    { B b = rs("Steppa Reese", "Performer 1 rolls the filter in 1/16s with swing and a fill at the end of every 4 bars.");
      b.perf(1, D1_16, {1, 0.3f, 0.7f, 0.2f, 1, 0.4f, 0.8f, 0.1f, 1, 0.3f, 0.7f, 0.2f, 1, 0.5f, 0.9f, 0.3f}, {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5})
       .groove(1, 0.45f, 0, 2).mod(P1, P_F1_CUTOFF, 0.4f).reese(0.3f, 0.3f, 0.0f).guard(120.0f).dist(0, 0.3f, 0.5f); add(b); }
    { B b = rs("Half-time Reese", "The same roll at half time, with x3 roll curves on the off-beats: halftime / 85 BPM weight.");
      b.perf(1, D1_16, {1, 0.5f, 0.9f, 0.4f, 1, 0.5f, 0.9f, 0.4f}, {3, 25, 5, 25, 3, 25, 5, 25})
       .groove(1, 0.0f, 1, 0).mod(P1, P_F1_CUTOFF, 0.45f).reese(0.4f, 0.2f, 0.1f).guard(120.0f).dist(0, 0.35f, 0.5f); add(b); }
    { B b = rs("Ladder Reese", "Mega ladder filter driven hard, 24 dB. Macro 1 opens it.");
      b.model(1, M_LADDER, K_LP, S_24).set(P_F1_CUTOFF, 700.0f).set(P_F1_RES, 0.35f).set(P_F1_DRIVE, 0.6f)
       .reese(0.35f, 0.3f, 0.0f).guard(120.0f); add(b); }
    { B b = rs("303 Reese", "TB-303 model at 18 dB with an accenting performer: acid meets Reese.");
      b.model(1, M_303, K_LP, S_18).set(P_F1_CUTOFF, 500.0f).set(P_F1_RES, 0.6f)
       .perf(1, D1_16, {1, 0.2f, 0.6f, 0.2f, 0.9f, 0.2f, 0.5f, 1}, {5, 5, 5, 5, 5, 5, 5, 5}).mod(P1, P_F1_CUTOFF, 0.5f)
       .reese(0.25f, 0.3f, 0.0f).guard(120.0f).dist(1, 0.25f, 0.4f); add(b); }
    { B b("Rollers Reese", "Reese", "The Rollers unit alone: clean sub under drifting saws, drive only on the top.");
      b.oscOff(0).co(kCharRollSubReese, 0.9f, 0.45f, 0.6f, 0.35f, 0.0f).filter(1, FilterType::LP24, 2500.0f, 0.1f)
       .env(1, 2.0f, 300.0f, 1.0f, 120.0f).mono(30.0f).mod(M1, P_F1_CUTOFF, 0.35f).mod(M2, P_CO_TIMBRE, 0.4f).ott(0.3f); add(b); }
    { B b = rs("Dread Reese", "Reese with Dread Bass's backwards-303 sweep layered in, Amiga crunch on the wheel.");
      b.co(kCharDread, 0.55f, 0.6f, 0.55f, 0.3f, 0.0f, 0.0f, 0.4f).set(P_FILTH, 0.6f).reese(0.3f, 0.3f, 0.0f).guard(120.0f)
       .fx(0, "Amiga", 0.35f); add(b); }

    // ------------------------------------------------------------------ SUB
    { B b("Guardian Sub", "Sub", "Triangle and sine sub through a tube drive, with the sub guardian keeping the lowest octave pure.");
      b.table(0, BuiltinTable::BasicShapes).set(P_A_POS, kTri).subOsc(0, 0.7f).filter(1, FilterType::LP24, 1200.0f, 0.0f)
       .env(1, 2.0f, 200.0f, 1.0f, 90.0f).mono(25.0f).guard(100.0f, 0.0f, 0.35f).dist(0, 0.5f, 0.7f); add(b); }
    { B b("Rollers Sub", "Sub", "The Rollers sine sub with a short pitch drop on every note: steppers' weight.");
      b.oscOff(0).co(kCharRollSub, 1.0f, 0.3f, 0.2f, 0.15f, 0.0f, 0.25f, 0.2f).set(P_F1_ON, 0)
       .env(1, 1.0f, 300.0f, 1.0f, 100.0f).mono(20.0f); add(b); }
    { B b("808 Drop", "Sub", "Long sine boom with a fifth-down pitch drop, driven, guarded below 90 Hz.");
      b.set(P_A_POS, kSine).env(1, 0.5f, 1500.0f, 0.0f, 400.0f).env(2, 0.5f, 120.0f, 0.0f, 80.0f, -0.6f)
       .mod(E2, -DST_PITCH, 0.3f).mono(0.0f, false).set(P_F1_ON, 0).guard(90.0f, 0.0f, 0.4f).dist(0, 0.55f, 0.6f, 5000.0f); add(b); }
    { B b("Donk Sub", "Sub", "Dread Bass's donk: the resonance lands on the 5th harmonic and rings out.");
      b.oscOff(0).co(kCharDonk, 1.0f, 0.68f, 0.7f, 0.1f, 0.35f, 0.0f, 0.3f).set(P_F1_ON, 0)
       .env(1, 1.0f, 400.0f, 0.0f, 150.0f).mono(0.0f, false); add(b); }
    { B b("Pluck Sub", "Sub", "Short sine-triangle pluck for fast basslines; velocity opens it.");
      b.set(P_A_POS, 0.15f).subOsc(0, 0.5f).filter(1, FilterType::LP12, 600.0f, 0.1f, 0.2f, 0.5f)
       .env(1, 1.0f, 220.0f, 0.0f, 80.0f).env(2, 0.5f, 120.0f, 0.0f, 60.0f).mod(E2, P_F1_CUTOFF, 0.3f).mod(VEL, P_F1_CUTOFF, 0.2f)
       .mono(0.0f, false); add(b); }
    { B b("Tri Sub Drive", "Sub", "Triangle sub through the Drive insert; above 120 Hz it gets dirty, below stays clean.");
      b.set(P_A_POS, kTri).set(P_F1_ON, 0).env(1, 1.0f, 300.0f, 1.0f, 100.0f).mono(25.0f).guard(120.0f, 0.0f, 0.2f)
       .fx(0, "Drive", 1.0f, {0.7f}); add(b); }

    // ------------------------------------------------------------------ HOOVER / STAB
    { B b("Unit Hoover", "Hoover", "The Hoover unit: PWM-saw stack with a pitch dive on every note and ensemble.");
      b.oscOff(0).co(kCharHoover, 1.0f, 0.5f, 0.6f, 0.15f, 0.25f, 0.5f, 0.35f).filter(1, FilterType::LP24, 6000.0f, 0.1f)
       .env(1, 3.0f, 400.0f, 0.9f, 250.0f).mono(70.0f).chorus(1, 0.4f).reverb(0, 0.15f, 0.4f); add(b); }
    { B b("Mentasm Unit", "Hoover", "Mentasm mode: an octave-down PWM-saw growls under the stack. Macro 1 = PWM.");
      b.oscOff(0).co(kCharMentasm, 1.0f, 0.55f, 0.5f, 0.25f, 0.2f, 0.35f, 0.4f).filter(1, FilterType::LP24, 4500.0f, 0.15f)
       .env(1, 5.0f, 400.0f, 0.9f, 300.0f).mono(90.0f).mod(M1, P_CO_CHAR, 0.4f).chorus(1, 0.35f); add(b); }
    { B b("Rave Scream", "Hoover", "Rave mode: octave-up scream layer, driven, ping-pong 1/8.");
      b.oscOff(0).co(kCharRave, 1.0f, 0.6f, 0.7f, 0.45f, 0.1f, 0.4f, 0.3f).filter(1, FilterType::LP24, 8000.0f, 0.1f)
       .env(1, 2.0f, 300.0f, 0.9f, 200.0f).mono(50.0f).delay(D1_8, 0.35f, 0.2f); add(b); }
    { B b("Rave Stab Chord", "Stab", "One finger, a whole rave stab (Chord: Rave Stab). Snappy saws into a plate.");
      b.set(P_A_POS, kSaw).unison(0, 3, 0.12f, 0.7f, 0.5f).filter(1, FilterType::LP24, 2500.0f, 0.2f)
       .env(1, 1.0f, 300.0f, 0.0f, 200.0f).env(2, 0.5f, 200.0f, 0.0f, 100.0f).mod(E2, P_F1_CUTOFF, 0.35f).chord(CH_RAVE)
       .set(P_VOICES, 8.0f).reverb(0, 0.25f, 0.45f); add(b); }
    { B b("Dub Stab Chord", "Stab", "Dub Stab chord memory into the Dub Delay: one key, the whole skank.");
      b.set(P_A_POS, kSquare).filter(1, FilterType::BP12, 1200.0f, 0.3f).env(1, 0.5f, 180.0f, 0.0f, 120.0f).chord(CH_DUB)
       .set(P_VOICES, 8.0f).fx(2, "Dub Delay", 0.4f).reverb(1, 0.25f, 0.6f); add(b); }
    { B b("Hoover Oct Stab", "Stab", "The Hoover unit with Chord: Hoover Oct (an octave below and above): the hoover chord.");
      b.oscOff(0).co(kCharHoover, 0.8f, 0.45f, 0.6f, 0.2f, 0.0f, 0.3f, 0.3f).chord(CH_HOOVER_OCT).set(P_VOICES, 8.0f)
       .filter(1, FilterType::LP24, 5000.0f, 0.1f).env(1, 2.0f, 500.0f, 0.6f, 300.0f).chorus(1, 0.3f).reverb(0, 0.2f, 0.5f); add(b); }

    // ------------------------------------------------------------------ PADS
    { B b("Reinforced Pad", "Pad", "Wide saw pad through the Ensemble and Super-Massive: 94-era atmospherics.");
      b.set(P_A_POS, kSaw).unison(0, 5, 0.2f, 0.8f, 0.9f).filter(1, FilterType::LP12, 2200.0f, 0.1f)
       .env(1, 600.0f, 1000.0f, 0.8f, 1800.0f).lfo(1, 0, false, 0.15f, 1).mod(L1, P_F1_CUTOFF, 0.12f)
       .fx(0, "Ensemble", 0.5f).fx(2, "Super-Massive", 0.4f); add(b); }
    { B b("Dread Pad", "Pad", "Minor 9 from every key (chord memory), slow filter, vinyl air. Keep it to single notes.");
      b.set(P_A_POS, 0.55f).unison(0, 3, 0.1f, 0.6f, 0.8f).set(P_VOICES, 8.0f).chord(CH_MIN9).filter(1, FilterType::LP24, 1400.0f, 0.15f)
       .env(1, 800.0f, 1200.0f, 0.8f, 2000.0f).set(P_NOISE_ON, 1).set(P_NOISE_TYPE, 3.0f).set(P_NOISE_LEVEL, 0.1f)
       .reverb(1, 0.4f, 0.8f, 0.8f); add(b); }
    { B b("Amen Air", "Pad", "Vowel table drifting through the SP-1200: grainy, breathing pad for intros.");
      b.table(0, BuiltinTable::Vowels).set(P_A_POS, 0.3f).unison(0, 3, 0.1f, 0.6f, 0.7f).filter(1, FilterType::LP12, 3000.0f, 0.1f)
       .env(1, 500.0f, 1000.0f, 0.8f, 1500.0f).lfo(1, 6, false, 0.2f, 1).mod(L1, P_A_POS, 0.25f)
       .fx(0, "SP-1200", 0.6f).reverb(1, 0.35f, 0.7f); add(b); }
    { B b("Scene Pad", "Pad", "Built for pad scenes: Macro 1 filter, 2 detune, 3 table position, 4 reverb send. Scenes 1-16 "
                              "climb from dark to wide open; turn Scene Pads on and play pads A01-A16.");
      b.table(0, BuiltinTable::HarmonicSweep).set(P_A_POS, 0.2f).unison(0, 5, 0.08f, 0.7f, 0.9f).filter(1, FilterType::LP24, 500.0f, 0.15f)
       .env(1, 400.0f, 1000.0f, 0.85f, 1500.0f).mod(M1, P_F1_CUTOFF, 0.55f).mod(M2, P_A_DETUNE, 0.4f).mod(M3, P_A_POS, 0.6f)
       .set(P_SCENE_MORPH, 1200.0f).reverb(1, 0.3f, 0.7f); add(b); }

    // ------------------------------------------------------------------ FX
    { B b("Dive Bomb", "FX", "The Hoover unit's dive at full depth and length: a two-octave swoop into a hall.");
      b.oscOff(0).co(kCharHoover, 1.0f, 0.6f, 0.7f, 0.3f, 0.0f, 1.0f, 0.85f).set(P_F1_ON, 0).env(1, 2.0f, 2000.0f, 0.7f, 800.0f)
       .reverb(1, 0.4f, 0.75f); add(b); }
    { B b("Jump Siren", "FX", "Siren on an LFO that jumps x4 faster while Macro 4 is up: ride it into the drop.");
      b.set(P_A_POS, kTri).set(P_F1_ON, 0).lfo(1, 1, true, D1_2, 1).set(P_L1_JUMP, 3.0f).set(P_LJ_MACRO, 3.0f)
       .mod(L1, -DST_PITCH, 0.3f).fx(2, "Tape Echo", 0.3f).reverb(0, 0.2f, 0.5f); add(b); }
    { B b("Rewind", "FX", "Hold a note: the pitch winds down two octaves over two seconds, through a tape echo.");
      b.set(P_A_POS, kSaw).unison(0, 3, 0.1f, 0.6f, 0.6f).filter(1, FilterType::LP24, 4000.0f, 0.1f)
       .env(2, 2000.0f, 100.0f, 1.0f, 100.0f).mod(E2, -DST_PITCH, -1.0f).set(P_BEND_RANGE, 24.0f)
       .fx(2, "Tape Echo", 0.35f); add(b); }
    { B b("Filth Sweep", "FX", "Noise and growl table swept up by an envelope; the mod wheel adds filth on top.");
      b.table(0, BuiltinTable::Growl).set(P_NOISE_ON, 1).set(P_NOISE_LEVEL, 0.3f).filter(1, FilterType::BP24, 400.0f, 0.4f)
       .env(2, 2500.0f, 500.0f, 1.0f, 500.0f).mod(E2, P_F1_CUTOFF, 0.7f).mod(E2, P_A_POS, 0.8f).set(P_FILTH, 1.0f)
       .reese(0.0f, 0.3f, 0.2f, false).reverb(1, 0.3f, 0.7f); add(b); }

    // Gain staging (as the originals: the golden-test performance peaks near -3 dBFS)
    static const float master[] = {
        -6.5f, -4.0f, -5.5f, -5.5f, -7.0f, -8.5f, -4.5f, -9.0f, -2.5f, -6.5f,
        -6.5f, -3.5f, -5.5f, -4.5f, -6.5f, -1.5f,
        -2.0f, -3.5f, -2.5f, -10.0f, -11.5f, -7.5f,
        -8.5f, -0.5f, -3.0f, -2.0f,
        -4.0f, -2.5f, -2.0f, -1.0f};
    static_assert(sizeof master / sizeof master[0] == 30, "one master per pack preset");
    for (size_t i = 0; i < pk.size() && i < 30; ++i) pk[i].params[P_MASTER] = master[i];
    for (auto& p : pk) v.push_back(p);
}

std::vector<Preset> build()
{
    std::vector<Preset> v;
    auto add = [&](B& b) { v.push_back(b.p); };

    // ------------------------------------------------------------------ BASS
    { B b = reeseCore("Classic Reese", "Two detuned saws and a clean sub. Macro 1 opens the filter, Macro 2 widens the detune.");
      b.dist(0, 0.2f, 0.5f); add(b); }
    { B b = reeseCore("Moving Reese", "Reese with a bar-long filter sweep and a drifting detune. Mod wheel deepens the sweep.");
      b.lfo(1, 1, true, D1BAR).lfo(2, 0, false, 0.15f, 1)
       .mod(L1, P_F1_CUTOFF, 0.25f).mod(L2, P_A_DETUNE, 0.25f).mod(MW, P_F1_CUTOFF, 0.3f, L1)
       .chorus(0, 0.2f).dist(0, 0.25f, 0.5f); add(b); }
    { B b("Neuro Reese", "Bass", "Serial ladder into formant, 1/8 LFO on the vowel, OTT and fold for neuro grit.");
      b.table(0, BuiltinTable::SyncSweep).set(P_A_POS, 0.35f).unison(0, 3, 0.2f, 0.7f, 0.3f)
       .set(P_A_W1_MODE, (float)Warp::FmOther).set(P_A_W1_AMT, 0.12f)
       .set(P_B_ON, 1).set(P_B_POS, kSaw).set(P_B_OCT, -1).set(P_B_LEVEL, 0.5f).set(P_B_TO_F1, 1).set(P_B_TO_F2, 0)
       .subOsc(0, 0.5f).filter(1, FilterType::Ladder24, 1400.0f, 0.3f, 0.35f).filter(2, FilterType::Formant, 300.0f, 0.5f)
       .set(P_FILTER_BLEND, 0.7f).lfo(1, 0, true, D1_8).mod(L1, P_F2_CUTOFF, 0.4f).mod(M1, P_A_POS, 0.6f)
       .mono(20.0f).env(1, 1.0f, 300.0f, 1.0f, 100.0f).ott(0.7f).dist(2, 0.25f, 0.6f); add(b); }
    { B b("Donk", "Bass", "UK bounce donk: a triangle pluck with a fast pitch drop.");
      b.set(P_A_POS, kTri).set(P_A_LEVEL, 0.9f).filter(1, FilterType::LP12, 1800.0f, 0.5f)
       .env(1, 0.5f, 220.0f, 0.0f, 80.0f).env(2, 0.5f, 60.0f, 0.0f, 60.0f, -0.6f)
       .mod(E2, -DST_PITCH, 0.5f).mod(E2, P_F1_CUTOFF, 0.3f).mod(VEL, P_F1_CUTOFF, 0.2f); add(b); }
    { B b("Growl", "Bass", "Growl table swept by a 1/8 LFO through a formant filter. Macro 1 = growl depth.");
      b.table(0, BuiltinTable::Growl).set(P_A_POS, 0.3f).unison(0, 2, 0.1f, 0.8f, 0.2f).subOsc(0, 0.5f)
       .filter(1, FilterType::Formant, 400.0f, 0.4f).lfo(1, 0, true, D1_8)
       .mod(L1, P_A_POS, 0.5f, M1).mod(L1, P_F1_CUTOFF, 0.35f).mono(20.0f).env(1, 1.0f, 300.0f, 1.0f, 100.0f)
       .dist(0, 0.4f, 0.7f).ott(0.6f).macros({1.0f}); add(b); }
    { B b("Wobble 1/8", "Bass", "Classic wobble: resonant low-pass on a 1/8 sine LFO. Macro 1 = wobble depth.");
      b.set(P_A_POS, kSaw).unison(0, 3, 0.12f, 0.8f, 0.3f).subOsc(0, 0.6f)
       .filter(1, FilterType::LP24, 300.0f, 0.45f, 0.3f).lfo(1, 0, true, D1_8)
       .mod(L1, P_F1_CUTOFF, 0.55f, M1).mono(15.0f).env(1, 1.0f, 300.0f, 1.0f, 100.0f).dist(1, 0.2f, 0.4f)
       .macros({1.0f}); add(b); }
    { B b("Sub Sine", "Bass", "Pure sine sub with a touch of tube warmth. Play it low.");
      b.set(P_A_POS, kSine).set(P_A_LEVEL, 0.9f).set(P_F1_ON, 0).env(1, 2.0f, 200.0f, 1.0f, 80.0f)
       .mono(25.0f).dist(0, 0.1f, 0.3f); add(b); }
    { B b("Dirty 808", "Bass", "Long sine boom with a pitch punch, driven hard.");
      b.set(P_A_POS, kSine).set(P_A_LEVEL, 0.9f).set(P_F1_ON, 0).env(1, 1.0f, 900.0f, 0.3f, 300.0f)
       .env(2, 0.5f, 50.0f, 0.0f, 50.0f, -0.5f).mod(E2, -DST_PITCH, 0.25f).mono(0.0f, false).dist(0, 0.45f, 0.6f, 6000.0f); add(b); }
    { B b = reeseCore("Rolling Reese", "Reese with a 1/16 performer rolling the filter, for 170 BPM rollers.");
      b.perf(1, D1_16, {1.0f, 0.2f, 0.6f, 0.0f, 1.0f, 0.3f, 0.8f, 0.1f}, {5, 5, 5, 5, 5, 5, 5, 5})
       .mod(P1, P_F1_CUTOFF, 0.35f).dist(0, 0.25f, 0.5f); add(b); }
    { B b("Talking Bass", "Bass", "Vowel table and formant filter speak on the beat. Pressure opens the mouth.");
      b.table(0, BuiltinTable::Vowels).set(P_A_POS, 0.0f).unison(0, 2, 0.1f, 0.8f, 0.2f).subOsc(0, 0.5f)
       .filter(1, FilterType::Formant, 100.0f, 0.5f).lfo(1, 1, true, D1_4)
       .mod(L1, P_A_POS, 0.9f).mod(L1, P_F1_CUTOFF, 0.5f).mod(PRS, P_F1_CUTOFF, 0.3f)
       .mono(30.0f).env(1, 2.0f, 300.0f, 1.0f, 120.0f).dist(0, 0.2f, 0.4f); add(b); }

    // ------------------------------------------------------------------ LEAD / HOOVER
    { B b("Hoover 91", "Lead", "The '91 hoover: seven detuned voices, pitch scoop, ensemble.");
      b.table(0, BuiltinTable::Hoover).set(P_A_POS, 0.6f).unison(0, 7, 0.35f, 0.85f, 0.8f)
       .set(P_A_W1_MODE, (float)Warp::Pwm).set(P_A_W1_AMT, 0.2f)
       .env(2, 0.5f, 250.0f, 0.0f, 200.0f).mod(E2, -DST_PITCH, -0.06f).mod(MW, P_A_POS, 0.4f)
       .filter(1, FilterType::LP24, 5000.0f, 0.1f).mono(80.0f).chorus(1, 0.45f).env(1, 5.0f, 400.0f, 0.9f, 250.0f); add(b); }
    { B b("Mentasm", "Lead", "Darker hoover with a slow table drift and long glide.");
      b.table(0, BuiltinTable::Hoover).set(P_A_POS, 1.0f).unison(0, 7, 0.45f, 0.9f, 0.9f)
       .set(P_A_W1_MODE, (float)Warp::Pwm).set(P_A_W1_AMT, 0.3f).lfo(1, 0, false, 0.4f, 1).mod(L1, P_A_POS, -0.3f)
       .filter(1, FilterType::LP24, 3500.0f, 0.2f).mono(120.0f).chorus(1, 0.5f).dist(0, 0.2f, 0.4f)
       .env(1, 10.0f, 400.0f, 0.9f, 300.0f); add(b); }
    { B b("Rave Lead", "Lead", "Square stack an octave apart, filter envelope, ping-pong 1/8.");
      b.set(P_A_POS, 0.9f).unison(0, 3, 0.1f, 0.8f, 0.5f).set(P_B_ON, 1).set(P_B_POS, kSaw).set(P_B_OCT, 1).set(P_B_LEVEL, 0.4f)
       .set(P_B_TO_F1, 1).set(P_B_TO_F2, 0).filter(1, FilterType::LP24, 2500.0f, 0.4f, 0.0f, 0.4f)
       .env(2, 1.0f, 400.0f, 0.2f, 300.0f).mod(E2, P_F1_CUTOFF, 0.3f).mod(VEL, P_F1_CUTOFF, 0.15f)
       .delay(D1_8, 0.35f, 0.25f).reverb(0, 0.15f, 0.4f); add(b); }
    { B b("Screamer", "Lead", "Hard-sync scream with an LFO on the sync and a folding distortion.");
      b.table(0, BuiltinTable::SyncSweep).set(P_A_POS, 0.5f).set(P_A_W1_MODE, (float)Warp::Sync).set(P_A_W1_AMT, 0.35f)
       .lfo(1, 1, true, D1_4).mod(L1, P_A_W1_AMT, 0.25f).mod(MW, P_A_POS, 0.5f)
       .filter(1, FilterType::LP24, 7000.0f, 0.2f).mono(30.0f).dist(2, 0.3f, 0.5f).delay(D1_8D, 0.3f, 0.2f); add(b); }
    { B b("Sync Lead", "Lead", "Classic sync sweep on every note, chorus and plate.");
      b.set(P_A_POS, kSaw).set(P_A_W1_MODE, (float)Warp::Sync).set(P_A_W1_AMT, 0.1f).unison(0, 2, 0.08f, 0.8f, 0.4f)
       .env(2, 1.0f, 400.0f, 0.0f, 200.0f).mod(E2, P_A_W1_AMT, 0.5f).filter(1, FilterType::LP12, 6000.0f, 0.1f)
       .chorus(0, 0.3f).reverb(0, 0.2f, 0.4f); add(b); }

    // ------------------------------------------------------------------ PAD / ATMOS
    { B b("90s Pad", "Pad", "Wide saw stack and a harmonic layer, ensemble and hall: Detroit / Reinforced era.");
      b.set(P_A_POS, kSaw).unison(0, 5, 0.25f, 0.7f, 1.0f).set(P_B_ON, 1).table(1, BuiltinTable::HarmonicSweep)
       .set(P_B_POS, 0.5f).set(P_B_LEVEL, 0.4f).set(P_B_TO_F1, 1).set(P_B_TO_F2, 0)
       .filter(1, FilterType::LP24, 2200.0f, 0.1f).env(1, 600.0f, 1000.0f, 0.85f, 1800.0f)
       .lfo(1, 0, false, 0.2f, 1).mod(L1, P_F1_CUTOFF, 0.1f).chorus(1, 0.5f).reverb(1, 0.35f, 0.6f); add(b); }
    { B b("Dark Atmos", "Atmos", "Slow vowel drift under vinyl dust, long hall and dub delay.");
      b.table(0, BuiltinTable::Vowels).set(P_A_POS, 0.2f).unison(0, 3, 0.15f, 0.8f, 0.8f).lfo(1, 0, false, 0.07f, 1)
       .mod(L1, P_A_POS, 0.6f).set(P_NOISE_ON, 1).set(P_NOISE_TYPE, 3).set(P_NOISE_LEVEL, 0.25f)
       .filter(1, FilterType::LP24, 900.0f, 0.2f).env(1, 1500.0f, 1000.0f, 1.0f, 3000.0f)
       .reverb(1, 0.5f, 0.85f, 0.9f).delay(D1_4, 0.5f, 0.2f, true, 2500.0f); add(b); }
    { B b("Vinyl Dust Pad", "Pad", "SP-1200 style: 12-bit crush at 26 kHz over a soft pad and record crackle.");
      b.table(0, BuiltinTable::HarmonicSweep).set(P_A_POS, 0.3f).unison(0, 3, 0.15f, 0.8f, 0.6f)
       .set(P_NOISE_ON, 1).set(P_NOISE_TYPE, 3).set(P_NOISE_LEVEL, 0.35f).filter(1, FilterType::LP12, 3000.0f, 0.1f)
       .env(1, 300.0f, 800.0f, 0.9f, 1200.0f).dist(3, 0.1f, 0.6f, 9000.0f).set(P_DIST_BITS, 12).set(P_DIST_RATE, 26040)
       .chorus(0, 0.3f); add(b); }
    { B b("Choir Pad", "Pad", "Vowel 'aah' to 'eeh', five voices, hall.");
      b.table(0, BuiltinTable::Vowels).set(P_A_POS, 0.0f).unison(0, 5, 0.2f, 0.8f, 0.9f).lfo(1, 0, false, 0.15f, 1)
       .mod(L1, P_A_POS, 0.2f).mod(MW, P_A_POS, 0.5f).set(P_F1_ON, 0).env(1, 400.0f, 800.0f, 0.9f, 1500.0f)
       .reverb(1, 0.4f, 0.7f); add(b); }
    { B b("Glass Pad", "Pad", "Few harmonics and a sine an octave up, phaser and plate.");
      b.table(0, BuiltinTable::HarmonicSweep).set(P_A_POS, 0.1f).unison(0, 3, 0.1f, 0.8f, 0.8f)
       .set(P_B_ON, 1).set(P_B_POS, kSine).set(P_B_OCT, 1).set(P_B_LEVEL, 0.3f).set(P_B_TO_F1, 1).set(P_B_TO_F2, 0)
       .set(P_F1_ON, 0).env(1, 400.0f, 800.0f, 0.8f, 1600.0f).set(P_PH_ON, 1).set(P_PH_MIX, 0.4f).reverb(0, 0.3f, 0.6f); add(b); }

    // ------------------------------------------------------------------ STAB
    { B b("Rave Stab", "Stab", "Seven-voice saw stab with a snappy filter: play chords.");
      b.set(P_A_POS, kSaw).unison(0, 7, 0.3f, 0.8f, 0.8f).filter(1, FilterType::LP24, 3000.0f, 0.2f)
       .env(1, 0.5f, 350.0f, 0.0f, 200.0f).env(2, 0.5f, 200.0f, 0.0f, 200.0f).mod(E2, P_F1_CUTOFF, 0.35f)
       .reverb(0, 0.25f, 0.4f).dist(0, 0.15f, 0.5f); add(b); }
    { B b("Dub Chord", "Stab", "Short square chord into a dotted-quarter ping-pong and hall.");
      b.set(P_A_POS, 0.8f).unison(0, 2, 0.08f, 0.8f, 0.4f).filter(1, FilterType::LP12, 1500.0f, 0.3f)
       .env(1, 0.5f, 250.0f, 0.0f, 150.0f).delay(D1_4D, 0.55f, 0.35f, true, 2500.0f).reverb(1, 0.3f, 0.6f); add(b); }

    // ------------------------------------------------------------------ FX
    { B b("FX Riser", "FX", "Four-second riser: noise and saws, band-pass and pitch climbing. Hold a note.");
      b.set(P_A_POS, kSaw).unison(0, 7, 0.4f, 0.8f, 1.0f).set(P_NOISE_ON, 1).set(P_NOISE_LEVEL, 0.6f)
       .filter(1, FilterType::BP12, 200.0f, 0.4f).lfo(1, 2, false, 0.25f, 2)
       .mod(L1, P_F1_CUTOFF, 0.5f).mod(L1, -DST_PITCH, 0.25f).env(1, 3000.0f, 1000.0f, 1.0f, 800.0f)
       .reverb(1, 0.4f, 0.7f); add(b); }
    { B b("Laser Zap", "FX", "Two-octave pitch dive with fold and a 1/16 echo.");
      b.set(P_A_POS, kSine).set(P_A_LEVEL, 0.8f).set(P_F1_ON, 0).env(1, 0.5f, 250.0f, 0.0f, 100.0f)
       .env(2, 0.5f, 180.0f, 0.0f, 100.0f, -0.4f).mod(E2, -DST_PITCH, 1.0f).dist(2, 0.3f, 0.5f)
       .delay(D1_16, 0.45f, 0.3f); add(b); }
    buildPack(v);
    // Gain staging: master level per preset so the golden-test performance peaks near -3 dBFS and helix-render's
    // test riff (which ends on a four-note chord) stays below 0 dBFS.
    static const float master[] = {-6.0f, -6.0f, -3.0f, -9.5f, -5.0f, -6.0f, -2.5f, -4.5f, -6.5f, -12.5f, -1.0f, -1.0f,
                                   -8.0f, 1.5f, -4.5f, -5.5f, -1.5f, -4.0f, -1.0f, -8.0f, -5.5f, 5.5f, -0.5f, -5.0f};
    for (size_t i = 0; i < v.size() && i < sizeof master / sizeof master[0]; ++i) v[i].params[P_MASTER] = master[i];
    return v;
}

} // namespace

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets = [] {
        auto v = build();
        for (auto& p : v) fxlib::foldRack(p.params);   // (written with the old rack; shown in the effect slots)
        return v;
    }();
    return presets;
}

} // namespace helix
