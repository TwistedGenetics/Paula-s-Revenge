/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the filter types (see cr_filter.h). Topology-preserving (trapezoidal) state-variable filters and
 * one-poles (Zavalishin / Simper), a ladder with saturating global feedback, all-passes and a comb; no libm.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_filter.h"
#include "cr_math.h"

#define FS 44100.f

/* vowels (male): three formants each, Hz, and their gains */
static const float VOWEL[5][3] = {{730.f, 1090.f, 2440.f}, {530.f, 1840.f, 2480.f}, {270.f, 2290.f, 3010.f},
                                  {570.f, 840.f, 2410.f}, {300.f, 870.f, 2240.f}};
static const float VGAIN[3] = {1.f, 0.55f, 0.3f};

static float fclampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
/* 0.20: where the ladder's loop starts to ring by itself, for its stages' G = 0, 0.1 .. 0.9 (its feedback is a sample
 * late, so the higher the cutoff the less resonance it takes; above it the ring heads for half the sample rate, a
 * buzz at each note's filter sweep). Resonance is scaled by it, so it behaves at every cutoff as it does low down. */
static const float LAD_KCRIT[10] = {3.646f, 3.23f, 2.741f, 2.344f, 2.025f, 1.766f, 1.553f, 1.377f, 1.228f, 1.103f};
static float lad_kscale(float G)
{
    float x = fclampf(G, 0.f, 0.9f) * 10.f;
    uint32_t i = (uint32_t)x;
    float a, b;
    if (i > 8u)
        i = 8u;
    a = LAD_KCRIT[i];
    b = LAD_KCRIT[i + 1u];
    return (a + (b - a) * (x - (float)i)) * (1.f / 3.646f);
}
static float tang(float hz) { return cr_tanf(3.14159265f * fclampf(hz, 10.f, 0.45f * FS) / FS); }

void cr_filt_init(cr_filt_t *f, int16_t *comb_mem)
{
    uint8_t *b = (uint8_t *)f;
    uint32_t i;
    for (i = 0; i < sizeof *f; i++)
        b[i] = 0;
    f->comb = comb_mem;
    for (i = 0; i < CR_COMB_LEN; i++)
        comb_mem[i] = 0;
    cr_filt_set(f, CR_F_SVF, 1000.f, 0.f, 0.f);
}

static void svf_coef(float g, float k, float *a1, float *a2, float *a3)
{
    *a1 = 1.f / (1.f + g * (g + k));
    *a2 = g * *a1;
    *a3 = g * *a2;
}

void cr_filt_set(cr_filt_t *f, uint32_t type, float hz, float res, float m)
{
    uint32_t i;
    if (type >= CR_NFTYPE)
        type = 0;
    if (type != f->type) {             /* a new type starts from silence in its states (no burst) */
        f->ic1 = f->ic2 = f->y4 = f->hp = f->apfb = f->comb_z = 0.f;
        for (i = 0; i < 4u; i++)
            f->s[i] = 0.f;
        for (i = 0; i < 3u; i++)
            f->fic1[i] = f->fic2[i] = 0.f;
        for (i = 0; i < 12u; i++)
            f->ap[i] = 0.f;
        f->type = type;
    }
    f->m = fclampf(m, 0.f, 1.f);
    res = fclampf(res, 0.f, 1.f);
    f->g = tang(hz);
    f->G = f->g / (1.f + f->g);
    switch (type) {
    case CR_F_LADDER:
    case CR_F_303:
        f->k = (type == CR_F_303 ? res * 3.4f : res * 4.1f) * lad_kscale(f->G);   /* the 303 never quite self-oscillates */
        f->drive = type == CR_F_303 ? 1.8f : 1.f;
        {   /* morph: four mixes of the stages, crossfaded (Xpander-style) */
            float x = f->m * 3.f;
            uint32_t seg = (uint32_t)x;
            float t = x - (float)(seg > 2u ? 2u : seg);
            if (seg > 2u)
                seg = 2u;
            for (i = 0; i < 4u; i++)
                f->w[i] = 0.f;
            f->w[seg] = 1.f - t;
            f->w[seg + 1u] = t;
        }
        break;
    case CR_F_FORMANT: {
        float x = f->m * 4.f, t, shift = cr_exp2f(cr_log2f(fclampf(hz, 20.f, 20000.f) / 1000.f) * 0.5f);   /* 1 kHz: natural */
        uint32_t v = (uint32_t)x;
        if (v > 3u)
            v = 3u;
        t = x - (float)v;
        for (i = 0; i < 3u; i++) {
            float fh = (VOWEL[v][i] + (VOWEL[v + 1u][i] - VOWEL[v][i]) * t) * shift;
            f->fg[i] = tang(fh);
            f->fk[i] = 0.35f - 0.3f * res;                     /* RESO narrows them */
            svf_coef(f->fg[i], f->fk[i], &f->fa1[i], &f->fa2[i], &f->fa3[i]);
            f->fgain[i] = VGAIN[i] * f->fk[i] * 2.2f;          /* each band about unity at its peak */
        }
        break;
    }
    case CR_F_DUAL: {
        float g2 = tang(hz * cr_exp2f(f->m * 3.f));
        f->k = 2.f * (1.f - 0.95f * res);
        svf_coef(f->g, f->k, &f->a1, &f->a2, &f->a3);
        f->fg[0] = g2;
        f->fk[0] = 0.25f + 0.6f * (1.f - res);
        svf_coef(g2, f->fk[0], &f->fa1[0], &f->fa2[0], &f->fa3[0]);
        break;
    }
    case CR_F_COMB:
        f->comb_d = fclampf(FS / fclampf(hz, 22.f, 20000.f), 2.f, (float)(CR_COMB_LEN - 3u));
        f->comb_fb = (f->m - 0.5f) * 2.f * 0.97f;
        f->comb_lp = 0.15f + 0.85f * res;                      /* RESO: brightness in the loop */
        break;
    case CR_F_PHASER: {
        float t = f->g;
        f->apc = (1.f - t) / (1.f + t);
        f->nap = 2u + 2u * (uint32_t)(f->m * 5.f + 0.5f);
        f->k = res * 0.9f;
        break;
    }
    case CR_F_SEM:
        f->k = 2.f * (1.f - 0.92f * res);
        svf_coef(f->g, f->k, &f->a1, &f->a2, &f->a3);
        break;
    case CR_F_DECIM:
        f->k = 2.f * (1.f - 0.985f * res);
        svf_coef(f->g, f->k, &f->a1, &f->a2, &f->a3);
        f->crush = cr_exp2f(15.f - 12.f * f->m);               /* levels: 16 bits -> 3 */
        break;
    default:                                                    /* the state-variable family */
        f->k = 2.f * (1.f - (type == CR_F_POLIVOKS ? 0.995f : 0.985f) * res);
        svf_coef(f->g, f->k, &f->a1, &f->a2, &f->a3);
        break;
    }
}

static float onepole(float *s, float G, float x)
{
    float v = (x - *s) * G, y = v + *s;
    *s = y + v;
    return y;
}

float cr_filt_run(cr_filt_t *f, float x)
{
    float m = f->m;
    switch (f->type) {
    case CR_F_LADDER:
    case CR_F_303: {
        float u, y1, y2, y3, y4, fbk = f->y4;
        if (f->type == CR_F_303) {                              /* the 303's high pass in the feedback */
            f->hp += 0.014f * (fbk - f->hp);
            fbk -= f->hp;
        }
        u = cr_tanh(x * f->drive - f->k * fbk);
        if (f->type == CR_F_303) {                              /* mismatched stages */
            y1 = onepole(&f->s[0], f->G * 1.08f, u);
            y2 = onepole(&f->s[1], f->G * 0.94f, y1);
            y3 = onepole(&f->s[2], f->G, y2);
            y4 = onepole(&f->s[3], f->G * 0.9f, y3);
            f->y4 = y4;
            return (f->w[0] * y4 + f->w[1] * y3 + f->w[2] * y2 + f->w[3] * 2.f * (y1 - y2)) * (1.f + f->k * 0.3f);
        }
        y1 = onepole(&f->s[0], f->G, u);
        y2 = onepole(&f->s[1], f->G, y1);
        y3 = onepole(&f->s[2], f->G, y2);
        y4 = onepole(&f->s[3], f->G, y3);
        f->y4 = y4;
        (void)y3;
        return (f->w[0] * y4 + f->w[1] * y2 + f->w[2] * 2.f * (y1 - y2) + f->w[3] * (u - 2.f * y1 + y2)) *
               (1.f + f->k * 0.35f);
    }
    case CR_F_FORMANT: {
        float y = 0.f;
        uint32_t i;
        for (i = 0; i < 3u; i++) {
            float v3 = x - f->fic2[i], v1 = f->fa1[i] * f->fic1[i] + f->fa2[i] * v3;
            float v2 = f->fic2[i] + f->fa2[i] * f->fic1[i] + f->fa3[i] * v3;
            f->fic1[i] = 2.f * v1 - f->fic1[i];
            f->fic2[i] = 2.f * v2 - f->fic2[i];
            y += v1 * f->fgain[i];
        }
        return y;
    }
    case CR_F_COMB: {
        float rp = (float)f->comb_w - f->comb_d, fr, a0, a1, d, y;
        uint32_t r0;
        if (rp < 0.f)
            rp += (float)CR_COMB_LEN;
        r0 = (uint32_t)rp;
        fr = rp - (float)r0;
        a0 = (float)f->comb[r0 & (CR_COMB_LEN - 1u)];
        a1 = (float)f->comb[(r0 + 1u) & (CR_COMB_LEN - 1u)];
        d = (a0 + (a1 - a0) * fr) * (1.f / 16384.f);
        f->comb_z += f->comb_lp * (d - f->comb_z);
        y = x + f->comb_fb * f->comb_z;
        {
            float wv = fclampf(y, -1.99f, 1.99f) * 16384.f;
            f->comb[f->comb_w] = (int16_t)wv;
        }
        f->comb_w = (f->comb_w + 1u) & (CR_COMB_LEN - 1u);
        return y * (1.f - 0.45f * (f->comb_fb < 0.f ? -f->comb_fb : f->comb_fb));
    }
    case CR_F_PHASER: {
        float u = x + f->k * f->apfb, c = f->apc;
        uint32_t i;
        for (i = 0; i < f->nap; i++) {                          /* first-order all-passes: y = -c u + z; z = u + c y */
            float y = -c * u + f->ap[i];
            f->ap[i] = u + c * y;
            u = y;
        }
        f->apfb = cr_tanh(u);
        return 0.5f * (x + u);
    }
    case CR_F_DUAL: {
        float v3 = x - f->ic2, v1 = f->a1 * f->ic1 + f->a2 * v3, v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3, w1, w2, w3;
        f->ic1 = 2.f * v1 - f->ic1;
        f->ic2 = 2.f * v2 - f->ic2;
        w3 = x - f->fic2[0];
        w1 = f->fa1[0] * f->fic1[0] + f->fa2[0] * w3;
        w2 = f->fic2[0] + f->fa2[0] * f->fic1[0] + f->fa3[0] * w3;
        f->fic1[0] = 2.f * w1 - f->fic1[0];
        f->fic2[0] = 2.f * w2 - f->fic2[0];
        (void)w2;
        return v2 + w1 * 0.8f;
    }
    default: {                                                  /* the state-variable family */
        float in = x, v3, v1, v2, hp, wl, wb, wh;
        if (f->type == CR_F_STEINER)
            in = cr_tanh(x * 1.6f + 0.25f * f->ic1) * 0.8f;     /* diode input, a little positive feedback */
        else if (f->type == CR_F_MS20)
            in = cr_tanh(x * 1.3f);
        v3 = in - f->ic2;
        v1 = f->a1 * f->ic1 + f->a2 * v3;
        v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
        f->ic1 = 2.f * v1 - f->ic1;
        f->ic2 = 2.f * v2 - f->ic2;
        switch (f->type) {
        case CR_F_MS20: f->ic1 = cr_tanh(f->ic1 * 0.8f) * 1.25f; break;           /* the screaming loop */
        case CR_F_POLIVOKS:                                                         /* clipping integrators */
            f->ic1 = fclampf(f->ic1, -0.9f, 0.9f);
            f->ic2 = fclampf(f->ic2, -1.1f, 1.1f);
            break;
        case CR_F_DECIM:
            f->ic1 = (float)(int32_t)(f->ic1 * f->crush) / f->crush;
            f->ic2 = (float)(int32_t)(f->ic2 * f->crush) / f->crush;
            return v2;
        default: break;
        }
        hp = in - f->k * v1 - v2;
        if (f->type == CR_F_SEM)                                /* low pass -> notch -> high pass */
            return (1.f - m) * v2 + m * hp;
        if (f->type == CR_F_POLIVOKS)                           /* low pass -> band pass */
            return (1.f - m) * v2 + m * v1 * 1.4f;
        wl = m < 0.5f ? 1.f - 2.f * m : 0.f;
        wh = m < 0.5f ? 0.f : 2.f * m - 1.f;
        wb = m < 0.5f ? 2.f * m : 2.f - 2.f * m;
        return wl * v2 + wb * v1 + wh * hp;
    }
    }
}
