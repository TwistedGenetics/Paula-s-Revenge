#include "helix/Engine.h"
#include "helix/Denormals.h"
#include "helix/Simd.h"
#include "helix/WavetableGen.h"
#include <chrono>
#include <cstring>

namespace helix {

using Clock = std::chrono::steady_clock;

Engine::Engine()
{
    NoiseSource::buildSharedLoops();
    for (int s = 0; s < kNumTableSlots; ++s)
    {
        // A default table so a fresh engine makes sound. Built here, on the constructing (non-audio) thread.
        active_[s] = makeBuiltinTable(BuiltinTable::BasicShapes);
        uiTables_[s] = active_[s];
    }
    for (int i = 0; i < kNumParams; ++i) live_[i].store(params_.getNormalised(i), std::memory_order_relaxed);
    for (auto& v : voiceLoad_) v.store(0.0f, std::memory_order_relaxed);
    ctx_.lfoShapes = &lfoShapes_;
    prepare(44100.0, 512);
}

Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlockSize;
    ctx_.sampleRate = (float)sampleRate;
    for (Voice& v : voices_) v.prepare((float)sampleRate);
    fx_.prepare((float)sampleRate);
    ott_.prepare(sampleRate);
    for (auto& sl : lib_) sl.prepare(sampleRate, kCtrlBlock);
    reset();
}

void Engine::reset()
{
    for (Voice& v : voices_) v.kill();
    fx_.reset();
    for (auto& sl : lib_) sl.reset();
    for (auto& b : sgLp_) b.reset();
    ott_.reset();
    for (auto& b : sgHp_) b.reset();
    pitchBend_ = modWheel_ = chanPressure_ = 0.0f;
    sustain_ = false;
    noteStackSize_ = 0;
    lastPitch_ = -1.0f;
}

// ------------------------------------------------------------------------------------------- tables

bool Engine::setTable(int slot, WavetablePtr table)
{
    if (slot < 0 || slot >= kNumTableSlots || !table) return false;
    collectGarbage();
    TableHandle h{table, slot};
    if (!pending_.push(std::move(h))) return false;
    uiTables_[slot] = std::move(table);
    return true;
}

WavetablePtr Engine::uiTable(int slot) const { return (slot >= 0 && slot < kNumTableSlots) ? uiTables_[slot] : nullptr; }

void Engine::collectGarbage()
{
    TableHandle h;
    while (retired_.pop(h)) h.ptr.reset();
}

void Engine::swapTables()
{
    while (!retired_.full())
    {
        TableHandle h;
        if (!pending_.pop(h)) break;
        TableHandle old{std::move(active_[h.slot]), h.slot};
        active_[h.slot] = std::move(h.ptr);
        retired_.push(std::move(old));
    }
}

// ------------------------------------------------------------------------------------------- per block

void Engine::snapshotParams(const Transport& tr, int n)
{
    for (int i = 0; i < kNumParams; ++i) ctx_.p[i] = params_.get(i);
    for (int s = 0; s < kNumTableSlots; ++s) ctx_.table[s] = active_[s].get();
    const int quality = (int)ctx_.p[P_QUALITY];
    const int guard = ctx_.p[P_AUTO_SCALE] >= 0.5f && realtime_.load(std::memory_order_relaxed) ? guardLevel_.load(std::memory_order_relaxed) : 0;
    ctx_.mipCrossfade = quality > 0 && guard < 2;
    ctx_.oversample = quality == 2 && guard < 1;
    static const int caps[] = {kMaxUnison, kMaxUnison, 5, 3, 2};
    ctx_.unisonCap = std::min(quality == 0 ? 3 : kMaxUnison, caps[clamp(guard, 0, 4)]);
    ctx_.pitchBend = pitchBend_;
    ctx_.pitchBendSemis = pitchBend_ * ctx_.p[P_BEND_RANGE];
    ctx_.modWheel = modWheel_;
    ctx_.channelPressure = chanPressure_;
    ctx_.bpm = tr.valid && tr.bpm > 1.0 ? tr.bpm : 120.0;
    ctx_.songBeats = ownBeats_;
    for (int l = 0; l < kNumLfos; ++l)
    {
        const int b = P_L1_SHAPE + l * (P_L2_SHAPE - P_L1_SHAPE);
        if (ctx_.p[b + 2] >= 0.5f)   // synced: lock to the song position
            ctx_.lfoFreePhase[l] = (float)std::fmod(ownBeats_ * lfoJumpFactor(ctx_.p, l) / syncDivBeats((int)ctx_.p[b + 4]), 1.0);
        else
        {
            ctx_.lfoFreePhase[l] = freePhase_[l];
            freePhase_[l] = wrap01(freePhase_[l] + ctx_.p[b + 3] * lfoJumpFactor(ctx_.p, l) * (float)n / (float)sampleRate_);
        }
    }
    ownBeats_ += (double)n / sampleRate_ * ctx_.bpm / 60.0;
    updateSceneMorph(n);
}

void Engine::storeScene(int s)
{
    if (s < 0 || s >= kNumScenes) return;
    for (int m = 0; m < kNumMacros; ++m) scenes_.v[s][m].store(params_.get(P_MACRO1 + m));
}

// Scene recall: an S-curve from where the macros are to the scene's values over Scene Morph. While it runs the
// macros this block sees are the morph's; at the end the macro parameters themselves take the scene's values.
void Engine::updateSceneMorph(int n)
{
    const int req = sceneRequest_.exchange(-1, std::memory_order_acq_rel);
    if (req >= 0)
    {
        for (int m = 0; m < kNumMacros; ++m)
        {
            morphFrom_[m] = ctx_.p[P_MACRO1 + m];   // the morph's current value if one is running (set last block)
            morphTo_[m] = scenes_.v[req][m].load(std::memory_order_relaxed);
        }
        if (morphing_) for (int m = 0; m < kNumMacros; ++m) morphFrom_[m] = morphNow_[m];
        morphLen_ = (double)ctx_.p[P_SCENE_MORPH] * 0.001 * sampleRate_;
        morphPos_ = 0.0;
        morphing_ = true;
        lastScene_.store(req, std::memory_order_relaxed);
    }
    if (!morphing_) return;
    const double t = morphLen_ < 1.0 ? 1.0 : std::min(1.0, morphPos_ / morphLen_);
    const float sm = (float)(t * t * (3.0 - 2.0 * t));
    for (int m = 0; m < kNumMacros; ++m)
    {
        morphNow_[m] = morphFrom_[m] + (morphTo_[m] - morphFrom_[m]) * sm;
        ctx_.p[P_MACRO1 + m] = morphNow_[m];
    }
    morphPos_ += n;
    if (t >= 1.0)
    {
        morphing_ = false;
        for (int m = 0; m < kNumMacros; ++m) params_.set(P_MACRO1 + m, morphTo_[m]);
    }
}

// ------------------------------------------------------------------------------------------- notes

NoteStart Engine::makeStart(int note, int vel, bool glide)
{
    NoteStart s;
    s.note = note;
    s.velocity = (float)vel / 127.0f;
    s.age = ++ageCounter_;
    s.seed = ageCounter_;
    s.glideFrom = glide && ctx_.p[P_GLIDE] > 0.0f && lastPitch_ >= 0.0f ? lastPitch_ : -1.0f;
    return s;
}

Voice* Engine::findVoiceFor(int note)
{
    const int maxV = clamp((int)ctx_.p[P_VOICES], 1, kMaxVoices);
    // 1. The same note still sounding: retrigger it (steal = click-free).
    for (int i = 0; i < maxV; ++i) if (voices_[i].active() && voices_[i].note() == note) return &voices_[i];
    // 2. A free voice.
    for (int i = 0; i < maxV; ++i) if (!voices_[i].active()) return &voices_[i];
    // 3. Steal: prefer voices already released, then by mode.
    const bool quietest = (int)ctx_.p[P_STEAL] == 1;
    Voice* best = nullptr;
    auto better = [&](Voice& v) {
        if (!best) return true;
        const bool vr = !v.held(), br = !best->held();
        if (vr != br) return vr;
        return quietest ? v.level() < best->level() : v.age() < best->age();
    };
    for (int i = 0; i < maxV; ++i) if (better(voices_[i])) best = &voices_[i];
    return best;
}

void Engine::noteOn(int note, int vel)
{
    const int mode = (int)ctx_.p[P_POLY_MODE];   // 0 poly, 1 mono, 2 legato
    const bool glideAlways = (int)ctx_.p[P_GLIDE_MODE] == 0;
    if (mode == 0)
    {
        Voice* v = findVoiceFor(note);
        const NoteStart s = makeStart(note, vel, glideAlways);
        if (v->active()) v->steal(s); else v->start(s);
        lastPitch_ = (float)note;
        return;
    }
    // Mono / legato: one voice, last-note priority, a note stack for returning to held notes.
    for (int i = 0; i < noteStackSize_; ++i)
        if (noteStack_[i] == note) { std::memmove(noteStack_ + i, noteStack_ + i + 1, sizeof(int) * (size_t)(noteStackSize_ - i - 1)); --noteStackSize_; break; }
    if (noteStackSize_ == 16) { std::memmove(noteStack_, noteStack_ + 1, sizeof(int) * 15); --noteStackSize_; }
    noteStack_[noteStackSize_++] = note;

    Voice& v = voices_[monoVoice()];
    for (int i = 1; i < kMaxVoices; ++i) if (voices_[i].active()) voices_[i].release();
    const bool overlapping = v.active() && v.held() && !v.stealing();
    const bool glide = ctx_.p[P_GLIDE] > 0.0f && (glideAlways || overlapping);
    if (overlapping)
        v.legatoTo(note, (float)vel / 127.0f, glide, mode == 1);
    else
    {
        NoteStart s = makeStart(note, vel, glide);
        if (v.active()) { s.glideFrom = glide ? v.pitch() : -1.0f; v.steal(s); }
        else v.start(s);
    }
    lastPitch_ = (float)note;
}

void Engine::noteOff(int note)
{
    const int mode = (int)ctx_.p[P_POLY_MODE];
    if (mode == 0)
    {
        for (Voice& v : voices_)
            if (v.active() && v.held() && v.note() == note)
            {
                if (sustain_) v.setSustained(true); else v.release();
            }
        return;
    }
    int idx = -1;
    for (int i = 0; i < noteStackSize_; ++i) if (noteStack_[i] == note) idx = i;
    if (idx < 0) return;
    const bool wasTop = idx == noteStackSize_ - 1;
    std::memmove(noteStack_ + idx, noteStack_ + idx + 1, sizeof(int) * (size_t)(noteStackSize_ - idx - 1));
    --noteStackSize_;
    Voice& v = voices_[monoVoice()];
    if (!wasTop) return;
    if (noteStackSize_ > 0)
    {
        const int back = noteStack_[noteStackSize_ - 1];
        v.legatoTo(back, v.level() > 0.0f ? 1.0f : 0.8f, ctx_.p[P_GLIDE] > 0.0f, false);
        lastPitch_ = (float)back;
    }
    else if (sustain_) v.setSustained(true);
    else v.release();
}

void Engine::allNotesOff(bool hard)
{
    for (Voice& v : voices_) { if (hard) v.kill(); else v.release(); }
    noteStackSize_ = 0;
}

// Chord memory: every played note also plays the chord's extra notes (poly mode only). The chord a note started
// with is remembered, so its release lets go of the same notes even if Chord changed in between.
void Engine::chordOn(int note, int vel)
{
    const int c = (int)ctx_.p[P_POLY_MODE] == 0 ? (int)params_.get(P_CHORD) : 0;
    chordUsed_[note & 127] = (uint8_t)c;
    noteOn(note, vel);
    int iv[6];
    const int k = choices::chordIntervals(c, iv);
    for (int i = 0; i < k; ++i)
        if (note + iv[i] >= 0 && note + iv[i] < 128) noteOn(note + iv[i], vel);
}

void Engine::chordOff(int note)
{
    const int c = chordUsed_[note & 127];
    chordUsed_[note & 127] = 0;
    noteOff(note);
    int iv[6];
    const int k = choices::chordIntervals(c, iv);
    for (int i = 0; i < k; ++i)
        if (note + iv[i] >= 0 && note + iv[i] < 128) noteOff(note + iv[i]);
}

void Engine::handleMidi(const MidiMsg& m)
{
    switch (m.status())
    {
        case 0x90: case 0x80:
        {
            // Scene Pads: notes 36-51 (pads A01-A16) recall scenes 1-16 instead of playing
            if (ctx_.p[P_SCENE_PADS] >= 0.5f && m.data[1] >= 36 && m.data[1] <= 51)
            {
                if (m.status() == 0x90 && m.data[2] > 0) recallScene(m.data[1] - 36);
                break;
            }
            if (m.status() == 0x90 && m.data[2] > 0) chordOn(m.data[1], m.data[2]); else chordOff(m.data[1]);
            break;
        }
        case 0xA0:   // poly aftertouch (pad pressure)
            for (Voice& v : voices_) if (v.active() && v.note() == m.data[1]) v.setPressure((float)m.data[2] / 127.0f);
            break;
        case 0xD0: chanPressure_ = (float)m.data[1] / 127.0f; break;
        case 0xE0: pitchBend_ = (float)(((int)m.data[1] | ((int)m.data[2] << 7)) - 8192) / 8192.0f; break;
        case 0xB0:
            switch (m.data[1])
            {
                case 1: modWheel_ = (float)m.data[2] / 127.0f; break;
                case 64:
                {
                    const bool on = m.data[2] >= 64;
                    if (sustain_ && !on)
                        for (Voice& v : voices_) if (v.sustained()) v.release();
                    sustain_ = on;
                    break;
                }
                case 120: allNotesOff(true); break;
                case 123: allNotesOff(false); break;
                default: break;
            }
            break;
        default: break;
    }
}

// ------------------------------------------------------------------------------------------- render

void Engine::renderBlock(float* l, float* r, int n)
{
    int count = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        Voice& v = voices_[i];
        if (!v.active()) continue;
        const auto t0 = Clock::now();
        v.render(ctx_, l, r, n);
        voiceTime_[i] += std::chrono::duration<double>(Clock::now() - t0).count();
        ++count;
    }
    activeVoices_.store(count, std::memory_order_relaxed);
}

void Engine::runFx(float* l, float* r, int n)
{
    // Global effects follow the newest voice's modulation (like most wavetable synths do).
    std::memcpy(fxp_, ctx_.p, sizeof(float) * kNumParams);
    const Voice* newest = nullptr;
    for (const Voice& v : voices_) if (v.active() && (!newest || v.age() > newest->age())) newest = &v;
    if (newest)
        for (int k = 0; k < newest->numModOffsets(); ++k)
        {
            const int pi = newest->modOffset(k).param;
            if (pi >= P_FX_ORDER1) fxp_[pi] = newest->modulatedParams()[pi];
        }
    // Sub guardian: the band below Freq is taken out here (mono, clean) and added back after every effect.
    const bool sg = fxp_[P_SG_ON] >= 0.5f;
    if (sg)
    {
        const float f = clamp(fxp_[P_SG_FREQ], 20.0f, 0.45f * (float)sampleRate_);
        if (f != sgKey_[0])
        {
            sgKey_[0] = f;
            // two cascaded Butterworth low-passes = Linkwitz-Riley 24 dB
            const float w = kTwoPi * f / (float)sampleRate_, cs = std::cos(w), al = std::sin(w) / (2.0f * 0.70710678f);
            const float a0 = 1.0f + al;
            for (auto& b : sgLp_)
            {
                b.b0 = (1.0f - cs) * 0.5f / a0; b.b1 = (1.0f - cs) / a0; b.b2 = b.b0;
                b.a1 = -2.0f * cs / a0; b.a2 = (1.0f - al) / a0;
            }
            for (auto& b : sgHp_)
            {
                b.b0 = (1.0f + cs) * 0.5f / a0; b.b1 = -(1.0f + cs) / a0; b.b2 = b.b0;
                b.a1 = -2.0f * cs / a0; b.a2 = (1.0f - al) / a0;
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const float ll = sgLp_[1].process(0, sgLp_[0].process(0, l[i]));
            const float lr = sgLp_[1].process(1, sgLp_[0].process(1, r[i]));
            l[i] = sgHp_[1].process(0, sgHp_[0].process(0, l[i]));
            r[i] = sgHp_[1].process(1, sgHp_[0].process(1, r[i]));
            sgLow_[i] = 0.5f * (ll + lr);
        }
    }

    // Effects library: Insert 1, Insert 2, then HELIX's FX rack, then FX 1, FX 2
    fxlib::Context lc;
    lc.bpm = ctx_.bpm;
    lc.beat = ctx_.songBeats;
    lc.params = fxp_;
    lc.oversample = ctx_.oversample;
    auto runSlot = [&](int s) {
        const int b = P_I1_TYPE + s * (P_I2_TYPE - P_I1_TYPE);
        lib_[s].process(l, r, n, (int)fxp_[b], fxp_[b + 1], fxp_ + b + 2, lc);
    };
    runSlot(0);
    runSlot(1);
    fx_.process(l, r, n, fxp_, ctx_.bpm, ctx_.oversample);
    runSlot(2);
    runSlot(3);
    if (sg)
    {
        const float g = dbToGain(clamp(fxp_[P_SG_LEVEL], -12.0f, 6.0f));
        const float w = clamp(fxp_[P_SG_WARMTH], 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            float s = sgLow_[i];
            // warmth: soft even + odd harmonics, level-matched at low drive
            if (w > 0.0f) { const float k = 1.0f + 3.0f * w; s = fastTanh(k * s + 0.2f * w * k * s * s) / k; }
            s *= g;
            l[i] += s;
            r[i] += s;
        }
    }
    ott_.process(l, r, n, fxp_);   // master OTT: the last thing before the master level
    heavyFx_.store(fx_.heavyActive() || lib_[0].heavy() || lib_[1].heavy() || lib_[2].heavy() || lib_[3].heavy(),
                   std::memory_order_relaxed);
}

void Engine::publishLive()
{
    const Voice* newest = nullptr;
    for (const Voice& v : voices_) if (v.active() && (!newest || v.age() > newest->age())) newest = &v;
    for (int i = 0; i < kNumParams; ++i)
        if (paramDef(i).modulatable) live_[i].store(params_.getNormalised(i), std::memory_order_relaxed);
    if (!newest) return;
    for (int k = 0; k < newest->numModOffsets(); ++k)
    {
        const auto& o = newest->modOffset(k);
        live_[o.param].store(clamp(params_.getNormalised(o.param) + o.norm, 0.0f, 1.0f), std::memory_order_relaxed);
    }
}

void Engine::updateGuard(double seconds, int n)
{
    const double real = (double)n / sampleRate_;
    const float load = (float)(seconds / std::max(real, 1e-9));
    const float smoothed = cpuLoad_.load(std::memory_order_relaxed) * 0.97f + load * 0.03f;
    cpuLoad_.store(smoothed, std::memory_order_relaxed);

    voiceWindow_ += real;
    if (voiceWindow_ >= 1.0)
    {
        for (int i = 0; i < kMaxVoices; ++i) { voiceLoad_[i].store((float)(voiceTime_[i] / voiceWindow_), std::memory_order_relaxed); voiceTime_[i] = 0.0; }
        voiceWindow_ = 0.0;
    }

    // CPU guard with hysteresis: step up quickly when over budget, step down slowly when well under.
    const float budget = cpuBudget_.load(std::memory_order_relaxed);
    guardTimer_ += real;
    if (!realtime_.load(std::memory_order_relaxed)) return;
    int level = guardLevel_.load(std::memory_order_relaxed);
    if (smoothed > budget && guardTimer_ > 0.25 && level < 4) { guardLevel_.store(level + 1); guardTimer_ = 0.0; }
    else if (smoothed < 0.6f * budget && guardTimer_ > 2.0 && level > 0) { guardLevel_.store(level - 1); guardTimer_ = 0.0; }
    else if (smoothed >= 0.6f * budget && smoothed <= budget && guardTimer_ > 2.0) guardTimer_ = 2.0;
}

void Engine::process(float* l, float* r, int n, const MidiMsg* events, int numEvents, const Transport& transport)
{
    const auto t0 = Clock::now();
    ScopedFlushDenormals noDenormals;
    swapTables();
    bufClear(l, n);
    bufClear(r, n);
    if (transport.valid && transport.playing) ownBeats_ = transport.ppq;

    int ev = 0, pos = 0;
    while (pos < n)
    {
        int end = std::min(n, pos + kCtrlBlock);
        if (ev < numEvents && events[ev].offset > pos && events[ev].offset < end) end = events[ev].offset;
        snapshotParams(transport, end - pos);
        while (ev < numEvents && events[ev].offset <= pos) handleMidi(events[ev++]);
        renderBlock(l + pos, r + pos, end - pos);
        runFx(l + pos, r + pos, end - pos);
        pos = end;
    }
    while (ev < numEvents) handleMidi(events[ev++]);

    const float master = dbToGain(params_.get(P_MASTER));
    bufScale(l, master, n);
    bufScale(r, master, n);
    // Decaying tails (reverb, delay, releases) end in tiny but normal floats (< 1e-20, far below -300 dBFS):
    // zero them so nothing downstream ever sees near-denormal values.
    for (int i = 0; i < n; ++i)
    {
        if (std::fabs(l[i]) < 1e-20f) l[i] = 0.0f;
        if (std::fabs(r[i]) < 1e-20f) r[i] = 0.0f;
    }
    publishLive();
    updateGuard(std::chrono::duration<double>(Clock::now() - t0).count(), n);
}

} // namespace helix
