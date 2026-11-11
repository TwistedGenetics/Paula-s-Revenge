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
    for (int i = 0; i < kMixerSlots; ++i) slotDelay[i].setMax (kMaxSlotDelay);   // (instrument inserts: no delay kept)
    for (auto& d : trackAlign) d.setMax (kMaxTrackDelay);
}

int MixerEngine::slotLatency (int slot) const
{
    if (slot < 0 || slot >= kTotalSlots || fx[slot] == nullptr) return 0;
    return std::clamp (fx[slot]->latency(), 0, kMaxSlotDelay);
}

bool MixerEngine::slotReady (const FxSlot& s, int slot) const
{
    return fx[slot] && fxType[slot] == s.type && (s.type != FxType::Plugin || fxKey[slot] == s.pluginKey);
}

void MixerEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    mods.prepare (sr);
    for (auto& t : stripMulNow) for (auto& v : t) v = 1.0f;
    for (auto& e : eq) e.prepare (sr);
    for (auto& f : fx) if (f) f->prepare (sr);
}

void MixerEngine::setFeedbackRide (const MixerSettings& m, float v)
{
    // the first echo on return B with a FEEDBACK control: ridden to v (< 0: back to its own setting)
    for (int k = 0; k < kInserts; ++k)
    {
        const auto& s = m.ret[1].inserts[k];
        if (! s.active()) continue;
        const auto& ps = fxParams (s.type);
        for (int i = 0; i < (int) ps.size(); ++i)
            if (ps[(size_t) i].name == "FEEDBACK")
            {
                if (v < 0) releaseAutomation (returnSlot (1, k), i); else setAutomation (returnSlot (1, k), i, v);
                return;
            }
    }
}

void MixerEngine::reset()
{
    mods.reset();
    for (auto& t : throwTo) t = 0.0f;
    for (auto& t : throwNow) t = 0.0f;
    dubEver = false;
    for (auto& t : stripMulNow) for (auto& v : t) v = 1.0f;
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
        autoOn[p.slot] = -1;
    }
    changes.clear();
}

void MixerEngine::syncEffects (const MixerSettings& m, std::vector<std::unique_ptr<Effect>>& old)
{
    auto changes = plan (m);
    apply (changes, old);
}

void MixerEngine::releaseAutomation (int slot, int param)
{
    if (slot >= 0 && slot < kTotalSlots && param >= 0 && param < kFxParams) autoParam[slot][param] = -1.0f;
}

void MixerEngine::setStripAutomation (int track, int what, float v)
{
    if (track >= 0 && track < kMixTracks && what >= 0 && what < kModStripParams) stripAuto[track][what] = v < 0 ? -1.0f : std::clamp (v, 0.0f, 1.0f);
}

void MixerEngine::clearAutomation()
{
    for (auto& s : stripAuto) for (auto& a : s) a = -1.0f;
    for (auto& s : autoParam) for (auto& a : s) a = -1.0f;
    for (int m = 0; m < kMods; ++m) mods.setMacro (m, -1.0f);   // (MACROs back to their knobs)
    for (int i = 0; i < kMixInstruments; ++i) for (int k = 0; k < kInstMacros; ++k) mods.setInstMacro (i, k, -1.0f);
    clearOnOff();
}

void MixerEngine::clearOnOff (int slot)
{
    for (int i = 0; i < kTotalSlots; ++i) if (slot < 0 || i == slot) autoOn[i] = -1;
}

void MixerEngine::setAutomation (int slot, int param, float v)
{
    if (slot < 0 || slot >= kTotalSlots || param < 0) return;
    v = std::clamp (v, 0.0f, 1.0f);
    if (param == kAutoOnOff) { autoOn[slot] = autoValueOn (v) ? 1 : 0; return; }
    if (fxType[slot] == FxType::Plugin) { if (fx[slot]) fx[slot]->setParameter (param, v); }
    else if (param < kFxParams) autoParam[slot][param] = v;
}

void MixerEngine::runInserts (const FxSlot* slots, int first, float* L, float* R, int n, double bpm)
{
    for (int k = 0; k < kInserts; ++k)
    {
        const FxSlot& s = slots[k];
        // (an effect that hasn't been created yet for a new type is skipped)
        const int ov = autoOn[first + k];
        const bool on = ov >= 0 ? (ov == 1 && s.type != FxType::None) : s.active();
        const bool ready = slotReady (s, first + k);
        if (! on && ready)
        {
            // switched off or bypassed: the audio still waits as long as the effect would make it
            const int lat = slotLatency (first + k);
            if (lat > 0) slotDelay[first + k].process (L, R, n, lat);
        }
        if (on && ready)
        {
            const float* a = autoParam[first + k];
            bool automated = false;
            if (s.type != FxType::Plugin) for (int i = 0; i < kFxParams; ++i) automated |= a[i] >= 0;
            const bool modded = s.type != FxType::Plugin && mods.slotModded[first + k];
            if (automated || modded)
            {
                float p[kFxParams];
                for (int i = 0; i < kFxParams; ++i) p[i] = a[i] >= 0 ? a[i] : s.p[i];
                if (modded)
                {
                    // MODULATION moves it from there (a switch flips past the middle)
                    const auto& info = fxParams (s.type);
                    for (int i = 0; i < kFxParams; ++i)
                    {
                        const float o = mods.slotOffset[first + k][i];
                        if (o == 0.0f) continue;
                        p[i] = std::clamp (p[i] + o, 0.0f, 1.0f);
                        if (i < (int) info.size() && info[(size_t) i].toggle) p[i] = p[i] > 0.5f ? 1.0f : 0.0f;
                    }
                }
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

    // delay compensation: how late each track comes out of its inserts
    int lat[kMixTracks] {}, maxLat = 0;
    for (int t = 0; t < numTracks && t < kMixTracks; ++t)
    {
        lat[t] = inputLat[t];
        for (int k = 0; k < kInserts; ++k)
            if (m.ch[t].inserts[k].type != FxType::None && slotReady (m.ch[t].inserts[k], trackSlot (t, k))) lat[t] += slotLatency (trackSlot (t, k));
        maxLat = std::max (maxLat, lat[t]);
        uiTrackLat[t].store (lat[t], std::memory_order_relaxed);
    }
    {
        int after = 0;   // the slowest return, plus the master's inserts
        for (int r = 0; r < kReturns; ++r)
        {
            int rl = 0;
            for (int k = 0; k < kInserts; ++k) if (m.ret[r].inserts[k].type != FxType::None && slotReady (m.ret[r].inserts[k], returnSlot (r, k))) rl += slotLatency (returnSlot (r, k));
            after = std::max (after, rl);
        }
        for (int k = 0; k < kInserts; ++k) if (m.master.inserts[k].type != FxType::None && slotReady (m.master.inserts[k], masterSlot (k))) after += slotLatency (masterSlot (k));
        uiTotalLat.store (std::min (maxLat + extraDelay, kMaxTrackDelay) + after, std::memory_order_relaxed);
    }

    // MODULATION: it hears every track first (for the followers), then works out this block
    const bool modsOn = m.mods.any() || m.anyInstMacro();
    if (modsOn || anyModLeft)
    {
        if (modsOn) for (int t = 0; t < numTracks && t < kMixTracks; ++t) mods.listen (t, tL[t], tR[t], n);
        mods.run (m.mods, n, bpm, m.instMacro);   // (with none left on: everything goes back to where it's set)
        for (const auto& ps : mods.pluginSets)
            if (ps.slot >= 0 && ps.slot < kTotalSlots && fxType[ps.slot] == FxType::Plugin && fx[ps.slot])
                fx[ps.slot]->setParameter (ps.param, ps.value);
        anyModLeft = modsOn;
    }

    for (int t = 0; t < numTracks && t < kMixTracks; ++t)
    {
        const auto& c = m.ch[t];
        float* L = tL[t];
        float* R = tR[t];
        if (c.eq.anyOn()) eq[t].process (L, R, n, c.eq);
        if (tapsLive && chTap.source.load (std::memory_order_relaxed) == t) chTap.push (L, R, n);
        runInserts (c.inserts, trackSlot (t, 0), L, R, n, bpm);
        trackAlign[t].process (L, R, n, maxLat - lat[t] + extraDelay);
        const float* sa = stripAuto[t];
        const float g = dbToGain (sa[0] >= 0 ? faderPosToDb (sa[0]) : c.volumeDb);
        // modulated VOLUME / SENDS glide across the block from where the last one ended
        const float* mulNew = mods.stripMul[t];
        float* mulOld = stripMulNow[t];
        const bool ramp = mulNew[0] != mulOld[0] || mulNew[1] != mulOld[1] || mulNew[2] != mulOld[2] || mulNew[0] != 1.0f || mulNew[1] != 1.0f || mulNew[2] != 1.0f;
        const float inv = 1.0f / (float) std::max (1, n);
        for (int r = 0; r < kReturns; ++r)
        {
            const float s = (sa[1 + r] >= 0 ? sa[1 + r] : c.send[r]) * (c.sendPre[r] ? 1.0f : g);
            if (s <= 0.0f) continue;
            if (! ramp) { for (int i = 0; i < n; ++i) { sendL[r][(size_t) i] += L[i] * s; sendR[r][(size_t) i] += R[i] * s; } continue; }
            const float a = mulOld[1 + r], b = mulNew[1 + r];
            const float va = mulOld[0], vb = mulNew[0];
            for (int i = 0; i < n; ++i)
            {
                const float f = (i + 1) * inv;
                const float sg = s * (a + (b - a) * f) * (c.sendPre[r] ? 1.0f : (va + (vb - va) * f));
                sendL[r][(size_t) i] += L[i] * sg; sendR[r][(size_t) i] += R[i] * sg;
            }
        }
        // DUB THROW: into return B, pre-fader, easing to where it's going (about 4 ms) so it never clicks
        if (throwTo[t] > 0.0f || throwNow[t] > 0.0f)
        {
            const float k = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
            float tn = throwNow[t];
            for (int i = 0; i < n; ++i)
            {
                tn += (throwTo[t] - tn) * k;
                sendL[1][(size_t) i] += L[i] * tn; sendR[1][(size_t) i] += R[i] * tn;
            }
            throwNow[t] = tn < 0.0001f && throwTo[t] == 0.0f ? 0.0f : tn;
        }
        if (! ramp) for (int i = 0; i < n; ++i) { L[i] *= g; R[i] *= g; outL[i] += L[i]; outR[i] += R[i]; }
        else
        {
            const float a = mulOld[0], b = mulNew[0];
            for (int i = 0; i < n; ++i)
            {
                const float gg = g * (a + (b - a) * (i + 1) * inv);
                L[i] *= gg; R[i] *= gg; outL[i] += L[i]; outR[i] += R[i];
            }
        }
        for (int k = 0; k < kModStripParams; ++k) mulOld[k] = mulNew[k];
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
