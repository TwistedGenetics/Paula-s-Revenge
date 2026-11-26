/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: MASSIVE, Jamie's Super-Massive ("Horizon", his reverb / delay for the microKORG 2, logue SDK, repo
 * TwistedGenetics/Super-Massive) in the FX rack (0.16). Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 * Horizon's engine (core/horizon_engine.h, horizon_modes.h, horizon_sync.h) ported to C for the FM-1: one 8-line
 * feedback delay network; ten modes set its shape (each line's delay as a ratio of TIME, clean at WARP 0 and
 * incommensurate at 100, which line feeds which, how much the lines mix (a rotation from identity to an 8x8 Hadamard
 * as DENSITY rises), the input diffusers, how the stereo goes in and comes out, the loop's filters, modulation and
 * drive). FEEDBACK is the gain a pass of the longest line, the others matched so every line decays at the same rate;
 * a cubic soft clip in the loop holds the self-oscillating modes. Mode changes fade the wet out, switch, fade in.
 *
 * What changed for the FM-1:
 *   - it runs at half rate (22.05 kHz): the pair of input samples averaged, the output interpolated back up. Darker
 *     at the top (11 kHz), half the memory and half the work;
 *   - its memory is what the FX arena has left (the other slot's effect first), laid out for the mode: each line as
 *     long as its longest ratio needs, so TIME reaches as far as the memory allows (shown in ms on the FX page when
 *     less than asked). Horizon had a fixed 1 MB at 48 kHz (1.25 s); with DELAY in the other slot MASSIVE has 80 KB
 *     (HALL ~0.3 s, the echo modes ~0.45 s), alone 176 KB (HALL ~0.75 s, PONG ~1.1 s). Long tails come from FEEDBACK,
 *     as in Horizon;
 *   - scalar float instead of NEON vectors (the FM-1's FPU is single, no SIMD), the maths from X0X's fastmath;
 *   - each line and diffuser a ring of its own length, laid out end to end in the slot's memory;
 *   - no dry / wet MIX (a send effect returns the wet), TIME on 7 bits: 0..12 the synced divisions (1/32 .. 1/2D, a
 *     division too long for the lines halved until it fits, on the beat), 13..127 10 ms .. 1250 ms. */
#include "dn_fx.h"
#include "dsp/fastmath.h"

#define FS 22050.f
#define KQ 16384.f
#define KIQ (1.f / 16384.f)
#define SAT 1.8f

typedef struct {
    float clean[8], warped[8];
    int8_t src[8];
    float dmin, dmax, diffMax, diffScale;
    uint8_t inject, out;
    float fbMax, hpHz, lpHz, modRate, modDepthMs, drive, inGain, outGain;
    uint8_t onBeat;
} fxm_mode_t;
enum { INJ_ALL, INJ_BLOOM, INJ_PAIR, INJ_MONO0, INJ_PRISM };
enum { OUT_ALL, OUT_BLOOM, OUT_PAIRPLUS };

const char *const DN_FXM_MODE[DN_FXM_NMODE] = {"ROOM", "HALL", "HALO", "SWELL", "ABYSS", "ECHOVRB", "ORBIT", "PRISM",
                                                "PONG", "DUB"};
const float DN_FXM_BEATS[13] = {0.125f, 1.f / 6.f, 0.25f, 1.f / 3.f, 0.375f, 0.5f, 2.f / 3.f, 0.75f, 1.f, 4.f / 3.f,
                                1.5f, 2.f, 3.f};
static const fxm_mode_t FXM[DN_FXM_NMODE] = {
    /* ROOM: fast attack, dense, shortish; TIME the size */
    {{0.500f, 0.375f, 0.333f, 0.250f, 0.200f, 0.167f, 0.125f, 0.100f},
     {0.500f, 0.437f, 0.391f, 0.344f, 0.307f, 0.271f, 0.244f, 0.217f}, {1, 0, 3, 2, 5, 4, 7, 6},
     0.85f, 1.00f, 0.75f, 1.0f, INJ_ALL, OUT_ALL, 0.96f, 40.f, 6000.f, 0.8f, 0.4f, 1.0f, 0.5f, 2.2f, 0},
    /* HALL: clean, filtered, realistic; cross-channel pairs */
    {{1.000f, 0.750f, 0.667f, 0.500f, 0.375f, 0.333f, 0.250f, 0.188f},
     {1.000f, 0.873f, 0.781f, 0.687f, 0.613f, 0.541f, 0.487f, 0.433f}, {1, 0, 3, 2, 5, 4, 7, 6},
     0.75f, 1.00f, 0.70f, 1.6f, INJ_ALL, OUT_ALL, 0.995f, 40.f, 8500.f, 0.5f, 0.9f, 1.0f, 0.5f, 2.0f, 0},
    /* HALO: fast attack, sparse echoes that diffuse as they decay; unfiltered, lush */
    {{1.000f, 0.750f, 0.667f, 0.500f, 0.375f, 0.333f, 0.250f, 0.188f},
     {1.000f, 0.873f, 0.781f, 0.687f, 0.613f, 0.541f, 0.487f, 0.433f}, {0, 1, 2, 3, 4, 5, 6, 7},
     0.00f, 0.55f, 0.35f, 2.0f, INJ_ALL, OUT_ALL, 0.995f, 30.f, 0.f, 0.25f, 2.5f, 1.0f, 0.5f, 1.7f, 0},
    /* SWELL: a slow attack that blooms */
    {{1.000f, 0.970f, 0.500f, 0.490f, 0.750f, 0.740f, 0.250f, 0.245f},
     {0.930f, 1.000f, 0.710f, 0.660f, 0.830f, 0.780f, 0.590f, 0.550f}, {6, 7, 0, 1, 2, 3, 4, 5},
     0.20f, 0.85f, 0.65f, 2.5f, INJ_BLOOM, OUT_BLOOM, 0.995f, 40.f, 9000.f, 0.35f, 1.5f, 1.0f, 0.6f, 2.2f, 0},
    /* ABYSS: a very slow attack, a near-endless pad tail */
    {{1.000f, 0.990f, 0.750f, 0.740f, 0.875f, 0.860f, 0.625f, 0.610f},
     {0.970f, 1.000f, 0.890f, 0.850f, 0.940f, 0.910f, 0.810f, 0.770f}, {6, 7, 0, 1, 2, 3, 4, 5},
     0.40f, 1.00f, 0.75f, 5.0f, INJ_BLOOM, OUT_BLOOM, 1.010f, 50.f, 6500.f, 0.12f, 3.5f, 1.0f, 0.6f, 1.8f, 0},
    /* ECHOVERB: a clear echo at TIME that blooms into reverb with DENSITY */
    {{1.000f, 1.000f, 0.500f, 0.500f, 0.250f, 0.250f, 0.125f, 0.125f},
     {1.000f, 1.000f, 0.370f, 0.410f, 0.290f, 0.230f, 0.170f, 0.130f}, {0, 1, 3, 2, 5, 4, 7, 6},
     0.00f, 0.80f, 0.50f, 1.5f, INJ_PAIR, OUT_PAIRPLUS, 0.99f, 40.f, 9000.f, 0.4f, 1.0f, 1.0f, 0.7f, 1.0f, 1},
    /* ORBIT: looper-ish, repeats at TIME; DENSITY fills the gaps with haze */
    {{1.000f, 1.000f, 0.500f, 0.500f, 0.250f, 0.250f, 0.125f, 0.125f},
     {1.000f, 1.000f, 0.310f, 0.270f, 0.190f, 0.160f, 0.110f, 0.070f}, {0, 1, 2, 3, 4, 5, 6, 7},
     0.00f, 0.60f, 0.40f, 2.0f, INJ_PAIR, OUT_PAIRPLUS, 1.010f, 25.f, 0.f, 0.2f, 0.6f, 1.0f, 0.7f, 1.0f, 1},
    /* PRISM: each echo through all eight lines before it comes back */
    {{1.000f, 0.500f, 0.750f, 0.250f, 0.875f, 0.375f, 0.625f, 0.125f},
     {1.000f, 0.618f, 0.809f, 0.382f, 0.927f, 0.472f, 0.705f, 0.236f}, {7, 0, 1, 2, 3, 4, 5, 6},
     0.00f, 0.70f, 0.30f, 1.0f, INJ_PRISM, OUT_ALL, 0.99f, 30.f, 0.f, 0.3f, 0.5f, 1.0f, 0.7f, 1.1f, 1},
    /* PONG: a sparse stereo delay; WARP offsets the right */
    {{1.000f, 1.000f, 0.500f, 0.500f, 0.250f, 0.250f, 0.125f, 0.125f},
     {1.000f, 0.750f, 0.330f, 0.290f, 0.210f, 0.170f, 0.120f, 0.090f}, {1, 0, 3, 2, 5, 4, 7, 6},
     0.00f, 0.60f, 0.30f, 0.8f, INJ_MONO0, OUT_PAIRPLUS, 0.99f, 60.f, 7000.f, 0.3f, 0.4f, 1.0f, 0.8f, 1.0f, 1},
    /* DUB: band-limited, saturating; self-oscillates near the top of FEEDBACK */
    {{1.000f, 0.750f, 0.500f, 0.375f, 0.250f, 0.188f, 0.125f, 0.094f},
     {1.000f, 0.667f, 0.410f, 0.330f, 0.230f, 0.170f, 0.130f, 0.090f}, {0, 1, 3, 2, 5, 4, 7, 6},
     0.00f, 0.50f, 0.25f, 0.8f, INJ_PAIR, OUT_PAIRPLUS, 1.12f, 120.f, 3200.f, 0.45f, 1.2f, 2.5f, 0.7f, 1.0f, 1},
};
static const float FXM_DIFF_MS[8] = {4.77f, 3.59f, 12.73f, 9.31f, 5.21f, 3.97f, 11.83f, 8.71f};
static const float FXM_SPREAD[8] = {1.f, 0.83f, 0.91f, 0.74f, 0.97f, 0.79f, 0.88f, 0.69f};

uint32_t dn_fxm_ram(void) { return 64u * 1024u; }   /* at least: the rest of the arena if there is more */

void dn_fxm_init(dn_fx_s_t *s)
{
    dn_fxm_t *m = &s->u.m;
    m->mem = (int16_t *)s->mem;
    m->cap = s->bytes / 2u;
    m->mode = m->pend = 1;
    m->inited = 0;
}

static float fxm_a(float hz)                        /* a one-pole's coefficient at hz */
{
    if (hz > 10000.f)
        hz = 10000.f;
    return 1.f - fm_expf(-6.2831853f * hz / FS);
}

/* the mode's memory: the diffusers (4 a side), then each line as long as its longest ratio needs; tmax from what's
 * left. The whole ring cleared (a mode change, or the start). */
static void fxm_layout(dn_fxm_t *m)
{
    const fxm_mode_t *md = &FXM[m->mode];
    uint32_t k, at = 0, cap = m->cap, margin = (uint32_t)(md->modDepthMs * FS * 0.001f) + 6u;
    float sum = 0.f, left;
    for (k = 0; k < cap; k++)
        m->mem[k] = 0;
    for (k = 0; k < DN_FXM_L; k++) {
        uint32_t l = (uint32_t)(FXM_DIFF_MS[k] * md->diffScale * FS * 0.001f);
        m->dlen[k] = l < 1u ? 1u : l;
        m->diff[k] = m->mem + at;                   /* each its own little ring */
        m->dreg[k] = m->dlen[k] + 1u;
        m->dw[k] = 0;
        at += m->dreg[k];
    }
    for (k = 0; k < DN_FXM_L; k++)
        sum += md->clean[k] > md->warped[k] ? md->clean[k] : md->warped[k];
    left = (float)(cap - at) - (float)(DN_FXM_L * (margin + 4u));
    m->tmax = left / sum;
    for (k = 0; k < DN_FXM_L; k++) {
        float r = md->clean[k] > md->warped[k] ? md->clean[k] : md->warped[k];
        m->llen[k] = (uint32_t)(r * m->tmax) + margin + 4u;
        m->line[k] = m->mem + at;
        m->lw[k] = 0;
        at += m->llen[k];
    }
}

static void fxm_matrix(dn_fxm_t *m)                /* the permutation then three butterfly rotations (Horizon's) */
{
    float th = m->blend * 0.78539816f, c = fm_cosf(th), s = fm_sinf(th), col[8][8];
    uint32_t i, r, len, a, j;
    for (i = 0; i < 64u; i++)
        col[i / 8u][i % 8u] = 0.f;
    for (i = 0; i < DN_FXM_L; i++) {
        float e[8] = {0};
        e[i] = 1.f;
        for (len = 1; len < 8u; len <<= 1)
            for (a = 0; a < 8u; a += len << 1)
                for (j = a; j < a + len; j++) {
                    float x = e[j], y = e[j + len];
                    e[j] = c * x + s * y;
                    e[j + len] = c * y - s * x;
                }
        for (r = 0; r < DN_FXM_L; r++)
            col[(uint32_t)m->src[i]][r] += e[r];
    }
    for (j = 0; j < DN_FXM_L; j++)
        for (r = 0; r < DN_FXM_L; r++)
            m->mat[j][r] = col[j][r];
    m->matBlend = m->blend;
}

static void fxm_apply_mode(dn_fxm_t *m)
{
    static const float SA[4] = {1.f, -1.f, 1.f, -1.f}, SB[4] = {1.f, 1.f, -1.f, -1.f};
    const fxm_mode_t *md = &FXM[m->mode];
    uint32_t k;
    for (k = 0; k < DN_FXM_L; k++) {
        m->src[k] = md->src[k];
        m->injL[k] = m->injR[k] = m->outL[k] = m->outR[k] = 0.f;
    }
    switch (md->inject) {
    case INJ_ALL:
        for (k = 0; k < 4u; k++)
            m->injL[2u * k] = 0.5f * SA[k], m->injR[2u * k + 1u] = 0.5f * SB[k];
        break;
    case INJ_MONO0:
        m->injL[0] = m->injR[0] = 0.5f;
        break;
    case INJ_PRISM:
        m->injL[0] = 1.f, m->injR[4] = 1.f;
        break;
    default:                                        /* BLOOM, PAIR */
        m->injL[0] = 1.f, m->injR[1] = 1.f;
        break;
    }
    switch (md->out) {
    case OUT_ALL:
        for (k = 0; k < 4u; k++)
            m->outL[2u * k] = 0.5f * SB[k], m->outR[2u * k + 1u] = 0.5f * SA[k];
        break;
    case OUT_BLOOM: {
        static const float W[4] = {0.12f, 0.35f, 0.65f, 1.0f};
        for (k = 0; k < 4u; k++)
            m->outL[2u * k] = W[k] * 0.75f * SA[k], m->outR[2u * k + 1u] = W[k] * 0.75f * SB[k];
        break;
    }
    default:
        m->outL[0] = m->outR[1] = 0.85f;
        for (k = 1; k < 4u; k++)
            m->outL[2u * k] = 0.25f * SA[k], m->outR[2u * k + 1u] = 0.25f * SB[k];
        break;
    }
    m->satIn = md->drive / SAT;
    m->satOut = SAT / md->drive;
    m->inGain = md->inGain;
    m->outGain = md->outGain;
    m->last_bpm = 0;                                /* (the targets again) */
    fxm_layout(m);
}

/* TIME as it plays: half-rate samples */
static float fxm_T(const dn_fxm_t *m, uint32_t v, uint32_t bpm, float tmax)
{
    float T;
    (void)m;
    if (v < 13u) {
        T = DN_FXM_BEATS[v] * 60.f / (float)(bpm ? bpm : 120u) * FS;
        while (T > tmax && T > 8.f)
            T *= 0.5f;                              /* halved until it fits: still on the beat */
    } else {
        T = 10.f * fm_powf(125.f, (float)(v - 13u) * (1.f / 114.f)) * FS * 0.001f;
        if (T > tmax)
            T = tmax;
    }
    return T < 10.f * FS * 0.001f ? 10.f * FS * 0.001f : T;
}

float dn_fxm_time_ms(const dn_fx_s_t *s, const dn_fx_p_t *p, uint32_t bpm)
{
    float tmax = s && s->type == DN_FX_MASSIVE && s->u.m.inited ? s->u.m.tmax : 1.25f * FS;
    return fxm_T(0, p->p[1], bpm, tmax) * (1000.f / FS);
}

static void fxm_targets(dn_fxm_t *m, const dn_fx_p_t *p, uint32_t bpm)
{
    const fxm_mode_t *md = &FXM[m->mode];
    float T = fxm_T(m, p->p[1], bpm, m->tmax), fbk = (float)p->p[2] * (1.f / 127.f), warp = (float)p->p[3] * (1.f / 127.f);
    float dens = (float)p->p[4] * (1.f / 127.f), mod = (float)p->p[5] * (1.f / 127.f);
    float tone = ((float)p->p[6] - 64.f) * (1.f / 63.f), k = 1.f - fbk, fb, lref = 4.f, rate, rc, depth, lph, llp;
    uint32_t i;
    if (tone > 1.f)
        tone = 1.f;
    if (tone < -1.f)
        tone = -1.f;
    fb = md->fbMax * (1.f - k * fm_sqrtf(k));       /* more resolution near the top */
    for (i = 0; i < DN_FXM_L; i++) {
        float r = md->clean[i] + warp * (md->warped[i] - md->clean[i]), l = T * r;
        if (l < 4.f)
            l = 4.f;
        m->lenT[i] = l;
        if (l > lref)
            lref = l;
    }
    for (i = 0; i < DN_FXM_L; i++)                  /* the same decay rate on every line */
        m->gT[i] = fb <= 0.f ? 0.f : fm_powf(fb, m->lenT[i] / lref);
    m->blendT = md->dmin + dens * (md->dmax - md->dmin);
    m->diffGT = md->diffMax * dens;
    m->diffMixT = md->onBeat ? dens : 1.f;
    rate = md->modRate * (0.5f + mod);
    rc = rate > 0.6f ? 0.6f / rate : 1.f;
    depth = mod * mod * md->modDepthMs * FS * 0.001f * rc;
    for (i = 0; i < DN_FXM_L; i++) {
        float dd = depth * FXM_SPREAD[i];
        if (dd > m->lenT[i] - 2.f)
            dd = m->lenT[i] - 2.f;
        m->md[i] = dd < 0.f ? 0.f : dd;
    }
    m->k0 = 6.2831853f * rate / FS;
    m->k1 = m->k0 * 1.37f;
    m->olpA = fxm_a(tone < 0.f ? 18000.f * fm_powf(600.f / 18000.f, -tone) : 18000.f);
    m->ohpA = fxm_a(tone > 0.f ? 20.f * fm_powf(60.f, tone) : 20.f);
    llp = md->lpHz;
    if (tone < 0.f)
        llp *= fm_powf(0.15f, -tone);
    lph = md->hpHz * (tone > 0.f ? fm_powf(8.f, tone) : 1.f);
    if (lph > 800.f)
        lph = 800.f;
    m->lpA = md->lpHz > 0.f ? fxm_a(llp) : 1.f;
    m->hpA = fxm_a(lph);
    m->glide = 1.f - fm_expf(-1.f / (0.12f * FS));
}

static inline float fxm_clip(float x)               /* Horizon's cubic: unity at 0, flat at +-1.5 */
{
    x = x > 1.5f ? 1.5f : x < -1.5f ? -1.5f : x;
    return x - 0.148148148f * x * x * x;
}
static inline float fxm_flush(float x) { return x < 1e-15f && x > -1e-15f ? 0.f : x; }

/* one half-rate sample: in (L, R, float, +-1 full scale) -> *ol, *or */
static inline void fxm_tick(dn_fxm_t *m, float dryL, float dryR, float dmix, float fade, float *ol, float *orr)
{
    uint32_t i, j;
    float inL = dryL * m->inGain, inR = dryR * m->inGain, g = m->diffG, dL = inL, dR = inR;
    float rd[8], x[8], f[8], mo[4], wetL = 0.f, wetR = 0.f;
    /* the input diffusion: 4 allpasses a side */
    for (j = 0; j < 4u; j++) {
        uint32_t kl = j, kr = j + 4u;
        uint32_t il = m->dw[kl], ir = m->dw[kr];
        uint32_t rl = il + m->dreg[kl] - m->dlen[kl], rr = ir + m->dreg[kr] - m->dlen[kr];
        float zl = (float)m->diff[kl][rl >= m->dreg[kl] ? rl - m->dreg[kl] : rl] * KIQ;
        float zr = (float)m->diff[kr][rr >= m->dreg[kr] ? rr - m->dreg[kr] : rr] * KIQ;
        float yl = zl - g * dL, yr = zr - g * dR;
        float vl = (dL + g * yl) * KQ, vr = (dR + g * yr) * KQ;
        m->diff[kl][il] = (int16_t)(vl > 32767.f ? 32767.f : vl < -32768.f ? -32768.f : vl);
        m->diff[kr][ir] = (int16_t)(vr > 32767.f ? 32767.f : vr < -32768.f ? -32768.f : vr);
        dL = yl, dR = yr;
        m->dw[kl] = il + 1u >= m->dreg[kl] ? 0u : il + 1u;
        m->dw[kr] = ir + 1u >= m->dreg[kr] ? 0u : ir + 1u;
    }
    dL = inL + (dL - inL) * dmix;
    dR = inR + (dR - inR) * dmix;
    /* the modulation: two quadrature LFOs, eight phases */
    m->s0 += m->k0 * m->c0, m->c0 -= m->k0 * m->s0;
    m->s1 += m->k1 * m->c1, m->c1 -= m->k1 * m->s1;
    mo[0] = m->s0, mo[1] = m->s1, mo[2] = m->c0, mo[3] = m->c1;
    /* the lines, read where they've glided to */
    for (i = 0; i < DN_FXM_L; i++) {
        float d, fr, a, c;
        uint32_t di, r0, r1, L = m->llen[i], wi = m->lw[i];
        m->len[i] += (m->lenT[i] - m->len[i]) * m->glide;
        d = m->len[i] + (i < 4u ? mo[i] : -mo[i - 4u]) * m->md[i];
        if (d < 1.f)
            d = 1.f;
        di = (uint32_t)d;
        fr = d - (float)di;
        r0 = wi + L - di;                           /* (written di samples ago) */
        r0 = r0 >= L ? r0 - L : r0;
        r1 = r0 ? r0 - 1u : L - 1u;
        a = (float)m->line[i][r0], c = (float)m->line[i][r1];
        rd[i] = (a + (c - a) * fr) * KIQ;
        wetL += m->outL[i] * rd[i];
        wetR += m->outR[i] * rd[i];
        x[i] = rd[i] * m->g[i];
        m->hp[i] += (x[i] - m->hp[i]) * m->hpA;
        x[i] -= m->hp[i];
        m->lp[i] += (x[i] - m->lp[i]) * m->lpA;
        x[i] = m->lp[i];
    }
    /* the feedback matrix, the input, the soft clip, written */
    for (i = 0; i < DN_FXM_L; i++) {
        float v = m->injL[i] * dL + m->injR[i] * dR;
        for (j = 0; j < DN_FXM_L; j++)
            v += m->mat[j][i] * x[j];
        f[i] = fxm_clip(v * m->satIn) * m->satOut;
        m->line[i][m->lw[i]] = (int16_t)(f[i] * KQ);   /* |f| <= 1.8: inside int16 */
        m->lw[i] = m->lw[i] + 1u >= m->llen[i] ? 0u : m->lw[i] + 1u;
    }
    /* the output tone */
    m->ohp[0] += (wetL - m->ohp[0]) * m->ohpA;
    m->ohp[1] += (wetR - m->ohp[1]) * m->ohpA;
    wetL -= m->ohp[0];
    wetR -= m->ohp[1];
    m->olp[0] += (wetL - m->olp[0]) * m->olpA;
    m->olp[1] += (wetR - m->olp[1]) * m->olpA;
    *ol = m->olp[0] * m->outGain * fade;
    *orr = m->olp[1] * m->outGain * fade;
}

void dn_fxm_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *st, int32_t *wl, int32_t *wr, uint32_t n,
                uint32_t bpm)
{
    dn_fxm_t *m = &s->u.m;
    uint32_t i, k, hn = 0, mode = p->p[0] % DN_FXM_NMODE, changed = 0;
    float bs, dmix0, dstep;
    if (!m->inited) {
        m->mode = m->pend = (uint8_t)mode;
        fxm_apply_mode(m);
        fxm_targets(m, p, bpm);
        for (k = 0; k < DN_FXM_L; k++)
            m->len[k] = m->lenT[k], m->g[k] = m->gT[k];
        m->blend = m->blendT, m->diffG = m->diffGT, m->diffMix = m->diffMixT;
        fxm_matrix(m);
        m->s0 = 0.f, m->c0 = 1.f, m->s1 = 0.7071f, m->c1 = 0.7071f;
        m->fade = 1.f;
        for (k = 0; k < DN_FX_NP; k++)
            m->last[k] = p->p[k];
        m->last_bpm = (uint16_t)bpm;
        m->inited = 1;
    }
    if (mode != m->pend)                            /* a new mode: fade out (15 ms), switch, fade in (60 ms) */
        m->pend = (uint8_t)mode;
    for (k = 1; k < DN_FX_NP; k++)
        changed |= m->last[k] != p->p[k];
    if (changed || m->last_bpm != bpm) {
        for (k = 0; k < DN_FX_NP; k++)
            m->last[k] = p->p[k];
        m->last_bpm = (uint16_t)bpm;
        fxm_targets(m, p, bpm);
    }
    /* once a block: the smoothing (tau 20 ms) */
    for (i = 0; i < n; i++)
        hn += (m->ph + i) & 1u;
    bs = 1.f - fm_expf(-(float)(hn ? hn : 1u) / (0.02f * FS));
    for (k = 0; k < DN_FXM_L; k++)
        m->g[k] += (m->gT[k] - m->g[k]) * bs;
    m->blend += (m->blendT - m->blend) * bs;
    if (m->blend - m->matBlend > 1e-4f || m->matBlend - m->blend > 1e-4f)
        fxm_matrix(m);
    m->diffG += (m->diffGT - m->diffG) * bs;
    dmix0 = m->diffMix;
    m->diffMix += (m->diffMixT - m->diffMix) * bs;
    dstep = (m->diffMix - dmix0) / (float)(hn ? hn : 1u);
    for (i = 0; i < n; i++) {
        float xl = (float)st[2u * i] * (1.f / 32768.f), xr = (float)st[2u * i + 1u] * (1.f / 32768.f), oL, oR;
        if (!m->ph) {
            m->in_l = xl, m->in_r = xr;
            oL = m->y_l, oR = m->y_r;
        } else {
            float fade = m->fade;
            if (m->pend != m->mode) {
                fade -= 1.f / (0.015f * FS);
                if (fade <= 0.f) {                  /* faded out: the new mode, its memory cleared */
                    fade = 0.f;
                    m->mode = m->pend;
                    fxm_apply_mode(m);
                    fxm_targets(m, p, bpm);
                    for (k = 0; k < DN_FXM_L; k++) {
                        m->len[k] = m->lenT[k];
                        m->hp[k] = m->lp[k] = 0.f;
                    }
                    fxm_matrix(m);
                }
            } else if (fade < 1.f) {
                fade += 1.f / (0.060f * FS);
                if (fade > 1.f)
                    fade = 1.f;
            }
            m->fade = fade;
            m->yp_l = m->y_l, m->yp_r = m->y_r;
            fxm_tick(m, (m->in_l + xl) * 0.5f, (m->in_r + xr) * 0.5f, dmix0, fade, &m->y_l, &m->y_r);
            dmix0 += dstep;
            oL = (m->yp_l + m->y_l) * 0.5f, oR = (m->yp_r + m->y_r) * 0.5f;
        }
        m->ph ^= 1u;
        wl[i] = (int32_t)(oL * 32768.f);
        wr[i] = (int32_t)(oR * 32768.f);
    }
    for (k = 0; k < DN_FXM_L; k++)
        m->hp[k] = fxm_flush(m->hp[k]), m->lp[k] = fxm_flush(m->lp[k]);
    for (k = 0; k < 2u; k++)
        m->ohp[k] = fxm_flush(m->ohp[k]), m->olp[k] = fxm_flush(m->olp[k]);
    {                                               /* the LFOs' amplitude renormalised (magic-circle drift) */
        float a = m->s0 * m->s0 + m->c0 * m->c0, q = 1.5f - 0.5f * a;
        m->s0 *= q, m->c0 *= q;
        a = m->s1 * m->s1 + m->c1 * m->c1, q = 1.5f - 0.5f * a;
        m->s1 *= q, m->c1 *= q;
    }
}
