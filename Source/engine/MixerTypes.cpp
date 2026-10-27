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
    i -= kTrackSlots;
    if (i < kReturnSlots) return ret[i / kInserts].inserts[i % kInserts];
    return master.inserts[std::clamp (i - kReturnSlots, 0, kInserts - 1)];
}

const FxSlot& MixerSettings::slot (int i) const { return const_cast<MixerSettings*> (this)->slot (i); }

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
    return true;   // (returns only hear what's sent to them)
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
    switch (t)
    {
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

double delaySeconds (const float* p, double bpm)
{
    if (p[1] > 0.5f)
        return kDivisions[divisionIndex (p[0])].beats * 60.0 / std::max (20.0, bpm);
    return std::pow (2000.0, (double) p[0]) / 1000.0;   // 1 ms .. 2 s
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
        default: return {};
    }
}
} // namespace prv
