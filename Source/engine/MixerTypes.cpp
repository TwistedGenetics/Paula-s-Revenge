// Paula's Revenge - mixer settings, effect descriptions
#include "MixerTypes.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prv
{
namespace
{
    std::string fmt (const char* f, double v) { char b[48]; std::snprintf (b, sizeof b, f, v); return b; }
    std::string hz (double v) { return v >= 1000.0 ? fmt ("%.1f KHZ", v / 1000.0) : fmt ("%.0f HZ", v); }
    std::string ms (double v) { return v >= 1000.0 ? fmt ("%.2f S", v / 1000.0) : (v < 10 ? fmt ("%.1f MS", v) : fmt ("%.0f MS", v)); }
    std::string pct (double v) { return fmt ("%.0f%%", v * 100.0); }

    struct Division { const char* name; double beats; };
    const Division kDivisions[] = { { "1/32", 0.125 }, { "1/16", 0.25 }, { "1/8 T", 1.0 / 3.0 }, { "1/16 D", 0.375 }, { "1/8", 0.5 },
                                    { "1/8 D", 0.75 }, { "1/4", 1.0 }, { "1/4 D", 1.5 }, { "1/2", 2.0 } };
    constexpr int kNumDivisions = (int) (sizeof kDivisions / sizeof kDivisions[0]);
    int divisionIndex (float v) { return std::clamp ((int) std::lround (v * (kNumDivisions - 1)), 0, kNumDivisions - 1); }
}

void MixerSettings::initReturns()
{
    for (auto& r : ret) r = ReturnStrip {};
    initFxSlot (ret[0].inserts[0], FxType::Reverb);
    ret[0].inserts[0].p[4] = 1.0f;     // fully wet on a return
    initFxSlot (ret[1].inserts[0], FxType::Delay);
    ret[1].inserts[0].p[5] = 1.0f;
}

FxSlot& MixerSettings::slot (int i)
{
    if (i < kTrackSlots) return ch[i / kInserts].inserts[i % kInserts];
    if (i >= kMixerSlots)
    {
        const int j = std::min (i - kMixerSlots, kInstrumentSlots - 1);
        return inst[j / kInserts][j % kInserts];
    }
    i -= kTrackSlots;
    if (i < kReturnSlots) return ret[i / kInserts].inserts[i % kInserts];
    return master.inserts[std::clamp (i - kReturnSlots, 0, kInserts - 1)];
}

const FxSlot& MixerSettings::slot (int i) const { return const_cast<MixerSettings*> (this)->slot (i); }

bool MixerSettings::setUpDub()
{
    const MixerSettings fresh;
    bool asNew = true, empty = true;
    for (int k = 0; k < kInserts; ++k)
    {
        asNew = asNew && ret[1].inserts[k] == fresh.ret[1].inserts[k];
        empty = empty && ret[1].inserts[k].type == FxType::None;
    }
    if (! asNew && ! empty) return false;
    for (auto& s : ret[1].inserts) s = FxSlot {};
    initFxSlot (ret[1].inserts[0], FxType::DubDelay);
    ret[1].inserts[0].p[2] = 0.55f;    // FEEDBACK: a good few repeats
    ret[1].inserts[0].p[9] = 1.0f;     // MIX: all echo (it's a return)
    initFxSlot (ret[1].inserts[1], FxType::Spring);
    ret[1].inserts[1].p[0] = 0.3f;     // MIX: a splash of spring on the echoes
    return true;
}

bool MixerSettings::dubReady() const
{
    for (const auto& s : ret[1].inserts)
        if (s.active())
            for (const auto& q : fxParams (s.type)) if (q.name == "FEEDBACK") return true;
    return false;
}

bool MixerSettings::isFlat() const
{
    for (const auto& c : ch)
    {
        if (c.volumeDb != 0.0f || c.eq.anyOn()) return false;
        for (const auto& s : c.inserts) if (s.active()) return false;
        for (float s : c.send) if (s > 0.0f) return false;
    }
    if (master.volumeDb != 0.0f) return false;
    for (const auto& s : master.inserts) if (s.active()) return false;
    for (int i = 0; i < kMixInstruments; ++i) if (instrumentChain (i)) return false;
    if (mods.any() || anyInstMacro()) return false;
    return true;   // (returns only hear what's sent to them)
}

std::string eqTypeName (EqType t)
{
    switch (t)
    {
        case EqType::Bell:      return "BELL";
        case EqType::LowShelf:  return "LOW SHELF";
        case EqType::HighShelf: return "HIGH SHELF";
        case EqType::HighPass:  return "HIGH-PASS";
        case EqType::LowPass:   return "LOW-PASS";
        default:                return "";
    }
}

EqType eqTypeForFreq (float hz)
{
    return hz < 80.0f ? EqType::LowShelf : (hz > 10000.0f ? EqType::HighShelf : EqType::Bell);
}

EqBand eqBandFromOld (int k, bool on, float freq, float gain, float q)
{
    static const EqType types[kEqOldBands] = { EqType::HighPass, EqType::LowShelf, EqType::Bell, EqType::Bell, EqType::HighShelf };
    EqBand b;
    if (! on || k < 0 || k >= kEqOldBands) return b;
    b.used = b.on = true;
    b.type = types[k];
    b.freq = freq; b.gain = b.hasGain() ? gain : 0.0f; b.q = q;
    return b;
}

std::string fxName (FxType t)
{
    switch (t)
    {
        case FxType::Filter:     return "FILTER";
        case FxType::Drive:      return "DRIVE";
        case FxType::Compressor: return "COMPRESSOR";
        case FxType::Delay:      return "DELAY";
        case FxType::Reverb:     return "REVERB";
        case FxType::Chorus:     return "CHORUS";
        case FxType::Plugin:     return "PLUG-IN";
        case FxType::Vast:       return "VAST";
        case FxType::Space:      return "SPACE";
        case FxType::TapeEcho:   return "TAPE ECHO";
        case FxType::Ensemble:   return "ENSEMBLE";
        case FxType::Rooms:      return "ROOMS";
        case FxType::Spring:     return "SPRING";
        case FxType::DubDelay:   return "DUB DELAY";
        case FxType::BbdDelay:   return "BBD DELAY";
        case FxType::Grain:      return "GRAIN DELAY";
        case FxType::MultiTap:   return "MULTI-TAP";
        case FxType::Flanger:    return "FLANGER";
        case FxType::Phaser:     return "PHASER";
        case FxType::Rotary:     return "ROTARY";
        case FxType::Tremolo:    return "TREMOLO";
        case FxType::FreqShift:  return "FREQ SHIFTER";
        case FxType::AutoFilter: return "AUTO-FILTER";
        case FxType::Dimension:  return "DIMENSION";
        default:                 return "EMPTY";
    }
}

std::string fxShortName (FxType t)
{
    switch (t)
    {
        case FxType::Filter:     return "FILT";
        case FxType::Drive:      return "DRIV";
        case FxType::Compressor: return "COMP";
        case FxType::Delay:      return "DLY";
        case FxType::Reverb:     return "VERB";
        case FxType::Chorus:     return "CHOR";
        case FxType::Plugin:     return "PLUG";
        case FxType::Vast:       return "VAST";
        case FxType::Space:      return "SPCE";
        case FxType::TapeEcho:   return "TAPE";
        case FxType::Ensemble:   return "ENSM";
        case FxType::Rooms:      return "ROOM";
        case FxType::Spring:     return "SPRG";
        case FxType::DubDelay:   return "DUB";
        case FxType::BbdDelay:   return "BBD";
        case FxType::Grain:      return "GRN";
        case FxType::MultiTap:   return "TAPS";
        case FxType::Flanger:    return "FLNG";
        case FxType::Phaser:     return "PHAS";
        case FxType::Rotary:     return "ROTR";
        case FxType::Tremolo:    return "TREM";
        case FxType::FreqShift:  return "SHFT";
        case FxType::AutoFilter: return "AFLT";
        case FxType::Dimension:  return "DIM";
        default:                 return "---";
    }
}

const std::vector<FxParamInfo>& fxParams (FxType t)
{
    static const std::vector<FxParamInfo> none;
    static const std::vector<FxParamInfo> filter { { "MODE", 0.0f }, { "CUTOFF", 0.7f }, { "RESO", 0.2f }, { "MIX", 1.0f } };
    static const std::vector<FxParamInfo> drive { { "DRIVE", 0.3f }, { "BITS", 1.0f }, { "RATE", 0.0f }, { "TONE", 1.0f }, { "MIX", 1.0f } };
    static const std::vector<FxParamInfo> comp { { "THRESH", 0.6f }, { "RATIO", 0.3f }, { "ATTACK", 0.3f }, { "RELEASE", 0.4f }, { "MAKEUP", 0.2f }, { "MIX", 1.0f } };
    static const std::vector<FxParamInfo> delay { { "TIME", 0.5f }, { "SYNC", 1.0f, true }, { "FEEDBACK", 0.35f }, { "TONE", 0.7f }, { "PING", 0.0f, true }, { "MIX", 0.3f } };
    static const std::vector<FxParamInfo> verb { { "SIZE", 0.6f }, { "DAMP", 0.5f }, { "WIDTH", 1.0f }, { "PREDELAY", 0.1f }, { "MIX", 0.25f } };
    static const std::vector<FxParamInfo> chorus { { "RATE", 0.3f }, { "DEPTH", 0.4f }, { "DELAY", 0.4f }, { "FEEDBACK", 0.0f }, { "MIX", 0.5f } };
    static const std::vector<FxParamInfo> vast { { "MIX", 0.35f }, { "DELAY", 0.5f }, { "SYNC", 1.0f, true }, { "FEEDBACK", 0.6f },
                                                 { "WARP", 0.35f }, { "DENSITY", 0.6f }, { "MOD RATE", 0.3f }, { "MOD DEPTH", 0.3f },
                                                 { "LOW CUT", 0.15f }, { "HIGH CUT", 0.75f }, { "MODE", 0.0f }, { "WIDTH", 1.0f } };
    static const std::vector<FxParamInfo> space { { "MIX", 0.3f }, { "SIZE", 0.6f }, { "DECAY", 0.45f }, { "PRE-DELAY", 0.1f },
                                                  { "PRE ECHO", 0.0f }, { "DAMP", 0.4f }, { "LOW CUT", 0.15f }, { "MOD", 0.3f },
                                                  { "CHARACTER", 0.0f }, { "SHIMMER", 0.0f }, { "FREEZE", 0.0f, true }, { "WIDTH", 1.0f } };
    static const std::vector<FxParamInfo> tape { { "TIME", 0.5f }, { "SYNC", 1.0f, true }, { "FEEDBACK", 0.4f }, { "HEADS", 0.0f },
                                                 { "WOW", 0.25f }, { "FLUTTER", 0.25f }, { "SATURATE", 0.3f }, { "TONE", 0.55f },
                                                 { "WEAR", 0.15f }, { "SPREAD", 0.5f }, { "MIX", 0.35f } };
    static const std::vector<FxParamInfo> ensemble { { "MODE", 0.0f }, { "RATE", 0.5f }, { "DEPTH", 0.5f }, { "WIDTH", 1.0f },
                                                     { "TONE", 0.6f }, { "NOISE", 0.15f }, { "MIX", 0.5f } };
    static const std::vector<FxParamInfo> rooms { { "MIX", 0.25f }, { "TYPE", 0.67f }, { "SIZE", 0.5f }, { "DECAY", 0.4f }, { "PRE-DELAY", 0.08f },
                                                  { "EARLY", 0.5f }, { "DAMP", 0.4f }, { "LOW CUT", 0.15f }, { "HIGH CUT", 0.85f }, { "MOD", 0.2f }, { "WIDTH", 1.0f } };
    static const std::vector<FxParamInfo> spring { { "MIX", 0.3f }, { "DECAY", 0.5f }, { "TONE", 0.5f }, { "SPRINGS", 0.5f },
                                                   { "DRIP", 0.5f }, { "DRIVE", 0.2f }, { "LOW CUT", 0.35f }, { "WIDTH", 0.8f } };
    static const std::vector<FxParamInfo> dub { { "TIME", 0.625f }, { "SYNC", 1.0f, true }, { "FEEDBACK", 0.6f }, { "FILTER", 0.5f }, { "RESO", 0.3f },
                                                { "DRIVE", 0.3f }, { "HOLD", 0.0f, true }, { "PING", 1.0f, true }, { "WOBBLE", 0.2f }, { "MIX", 0.35f } };
    static const std::vector<FxParamInfo> bbd { { "TIME", 0.75f }, { "SYNC", 0.0f, true }, { "FEEDBACK", 0.45f }, { "MOD RATE", 0.3f }, { "MOD DEPTH", 0.25f },
                                                { "AGE", 0.3f }, { "DRIVE", 0.3f }, { "WIDTH", 0.5f }, { "MIX", 0.35f } };
    static const std::vector<FxParamInfo> grain { { "MODE", 0.0f }, { "SIZE", 0.5f }, { "SYNC", 1.0f, true }, { "FEEDBACK", 0.3f }, { "PITCH", 0.5f },
                                                  { "SPRAY", 0.2f }, { "DENSITY", 0.5f }, { "WIDTH", 0.7f }, { "MIX", 0.4f } };
    static const std::vector<FxParamInfo> taps { { "TIME", 0.5f }, { "SYNC", 1.0f, true }, { "PATTERN", 0.0f }, { "TAPS", 3.0f / 7.0f }, { "DECAY", 0.4f },
                                                 { "FEEDBACK", 0.2f }, { "SPREAD", 0.7f }, { "TONE", 0.7f }, { "MIX", 0.35f } };
    static const std::vector<FxParamInfo> flanger { { "RATE", 0.3f }, { "DEPTH", 0.7f }, { "MANUAL", 0.4f }, { "FEEDBACK", 0.7f },
                                                    { "THRU-ZERO", 0.0f, true }, { "STEREO", 0.5f }, { "MIX", 0.5f } };
    static const std::vector<FxParamInfo> phaser { { "STAGES", 0.5f }, { "RATE", 0.3f }, { "DEPTH", 0.7f }, { "CENTRE", 0.5f },
                                                   { "FEEDBACK", 0.7f }, { "STEREO", 0.5f }, { "MIX", 0.5f } };
    static const std::vector<FxParamInfo> rotary { { "FAST", 0.0f, true }, { "ACCEL", 0.5f }, { "HORN", 0.5f }, { "DRIVE", 0.2f }, { "WIDTH", 0.8f }, { "MIX", 1.0f } };
    static const std::vector<FxParamInfo> tremolo { { "MODE", 0.0f }, { "RATE", 0.5f }, { "SYNC", 0.0f, true }, { "DEPTH", 0.6f }, { "SHAPE", 0.0f }, { "STEREO", 0.0f } };
    static const std::vector<FxParamInfo> shifter { { "MODE", 0.0f }, { "FREQ", 0.6f }, { "FINE", 0.5f }, { "FEEDBACK", 0.0f }, { "STEREO", 0.0f }, { "MIX", 0.5f } };
    static const std::vector<FxParamInfo> autofilter { { "MODE", 0.0f }, { "CUTOFF", 0.45f }, { "RESO", 0.4f }, { "LFO RATE", 0.5f }, { "SYNC", 1.0f, true },
                                                       { "LFO DEPTH", 0.4f }, { "ENV AMOUNT", 0.5f }, { "ENV SPEED", 0.4f }, { "DRIVE", 0.1f }, { "MIX", 1.0f } };
    static const std::vector<FxParamInfo> dimension { { "MODE", 0.0f }, { "WIDTH", 1.0f }, { "MIX", 1.0f } };
    switch (t)
    {
        case FxType::Vast:       return vast;
        case FxType::Space:      return space;
        case FxType::TapeEcho:   return tape;
        case FxType::Ensemble:   return ensemble;
        case FxType::Rooms:      return rooms;
        case FxType::Spring:     return spring;
        case FxType::DubDelay:   return dub;
        case FxType::BbdDelay:   return bbd;
        case FxType::Grain:      return grain;
        case FxType::MultiTap:   return taps;
        case FxType::Flanger:    return flanger;
        case FxType::Phaser:     return phaser;
        case FxType::Rotary:     return rotary;
        case FxType::Tremolo:    return tremolo;
        case FxType::FreqShift:  return shifter;
        case FxType::AutoFilter: return autofilter;
        case FxType::Dimension:  return dimension;
        case FxType::Filter:     return filter;
        case FxType::Drive:      return drive;
        case FxType::Compressor: return comp;
        case FxType::Delay:      return delay;
        case FxType::Reverb:     return verb;
        case FxType::Chorus:     return chorus;
        default:                 return none;
    }
}

void initFxSlot (FxSlot& s, FxType t)
{
    s = FxSlot {};
    s.type = t;
    const auto& ps = fxParams (t);
    for (int i = 0; i < (int) ps.size() && i < kFxParams; ++i) s.p[i] = ps[(size_t) i].def;
}

double delaySeconds (const float* p, double bpm) { return delaySeconds (p[0], p[1] > 0.5f, bpm); }

double delaySeconds (float time, bool sync, double bpm)
{
    if (sync)
        return kDivisions[divisionIndex (time)].beats * 60.0 / std::max (20.0, bpm);
    return std::pow (2000.0, (double) time) / 1000.0;   // 1 ms .. 2 s
}

std::string fxCategoryName (FxCategory c)
{
    switch (c)
    {
        case FxCategory::Reverb:         return "REVERB";
        case FxCategory::Delay:          return "DELAY";
        case FxCategory::Modulation:     return "MODULATION";
        case FxCategory::DriveCrush:     return "DRIVE & CRUSH";
        case FxCategory::FilterDynamics: return "FILTER & DYNAMICS";
        default:                         return "";
    }
}

std::vector<FxType> fxTypesIn (FxCategory c)
{
    switch (c)
    {
        case FxCategory::Reverb:         return { FxType::Vast, FxType::Space, FxType::Rooms, FxType::Spring, FxType::Reverb };
        case FxCategory::Delay:          return { FxType::TapeEcho, FxType::DubDelay, FxType::BbdDelay, FxType::MultiTap, FxType::Grain, FxType::Delay };
        case FxCategory::Modulation:     return { FxType::Ensemble, FxType::Dimension, FxType::Flanger, FxType::Phaser, FxType::Rotary,
                                                  FxType::Tremolo, FxType::FreqShift, FxType::Chorus };
        case FxCategory::DriveCrush:     return { FxType::Drive };
        case FxCategory::FilterDynamics: return { FxType::Filter, FxType::AutoFilter, FxType::Compressor };
        default:                         return {};
    }
}

FxCategory fxCategory (FxType t)
{
    for (int c = 0; c < (int) FxCategory::Count; ++c)
        for (auto x : fxTypesIn ((FxCategory) c)) if (x == t) return (FxCategory) c;
    return FxCategory::Count;
}

namespace
{
    // modes and choices: v (0..1) -> one of n
    int choiceOf (float v, int n) { return std::clamp ((int) std::lround (v * (n - 1)), 0, n - 1); }
    const char* const kVastModes[] = { "NEBULA", "ABYSS", "BLOOM", "DRIFT" };
    const char* const kSpaceChars[] = { "GROUNDED", "AIRY", "COSMIC" };
    const char* const kTapeHeads[] = { "1", "2", "3", "1+2", "1+3", "2+3", "1+2+3" };
    const char* const kEnsembleModes[] = { "I", "II", "I+II", "STRINGS" };
    const char* const kRoomTypes[] = { "ROOM", "CHAMBER", "HALL", "PLATE" };
    const char* const kGrainModes[] = { "REVERSE", "GRAINS" };
    const char* const kTapPatterns[] = { "STRAIGHT", "DOTTED", "TRIPLET", "SWING", "SPEED UP", "SLOW DOWN", "SCATTER" };
    const char* const kTremModes[] = { "TREMOLO", "AUTO-PAN", "HARMONIC" };
    const char* const kShiftModes[] = { "SHIFT", "RING MOD" };
    const char* const kFilterModes[] = { "LOW-PASS", "BAND-PASS", "HIGH-PASS" };
    std::string bipolarPct (float v, double scale) { const double x = (2.0 * v - 1.0) * scale * 100.0; return fmt (x > 0.5 ? "+%.0f%%" : "%.0f%%", x); }
    std::string semis (double s) { return std::abs (s) < 0.05 ? std::string ("0 ST") : fmt (s > 0 ? "+%.1f ST" : "%.1f ST", s); }
    std::string lowCut (float v) { return v < 0.005f ? std::string ("OFF") : hz (20.0 * std::pow (100.0, (double) v)); }
    std::string highCut (float v) { return v > 0.995f ? std::string ("OPEN") : hz (500.0 * std::pow (40.0, (double) v)); }
    std::string secs (double s) { return s >= 10 ? fmt ("%.0f S", s) : fmt ("%.1f S", s); }
}

int fxChoice (float v, int n) { return choiceOf (v, n); }
double vastModRateHz (float v) { return 0.02 * std::pow (400.0, (double) v); }
double spaceDecaySeconds (float v) { return 0.3 * std::pow (40.0 / 0.3, (double) v); }
double tapeToneHz (float v) { return 1200.0 * std::pow (16000.0 / 1200.0, (double) v); }
double ensembleRateScale (float v) { return std::pow (4.0, 2.0 * (double) v - 1.0); }
double lfoHz (float v) { return 0.02 * std::pow (400.0, (double) v); }
double tremoloHz (float v) { return 0.1 * std::pow (200.0, (double) v); }
double syncedHz (float v, double bpm) { return 1.0 / delaySeconds (v, true, bpm); }
double grainPitch (float v) { return std::round (24.0 * (double) v - 12.0); }
double freqShiftHz (float v) { const double x = 2.0 * v - 1.0; return 1000.0 * x * x * x; }
double ringHz (float v) { return std::pow (2000.0, (double) v); }
std::string divisionName (float v) { return kDivisions[divisionIndex (v)].name; }

const std::vector<FxPreset>& fxFactoryPresets (FxType t)
{
    using P = std::vector<FxPreset>;
    //                                         MIX   DELAY SYNC  FB    WARP  DENS  RATE  DEPTH LOCUT HICUT MODE  WIDTH
    static const P vast { { "GALAXY CLOUD",    FxType::Vast, { 0.40f, 0.62f, 1, 0.82f, 0.70f, 0.85f, 0.25f, 0.35f, 0.15f, 0.70f, 0.00f, 1.0f } },
                          { "DEEP ABYSS",      FxType::Vast, { 0.45f, 0.75f, 1, 0.92f, 0.80f, 1.00f, 0.15f, 0.40f, 0.25f, 0.55f, 0.34f, 1.0f } },
                          { "SLOW BLOOM",      FxType::Vast, { 0.40f, 0.50f, 1, 0.80f, 0.90f, 0.70f, 0.20f, 0.25f, 0.20f, 0.80f, 0.67f, 1.0f } },
                          { "DRIFTING ECHOES", FxType::Vast, { 0.35f, 0.62f, 1, 0.65f, 0.25f, 0.30f, 0.35f, 0.55f, 0.20f, 0.65f, 1.00f, 1.0f } },
                          { "CLEAN REPEATS",   FxType::Vast, { 0.30f, 0.50f, 1, 0.45f, 0.00f, 0.00f, 0.30f, 0.00f, 0.10f, 0.85f, 0.00f, 0.8f } },
                          { "SMEARED TAPS",    FxType::Vast, { 0.35f, 0.37f, 1, 0.60f, 0.45f, 0.40f, 0.40f, 0.30f, 0.20f, 0.70f, 0.00f, 1.0f } },
                          { "INFINITE PAD",    FxType::Vast, { 0.60f, 0.75f, 1, 1.00f, 1.00f, 1.00f, 0.15f, 0.45f, 0.30f, 0.60f, 0.34f, 1.0f } },
                          { "SHORT SPACE",     FxType::Vast, { 0.30f, 0.45f, 0, 0.55f, 0.85f, 0.90f, 0.30f, 0.20f, 0.15f, 0.75f, 0.00f, 1.0f } } };
    //                                         MIX   SIZE  DECAY PRE   PREFB DAMP  LOCUT MOD   CHAR  SHIM  FRZ   WIDTH
    static const P space { { "SMALL ROOM",     FxType::Space, { 0.25f, 0.20f, 0.15f, 0.02f, 0.0f, 0.55f, 0.15f, 0.15f, 0.0f, 0.0f, 0, 0.8f } },
                           { "VOCAL PLATE",    FxType::Space, { 0.25f, 0.45f, 0.35f, 0.08f, 0.0f, 0.35f, 0.25f, 0.30f, 0.5f, 0.0f, 0, 1.0f } },
                           { "BIG HALL",       FxType::Space, { 0.30f, 0.80f, 0.55f, 0.06f, 0.0f, 0.45f, 0.15f, 0.30f, 0.0f, 0.0f, 0, 1.0f } },
                           { "AIRY CATHEDRAL", FxType::Space, { 0.35f, 1.00f, 0.70f, 0.10f, 0.0f, 0.30f, 0.20f, 0.35f, 0.5f, 0.0f, 0, 1.0f } },
                           { "COSMIC SHIMMER", FxType::Space, { 0.45f, 0.90f, 0.75f, 0.10f, 0.0f, 0.30f, 0.30f, 0.50f, 1.0f, 0.6f, 0, 1.0f } },
                           { "ECHO CHAMBER",   FxType::Space, { 0.35f, 0.60f, 0.45f, 0.40f, 0.6f, 0.45f, 0.20f, 0.25f, 0.0f, 0.0f, 0, 1.0f } },
                           { "DARK SPACE",     FxType::Space, { 0.40f, 0.85f, 0.65f, 0.05f, 0.0f, 0.80f, 0.10f, 0.40f, 1.0f, 0.0f, 0, 1.0f } },
                           { "FROZEN",         FxType::Space, { 0.50f, 0.90f, 0.80f, 0.05f, 0.0f, 0.35f, 0.25f, 0.40f, 1.0f, 0.2f, 1, 1.0f } } };
    //                                         TIME  SYNC  FB    HEADS WOW   FLUT  SAT   TONE  WEAR  SPRD  MIX
    static const P tape { { "SLAPBACK",        FxType::TapeEcho, { 0.60f, 0, 0.10f, 0.00f, 0.15f, 0.20f, 0.30f, 0.60f, 0.10f, 0.20f, 0.30f } },
                          { "SPACE ECHO",      FxType::TapeEcho, { 0.50f, 1, 0.50f, 1.00f, 0.25f, 0.25f, 0.40f, 0.50f, 0.20f, 0.60f, 0.35f } },
                          { "DUB SIREN",       FxType::TapeEcho, { 0.62f, 1, 0.85f, 0.50f, 0.30f, 0.20f, 0.55f, 0.40f, 0.25f, 0.70f, 0.40f } },
                          { "RUNAWAY",         FxType::TapeEcho, { 0.50f, 1, 0.95f, 0.67f, 0.35f, 0.30f, 0.60f, 0.45f, 0.30f, 0.50f, 0.40f } },
                          { "WORN CASSETTE",   FxType::TapeEcho, { 0.50f, 1, 0.35f, 0.00f, 0.70f, 0.55f, 0.50f, 0.30f, 0.80f, 0.30f, 0.35f } },
                          { "CLEAN TAPE",      FxType::TapeEcho, { 0.50f, 1, 0.35f, 0.00f, 0.05f, 0.05f, 0.10f, 0.85f, 0.00f, 0.40f, 0.30f } },
                          { "MULTI-HEAD",      FxType::TapeEcho, { 0.62f, 1, 0.45f, 1.00f, 0.20f, 0.20f, 0.35f, 0.55f, 0.15f, 1.00f, 0.35f } } };
    //                                         MODE  RATE  DEPTH WIDTH TONE  NOISE MIX
    static const P ens { { "CLASSIC I",        FxType::Ensemble, { 0.00f, 0.5f, 0.5f, 1.0f, 0.6f, 0.15f, 0.5f } },
                         { "CLASSIC II",       FxType::Ensemble, { 0.34f, 0.5f, 0.5f, 1.0f, 0.6f, 0.15f, 0.5f } },
                         { "FAST SHIMMER",     FxType::Ensemble, { 0.67f, 0.5f, 0.5f, 1.0f, 0.6f, 0.10f, 0.5f } },
                         { "STRING MACHINE",   FxType::Ensemble, { 1.00f, 0.5f, 0.6f, 1.0f, 0.5f, 0.10f, 0.6f } },
                         { "SUBTLE WIDENER",   FxType::Ensemble, { 0.00f, 0.4f, 0.3f, 1.0f, 0.8f, 0.00f, 0.3f } },
                         { "SEASICK",          FxType::Ensemble, { 0.34f, 0.7f, 1.0f, 1.0f, 0.5f, 0.20f, 0.6f } } };
    //                                         TIME  SYNC  FB    TONE  PING  MIX
    static const P delay { { "QUARTER NOTE",   FxType::Delay, { 0.75f, 1, 0.35f, 0.7f, 0, 0.3f } },
                           { "DOTTED EIGHTH",  FxType::Delay, { 0.62f, 1, 0.45f, 0.6f, 0, 0.3f } },
                           { "PING-PONG 1/8",  FxType::Delay, { 0.50f, 1, 0.50f, 0.7f, 1, 0.35f } },
                           { "SLAP",           FxType::Delay, { 0.33f, 0, 0.05f, 0.8f, 0, 0.3f } } };
    //                                         SIZE  DAMP  WIDTH PRE   MIX
    static const P verb { { "ROOM",            FxType::Reverb, { 0.30f, 0.6f, 0.8f, 0.05f, 0.2f } },
                          { "HALL",            FxType::Reverb, { 0.80f, 0.4f, 1.0f, 0.15f, 0.3f } } };
    //                                         RATE  DEPTH DELAY FB    MIX
    static const P chorus { { "LUSH",          FxType::Chorus, { 0.30f, 0.6f, 0.5f, 0.2f, 0.5f } },
                            { "FLANGE",        FxType::Chorus, { 0.20f, 0.7f, 0.0f, 0.7f, 0.5f } } };
    //                                         MODE  CUTOFF RESO MIX
    static const P filter { { "TELEPHONE",     FxType::Filter, { 0.5f, 0.55f, 0.3f, 1.0f } },
                            { "WARM LOW-PASS", FxType::Filter, { 0.0f, 0.60f, 0.15f, 1.0f } },
                            { "THIN HIGH-PASS",FxType::Filter, { 1.0f, 0.45f, 0.1f, 1.0f } } };
    //                                         DRIVE BITS  RATE  TONE  MIX
    static const P drive { { "WARM",           FxType::Drive, { 0.25f, 1.0f, 0.0f, 0.8f, 1.0f } },
                           { "AMIGA 8-BIT",    FxType::Drive, { 0.10f, 0.43f, 0.1f, 1.0f, 1.0f } },
                           { "DESTROYED",      FxType::Drive, { 0.80f, 0.15f, 0.4f, 0.6f, 1.0f } } };
    //                                         THRESH RATIO ATT  REL   MAKEUP MIX
    static const P comp { { "GLUE",            FxType::Compressor, { 0.70f, 0.10f, 0.5f, 0.4f, 0.10f, 1.0f } },
                          { "SMASH",           FxType::Compressor, { 0.35f, 0.80f, 0.1f, 0.3f, 0.45f, 1.0f } },
                          { "PARALLEL",        FxType::Compressor, { 0.40f, 0.60f, 0.2f, 0.3f, 0.40f, 0.5f } } };
    //                                         MIX   TYPE  SIZE  DECAY PRE   EARLY DAMP  LOCUT HICUT MOD   WIDTH
    static const P rooms { { "DRUM ROOM",      FxType::Rooms, { 0.25f, 0.00f, 0.35f, 0.15f, 0.02f, 0.70f, 0.45f, 0.20f, 0.85f, 0.10f, 0.9f } },
                           { "LIVE CHAMBER",   FxType::Rooms, { 0.25f, 0.33f, 0.50f, 0.30f, 0.06f, 0.55f, 0.40f, 0.15f, 0.80f, 0.20f, 1.0f } },
                           { "CONCERT HALL",   FxType::Rooms, { 0.30f, 0.67f, 0.75f, 0.50f, 0.10f, 0.40f, 0.40f, 0.10f, 0.80f, 0.25f, 1.0f } },
                           { "BRIGHT PLATE",   FxType::Rooms, { 0.25f, 1.00f, 0.50f, 0.40f, 0.04f, 0.20f, 0.20f, 0.30f, 1.00f, 0.20f, 1.0f } },
                           { "DARK PLATE",     FxType::Rooms, { 0.25f, 1.00f, 0.55f, 0.45f, 0.04f, 0.20f, 0.70f, 0.25f, 0.60f, 0.20f, 1.0f } },
                           { "SMALL BOOTH",    FxType::Rooms, { 0.20f, 0.00f, 0.10f, 0.05f, 0.00f, 0.80f, 0.60f, 0.25f, 0.75f, 0.00f, 0.6f } } };
    //                                         MIX   DECAY TONE  SPR   DRIP  DRIVE LOCUT WIDTH
    static const P spring { { "SURF TANK",     FxType::Spring, { 0.40f, 0.60f, 0.55f, 0.50f, 0.80f, 0.30f, 0.35f, 0.8f } },
                            { "DUB SPRING",    FxType::Spring, { 0.45f, 0.80f, 0.35f, 1.00f, 0.60f, 0.40f, 0.40f, 1.0f } },
                            { "SUBTLE",        FxType::Spring, { 0.20f, 0.35f, 0.60f, 0.50f, 0.30f, 0.10f, 0.35f, 0.7f } },
                            { "SINGLE SPRING", FxType::Spring, { 0.35f, 0.50f, 0.50f, 0.00f, 0.70f, 0.20f, 0.30f, 0.3f } } };
    //                                         TIME  SYNC  FB    FILT  RESO  DRIVE HOLD  PING  WOB   MIX
    static const P dub { { "KING TUBBY",       FxType::DubDelay, { 0.625f, 1, 0.70f, 0.45f, 0.40f, 0.40f, 0, 1, 0.25f, 0.40f } },
                         { "STEPPERS",         FxType::DubDelay, { 0.50f,  1, 0.55f, 0.55f, 0.25f, 0.25f, 0, 1, 0.15f, 0.35f } },
                         { "SIREN SPIRAL",     FxType::DubDelay, { 0.75f,  1, 1.00f, 0.60f, 0.60f, 0.60f, 0, 1, 0.35f, 0.45f } },
                         { "TELEPHONE DUB",    FxType::DubDelay, { 0.625f, 1, 0.65f, 0.70f, 0.80f, 0.35f, 0, 0, 0.20f, 0.40f } } };
    //                                         TIME  SYNC  FB    RATE  DEPTH AGE   DRIVE WIDTH MIX
    static const P bbd { { "ANALOG SLAP",      FxType::BbdDelay, { 0.60f, 0, 0.15f, 0.30f, 0.15f, 0.20f, 0.20f, 0.3f, 0.35f } },
                         { "MEMORY MAN",       FxType::BbdDelay, { 0.75f, 0, 0.55f, 0.35f, 0.35f, 0.35f, 0.35f, 0.5f, 0.40f } },
                         { "WARBLED",          FxType::BbdDelay, { 0.70f, 0, 0.45f, 0.55f, 0.80f, 0.60f, 0.30f, 0.7f, 0.40f } },
                         { "SYNCED EIGHTHS",   FxType::BbdDelay, { 0.50f, 1, 0.45f, 0.30f, 0.20f, 0.30f, 0.30f, 0.5f, 0.35f } } };
    //                                         MODE  SIZE  SYNC  FB    PITCH SPRAY DENS  WIDTH MIX
    static const P grain { { "REVERSE QUARTER",FxType::Grain, { 0.0f, 0.75f, 1, 0.30f, 0.50f, 0.00f, 0.50f, 0.6f, 0.45f } },
                           { "REVERSE OCTAVE", FxType::Grain, { 0.0f, 0.75f, 1, 0.40f, 1.00f, 0.00f, 0.50f, 0.8f, 0.45f } },
                           { "GRAIN CLOUD",    FxType::Grain, { 1.0f, 0.50f, 1, 0.50f, 0.50f, 0.60f, 0.80f, 1.0f, 0.50f } },
                           { "SHIMMER GRAINS", FxType::Grain, { 1.0f, 0.62f, 1, 0.60f, 1.00f, 0.40f, 0.70f, 1.0f, 0.45f } },
                           { "SUB GRAINS",     FxType::Grain, { 1.0f, 0.50f, 1, 0.30f, 0.00f, 0.30f, 0.50f, 0.6f, 0.40f } } };
    //                                         TIME  SYNC  PATT  TAPS  DECAY FB    SPRD  TONE  MIX
    static const P taps { { "PING-PONG 1/8",   FxType::MultiTap, { 0.50f, 1, 0.00f, 3.0f / 7, 0.25f, 0.30f, 1.0f, 0.70f, 0.35f } },
                          { "DOTTED BOUNCE",   FxType::MultiTap, { 0.25f, 1, 0.17f, 5.0f / 7, 0.30f, 0.20f, 0.8f, 0.65f, 0.35f } },
                          { "TRIPLET CASCADE", FxType::MultiTap, { 0.50f, 1, 0.33f, 5.0f / 7, 0.25f, 0.10f, 0.7f, 0.70f, 0.35f } },
                          { "BOUNCING BALL",   FxType::MultiTap, { 0.75f, 1, 0.67f, 1.0f,     0.20f, 0.00f, 0.5f, 0.70f, 0.40f } },
                          { "SCATTERED",       FxType::MultiTap, { 0.50f, 1, 1.00f, 1.0f,     0.30f, 0.20f, 1.0f, 0.60f, 0.35f } } };
    //                                         RATE  DEPTH MAN   FB    TZ    STEREO MIX
    static const P flanger { { "JET",          FxType::Flanger, { 0.20f, 0.80f, 0.45f, 0.85f, 0, 0.25f, 0.5f } },
                             { "THROUGH ZERO", FxType::Flanger, { 0.15f, 0.90f, 0.50f, 0.60f, 1, 0.00f, 0.5f } },
                             { "METALLIC",     FxType::Flanger, { 0.40f, 0.40f, 0.20f, 0.95f, 0, 0.50f, 0.5f } },
                             { "SUBTLE SWEEP", FxType::Flanger, { 0.15f, 0.50f, 0.50f, 0.55f, 0, 1.00f, 0.35f } } };
    //                                         STAGE RATE  DEPTH CENT  FB    STEREO MIX
    static const P phaser { { "CLASSIC 4",     FxType::Phaser, { 0.0f, 0.30f, 0.70f, 0.45f, 0.70f, 0.30f, 0.5f } },
                            { "DEEP 12",       FxType::Phaser, { 1.0f, 0.20f, 0.80f, 0.50f, 0.85f, 0.50f, 0.5f } },
                            { "SLOW SWIRL",    FxType::Phaser, { 0.5f, 0.10f, 0.90f, 0.50f, 0.60f, 1.00f, 0.5f } },
                            { "FAST WOBBLE",   FxType::Phaser, { 0.5f, 0.60f, 0.50f, 0.60f, 0.30f, 0.25f, 0.5f } } };
    //                                         FAST  ACCEL HORN  DRIVE WIDTH MIX
    static const P rotary { { "CHORALE (SLOW)",FxType::Rotary, { 0, 0.5f, 0.5f, 0.15f, 0.8f, 1.0f } },
                            { "TREMOLO (FAST)",FxType::Rotary, { 1, 0.5f, 0.5f, 0.15f, 0.8f, 1.0f } },
                            { "DRIVEN CABINET",FxType::Rotary, { 0, 0.4f, 0.6f, 0.60f, 1.0f, 1.0f } } };
    //                                         MODE  RATE  SYNC  DEPTH SHAPE STEREO
    static const P trem { { "VINTAGE AMP",     FxType::Tremolo, { 0.0f, 0.50f, 0, 0.60f, 0.0f, 0.0f } },
                          { "SQUARE CHOP 1/16",FxType::Tremolo, { 0.0f, 0.125f, 1, 1.00f, 1.0f, 0.0f } },
                          { "AUTO-PAN 1/4",    FxType::Tremolo, { 0.5f, 0.75f, 1, 0.80f, 0.0f, 0.0f } },
                          { "HARMONIC",        FxType::Tremolo, { 1.0f, 0.45f, 0, 0.70f, 0.0f, 0.0f } },
                          { "STEREO SHIMMER",  FxType::Tremolo, { 0.0f, 0.62f, 0, 0.50f, 0.0f, 1.0f } } };
    //                                         MODE  FREQ  FINE  FB    STEREO MIX
    static const P shift { { "BARBERPOLE UP",  FxType::FreqShift, { 0.0f, 0.55f, 0.5f, 0.70f, 0.0f, 0.5f } },
                           { "STEREO DETUNE",  FxType::FreqShift, { 0.0f, 0.50f, 0.8f, 0.00f, 1.0f, 0.4f } },
                           { "ROBOT RING",     FxType::FreqShift, { 1.0f, 0.68f, 0.5f, 0.00f, 0.0f, 0.6f } },
                           { "METAL RING",     FxType::FreqShift, { 1.0f, 0.85f, 0.5f, 0.20f, 0.5f, 0.5f } } };
    //                                         MODE  CUT   RESO  RATE  SYNC  LFO   ENV   SPEED DRIVE MIX
    static const P afilt { { "ENVELOPE FUNK",  FxType::AutoFilter, { 0.0f, 0.30f, 0.60f, 0.50f, 1, 0.00f, 0.85f, 0.25f, 0.20f, 1.0f } },
                           { "QUARTER SWEEP",  FxType::AutoFilter, { 0.0f, 0.45f, 0.50f, 0.75f, 1, 0.60f, 0.50f, 0.40f, 0.10f, 1.0f } },
                           { "WAH BAND",       FxType::AutoFilter, { 0.5f, 0.45f, 0.70f, 0.40f, 0, 0.50f, 0.65f, 0.30f, 0.10f, 1.0f } },
                           { "SIXTEENTH CHOP", FxType::AutoFilter, { 0.0f, 0.35f, 0.40f, 0.125f, 1, 0.70f, 0.50f, 0.40f, 0.20f, 1.0f } },
                           { "DUCKING HIGH-PASS", FxType::AutoFilter, { 1.0f, 0.35f, 0.30f, 0.50f, 1, 0.00f, 0.15f, 0.50f, 0.00f, 1.0f } } };
    //                                         MODE  WIDTH MIX
    static const P dim { { "MODE 1",           FxType::Dimension, { 0.00f, 1.0f, 1.0f } },
                         { "MODE 2",           FxType::Dimension, { 0.34f, 1.0f, 1.0f } },
                         { "MODE 3",           FxType::Dimension, { 0.67f, 1.0f, 1.0f } },
                         { "MODE 4",           FxType::Dimension, { 1.00f, 1.0f, 1.0f } } };
    static const P none;
    switch (t)
    {
        case FxType::Rooms:      return rooms;
        case FxType::Spring:     return spring;
        case FxType::DubDelay:   return dub;
        case FxType::BbdDelay:   return bbd;
        case FxType::Grain:      return grain;
        case FxType::MultiTap:   return taps;
        case FxType::Flanger:    return flanger;
        case FxType::Phaser:     return phaser;
        case FxType::Rotary:     return rotary;
        case FxType::Tremolo:    return trem;
        case FxType::FreqShift:  return shift;
        case FxType::AutoFilter: return afilt;
        case FxType::Dimension:  return dim;
        case FxType::Vast:       return vast;
        case FxType::Space:      return space;
        case FxType::TapeEcho:   return tape;
        case FxType::Ensemble:   return ens;
        case FxType::Delay:      return delay;
        case FxType::Reverb:     return verb;
        case FxType::Chorus:     return chorus;
        case FxType::Filter:     return filter;
        case FxType::Drive:      return drive;
        case FxType::Compressor: return comp;
        default:                 return none;
    }
}

void applyFxPreset (FxSlot& s, const FxPreset& pr)
{
    const bool bypass = s.bypass;
    initFxSlot (s, pr.type);
    s.bypass = bypass;
    for (size_t i = 0; i < pr.p.size() && i < (size_t) kFxParams; ++i) s.p[i] = std::clamp (pr.p[i], 0.0f, 1.0f);
}

std::string fxParamText (FxType t, int i, const float* params)
{
    const auto& ps = fxParams (t);
    if (i < 0 || i >= (int) ps.size()) return {};
    const float v = params[i];
    if (ps[(size_t) i].toggle) return v > 0.5f ? "ON" : "OFF";
    switch (t)
    {
        case FxType::Filter:
            if (i == 0) return v < 0.34f ? "LOW-PASS" : v < 0.67f ? "BAND-PASS" : "HIGH-PASS";
            if (i == 1) return hz (20.0 * std::pow (1000.0, (double) v));
            return pct (v);
        case FxType::Drive:
            if (i == 0) return fmt ("+%.0f DB", 20.0 * std::log10 (1.0 + 39.0 * v * v));
            if (i == 1) return fmt ("%.0f BIT", 2.0 + std::round (14.0 * v));
            if (i == 2) { const int d = 1 + (int) std::lround (31.0 * v); return d == 1 ? std::string ("FULL") : fmt ("1/%.0f", (double) d); }
            if (i == 3) return v >= 0.999f ? std::string ("OPEN") : hz (1000.0 * std::pow (20.0, (double) v));
            return pct (v);
        case FxType::Compressor:
            if (i == 0) return fmt ("%.0f DB", -48.0 + 48.0 * v);
            if (i == 1) return fmt ("%.1f:1", 1.0 + 19.0 * v);
            if (i == 2) return ms (0.1 * std::pow (1000.0, (double) v));
            if (i == 3) return ms (10.0 * std::pow (100.0, (double) v));
            if (i == 4) return fmt ("+%.1f DB", 24.0 * v);
            return pct (v);
        case FxType::Delay:
            if (i == 0) return params[1] > 0.5f ? std::string (kDivisions[divisionIndex (v)].name) : ms (std::pow (2000.0, (double) v));
            if (i == 2) return pct (0.95 * v);
            if (i == 3) return hz (500.0 * std::pow (40.0, (double) v));
            return pct (v);
        case FxType::Reverb:
            if (i == 3) return ms (200.0 * v);
            return pct (v);
        case FxType::Chorus:
            if (i == 0) return fmt ("%.2f HZ", 0.05 * std::pow (100.0, (double) v));
            if (i == 2) return ms (5.0 + 20.0 * v);
            if (i == 3) return pct (0.8 * v);
            return pct (v);
        case FxType::Vast:
            if (i == 1) return params[2] > 0.5f ? std::string (kDivisions[divisionIndex (v)].name) : ms (std::pow (2000.0, (double) v));
            if (i == 6) return fmt ("%.2f HZ", vastModRateHz (v));
            if (i == 8) return lowCut (v);
            if (i == 9) return highCut (v);
            if (i == 10) return kVastModes[choiceOf (v, 4)];
            return pct (v);
        case FxType::Space:
            if (i == 1) return pct (v);
            if (i == 2) return params[10] > 0.5f ? std::string ("FROZEN") : secs (spaceDecaySeconds (v));
            if (i == 3) return ms (500.0 * v);
            if (i == 4) return pct (0.9 * v);
            if (i == 6) return lowCut (v);
            if (i == 8) return kSpaceChars[choiceOf (v, 3)];
            return pct (v);
        case FxType::TapeEcho:
            if (i == 0) return params[1] > 0.5f ? std::string (kDivisions[divisionIndex (v)].name) : ms (std::pow (2000.0, (double) v));
            if (i == 2) return pct (1.15 * v);
            if (i == 3) return kTapeHeads[choiceOf (v, 7)];
            if (i == 7) return hz (tapeToneHz (v));
            return pct (v);
        case FxType::Rooms:
            if (i == 1) return kRoomTypes[choiceOf (v, 4)];
            if (i == 3) return secs (spaceDecaySeconds (v));
            if (i == 4) return ms (250.0 * v);
            if (i == 7) return lowCut (v);
            if (i == 8) return highCut (v);
            return pct (v);
        case FxType::Spring:
            if (i == 1) return secs (0.5 + 4.5 * v);
            if (i == 2) return hz (1500.0 * std::pow (6.0, (double) v));
            if (i == 3) return fmt ("%.0f", 1.0 + choiceOf (v, 3));
            if (i == 6) return lowCut (v);
            return pct (v);
        case FxType::DubDelay:
            if (i == 0) return params[1] > 0.5f ? divisionName (v) : ms (std::pow (2000.0, (double) v));
            if (i == 2) return pct (1.1 * v);
            if (i == 3) return hz (200.0 * std::pow (25.0, (double) v));
            return pct (v);
        case FxType::BbdDelay:
            if (i == 0) return params[1] > 0.5f ? divisionName (v) : ms (std::min (1000.0, std::pow (2000.0, (double) v)));
            if (i == 2) return pct (1.05 * v);
            if (i == 3) return fmt ("%.2f HZ", lfoHz (v));
            return pct (v);
        case FxType::Grain:
            if (i == 0) return kGrainModes[choiceOf (v, 2)];
            if (i == 1) return params[2] > 0.5f ? divisionName (v) : ms (std::pow (2000.0, (double) v));
            if (i == 4) return semis (grainPitch (v));
            return pct (v);
        case FxType::MultiTap:
            if (i == 0) return params[1] > 0.5f ? divisionName (v) : ms (std::pow (2000.0, (double) v));
            if (i == 2) return kTapPatterns[choiceOf (v, 7)];
            if (i == 3) return fmt ("%.0f", 1.0 + choiceOf (v, 8));
            if (i == 7) return hz (tapeToneHz (v));
            return pct (v);
        case FxType::Flanger:
            if (i == 0) return fmt ("%.2f HZ", lfoHz (v));
            if (i == 2) return ms (0.3 + 7.7 * v * v);
            if (i == 3) return bipolarPct (v, 0.95);
            if (i == 5) return fmt ("%.0f DEG", 180.0 * v);
            return pct (v);
        case FxType::Phaser:
            if (i == 0) return fmt ("%.0f", 4.0 + 4.0 * choiceOf (v, 3));
            if (i == 1) return fmt ("%.2f HZ", lfoHz (v));
            if (i == 3) return hz (200.0 * std::pow (10.0, (double) v));
            if (i == 4) return bipolarPct (v, 0.9);
            if (i == 5) return fmt ("%.0f DEG", 180.0 * v);
            return pct (v);
        case FxType::Rotary:
            if (i == 1) return fmt ("%.1f S", 0.3 + 3.7 * v);
            if (i == 2) { const int h = (int) std::lround (v * 100); return h == 50 ? std::string ("EVEN") : h > 50 ? fmt ("HORN %.0f", (double) h) : fmt ("DRUM %.0f", 100.0 - h); }
            return pct (v);
        case FxType::Tremolo:
            if (i == 0) return kTremModes[choiceOf (v, 3)];
            if (i == 1) return params[2] > 0.5f ? divisionName (v) : fmt ("%.2f HZ", tremoloHz (v));
            if (i == 4) return v < 0.02f ? std::string ("SINE") : v > 0.98f ? std::string ("SQUARE") : pct (v);
            if (i == 5) return fmt ("%.0f DEG", 180.0 * v);
            return pct (v);
        case FxType::FreqShift:
            if (i == 0) return kShiftModes[choiceOf (v, 2)];
            if (i == 1) return choiceOf (params[0], 2) == 1 ? hz (ringHz (v)) : fmt (freqShiftHz (v) >= 0 ? "+%.1f HZ" : "%.1f HZ", freqShiftHz (v));
            if (i == 2) return fmt (v >= 0.5f ? "+%.2f HZ" : "%.2f HZ", 20.0 * v - 10.0);
            if (i == 3) return pct (0.9 * v);
            return pct (v);
        case FxType::AutoFilter:
            if (i == 0) return kFilterModes[choiceOf (v, 3)];
            if (i == 1) return hz (20.0 * std::pow (1000.0, (double) v));
            if (i == 3) return params[4] > 0.5f ? divisionName (v) : fmt ("%.2f HZ", lfoHz (v));
            if (i == 5) return fmt ("%.1f OCT", 4.0 * v);
            if (i == 6) return fmt (v >= 0.5f ? "+%.1f OCT" : "%.1f OCT", 8.0 * v - 4.0);
            if (i == 7) return ms (20.0 + 980.0 * v);   // (the release; the attack follows it)
            return pct (v);
        case FxType::Dimension:
            if (i == 0) return fmt ("%.0f", 1.0 + choiceOf (v, 4));
            return pct (v);
        case FxType::Ensemble:
            if (i == 0) return kEnsembleModes[choiceOf (v, 4)];
            if (i == 1) return fmt ("X%.2f", ensembleRateScale (v));
            if (i == 2) return pct (2.0 * v);
            if (i == 4) return hz (2000.0 * std::pow (8.0, (double) v));
            return pct (v);
        default: return {};
    }
}
} // namespace prv
