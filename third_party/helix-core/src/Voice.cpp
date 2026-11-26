#include "helix/Voice.h"
#include "helix/Engine.h"
#include "helix/Simd.h"
#include <cstring>

namespace helix {

void Voice::prepare(float sampleRate)
{
    sr_ = sampleRate;
    amp_.setSampleRate(sampleRate);
    for (auto& e : env_) e.setSampleRate(sampleRate);
    for (auto& o : osc_) o.setSampleRate(sampleRate);
    sub_.setSampleRate(sampleRate);
    noise_.setSampleRate(sampleRate);
    co_.prepare(sampleRate);
    for (auto& f : filt_) f.prepare(sampleRate);
    std::memset(prevM_, 0, sizeof prevM_);
    stealLen_ = std::max(1, (int)(0.003f * sampleRate));   // 3 ms steal fade
}

void Voice::startNow(const NoteStart& s)
{
    note_ = s.note;
    vel_ = s.velocity;
    age_ = s.age;
    held_ = true;
    sustained_ = false;
    pressure_ = 0.0f;
    rng_ = Rng(s.seed * 2654435761u + 12345u);
    random_ = rng_.bi();
    glideFrom_ = s.glideFrom;
    glide_ = s.glideFrom >= 0.0f;
    pitch_ = glide_ ? s.glideFrom : (float)s.note;
    beatsSinceStart_ = 0.0;
    fresh_ = true;
    for (auto& f : filt_) f.reset();
    for (auto& f : mfilt_) f.reset();
    std::memset(prevM_, 0, sizeof prevM_);
    amp_.hardReset();
    amp_.gate(true);
    for (auto& e : env_) { e.hardReset(); e.gate(true); }
    co_.start(true);
}

void Voice::start(const NoteStart& s) { stealing_ = false; startNow(s); }

void Voice::steal(const NoteStart& s)
{
    if (!amp_.active()) { start(s); return; }
    pending_ = s;
    held_ = true;
    sustained_ = false;
    if (!stealing_) { stealing_ = true; stealLeft_ = stealLen_; }
}

void Voice::legatoTo(int note, float velocity, bool glide, bool retrigger)
{
    if (stealing_) { pending_.note = note; pending_.velocity = velocity; return; }
    note_ = note;
    vel_ = velocity;
    held_ = true;
    sustained_ = false;
    glide_ = glide;
    if (!glide) pitch_ = (float)note;
    if (retrigger)
    {
        amp_.gate(true);
        for (auto& e : env_) e.gate(true);
        beatsSinceStart_ = 0.0;
        co_.start(false);
    }
}

void Voice::release()
{
    held_ = false;
    sustained_ = false;
    if (stealing_) return;   // the pending note will start held; release it when it starts
    amp_.gate(false);
    for (auto& e : env_) e.gate(false);
}

void Voice::kill()
{
    held_ = false;
    sustained_ = false;
    stealing_ = false;
    amp_.hardReset();
    for (auto& e : env_) e.hardReset();
    for (auto& f : filt_) f.reset();
    for (auto& f : mfilt_) f.reset();
}

static inline bool warpUsesOther(float mode)
{
    const Warp w = (Warp)(int)mode;
    return w == Warp::FmOther || w == Warp::Am || w == Warp::Rm;
}

// ------------------------------------------------------------------------------------------- modulation

void Voice::computeModulation(const EngineContext& ctx, int n)
{
    const float* p = ctx.p;
    float src[kNumModSources];
    src[SRC_NONE] = 0.0f;
    src[SRC_ENV1] = amp_.value();
    const int eOff = P_E2_A - P_E1_A;
    for (int e = 0; e < 3; ++e)
    {
        const int b = P_E2_A + e * eOff;
        env_[e].set(p[b], p[b + 1], p[b + 2], p[b + 3], p[b + 4], p[b + 5], p[b + 6]);
        src[SRC_ENV2 + e] = env_[e].advance(n);
    }

    const int lOff = P_L2_SHAPE - P_L1_SHAPE;
    for (int l = 0; l < kNumLfos; ++l)
    {
        const int b = P_L1_SHAPE + l * lOff;
        const bool sync = p[b + 2] >= 0.5f;
        const LfoMode mode = (LfoMode)(int)p[b + 1];
        float rate;
        if (sync) rate = (float)(ctx.bpm / 60.0 / syncDivBeats((int)p[b + 4]));
        else
        {
            const ParamDef& d = paramDef(b + 3);
            rate = d.toPlain(clamp(d.toNormalised(p[b + 3]) + lfoRateOffset_[l], 0.0f, 1.0f));
        }
        rate *= lfoJumpFactor(p, l);
        const float freePhase = mode == LfoMode::Free ? wrap01(ctx.lfoFreePhase[l] + p[b + 7]) : -1.0f;
        if (fresh_) lfo_[l].reset(p[b + 7], (uint32_t)(age_ * 7919u + (uint32_t)l * 104729u + 1u));
        src[SRC_LFO1 + l] = lfo_[l].advance(n, sr_, rate, (LfoShape)(int)p[b], mode, p[b + 5], p[b + 6], freePhase, l,
                                            *ctx.lfoShapes, (int)p[b + 8]);
    }

    beatsSinceStart_ += (double)n / sr_ * ctx.bpm / 60.0;
    for (int k = 0; k < kNumPerformers; ++k)
    {
        const int b = k == 0 ? P_P1_DIV : P_P2_DIV;
        const int mode = (int)p[b + 2];
        const int len = clamp((int)p[b + 1], 1, kMaxSteps);
        double pos = performerSteps(p, k, mode == 1 ? ctx.songBeats : beatsSinceStart_);
        if (mode == 2) pos = std::min(pos, (double)len - 1e-6);   // one-shot holds the last step
        src[SRC_PERF1 + k] = performerValue(p, k, pos);
    }

    src[SRC_VELOCITY] = vel_;
    src[SRC_PRESSURE] = pressure_;
    src[SRC_CHAN_AT] = ctx.channelPressure;
    src[SRC_MODWHEEL] = ctx.modWheel;
    src[SRC_PITCHBEND] = ctx.pitchBend;
    src[SRC_NOTE] = ((float)note_ - 60.0f) / 60.0f;
    src[SRC_RANDOM] = random_;
    for (int m = 0; m < kNumMacros; ++m) src[SRC_MACRO1 + m] = p[P_MACRO1 + m];

    // Matrix
    std::memcpy(vp_, p, sizeof(float) * kNumParams);
    pitchMod_ = pitchModA_ = pitchModB_ = ampMod_ = panMod_ = 0.0f;
    numOffsets_ = 0;
    for (int l = 0; l < kNumLfos; ++l) lfoRateOffset_[l] = 0.0f;
    const int sOff = P_M2_SRC - P_M1_SRC;
    for (int m = 0; m < kNumModSlots; ++m)
    {
        const int b = P_M1_SRC + m * sOff;
        const int s = (int)p[b], d = (int)p[b + 1], aux = (int)p[b + 3];
        const float amt = p[b + 2];
        if (s <= SRC_NONE || s >= kNumModSources || d <= DST_NONE || amt == 0.0f) continue;
        float v = modCurve(src[s], p[b + 4]) * amt;
        if (aux > SRC_NONE && aux < kNumModSources) v *= src[aux];
        switch (d)
        {
            case DST_PITCH: pitchMod_ += v * 24.0f; continue;
            case DST_A_PITCH: pitchModA_ += v * 24.0f; continue;
            case DST_B_PITCH: pitchModB_ += v * 24.0f; continue;
            case DST_AMP: ampMod_ += v; continue;
            case DST_PAN: panMod_ += v; continue;
            default: break;
        }
        const int pi = modDestParam(d);
        if (pi < 0) continue;
        int k = 0;
        while (k < numOffsets_ && offsets_[k].param != pi) ++k;
        if (k == numOffsets_) { offsets_[numOffsets_++] = {pi, 0.0f}; }
        offsets_[k].norm += v;
    }
    for (int k = 0; k < numOffsets_; ++k)
    {
        const int pi = offsets_[k].param;
        const ParamDef& d = paramDef(pi);
        vp_[pi] = d.toPlain(clamp(d.toNormalised(p[pi]) + offsets_[k].norm, 0.0f, 1.0f));
        for (int l = 0; l < kNumLfos; ++l)
            if (pi == P_L1_RATE + l * lOff) lfoRateOffset_[l] = offsets_[k].norm;   // applied next block
    }

    // Filter envelope (Env 2): each filter's bipolar Env Amt moves its cutoff up to +-6 octaves
    for (int f = 0; f < 2; ++f)
    {
        const int o = f == 0 ? 0 : P_F2_CUTOFF - P_F1_CUTOFF;
        const float amt = p[f == 0 ? P_F1_ENV : P_F2_ENV];
        if (amt != 0.0f) vp_[P_F1_CUTOFF + o] = clamp(vp_[P_F1_CUTOFF + o] * fastExp2(6.0f * amt * src[SRC_ENV2]), 20.0f, 20000.0f);
    }

    // Filth: the mod wheel x Filth pushes Growl (cutoff, drive, positions, warps), filter drive and the character
    // oscillator's drive all at once
    const float filth = ctx.modWheel * p[P_FILTH];
    if (filth > 0.0f)
    {
        vp_[P_RS_GROWL] = clamp(vp_[P_RS_GROWL] + filth, 0.0f, 1.0f);
        vp_[P_F1_DRIVE] = clamp(vp_[P_F1_DRIVE] + 0.3f * filth, 0.0f, 1.0f);
        vp_[P_CO_DRIVE] = clamp(vp_[P_CO_DRIVE] + 0.8f * filth, 0.0f, 1.0f);
    }

    // Reese growl: one amount moves cutoff, drive, both wavetable positions and both first warps together
    const float g = vp_[P_RS_GROWL];
    if (g > 0.0f)
    {
        vp_[P_F1_CUTOFF] = clamp(vp_[P_F1_CUTOFF] * fastExp2(g * vp_[P_RS_G_CUT]), 20.0f, 20000.0f);
        vp_[P_F1_DRIVE] = clamp(vp_[P_F1_DRIVE] + g * vp_[P_RS_G_DRIVE], 0.0f, 1.0f);
        const float dp = g * vp_[P_RS_G_POS], dw = g * vp_[P_RS_G_WARP];
        vp_[P_A_POS] = clamp(vp_[P_A_POS] + dp, 0.0f, 1.0f);
        vp_[P_B_POS] = clamp(vp_[P_B_POS] + dp, 0.0f, 1.0f);
        vp_[P_A_W1_AMT] = clamp(vp_[P_A_W1_AMT] + dw, 0.0f, 1.0f);
        vp_[P_B_W1_AMT] = clamp(vp_[P_B_W1_AMT] + dw, 0.0f, 1.0f);
    }

    // Reese drift: a slow random walk per voice; Osc A goes up by it and Osc B down
    const float drift = vp_[P_RS_DRIFT];
    if (drift > 0.0f)
    {
        const float rate = clamp(vp_[P_RS_DRIFT_RATE], 0.01f, 8.0f);
        driftPhase_ += rate * (float)n / sr_;
        if (driftPhase_ >= 1.0f || fresh_)
        {
            driftPhase_ = fresh_ ? 0.0f : driftPhase_ - std::floor(driftPhase_);
            driftFrom_ = fresh_ ? rng_.bi() : driftTo_;
            driftTo_ = rng_.bi();
        }
        const float u = driftPhase_ * driftPhase_ * (3.0f - 2.0f * driftPhase_);
        const float c = drift * 0.25f * (driftFrom_ + (driftTo_ - driftFrom_) * u);   // semitones (25 cents max)
        pitchModA_ += c;
        pitchModB_ -= c;
    }
}

// ------------------------------------------------------------------------------------------- render

// sin(2 pi p) for p in 0..1 (parabola plus one refinement: ~0.1% error, cheap on the MPC's ARM)
static inline float sine01(float p)
{
    const float x = kTwoPi * p - 3.14159265f;   // -pi..pi; sin(2 pi p) = -sin(x)
    float y = 1.27323954f * x - 0.40528473f * x * std::fabs(x);
    y = 0.225f * (y * std::fabs(y) - y) + y;
    return -y;
}

void Voice::renderOsc(const EngineContext& ctx, int s, const float* other, int n)
{
    const float* p = vp_;
    const int o = s == 0 ? 0 : (P_B_ON - P_A_ON);
    bufClear(oscL_[s], n);
    bufClear(oscR_[s], n);
    if (p[P_A_ON + o] < 0.5f) { bufClear(oscM_[s], n); return; }

    OscBlock b;
    const float semis = pitch_ + p[P_A_OCT + o] * 12.0f + p[P_A_SEMI + o] + p[P_A_FINE + o] * 0.01f +
                        ctx.pitchBendSemis + pitchMod_ + (s == 0 ? pitchModA_ : pitchModB_);
    b.freqHz = noteToHz(clamp(semis, -24.0f, 150.0f));
    b.pos1 = p[P_A_POS + o];
    b.level1 = p[P_A_LEVEL + o];
    b.warp[0] = (Warp)(int)p[P_A_W1_MODE + o];
    b.warp[1] = (Warp)(int)p[P_A_W2_MODE + o];
    b.warpAmt1[0] = p[P_A_W1_AMT + o];
    b.warpAmt1[1] = p[P_A_W2_AMT + o];
    if (fresh_) { lastPos_[s] = b.pos1; lastLevel_[s] = b.level1; lastWarp_[s][0] = b.warpAmt1[0]; lastWarp_[s][1] = b.warpAmt1[1]; }
    b.pos0 = lastPos_[s];
    b.level0 = lastLevel_[s];
    b.warpAmt0[0] = lastWarp_[s][0];
    b.warpAmt0[1] = lastWarp_[s][1];
    b.pan = clamp(p[P_A_PAN + o] + panMod_, -1.0f, 1.0f);
    b.unison = std::min((int)p[P_A_UNISON + o], ctx.unisonCap);
    b.detune = p[P_A_DETUNE + o];
    b.blend = p[P_A_BLEND + o];
    b.spread = p[P_A_SPREAD + o];
    b.crossfade = ctx.mipCrossfade;
    b.oversample = ctx.oversample;

    OscModInputs mod;
    mod.other = other;
    mod.sub = sub_buf_;
    mod.noise = noise_buf_;
    const int bit = s == 0 ? 1 : 2;   // target choices: 1 = Osc A, 2 = Osc B, 3 = both
    if (moOn_)
    {
        if ((int)p[P_MO_PM_TGT] & bit) { mod.pm = mo_buf_; mod.pmAmt = p[P_MO_PM_AMT]; }
        if ((int)p[P_MO_POS_TGT] & bit) { mod.posMod = mo_buf_; mod.posAmt = p[P_MO_POS_AMT]; }
    }
    osc_[s].render(ctx.table[s], b, mod, oscL_[s], oscR_[s], oscM_[s], n);
    if (moOn_ && ((int)p[P_MO_RM_TGT] & bit))
    {
        // Ring mod: crossfade from the dry oscillator to oscillator x mod osc
        const float a = clamp(p[P_MO_RM_AMT], 0.0f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float g = 1.0f - a + a * mo_buf_[i];
            oscL_[s][i] *= g; oscR_[s][i] *= g; oscM_[s][i] *= g;
        }
    }

    lastPos_[s] = b.pos1; lastLevel_[s] = b.level1;
    lastWarp_[s][0] = b.warpAmt1[0]; lastWarp_[s][1] = b.warpAmt1[1];
}

void Voice::renderFilter(int f, float* bl, float* br, float cutoffHz, int n)
{
    const float* vp = vp_;
    const int o = f * (P_F2_ON - P_F1_ON);
    const int mo = f * (P_F2_MODEL - P_F1_MODEL);
    const int model = (int)vp[P_F1_MODEL + mo];
    if (model <= 0)
    {
        Filter& flt = filt_[f];
        flt.setType((FilterType)(int)vp[P_F1_TYPE + o]);
        flt.update(cutoffHz, vp[P_F1_RES + o], vp[P_F1_DRIVE + o], vp[P_F1_FAT + o]);
        flt.processBlock(bl, br, n);
    }
    else
    {
        // Fat = the Mega Synth's analogue Warmth
        mega::MegaFilter& mf = mfilt_[f];
        mf.configure(model - 1, (int)vp[P_F1_KIND + mo], (int)vp[P_F1_SLOPE + mo] - 1);
        mf.update(cutoffHz, vp[P_F1_RES + o], vp[P_F1_DRIVE + o], vp[P_F1_FAT + o], sr_);
        mf.processBlock(bl, br, n);
    }
}

void Voice::render(const EngineContext& ctx, float* l, float* r, int n)
{
    const float* p = ctx.p;
    amp_.set(p[P_E1_A], p[P_E1_D], p[P_E1_S], p[P_E1_R], p[P_E1_AC], p[P_E1_DC], p[P_E1_RC]);

    // Glide (portamento): exponential approach, ~95% of the way after the glide time.
    if (glide_ && p[P_GLIDE] > 0.0f)
    {
        const float tau = p[P_GLIDE] * 0.001f / 3.0f * sr_;
        pitch_ += ((float)note_ - pitch_) * (1.0f - std::exp(-(float)n / std::max(tau, 1.0f)));
        if (std::fabs((float)note_ - pitch_) < 0.001f) pitch_ = (float)note_;
    }
    else pitch_ = (float)note_;

    computeModulation(ctx, n);
    const float* vp = vp_;

    if (fresh_)
    {
        for (int s = 0; s < 2; ++s)
        {
            const int o = s == 0 ? 0 : (P_B_ON - P_A_ON);
            // Phase Lock: every note starts at phase 0 with no random spread (the same punch on every hit)
            if (vp[P_RS_LOCK] >= 0.5f) osc_[s].start(0.0f, 0.0f, rng_);
            else osc_[s].start(vp[P_A_PHASE + o], vp[P_A_RAND + o], rng_);
        }
        sub_.start(0.0f);
        noise_.start(rng_, vp[P_NOISE_LOOP] >= 0.5f);
    }

    const float bend = ctx.pitchBendSemis + pitchMod_;
    if (vp[P_SUB_ON] >= 0.5f)
    {
        const float div = vp[P_SUB_OCT] >= 0.5f ? 24.0f : 12.0f;
        sub_.render((int)vp[P_SUB_SHAPE], noteToHz(clamp(pitch_ - div + bend, -24.0f, 140.0f)), vp[P_SUB_LEVEL], sub_buf_, n);
    }
    else bufClear(sub_buf_, n);
    if (vp[P_NOISE_ON] >= 0.5f)
    {
        const float ratio = vp[P_NOISE_PITCH] >= 0.5f ? fastExp2((pitch_ - 60.0f + bend) / 12.0f) : 1.0f;
        noise_.render((int)vp[P_NOISE_TYPE], ratio, vp[P_NOISE_LOOP] >= 0.5f, vp[P_NOISE_LEVEL], noise_buf_, n);
    }
    else bufClear(noise_buf_, n);

    // Character oscillator (Hoover / Dread Bass / Rollers engines)
    const int coType = (int)vp[P_CO_TYPE];
    if (coType > 0)
    {
        CharKnobs k;
        k.timbre = clamp(vp[P_CO_TIMBRE], 0.0f, 1.0f);
        k.character = clamp(vp[P_CO_CHAR], 0.0f, 1.0f);
        k.drive = clamp(vp[P_CO_DRIVE], 0.0f, 1.0f);
        k.sub = vp[P_CO_SUB];
        k.env = vp[P_CO_ENV];
        k.time = vp[P_CO_TIME];
        co_.render(coType, clamp(pitch_ + bend + vp[P_CO_SEMI], 0.0f, 127.0f), k, co_buf_, n);
        bufScale(co_buf_, std::max(0.0f, vp[P_CO_LEVEL]), n);
    }

    // Modulation oscillator: a sine at the note's pitch + MO Pitch (follows glide and bend)
    moOn_ = vp[P_MO_ON] >= 0.5f;
    if (moOn_)
    {
        if (fresh_) moPhase_ = 0.0f;
        const float inc = clamp(noteToHz(clamp(pitch_ + bend + vp[P_MO_PITCH], -24.0f, 150.0f)) / sr_, 0.0f, 0.49f);
        float ph = moPhase_;
        for (int i = 0; i < n; ++i)
        {
            mo_buf_[i] = sine01(ph);
            ph += inc;
            if (ph >= 1.0f) ph -= 1.0f;
        }
        moPhase_ = ph;
    }

    const bool aNeedsB = warpUsesOther(vp[P_A_W1_MODE]) || warpUsesOther(vp[P_A_W2_MODE]);
    const bool bNeedsA = warpUsesOther(vp[P_B_W1_MODE]) || warpUsesOther(vp[P_B_W2_MODE]);
    if (bNeedsA && !aNeedsB)
    {
        renderOsc(ctx, 0, nullptr, n);
        renderOsc(ctx, 1, oscM_[0], n);
    }
    else
    {
        renderOsc(ctx, 1, bNeedsA ? prevM_[0] : nullptr, n);
        renderOsc(ctx, 0, oscM_[1], n);
    }
    std::memcpy(prevM_[0], oscM_[0], sizeof(float) * (size_t)n);
    std::memcpy(prevM_[1], oscM_[1], sizeof(float) * (size_t)n);

    // Sources -> filter buses (per-source sends), sub direct-out past both filters.
    for (int f = 0; f < 2; ++f) { bufClear(busL_[f], n); bufClear(busR_[f], n); }
    bufClear(directL_, n); bufClear(directR_, n);
    const int sendOff = P_A_TO_F2 - P_A_TO_F1;
    for (int s = 0; s < 2; ++s)
    {
        const int o = s == 0 ? 0 : (P_B_ON - P_A_ON);
        for (int f = 0; f < 2; ++f)
        {
            const float send = vp[P_A_TO_F1 + o + f * sendOff];
            if (send <= 0.0f) continue;
            bufMixIn(busL_[f], oscL_[s], send, n);
            bufMixIn(busR_[f], oscR_[s], send, n);
        }
    }
    auto panGains = [](float pan, float& gl, float& gr) {
        pan = clamp(pan, -1.0f, 1.0f);
        gl = std::sqrt(0.5f * (1.0f - pan)) * 1.41421356f;
        gr = std::sqrt(0.5f * (1.0f + pan)) * 1.41421356f;
    };
    if (vp[P_SUB_ON] >= 0.5f)
    {
        float gl, gr; panGains(vp[P_SUB_PAN] + panMod_, gl, gr);
        if (vp[P_SUB_DIRECT] >= 0.5f) { bufMixIn(directL_, sub_buf_, gl, n); bufMixIn(directR_, sub_buf_, gr, n); }
        else
            for (int f = 0; f < 2; ++f)
            {
                const float send = f == 0 ? vp[P_SUB_TO_F1] : vp[P_SUB_TO_F2];
                bufMixIn(busL_[f], sub_buf_, gl * send, n);
                bufMixIn(busR_[f], sub_buf_, gr * send, n);
            }
    }
    if (coType > 0)
    {
        float gl, gr; panGains(vp[P_CO_PAN] + panMod_, gl, gr);
        for (int f = 0; f < 2; ++f)
        {
            const float send = f == 0 ? vp[P_CO_TO_F1] : vp[P_CO_TO_F2];
            if (send <= 0.0f) continue;
            bufMixIn(busL_[f], co_buf_, gl * send, n);
            bufMixIn(busR_[f], co_buf_, gr * send, n);
        }
    }
    if (vp[P_NOISE_ON] >= 0.5f)
    {
        float gl, gr; panGains(vp[P_NOISE_PAN] + panMod_, gl, gr);
        for (int f = 0; f < 2; ++f)
        {
            const float send = f == 0 ? vp[P_NOISE_TO_F1] : vp[P_NOISE_TO_F2];
            bufMixIn(busL_[f], noise_buf_, gl * send, n);
            bufMixIn(busR_[f], noise_buf_, gr * send, n);
        }
    }

    // Filters. Blend 0 = parallel (F1 + F2), 1 = serial (F1 -> F2).
    const float blend = clamp(vp[P_FILTER_BLEND], 0.0f, 1.0f);
    const int fOff = P_F2_ON - P_F1_ON;
    for (int f = 0; f < 2; ++f)
    {
        const int o = f * fOff;
        float* bl = busL_[f];
        float* br = busR_[f];
        if (f == 1 && blend > 0.0f) { bufMixIn(bl, busL_[0], blend, n); bufMixIn(br, busR_[0], blend, n); }
        if (vp[P_F1_ON + o] >= 0.5f)
        {
            const float kt = vp[P_F1_KEYTRK + o] * (pitch_ - 60.0f) / 12.0f;
            const float cut = vp[P_F1_CUTOFF + o] * fastExp2(kt);
            const bool fm = moOn_ && ((int)vp[P_MO_FM_TGT] & (f == 0 ? 1 : 2)) && vp[P_MO_FM_AMT] > 0.0f;
            if (!fm) renderFilter(f, bl, br, cut, n);
            else
            {
                // Filter FM: the cutoff follows the mod osc (up to +-4 octaves), updated every 4 samples
                const float oct = 4.0f * vp[P_MO_FM_AMT];
                for (int i = 0; i < n; i += 4)
                {
                    const int m = std::min(4, n - i);
                    renderFilter(f, bl + i, br + i, clamp(cut * fastExp2(oct * mo_buf_[i]), 20.0f, 20000.0f), m);
                }
            }
            bufScale(bl, vp[P_F1_LEVEL + o], n);
            bufScale(br, vp[P_F1_LEVEL + o], n);
        }
    }
    bufScale(busL_[0], 1.0f - blend, n);
    bufScale(busR_[0], 1.0f - blend, n);
    bufMixIn(busL_[0], busL_[1], 1.0f, n);
    bufMixIn(busR_[0], busR_[1], 1.0f, n);
    const float* outL = busL_[0];
    const float* outR = busR_[0];

    // Amp: envelope x velocity x Amp modulation, plus the steal fade.
    const float velGain = 1.0f - p[P_VEL_SENS] * (1.0f - vel_);
    const float ampMul = std::max(0.0f, 1.0f + ampMod_) * velGain;
    const float g0 = fresh_ ? ampMul : lastAmpGain_, dg = (ampMul - g0) / (float)n;
    float g = g0;
    const float stealStep = 1.0f / (float)stealLen_;
    for (int i = 0; i < n; ++i)
    {
        float a = amp_.process() * g;
        if (stealing_)
        {
            a *= (float)stealLeft_ * stealStep;
            if (stealLeft_ > 0) --stealLeft_;
        }
        l[i] += (outL[i] + directL_[i]) * a;
        r[i] += (outR[i] + directR_[i]) * a;
        g += dg;
    }
    lastAmpGain_ = ampMul;
    fresh_ = false;

    if (stealing_ && stealLeft_ <= 0)
    {
        stealing_ = false;
        const bool stillHeld = held_;
        startNow(pending_);
        if (!stillHeld) release();
    }
}

} // namespace helix
