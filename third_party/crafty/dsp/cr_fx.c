/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the effects (see cr_fx.h). Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_fx.h"
#include "cr_math.h"

#define FS 44100.f

static float xclampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float soft3(float x)            /* cubic soft clip: unity at 0, flat at +-1.5 */
{
    x = xclampf(x, -1.5f, 1.5f);
    return x - 0.148148148f * x * x * x;
}

/* ---- EraCrush's machines: native rate, bits, mu (0 linear), output filter Hz (0: tracks the rate), its Q ---- */
static const float ERA[5][5] = {
    {26040.f, 12.f, 0.f, 11000.f, 0.62f},     /* SP-1200: gritty 12-bit linear, a biting output filter */
    {40000.f, 12.f, 18.f, 15000.f, 0.71f},    /* MPC60: companded 12-bit, smooth */
    {31250.f, 12.f, 0.f, 0.f, 0.71f},         /* S950: filters track the rate */
    {44100.f, 16.f, 0.f, 0.f, 0.71f},         /* S1000: clean, for rate tricks */
    {28867.f, 8.f, 0.f, 4400.f, 0.66f}};      /* Amiga (Paula): 8-bit, the LED filter */

/* ---- Horizon's modes, in small: clean and warped ratios, the feedback permutation, density (Hadamard blend)
 * at DENSITY 0 / 100, input diffusion, the most gain a pass, loop high / low pass, mod rate and depth (ms), drive,
 * bloom (input into lines 0-1 only, output from the late ones) ---- */
typedef struct {
    float clean[8], warped[8];
    int8_t src[8];
    float dmin, dmax, diff, fbmax, hp, lp, mrate, mdepth, drive;
    uint8_t bloom;
} vmode_t;
static const vmode_t VM[6] = {
    {{0.5f, 0.375f, 0.333f, 0.25f, 0.2f, 0.167f, 0.125f, 0.1f}, {0.5f, 0.437f, 0.391f, 0.344f, 0.307f, 0.271f, 0.244f, 0.217f},
     {1, 0, 3, 2, 5, 4, 7, 6}, 0.85f, 1.f, 0.75f, 0.96f, 40.f, 6000.f, 0.8f, 0.4f, 1.f, 0},          /* Room */
    {{1.f, 0.75f, 0.667f, 0.5f, 0.375f, 0.333f, 0.25f, 0.188f}, {1.f, 0.873f, 0.781f, 0.687f, 0.613f, 0.541f, 0.487f, 0.433f},
     {1, 0, 3, 2, 5, 4, 7, 6}, 0.75f, 1.f, 0.7f, 0.995f, 40.f, 8500.f, 0.5f, 0.9f, 1.f, 0},          /* Hall */
    {{1.f, 0.75f, 0.667f, 0.5f, 0.375f, 0.333f, 0.25f, 0.188f}, {1.f, 0.873f, 0.781f, 0.687f, 0.613f, 0.541f, 0.487f, 0.433f},
     {0, 1, 2, 3, 4, 5, 6, 7}, 0.f, 0.55f, 0.35f, 0.995f, 30.f, 16000.f, 0.25f, 2.f, 1.f, 0},        /* Halo */
    {{1.f, 0.97f, 0.5f, 0.49f, 0.75f, 0.74f, 0.25f, 0.245f}, {0.93f, 1.f, 0.71f, 0.66f, 0.83f, 0.78f, 0.59f, 0.55f},
     {6, 7, 0, 1, 2, 3, 4, 5}, 0.2f, 0.85f, 0.65f, 0.995f, 40.f, 9000.f, 0.35f, 1.5f, 1.f, 1},       /* Swell */
    {{1.f, 0.99f, 0.75f, 0.74f, 0.875f, 0.86f, 0.625f, 0.61f}, {0.97f, 1.f, 0.89f, 0.85f, 0.94f, 0.91f, 0.81f, 0.77f},
     {6, 7, 0, 1, 2, 3, 4, 5}, 0.4f, 1.f, 0.75f, 1.01f, 50.f, 6500.f, 0.12f, 2.5f, 1.f, 1},          /* Abyss */
    {{1.f, 1.f, 0.5f, 0.5f, 0.75f, 0.75f, 0.25f, 0.25f}, {1.f, 0.98f, 0.5f, 0.52f, 0.74f, 0.77f, 0.26f, 0.24f},
     {1, 0, 3, 2, 5, 4, 7, 6}, 0.f, 0.4f, 0.25f, 1.04f, 150.f, 3200.f, 0.3f, 0.6f, 2.2f, 0},         /* Dub */
};
static const uint16_t DIFF_LEN[4] = {142, 107, 379, 277};

void cr_fx_init(cr_fx_t *f, int16_t *mem)
{
    uint8_t *b = (uint8_t *)f;
    uint32_t i;
    for (i = 0; i < sizeof *f; i++)
        b[i] = 0;
    for (i = 0; i < CR_FX_MEM; i++)
        mem[i] = 0;
    f->line = mem;
    f->diff = mem + 8u * CR_VLINE;
    f->cho = f->diff + 4u * CR_VDIFF;
    for (i = 0; i < 8u; i++)
        f->vlen[i] = f->vlen_t[i] = 1000.f;
    f->er_q = 2048.f;
}

void cr_fx_set(cr_fx_t *f, uint32_t era, float crush, float phmix, float phrate, float chmix, float chrate,
               uint32_t vmode, float size, float decay, float vmix, float warp, float dens, float vmod, float ott)
{
    uint32_t i;
    /* EraCrush */
    f->era = era > 5u ? 5u : era;
    if (f->era) {
        const float *m = ERA[f->era - 1u];
        float rate = m[0] * cr_exp2f(-crush * 4.32f), post = m[3] > 0.f ? m[3] : 0.45f * rate, g;
        f->er_inc = rate / FS;
        f->er_q = cr_exp2f(m[1] - 1.f);
        f->er_mu = m[2];
        f->er_munorm = m[2] > 0.f ? 1.f / cr_log2f(1.f + m[2]) : 1.f;
        f->er_g = 1.f + crush * 1.5f;                                     /* a little hotter as it crushes */
        f->er_mk = 1.f / (1.f + crush * 0.6f);
        g = cr_tanf(3.14159265f * xclampf(post, 50.f, 0.45f * FS) / FS);
        f->er_post_g = g;
        f->er_post_k = 1.f / m[4];
    }
    /* phaser, chorus */
    f->ph_mix = phmix;
    f->ph_inc = 0.02f * cr_exp2f(phrate * 8.2f) / FS;                    /* 0.02 .. 6 Hz */
    f->ch_mix = chmix;
    f->ch_inc = 0.1f * cr_exp2f(chrate * 6.3f) / FS;                     /* 0.1 .. 8 Hz */
    f->ch_depth = (0.0035f / (1.f + chrate * 3.f)) * FS;                  /* deeper when slow (stays in tune) */
    /* reverb */
    if (vmode > 5u)
        vmode = 5u;
    f->vmode = vmode;
    {
        const vmode_t *v = &VM[vmode];
        float base = 0.010f * cr_exp2f(size * 3.2f) * FS;                 /* 10 .. 92 ms: the longest line */
        for (i = 0; i < 8u; i++) {
            float r = v->clean[i] + (v->warped[i] - v->clean[i]) * warp;
            f->vlen_t[i] = xclampf(base * r, 8.f, (float)CR_VLINE - 80.f);
        }
        /* DECAY: the reverb time, 0.3 .. 30 s, each line's gain set by its length (short lines lose a little at
         * each of their many passes). At the top the sustaining modes (Abyss, Dub: most gain above 1) hold */
        {
            float rt = 0.3f * cr_exp2f(xclampf(decay, 0.f, 1.f) * 6.64f);
            for (i = 0; i < 8u; i++)
                f->vgl[i] = decay >= 0.98f && v->fbmax > 1.f ? v->fbmax : cr_exp2f(-f->vlen_t[i] * 9.966f / (rt * FS));
            f->vg = f->vgl[0];
        }
        f->vdense = v->dmin + (v->dmax - v->dmin) * dens;
        /* the mixing: butterflies at an angle (45 degrees: a Hadamard), so energy is kept at any DENSITY */
        f->vsin = cr_sin_turn(f->vdense * 0.125f);
        f->vcos = cr_sin_turn(f->vdense * 0.125f + 0.25f);
        f->vdiff = v->diff * dens;
        f->vlp = 1.f - cr_exp2f(-6.2832f * xclampf(v->lp * 1.6f, 100.f, 18000.f) / FS * 1.4427f);
        f->vhp = 1.f - cr_exp2f(-6.2832f * v->hp * 0.25f / FS * 1.4427f);   /* (short lines: many passes a second) */
        f->vmod_inc = v->mrate * (0.3f + vmod) / FS;
        f->vmod_d = v->mdepth * 0.001f * FS * vmod;
        f->vdrive = v->drive;
        f->vout_g = 0.5f;
    }
    f->vmix = vmix;
    f->ott = ott;
    f->ot_mk = 1.f + ott * 0.5f;
}

float cr_fx_era(cr_fx_t *f, float x)
{
    float v3, v1, v2;
    if (!f->era)
        return x;
    f->er_ph += f->er_inc;
    if (f->er_ph >= 1.f) {                                                /* the sample clock */
        float c = xclampf(x * f->er_g, -1.f, 1.f);
        f->er_ph -= 1.f;
        if (f->er_mu > 0.f) {                                             /* companded (MPC60) */
            float a = c < 0.f ? -c : c, e;
            e = cr_log2f(1.f + f->er_mu * a) * f->er_munorm;
            e = (float)(int32_t)(e * f->er_q + 0.5f) / f->er_q;
            e = (cr_exp2f(e / f->er_munorm) - 1.f) / f->er_mu;
            f->er_hold = c < 0.f ? -e : e;
        } else
            f->er_hold = (float)(int32_t)(c * f->er_q + (c < 0.f ? -0.5f : 0.5f)) / f->er_q;
    }
    /* the machine's output filter: a 2-pole low pass on the held samples */
    {
        float g = f->er_post_g, k = f->er_post_k, a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        v3 = f->er_hold - f->er_s2;
        v1 = a1 * f->er_s1 + a2 * v3;
        v2 = f->er_s2 + a2 * f->er_s1 + a3 * v3;
        f->er_s1 = 2.f * v1 - f->er_s1;
        f->er_s2 = 2.f * v2 - f->er_s2;
    }
    return v2 * f->er_mk;
}

float cr_fx_phaser(cr_fx_t *f, float x)
{
    float lfo, hz, t, c, u, y;
    uint32_t i;
    if (f->ph_mix <= 0.f)
        return x;
    f->ph_lfo += f->ph_inc;
    if (f->ph_lfo >= 1.f)
        f->ph_lfo -= 1.f;
    lfo = f->ph_lfo < 0.5f ? 2.f * f->ph_lfo : 2.f - 2.f * f->ph_lfo;    /* a triangle */
    hz = 200.f * cr_exp2f(lfo * 4.f);
    t = 3.14159265f * hz / FS;                                            /* tan for small angles */
    c = (1.f - t) / (1.f + t);
    u = x + 0.55f * f->ph_fb;
    for (i = 0; i < 4u; i++) {
        float a = -c * u + f->ph_ap[i];
        f->ph_ap[i] = u + c * a;
        u = a;
    }
    f->ph_fb = u;
    y = 0.5f * (x + u);
    return x + (y - x) * f->ph_mix;
}

static float tap(const int16_t *buf, uint32_t len, uint32_t w, float d)
{
    float rp = (float)w - d, fr, a0, a1;
    uint32_t r0;
    if (rp < 0.f)
        rp += (float)len;
    r0 = (uint32_t)rp;
    fr = rp - (float)r0;
    a0 = (float)buf[r0 & (len - 1u)];
    a1 = (float)buf[(r0 + 1u) & (len - 1u)];
    return (a0 + (a1 - a0) * fr) * (1.f / 16384.f);
}

void cr_fx_chorus(cr_fx_t *f, float x, float *l, float *r)
{
    float tri, dl, dr, wl, wr, c = 0.007f * FS;                          /* 7 ms in the middle */
    if (f->ch_mix <= 0.f) {
        *l = *r = x;
        return;
    }
    f->cho[f->cho_w] = (int16_t)(xclampf(x, -1.99f, 1.99f) * 16384.f);
    f->ch_lfo += f->ch_inc;
    if (f->ch_lfo >= 1.f)
        f->ch_lfo -= 1.f;
    tri = f->ch_lfo < 0.5f ? 4.f * f->ch_lfo - 1.f : 3.f - 4.f * f->ch_lfo;
    dl = c + f->ch_depth * tri;
    dr = c - f->ch_depth * tri;                                           /* the other side the other way */
    wl = tap(f->cho, CR_CHO_LEN, f->cho_w, dl);
    wr = tap(f->cho, CR_CHO_LEN, f->cho_w, dr);
    f->cho_w = (f->cho_w + 1u) & (CR_CHO_LEN - 1u);
    *l = x * (1.f - 0.5f * f->ch_mix) + wl * f->ch_mix * 0.8f;
    *r = x * (1.f - 0.5f * f->ch_mix) + wr * f->ch_mix * 0.8f;
}

void cr_fx_reverb(cr_fx_t *f, float *l, float *r)
{
    const vmode_t *v = &VM[f->vmode];
    float in = 0.5f * (*l + *r), d, y[8], fb[8], wl = 0.f, wr = 0.f;
    uint32_t i;
    if (f->vmix <= 0.f)
        return;
    /* the delays glide to their lengths (moving SIZE bends the tail, no clicks) */
    for (i = 0; i < 8u; i++)
        f->vlen[i] += (f->vlen_t[i] - f->vlen[i]) * 0.0005f;
    /* input diffusion: 4 all-passes */
    d = in;
    for (i = 0; i < 4u; i++) {
        int16_t *buf = f->diff + i * CR_VDIFF;
        float dz = (float)buf[(f->dw - DIFF_LEN[i]) & (CR_VDIFF - 1u)] * (1.f / 16384.f), g = f->vdiff, u = d + g * dz;
        buf[f->dw] = (int16_t)(xclampf(u, -1.99f, 1.99f) * 16384.f);
        d = dz - g * u;
    }
    f->dw = (f->dw + 1u) & (CR_VDIFF - 1u);
    /* the lines, read (modulated: the sweeps worked out every 8 samples) */
    if ((f->vtick++ & 7u) == 0u) {
        f->vmod_ph += f->vmod_inc * 8.f;
        if (f->vmod_ph >= 1.f)
            f->vmod_ph -= 1.f;
        for (i = 0; i < 8u; i++) {
            float ph = f->vmod_ph + (float)i * 0.125f;
            ph -= (float)(int32_t)ph;
            f->vmo[i] = f->vmod_d * (1.f + cr_sin_turn(ph));
        }
    }
    for (i = 0; i < 8u; i++)
        y[i] = tap(f->line + i * CR_VLINE, CR_VLINE, f->vw, f->vlen[i] + f->vmo[i]);
    /* the feedback: rotations mixing the lines (DENSITY: their angle, 45 degrees all the way to a Hadamard), then
     * the mode's permutation: an orthogonal matrix, so the loop loses only what DECAY and the filters take */
    {
        float h[8], a, b2, c = f->vcos, sn = f->vsin;
        uint32_t st, j;
        for (i = 0; i < 8u; i++)
            h[i] = y[i];
        for (st = 1; st < 8u; st <<= 1)
            for (i = 0; i < 8u; i += 2u * st)
                for (j = i; j < i + st; j++) {
                    a = h[j];
                    b2 = h[j + st];
                    h[j] = a * c + b2 * sn;
                    h[j + st] = b2 * c - a * sn;
                }
        for (i = 0; i < 8u; i++)
            fb[i] = h[(uint32_t)v->src[i]] * f->vgl[i];
    }
    for (i = 0; i < 8u; i++) {
        float x = fb[i];
        f->vlpz[i] += f->vlp * (x - f->vlpz[i]);                          /* loop low pass */
        f->vhpz[i] += f->vhp * (f->vlpz[i] - f->vhpz[i]);                 /* loop high pass */
        x = f->vlpz[i] - f->vhpz[i];
        if (v->bloom ? i < 2u : 1)
            x += d * (v->bloom ? 0.7f : ((i & 1u) ? -0.35f : 0.35f));
        if (f->vgl[i] > 0.97f || f->vdrive > 1.f)                         /* the saturator holds the sustain modes */
            x = soft3(x * f->vdrive) * (1.f / f->vdrive);
        f->line[i * CR_VLINE + f->vw] = (int16_t)(xclampf(x, -1.99f, 1.99f) * 16384.f);
        if (v->bloom) {
            float w = 0.4f + 0.15f * (float)i;
            if (i & 1u) wr += y[i] * w; else wl += y[i] * w;
        } else if (i & 1u)
            wr += (i & 2u) ? -y[i] : y[i];
        else
            wl += (i & 2u) ? -y[i] : y[i];
    }
    f->vw = (f->vw + 1u) & (CR_VLINE - 1u);
    wl *= f->vout_g;
    wr *= f->vout_g;
    *l += (wl - *l) * f->vmix;
    *r += (wr - *r) * f->vmix;
}

void cr_fx_ott(cr_fx_t *f, float *l, float *r)
{
    float b[2][3], peak[3], g[3];
    uint32_t c, k;
    if (f->ott <= 0.f) {
        f->ot_tick = 0;
        return;
    }
    for (c = 0; c < 2u; c++) {                                            /* three bands: < 120 Hz, mid, > 2.5 kHz */
        float x = c ? *r : *l, rest;
        f->ot_lo[c] += 0.0170f * (x - f->ot_lo[c]);
        rest = x - f->ot_lo[c];
        f->ot_hi[c] += 0.3005f * (rest - f->ot_hi[c]);
        b[c][0] = f->ot_lo[c];
        b[c][1] = f->ot_hi[c];
        b[c][2] = rest - f->ot_hi[c];
    }
    for (k = 0; k < 3u; k++) {
        float a = b[0][k] < 0.f ? -b[0][k] : b[0][k], a2 = b[1][k] < 0.f ? -b[1][k] : b[1][k], e;
        peak[k] = a > a2 ? a : a2;
        e = f->ot_env[k];
        e += (peak[k] > e ? 0.0075f : 0.00028f) * (peak[k] - e);         /* 3 ms attack, 80 ms release */
        f->ot_env[k] = e;
        /* towards -18 dB: quiet bands up (to x8), loud down (to x0.25), by DEPTH (every 8 samples) */
        if ((f->ot_tick & 7u) == 0u) {
            float t = 0.125f / (e + 1e-4f), gg = t > 8.f ? 8.f : t < 0.25f ? 0.25f : t;
            f->ot_g[k] = cr_exp2f(cr_log2f(gg) * 0.6f * f->ott);
        }
        g[k] = f->ot_g[k];
    }
    f->ot_tick++;
    *l = (b[0][0] * g[0] + b[0][1] * g[1] + b[0][2] * g[2] * 1.1f) * f->ot_mk * 0.6f + *l * (1.f - f->ott) * 0.4f;
    *r = (b[1][0] * g[0] + b[1][1] * g[1] + b[1][2] * g[2] * 1.1f) * f->ot_mk * 0.6f + *r * (1.f - f->ott) * 0.4f;
}
