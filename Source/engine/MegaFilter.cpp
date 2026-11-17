// Paula's Revenge - the Mega Synth filters on an instrument (see MegaFilter.h)
#include "MegaFilter.h"
#include "mega/Filter.h"
#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    // Mega Synth's own names (its Params.h kFilterLabels and CharFilter.h's models), in capitals
    const char* const kClassic[kFilterClassicModels] = {
        "STANDARD LP", "SALLEN-KEY LP", "LADDER", "STEINER-PARKER", "STATE VARIABLE", "DISCRETE OTA", "POLIVOKS",
        "SWITCHED CAPACITOR", "ACID FILTER APPROX", "MS-20", "OBERHEIM", "SEM", "TB-303", "MINIMOOG - FAT CAT",
        "ARP ODYSSEY", "CS-15", "SH-2" };
    const char* const kClassicAbout[kFilterClassicModels] = {
        "A plain 12 dB low pass: clean", "Two 12 dB stages: a smooth 24 dB", "A 4-pole transistor ladder: deep, resonant",
        "High pass, band and low pass in a chain: the Steiner's bite", "A 2-pole state variable: smooth and even",
        "An OTA filter driven into its stage: fat and gritty", "The Russian Polivoks: a hard-clipping, screaming 12 dB",
        "A switched-capacitor chip with its notch: digital-analogue", "An acid squelch: a driven 24 dB, limited",
        "The Korg MS-20: high pass into low pass, aggressive", "Oberheim: high pass into low pass, rounded",
        "The Oberheim SEM 12 dB: band into low pass, open", "The TB-303's diode ladder: resonance fades as it closes",
        "The Minimoog's ladder: fat at the bottom", "ARP Odyssey: high pass into low pass", "Yamaha CS-15: three stages, bright",
        "Roland SH-2: two 12 dB stages, smooth" };
    const char* const kChar[kFilterCharModels] = {
        "FORMANT", "FORMANT 2", "ALL PASS", "POWER LP", "POWER BP", "WAVE SHAPER", "RESONATOR", "RESAMPLER", "LP + RESAMPLE",
        "SHIFTER", "ANALOGUE LP", "DJ FILTER", "COMB +/-", "VOWEL MORPH", "PHASER FILTER", "RING FILTER", "DECIMATOR LP", "DUAL PEAK" };
    const char* const kCharKnob[kFilterCharModels] = {   // (short: the CHARACTER slider's label)
        "VOWEL", "VOWEL", "SPREAD", "SATUR", "SATUR", "FOLD", "DECAY", "BITS", "RATE", "SHIFT", "DRIFT",
        "DJ", "COMB", "VOWEL", "STAGES", "RING", "CRUSH", "PEAK 2" };

    int tgType (FilterType t)
    {
        switch (t)
        {
            case FilterType::HighPass: return tg::FT_HP;
            case FilterType::BandPass: return tg::FT_BP;
            case FilterType::Notch:    return tg::FT_NOTCH;
            case FilterType::Peak:     return tg::FT_PEAK;
            case FilterType::AllPass:  return tg::FT_AP;
            default:                   return tg::FT_LP;
        }
    }
    // Paula's RESONANCE (0..1) as Mega Synth's resonance (a Q of 0.5 - flat - to 25)
    float qOf (float res) { const float r = std::clamp (res, 0.0f, 1.0f); return 0.5f + 24.5f * r * r; }
    float driveK (float db) { return std::pow (10.0f, std::clamp (db, 0.0f, 24.0f) / 20.0f); }

    void configureUnit (tg::FilterUnit& u, const Instrument& in, FilterType type, bool immediate)
    {
        const int m = std::clamp (in.filterModel, 0, kFilterModels - 1);
        u.setCharacter (in.filterChar);
        if (m > kFilterClassicModels) u.configure (0, tg::FT_LP, 1, immediate, m - kFilterClassicModels);
        else
        {
            const int model = m == 0 ? 4 : m - 1;   // (model 0 with NOTCH / PEAK / ALL PASS: the state variable's multimode)
            const int t = tgType (type);
            u.configure (model, t, tg::effectiveFilterSlope (t, std::clamp (in.filterSlope, 0, 3)), immediate, 0);
        }
        u.setDrive (driveK (in.filterDrive));
        u.setMix (in.filterMix);
        u.setAnalog (0.0f, 0.0f);
    }
}

const char* filterModelName (int m)
{
    if (m <= 0) return "PAULA SVF";
    if (m <= kFilterClassicModels) return kClassic[m - 1];
    return m - kFilterClassicModels <= kFilterCharModels ? kChar[m - kFilterClassicModels - 1] : "PAULA SVF";
}

const char* filterModelAbout (int m)
{
    if (m <= 0) return "Paula's own state-variable filter: LOW, HIGH and BAND, as it always was";
    if (m <= kFilterClassicModels) return kClassicAbout[m - 1];
    const int c = m - kFilterClassicModels;
    return c >= 1 && c <= kFilterCharModels ? tg::charModelInfo (c).about : "";
}

const char* filterCharName (int m) { return filterIsCharModel (m) ? kCharKnob[m - kFilterClassicModels - 1] : ""; }
bool filterIsCharModel (int m) { return m > kFilterClassicModels && m < kFilterModels; }
int  filterNativeSlope (int m) { return m >= 1 && m <= kFilterClassicModels ? tg::nativeFilterSlope (m - 1) : 1; }
bool filterTypeAllowsSlope (FilterType t, int slope) { return (tg::filterSlopeMask (tgType (t)) >> slope) & 1; }

const std::vector<FilterGroup>& filterGroups()
{
    // Mega Synth's own grouping of its one Filter list
    static const std::vector<FilterGroup> g = {
        { "PAULA (AS BEFORE)", { 0 } },
        { "CLEAN", { 1, 2, 5, 8 } },
        { "ANALOGUE", { 3, 4, 6, 7, 10, 11, 12, 14, 15, 16, 17 } },
        { "ACID", { 9, 13 } },
        { "DRIVE & CHARACTER", { 21, 22, 23, 28, 34 } },
        { "FORMANT & VOWEL", { 18, 19, 31 } },
        { "COMB, PHASER & PEAKS", { 20, 24, 30, 32, 33, 35, 27 } },
        { "DIGITAL / LO-FI", { 25, 26, 29 } } };
    return g;
}

MegaFilter::MegaFilter() : unit (std::make_unique<tg::FilterUnit>()) {}
MegaFilter::~MegaFilter() = default;

void MegaFilter::reset() { unit->reset(); key = -1; }

void MegaFilter::setup (const Instrument& in, FilterType type, double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    const int k = std::clamp (in.filterModel, 0, 63) | ((int) type << 6) | (std::clamp (in.filterSlope, 0, 3) << 9);
    if (k != key) { configureUnit (*unit, in, type, key < 0); key = k; }
    else
    {
        unit->setCharacter (in.filterChar);
        unit->setDrive (driveK (in.filterDrive));
        unit->setMix (in.filterMix);
    }
}

void MegaFilter::setCutoff (float hz, float res) { unit->update (hz, qOf (res), sr); }

void MegaFilter::process (float& l, float& r)
{
    float yl, yr;
    unit->processFrame (l, r, true, yl, yr);
    // (a resonant peak far over full scale - a classic chain at the top of RES, driven - is rounded off,
    // as Mega Synth's output stage does; normal levels pass untouched)
    l = tg::softKnee (yl, 1.5f); r = tg::softKnee (yr, 1.5f);
}

std::vector<float> megaFilterResponse (const Instrument& in, double sampleRate, const std::vector<double>& hz)
{
    const double sr = sampleRate > 0 ? sampleRate : 48000.0;
    constexpr int n = 4096;
    tg::FilterUnit u;
    u.setPreview (true);
    configureUnit (u, in, in.filter == FilterType::Off ? FilterType::LowPass : in.filter, true);
    u.setDrive (1.0f);
    std::vector<float> ir ((size_t) n);
    const float amp = 1.0e-3f;
    for (int i = 0; i < n; ++i)
    {
        if ((i & 15) == 0) u.update (in.cutoff, qOf (in.resonance), sr);
        float yl, yr;
        u.processFrame (i == 0 ? amp : 0.0f, 0.0f, false, yl, yr);
        ir[(size_t) i] = std::isfinite (yl) ? yl / amp : 0.0f;
    }
    std::vector<float> out;
    out.reserve (hz.size());
    for (double f : hz)
    {
        // the impulse response's spectrum at f (a direct sum: only the frequencies drawn)
        const double w = 2.0 * 3.141592653589793 * f / sr;
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i) { re += ir[(size_t) i] * std::cos (w * i); im -= ir[(size_t) i] * std::sin (w * i); }
        out.push_back ((float) std::sqrt (re * re + im * im));
    }
    return out;
}
} // namespace prv
