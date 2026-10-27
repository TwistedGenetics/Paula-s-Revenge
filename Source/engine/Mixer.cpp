// Paula's Revenge - the mixer
#include "Mixer.h"
#include <algorithm>
#include <cmath>

namespace prv
{
namespace
{
    inline float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }
    void peakInto (std::atomic<float>& meter, const float* a, const float* b, int n)
    {
        float pk = 0;
        for (int i = 0; i < n; ++i) pk = std::max ({ pk, std::abs (a[i]), std::abs (b[i]) });
        if (pk > meter.load (std::memory_order_relaxed)) meter.store (pk, std::memory_order_relaxed);
    }
}

MixerEngine::MixerEngine()
{
    clearAutomation();
    for (int r = 0; r < kReturns; ++r) { sendL[r].assign (kMaxBlock, 0.0f); sendR[r].assign (kMaxBlock, 0.0f); }
}

void MixerEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& e : eq) e.prepare (sr);
    for (auto& f : fx) if (f) f->prepare (sr);
}

void MixerEngine::reset()
{
    for (auto& e : eq) e.reset();
    for (auto& f : fx) if (f) f->reset();
}

std::vector<MixerEngine::Pending> MixerEngine::plan (const MixerSettings& m, bool reloadAll) const
{
    std::vector<Pending> out;
    for (int i = 0; i < kTotalSlots; ++i)
    {
        const auto& s = m.slot (i);
        const FxType want = s.type;
        const std::string key = want == FxType::Plugin ? s.pluginKey : std::string();
        const bool have = fx[i] != nullptr || (want == FxType::Plugin && fxType[i] == FxType::Plugin);
        if (want == fxType[i] && key == fxKey[i] && (want == FxType::None || have) && ! (reloadAll && want != FxType::None)) continue;
        Pending p { i, want, key, nullptr };
        if (want == FxType::Plugin) { if (pluginFactory) p.fx = pluginFactory (i, s, sr); }
        else p.fx = makeEffect (want);
        if (p.fx && want != FxType::Plugin) p.fx->prepare (sr);   // (the factory prepares plug-ins)
        out.push_back (std::move (p));
    }
    return out;
}

void MixerEngine::apply (std::vector<Pending>& changes, std::vector<std::unique_ptr<Effect>>& old)
{
    for (auto& p : changes)
    {
        if (fx[p.slot]) old.push_back (std::move (fx[p.slot]));
        fx[p.slot] = std::move (p.fx);
        fxType[p.slot] = p.type;
        fxKey[p.slot] = p.key;
        for (auto& a : autoParam[p.slot]) a = -1.0f;
    }
    changes.clear();
}

void MixerEngine::syncEffects (const MixerSettings& m, std::vector<std::unique_ptr<Effect>>& old)
{
    auto changes = plan (m);
    apply (changes, old);
}

void MixerEngine::clearAutomation()
{
    for (auto& s : autoParam) for (auto& a : s) a = -1.0f;
}

void MixerEngine::setAutomation (int slot, int param, float v)
{
    if (slot < 0 || slot >= kTotalSlots || param < 0) return;
    v = std::clamp (v, 0.0f, 1.0f);
    if (fxType[slot] == FxType::Plugin) { if (fx[slot]) fx[slot]->setParameter (param, v); }
    else if (param < kFxParams) autoParam[slot][param] = v;
}

void MixerEngine::runInserts (const FxSlot* slots, int first, float* L, float* R, int n, double bpm)
{
    for (int k = 0; k < kInserts; ++k)
    {
        const FxSlot& s = slots[k];
        // (an effect that hasn't been created yet for a new type is skipped)
        if (s.active() && fx[first + k] && fxType[first + k] == s.type && (s.type != FxType::Plugin || fxKey[first + k] == s.pluginKey))
        {
            const float* a = autoParam[first + k];
            if (s.type != FxType::Plugin && (a[0] >= 0 || a[1] >= 0 || a[2] >= 0 || a[3] >= 0 || a[4] >= 0 || a[5] >= 0))
            {
                float p[kFxParams];
                for (int i = 0; i < kFxParams; ++i) p[i] = a[i] >= 0 ? a[i] : s.p[i];
                fx[first + k]->process (L, R, n, p, bpm);
            }
            else fx[first + k]->process (L, R, n, s.p, bpm);
        }
    }
}

void MixerEngine::process (const MixerSettings& m, float* const* tL, float* const* tR, int numTracks,
                           const float* exL, const float* exR, float* outL, float* outR, int n, double bpm)
{
    n = std::min (n, kMaxBlock);
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);
    for (int r = 0; r < kReturns; ++r) { std::fill (sendL[r].begin(), sendL[r].begin() + n, 0.0f); std::fill (sendR[r].begin(), sendR[r].begin() + n, 0.0f); }

    for (int t = 0; t < numTracks && t < kMixTracks; ++t)
    {
        const auto& c = m.ch[t];
        float* L = tL[t];
        float* R = tR[t];
        if (c.eq.anyOn()) eq[t].process (L, R, n, c.eq);
        runInserts (c.inserts, trackSlot (t, 0), L, R, n, bpm);
        const float g = dbToGain (c.volumeDb);
        for (int r = 0; r < kReturns; ++r)
        {
            const float s = c.send[r] * (c.sendPre[r] ? 1.0f : g);
            if (s <= 0.0f) continue;
            for (int i = 0; i < n; ++i) { sendL[r][(size_t) i] += L[i] * s; sendR[r][(size_t) i] += R[i] * s; }
        }
        for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; outL[i] += L[i]; outR[i] += R[i]; }
        peakInto (trackPeak[t], L, R, n);
    }

    for (int r = 0; r < kReturns; ++r)
    {
        const auto& rs = m.ret[r];
        float* L = sendL[r].data();
        float* R = sendR[r].data();
        runInserts (rs.inserts, returnSlot (r, 0), L, R, n, bpm);
        const float g = rs.mute ? 0.0f : dbToGain (rs.volumeDb);
        const float pan = (std::clamp (rs.pan, 0, 255) - 128) / 127.0f;   // balance
        const float gl = g * std::min (1.0f, 1.0f - pan), gr = g * std::min (1.0f, 1.0f + pan);
        for (int i = 0; i < n; ++i) { L[i] *= gl; R[i] *= gr; outL[i] += L[i]; outR[i] += R[i]; }
        peakInto (returnPeak[r], L, R, n);
    }

    if (exL != nullptr) for (int i = 0; i < n; ++i) { outL[i] += exL[i]; outR[i] += exR[i]; }
    runInserts (m.master.inserts, masterSlot (0), outL, outR, n, bpm);
    const float g = dbToGain (m.master.volumeDb);
    for (int i = 0; i < n; ++i) { outL[i] *= g; outR[i] *= g; }
    float pl = 0, pr = 0;
    for (int i = 0; i < n; ++i) { pl = std::max (pl, std::abs (outL[i])); pr = std::max (pr, std::abs (outR[i])); }
    if (pl > masterPeakL.load (std::memory_order_relaxed)) masterPeakL.store (pl, std::memory_order_relaxed);
    if (pr > masterPeakR.load (std::memory_order_relaxed)) masterPeakR.store (pr, std::memory_order_relaxed);
}
} // namespace prv
