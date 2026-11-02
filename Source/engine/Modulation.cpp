// Paula's Revenge - MODULATION (see Modulation.h)
#include "Modulation.h"
#include "Song.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace prv
{
namespace
{
    constexpr double kTwoPiM = 6.283185307179586;
    std::string fmtM (const char* f, double v) { char b[48]; std::snprintf (b, sizeof b, f, v); return b; }
    int choice (float v, int n) { return std::clamp ((int) std::lround (v * (n - 1)), 0, n - 1); }

    struct Div { const char* name; double beats; };
    const Div kDivs[] = { { "1/32", 0.125 }, { "1/16", 0.25 }, { "1/8 T", 1.0 / 3.0 }, { "1/8", 0.5 }, { "1/4 T", 2.0 / 3.0 }, { "1/8 D", 0.75 },
                          { "1/4", 1.0 }, { "1/4 D", 1.5 }, { "1/2", 2.0 }, { "1 BAR", 4.0 }, { "2 BARS", 8.0 }, { "4 BARS", 16.0 } };
    constexpr int kNumDivs = (int) (sizeof kDivs / sizeof kDivs[0]);
    const char* const kShapes[] = { "SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "RANDOM S&H" };
    constexpr int kNumShapes = 6;
    double freeHz (float v) { return 0.02 * std::pow (1000.0, (double) v); }
    std::string notesFrom (float v) { const int c = choice (v, kMixTracks + 1); return c == 0 ? std::string ("ANY TRACK") : fmtM ("TRACK %.0f", (double) c); }
    int noteOf (float v) { return 1 + (int) std::lround (v * (kNoteMax - 1)); }
}

double modBeats (float v) { return kDivs[choice (v, kNumDivs)].beats; }
std::string modDivisionName (float v) { return kDivs[choice (v, kNumDivs)].name; }

std::string modSourceName (ModSource s)
{
    switch (s)
    {
        case ModSource::Lfo:      return "LFO";
        case ModSource::Follower: return "FOLLOWER";
        case ModSource::Stepper:  return "STEPPER";
        case ModSource::Random:   return "RANDOM";
        case ModSource::Key:      return "KEY";
        case ModSource::Velocity: return "VELOCITY";
        case ModSource::Macro:    return "MACRO";
        default:                  return "OFF";
    }
}

const std::vector<FxParamInfo>& modParams (ModSource s)
{
    static const std::vector<FxParamInfo> none;
    static const std::vector<FxParamInfo> lfo { { "SHAPE", 0.0f }, { "RATE", 6.0f / 11.0f }, { "SYNC", 1.0f, true }, { "RETRIG", 0.0f, true },
                                                { "PHASE", 0.0f }, { "NOTES FROM", 0.0f } };
    static const std::vector<FxParamInfo> follower { { "TRACK", 0.0f }, { "ATTACK", 0.2f }, { "RELEASE", 0.4f }, { "GAIN", 0.5f } };
    static const std::vector<FxParamInfo> stepper { { "RATE", 1.0f / 11.0f }, { "STEPS", 1.0f }, { "GLIDE", 0.0f }, { "RETRIG", 0.0f, true }, { "NOTES FROM", 0.0f } };
    static const std::vector<FxParamInfo> random { { "RATE", 6.0f / 11.0f }, { "SYNC", 1.0f, true }, { "SMOOTH", 0.0f } };
    static const std::vector<FxParamInfo> key { { "LOW NOTE", 12.0f / 95.0f }, { "HIGH NOTE", 72.0f / 95.0f }, { "NOTES FROM", 0.0f } };
    static const std::vector<FxParamInfo> velocity { { "NOTES FROM", 0.0f } };
    static const std::vector<FxParamInfo> macro { { "VALUE", 0.5f } };
    switch (s)
    {
        case ModSource::Lfo:      return lfo;
        case ModSource::Follower: return follower;
        case ModSource::Stepper:  return stepper;
        case ModSource::Random:   return random;
        case ModSource::Key:      return key;
        case ModSource::Velocity: return velocity;
        case ModSource::Macro:    return macro;
        default:                  return none;
    }
}

std::string modParamText (ModSource s, int i, const float* p)
{
    const auto& ps = modParams (s);
    if (i < 0 || i >= (int) ps.size()) return {};
    const float v = p[i];
    if (ps[(size_t) i].toggle) return v > 0.5f ? "ON" : "OFF";
    if (ps[(size_t) i].name == "NOTES FROM") return notesFrom (v);
    switch (s)
    {
        case ModSource::Lfo:
            if (i == 0) return kShapes[choice (v, kNumShapes)];
            if (i == 1) return p[2] > 0.5f ? modDivisionName (v) : fmtM (freeHz (v) < 1 ? "%.2f HZ" : "%.1f HZ", freeHz (v));
            if (i == 4) return fmtM ("%.0f DEG", 360.0 * v);
            break;
        case ModSource::Follower:
            if (i == 0) return fmtM ("TRACK %.0f", 1.0 + choice (v, kMixTracks));
            if (i == 1) return fmtM ("%.0f MS", std::pow (100.0, (double) v));
            if (i == 2) return fmtM ("%.0f MS", 20.0 * std::pow (100.0, (double) v));
            if (i == 3) return fmtM ("+%.1f DB", 24.0 * v);
            break;
        case ModSource::Stepper:
            if (i == 0) return modDivisionName (v);
            if (i == 1) return fmtM ("%.0f", 1.0 + choice (v, kModSteps));
            break;
        case ModSource::Random:
            if (i == 0) return p[1] > 0.5f ? modDivisionName (v) : fmtM (freeHz (v) < 1 ? "%.2f HZ" : "%.1f HZ", freeHz (v));
            break;
        case ModSource::Key:
            if (i <= 1) return noteName ((uint8_t) noteOf (v));
            break;
        default: break;
    }
    return fmtM ("%.0f%%", v * 100.0);
}

void initModulator (Modulator& m, ModSource s)
{
    m.source = s;
    std::fill (std::begin (m.p), std::end (m.p), 0.0f);
    const auto& ps = modParams (s);
    for (size_t i = 0; i < ps.size() && i < (size_t) kModParams; ++i) m.p[i] = ps[i].def;
    // a stepper starts as a rising ramp of 16 steps
    for (int i = 0; i < kModSteps; ++i) m.steps[i] = (float) i / (kModSteps - 1);
}

bool modBipolar (ModSource s) { return s == ModSource::Lfo || s == ModSource::Random; }

int modNotesFrom (const Modulator& m)
{
    const auto& ps = modParams (m.source);
    for (size_t i = 0; i < ps.size(); ++i)
        if (ps[i].name == "NOTES FROM") { const int c = choice (m.p[i], kMixTracks + 1); return c - 1; }
    return -1;
}

std::string modSummary (const Modulator& m)
{
    switch (m.source)
    {
        case ModSource::Lfo:      return "LFO " + modParamText (m.source, 0, m.p) + " " + modParamText (m.source, 1, m.p);
        case ModSource::Follower: return "FOLLOW " + modParamText (m.source, 0, m.p);
        case ModSource::Stepper:  return "STEPPER " + modParamText (m.source, 1, m.p) + " X " + modParamText (m.source, 0, m.p);
        case ModSource::Random:   return "RANDOM " + modParamText (m.source, 0, m.p);
        case ModSource::Key:      return "KEY " + modParamText (m.source, 0, m.p) + "-" + modParamText (m.source, 1, m.p);
        case ModSource::Velocity: return "VELOCITY";
        case ModSource::Macro:    return "MACRO " + modParamText (m.source, 0, m.p);
        default:                  return "OFF";
    }
}

// ---------------------------------------------------------------- the engine
void ModEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    pluginSets.reserve (kMods * kModRoutes + kMixInstruments * kInstMacros * kInstMacroRoutes);
    reset();
}

void ModEngine::reset()
{
    for (int m = 0; m < kMods; ++m)
    {
        out[m] = 0; phase[m] = 0; holdPpq[m] = -1; sh[m] = 0.5f; target[m] = 0; env[m] = 0; glideOut[m] = 0;
        lastStep[m] = -1; retrigged[m] = false; seenSerial[m] = 0; macroAuto[m] = -1.0f;
        uiOut[m].store (0.0f, std::memory_order_relaxed);
    }
    for (int t = 0; t <= kMixTracks; ++t) { noteOfTrack[t] = 37; velOfTrack[t] = 1.0f; noteSerial[t] = 0; }
    for (auto& in : instMacroAuto) for (auto& v : in) v = -1.0f;
    for (auto& l : level) l = 0;
    std::memset (slotModded, 0, sizeof slotModded);
    std::memset (slotOffset, 0, sizeof slotOffset);
    for (auto& s : stripMul) for (auto& v : s) v = 1.0f;
    pluginSets.clear();
}

void ModEngine::noteOn (int track, int note, float vel)
{
    // the track's own (if it's one), and ANY TRACK
    for (int t : { track >= 0 && track < kMixTracks ? track : (int) kMixTracks, (int) kMixTracks })
    {
        noteOfTrack[t] = note; velOfTrack[t] = std::clamp (vel, 0.0f, 1.0f); ++noteSerial[t];
        if (t == kMixTracks) break;
    }
}

void ModEngine::setInstMacro (int ins, int k, float v)
{
    if (ins >= 0 && ins < kMixInstruments && k >= 0 && k < kInstMacros) instMacroAuto[ins][k] = v < 0 ? -1.0f : std::clamp (v, 0.0f, 1.0f);
}

void ModEngine::setMacro (int m, float v)
{
    if (m >= 0 && m < kMods) macroAuto[m] = v < 0 ? -1.0f : std::clamp (v, 0.0f, 1.0f);
}

void ModEngine::listen (int track, const float* L, const float* R, int n)
{
    if (track < 0 || track >= kMixTracks) return;
    float pk = 0;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
    level[track] = pk;
}

void ModEngine::run (const ModSettings& s, int n, double bpm, const InstMacro (*instMacros)[kInstMacros])
{
    const double bps = std::max (20.0, bpm) / 60.0;
    const double dt = (double) n / sr;
    const bool started = isPlaying && ! wasPlaying;
    for (int m = 0; m < kMods; ++m)
    {
        const auto& md = s.mod[m];
        if (! md.on()) { out[m] = 0; uiOut[m].store (0.0f, std::memory_order_relaxed); continue; }
        const float* p = md.p;
        const int from = modNotesFrom (md);
        const int nt = from < 0 ? kMixTracks : from;
        const bool gotNote = noteSerial[nt] != seenSerial[m];
        seenSerial[m] = noteSerial[nt];
        float v = 0;
        switch (md.source)
        {
            case ModSource::Lfo:
            {
                const bool sync = p[2] > 0.5f, retrig = p[3] > 0.5f;
                const double beats = modBeats (p[1]);
                const double hz = sync ? bps / beats : freeHz (p[1]);
                bool wrapped = false;
                if (sync && isPlaying && ! retrig)
                {
                    // locked to the song position
                    const double cyc = songPpq / beats;
                    wrapped = std::floor (cyc) != holdPpq[m];
                    holdPpq[m] = std::floor (cyc);
                    phase[m] = cyc - std::floor (cyc);
                }
                else
                {
                    if (retrig && gotNote) { phase[m] = 0; wrapped = true; }
                    else { phase[m] += hz * dt; if (phase[m] >= 1.0) { phase[m] -= std::floor (phase[m]); wrapped = true; } }
                }
                if (wrapped || started) sh[m] = random01();
                double ph = phase[m] + p[4]; ph -= std::floor (ph);
                switch (choice (p[0], kNumShapes))
                {
                    case 0:  v = (float) std::sin (kTwoPiM * ph); break;
                    case 1:  v = (float) (ph < 0.5 ? 4 * ph - 1 : 3 - 4 * ph); break;
                    case 2:  v = (float) (2 * ph - 1); break;
                    case 3:  v = (float) (1 - 2 * ph); break;
                    case 4:  v = ph < 0.5 ? 1.0f : -1.0f; break;
                    default: v = sh[m] * 2.0f - 1.0f; break;
                }
                break;
            }
            case ModSource::Follower:
            {
                const float lvl = level[choice (p[0], kMixTracks)];
                const double att = std::pow (100.0, (double) p[1]) * 0.001, rel = 20.0 * std::pow (100.0, (double) p[2]) * 0.001;
                const float c = (float) std::exp (-dt / (lvl > env[m] ? att : rel));
                env[m] = lvl + (env[m] - lvl) * c;
                v = std::clamp (env[m] * (float) std::pow (10.0, 24.0 * p[3] / 20.0), 0.0f, 1.0f);
                break;
            }
            case ModSource::Stepper:
            {
                const double beats = modBeats (p[0]);
                const int steps = 1 + choice (p[1], kModSteps);
                const bool retrig = p[3] > 0.5f;
                double pos;
                if (isPlaying && ! retrig) pos = songPpq / beats;
                else
                {
                    if (retrig && gotNote) phase[m] = 0;
                    else phase[m] += bps / beats * dt;
                    pos = phase[m];
                }
                const int idx = ((int) std::floor (pos) % steps + steps) % steps;
                lastStep[m] = idx;
                uiStep[m].store (idx, std::memory_order_relaxed);
                const float tgt = md.steps[idx];
                if (p[2] <= 0.001f) glideOut[m] = tgt;
                else
                {
                    const double tau = p[2] * beats / bps;
                    glideOut[m] = tgt + (glideOut[m] - tgt) * (float) std::exp (-dt / tau);
                }
                v = glideOut[m];
                break;
            }
            case ModSource::Random:
            {
                const bool sync = p[1] > 0.5f;
                const double beats = modBeats (p[0]);
                const double hz = sync ? bps / beats : freeHz (p[0]);
                bool wrapped = false;
                if (sync && isPlaying)
                {
                    const double cyc = std::floor (songPpq / beats);
                    wrapped = cyc != holdPpq[m];
                    holdPpq[m] = cyc;
                }
                else { phase[m] += hz * dt; if (phase[m] >= 1.0) { phase[m] -= std::floor (phase[m]); wrapped = true; } }
                if (wrapped || started) target[m] = random01() * 2.0f - 1.0f;
                if (p[2] <= 0.001f) glideOut[m] = target[m];
                else
                {
                    const double tau = p[2] / hz;
                    glideOut[m] = target[m] + (glideOut[m] - target[m]) * (float) std::exp (-dt / tau);
                }
                v = glideOut[m];
                break;
            }
            case ModSource::Key:
            {
                const int lo = noteOf (p[0]), hi = noteOf (p[1]);
                const int note = noteOfTrack[nt];
                v = hi == lo ? (note >= lo ? 1.0f : 0.0f) : std::clamp ((float) (note - lo) / (float) (hi - lo), 0.0f, 1.0f);
                break;
            }
            case ModSource::Velocity: v = velOfTrack[nt]; break;
            case ModSource::Macro:    v = macroAuto[m] >= 0 ? macroAuto[m] : p[0]; break;
            default: break;
        }
        out[m] = v;
        uiOut[m].store (v, std::memory_order_relaxed);
    }
    wasPlaying = isPlaying;

    // what the routes add
    std::memset (slotModded, 0, sizeof slotModded);
    std::memset (slotOffset, 0, sizeof slotOffset);
    for (auto& st : stripMul) for (auto& x : st) x = 1.0f;
    pluginSets.clear();
    for (int m = 0; m < kMods; ++m)
    {
        const auto& md = s.mod[m];
        if (! md.on()) continue;
        for (const auto& r : md.route)
        {
            if (! r.valid()) continue;
            const float v = out[m] * r.amount;
            if (r.target < kTotalSlots)
            {
                if (r.param < kFxParams) { slotOffset[r.target][r.param] += v; slotModded[r.target] = true; }
                if (pluginSets.size() < pluginSets.capacity()) pluginSets.push_back ({ r.target, r.param, std::clamp (r.base + v, 0.0f, 1.0f) });
            }
            else if (r.target >= kModStripBase && r.target < kModStripBase + kMixTracks && r.param < kModStripParams)
                stripMul[r.target - kModStripBase][r.param] *= std::max (0.0f, 1.0f + v);
        }
    }
    // the instruments' MACRO knobs, the same way
    if (instMacros != nullptr)
        for (int ins = 0; ins < kMixInstruments; ++ins)
            for (int k = 0; k < kInstMacros; ++k)
            {
                const auto& im = instMacros[ins][k];
                if (im.routes() == 0) continue;
                const float val = instMacroAuto[ins][k] >= 0 ? instMacroAuto[ins][k] : im.value;
                for (const auto& r : im.route)
                {
                    if (! r.valid() || r.target >= kTotalSlots) continue;
                    const float v = val * r.amount;
                    if (r.param < kFxParams) { slotOffset[r.target][r.param] += v; slotModded[r.target] = true; }
                    if (pluginSets.size() < pluginSets.capacity()) pluginSets.push_back ({ r.target, r.param, std::clamp (r.base + v, 0.0f, 1.0f) });
                }
            }
}
} // namespace prv
