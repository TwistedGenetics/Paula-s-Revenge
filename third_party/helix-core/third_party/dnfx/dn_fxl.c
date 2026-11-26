/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the light effects (0.41), small in memory and in CPU, for the FX slots beside the reverbs and echoes.
 * Copyright (C) 2026 Jamie (TwistedGenetics).
 *
 *   FILTER   a resonant state-variable filter (LP, BP, HP, NOTCH) with a drive before it and an LFO on its cutoff,
 *            synced to the tempo (1/16 .. 8 bars; sine, triangle, saw, square, S&H)
 *   PHASER   4, 6 or 8 allpass stages swept by a synced LFO, with feedback; left and right apart by STEREO. Its wet
 *            is the phase-shifted send: the notches come where it meets the dry sound in the mix
 *   CHORUS   CHORUS or FLANGE: a short swept delay a side (23 ms of line), FEEDBACK for the flanger's ring
 *   CRUSH    bits (16 .. 1) and the sample rate down (held samples, 44.1 kHz .. 170 Hz), a drive, a tone after
 *   RING     a ring modulator: the send times a sine (or a soft square), 20 Hz .. 5 kHz, its pitch moved by a synced LFO
 *   DRIVE    SOFT, HARD, FOLD, TUBE (asymmetric) or RECT (an octave up), a LOW CUT before it (keep the sub clean,
 *            drive the rest: the parallel bass trick) and a TONE after
 *
 * They take the limited send in stereo and give a wet signal (each slot's return adds it to the mix). Coefficients
 * once a 32-sample block, float inside. Memory: CHORUS 4 KB (its lines), the others none of their own (16 bytes, so a
 * slot holds them like any other). */
#include "dn_fx.h"
#include "dsp/fastmath.h"

#define FL_FS 44100.f
#define FL_CH 1024u                                 /* CHORUS: a side's line, samples (23 ms) */

/* the LFOs' lengths, beats (1/16 .. 8 bars) */
#define FL_NDIV 10u
static const char *const FL_DIVN[FL_NDIV] = {"1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2", "1 BAR", "2 BAR", "4 BAR",
                                             "8 BAR"};
static const float FL_DIVB[FL_NDIV] = {0.25f, 1.f / 3.f, 0.5f, 2.f / 3.f, 1.f, 2.f, 4.f, 8.f, 16.f, 32.f};
static const char *const FL_FMODE[4] = {"LP", "BP", "HP", "NOTCH"};
static const char *const FL_SHAPE[5] = {"SINE", "TRI", "SAW", "SQUARE", "S&H"};
static const char *const FL_CMODE[2] = {"CHORUS", "FLANGE"};
static const char *const FL_DTYPE[5] = {"SOFT", "HARD", "FOLD", "TUBE", "RECT"};
static const char *const FL_RSHAPE[2] = {"SINE", "SQUARE"};

/* each effect's values: names (7 at most; "-" none), tops */
static const char *const FL_PN[6][DN_FX_NP] = {
    {"MODE", "CUTOFF", "RES", "LFO RATE", "LFO AMT", "DRIVE", "LFO SHP"},       /* FILTER */
    {"RATE", "DEPTH", "FEEDBACK", "STAGES", "CENTER", "STEREO", "-"},          /* PHASER */
    {"MODE", "RATE", "DEPTH", "FEEDBACK", "DELAY", "STEREO", "-"},             /* CHORUS */
    {"BITS", "RATE", "DRIVE", "TONE", "-", "-", "-"},                          /* CRUSH */
    {"FREQ", "LFO RATE", "LFO AMT", "SHAPE", "-", "-", "-"},                   /* RING */
    {"TYPE", "DRIVE", "TONE", "LOW CUT", "-", "-", "-"},                       /* DRIVE */
};
static const uint8_t FL_PMAX[6][DN_FX_NP] = {
    {3, 127, 127, FL_NDIV - 1, 127, 127, 4},
    {FL_NDIV - 1, 127, 127, 2, 127, 127, 0},
    {1, 127, 127, 127, 127, 127, 0},
    {15, 127, 127, 127, 0, 0, 0},
    {127, FL_NDIV - 1, 127, 1, 0, 0, 0},
    {4, 127, 127, 127, 0, 0, 0},
};
static const uint8_t FL_DEF[6][DN_FX_NP] = {
    {0, 64, 70, 6, 90, 20, 0},                      /* FILTER: LP, resonant, swept over a bar */
    {6, 90, 70, 1, 64, 40, 0},                      /* PHASER: a bar, 6 stages */
    {0, 30, 60, 64, 50, 90, 0},                     /* CHORUS */
    {7, 70, 30, 100, 0, 0, 0},                      /* CRUSH: 8 bits, ~7 kHz */
    {60, 6, 0, 0, 0, 0, 0},                         /* RING */
    {0, 70, 90, 50, 0, 0, 0},                       /* DRIVE: soft, the low end kept out */
};

static int fl_is(uint32_t t) { return t >= DN_FX_FILTER && t < DN_FX_N; }
uint32_t dn_fxl_ram(uint32_t type) { return type == DN_FX_CHORUS ? 2u * FL_CH * sizeof(int16_t) : 16u; }
uint32_t dn_fxl_np(uint32_t type)
{
    uint32_t n = 0;
    while (fl_is(type) && n < DN_FX_NP && FL_PN[type - DN_FX_FILTER][n][0] != '-')
        n++;
    return n;
}
void dn_fxl_default(dn_fx_p_t *p)
{
    uint32_t i;
    for (i = 0; i < DN_FX_NP; i++)
        p->p[i] = fl_is(p->type) ? FL_DEF[p->type - DN_FX_FILTER][i] : 0u;
}
const char *dn_fxl_pname(uint32_t type, uint32_t i)
{
    return fl_is(type) && i < DN_FX_NP ? FL_PN[type - DN_FX_FILTER][i] : "-";
}
int32_t dn_fxl_pmax(uint32_t type, uint32_t i)
{
    return fl_is(type) && i < DN_FX_NP ? FL_PMAX[type - DN_FX_FILTER][i] : 0;
}

/* the values in their units */
static float fl_cut_hz(float c) { return 20.f * fm_exp2f(c * (9.9658f / 127.f)); }      /* 0..127: 20 Hz .. 20 kHz */
static float fl_ring_hz(uint32_t v) { return 20.f * fm_exp2f((float)v * (8.f / 127.f)); }   /* 20 Hz .. 5.1 kHz */
static float fl_crush_hz(uint32_t v) { return FL_FS * fm_exp2f(-(float)v * (8.f / 127.f)); }   /* 44.1 kHz .. 172 Hz */
static float fl_chorus_hz(uint32_t v) { return 0.05f * fm_exp2f((float)v * (7.f / 127.f)); }   /* 0.05 .. 6.4 Hz */
static void fl_put(char out[8], const char *w)
{
    uint32_t i;
    for (i = 0; w[i] && i < 7u; i++)
        out[i] = w[i];
    out[i] = 0;
}
static void fl_num(char out[8], int32_t v, const char *unit)
{
    char t[12];
    uint32_t n = 0, i = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    do
        t[n++] = (char)('0' + u % 10u), u /= 10u;
    while (u && n < 10u);
    if (v < 0)
        out[i++] = '-';
    while (n && i < 7u)
        out[i++] = t[--n];
    while (*unit && i < 7u)
        out[i++] = *unit++;
    out[i] = 0;
}
static void fl_hz(char out[8], float hz)            /* 950HZ, 1.2K, 12K */
{
    if (hz < 1000.f) {
        fl_num(out, (int32_t)(hz + 0.5f), "Hz");
    } else if (hz < 10000.f) {
        int32_t d = (int32_t)(hz / 100.f + 0.5f);
        char t[8];
        fl_num(t, d / 10, "");
        fl_put(out, t);
        {
            uint32_t n = 0;
            while (out[n])
                n++;
            out[n++] = '.';
            out[n++] = (char)('0' + d % 10);
            out[n++] = 'K';
            out[n] = 0;
        }
    } else {
        fl_num(out, (int32_t)(hz / 1000.f + 0.5f), "K");
    }
}
int dn_fxl_ptext(uint32_t t, uint32_t i, uint32_t v, char out[8])
{
    if (!fl_is(t))
        return 0;
    switch (t) {
    case DN_FX_FILTER:
        if (i == 0u) fl_put(out, FL_FMODE[v & 3u]);
        else if (i == 1u) fl_hz(out, fl_cut_hz((float)v));
        else if (i == 3u) fl_put(out, FL_DIVN[v < FL_NDIV ? v : 0u]);
        else if (i == 4u) fl_num(out, (int32_t)v - 64, "");
        else if (i == 6u) fl_put(out, FL_SHAPE[v < 5u ? v : 0u]);
        else fl_num(out, (int32_t)v, "");
        return 1;
    case DN_FX_PHASER:
        if (i == 0u) fl_put(out, FL_DIVN[v < FL_NDIV ? v : 0u]);
        else if (i == 3u) fl_num(out, 4 + 2 * (int32_t)(v < 3u ? v : 2u), "");
        else if (i == 4u) fl_hz(out, fl_cut_hz(40.f + (float)v * 0.6f));
        else fl_num(out, (int32_t)v, "");
        return 1;
    case DN_FX_CHORUS:
        if (i == 0u) fl_put(out, FL_CMODE[v & 1u]);
        else if (i == 1u) {                         /* Hz with a decimal under 10 */
            float hz = fl_chorus_hz(v);
            int32_t d = (int32_t)(hz * 100.f + 0.5f);
            char a[8];
            uint32_t n = 0;
            fl_num(a, d / 100, "");
            fl_put(out, a);
            while (out[n])
                n++;
            out[n++] = '.';
            out[n++] = (char)('0' + d / 10 % 10);
            out[n++] = (char)('0' + d % 10);
            out[n] = 0;
        } else if (i == 3u) fl_num(out, (int32_t)v - 64, "");
        else if (i == 4u) {                         /* the delay: 0.5 .. 20 ms */
            int32_t us = (int32_t)(500.f + (float)v * (19500.f / 127.f));
            fl_num(out, us / 1000, "ms");
        } else fl_num(out, (int32_t)v, "");
        return 1;
    case DN_FX_CRUSH:
        if (i == 0u) fl_num(out, 16 - (int32_t)(v & 15u), "BIT");
        else if (i == 1u) fl_hz(out, fl_crush_hz(v));
        else if (i == 3u) fl_hz(out, fl_cut_hz(64.f + (float)v * 0.5f));
        else fl_num(out, (int32_t)v, "");
        return 1;
    case DN_FX_RING:
        if (i == 0u) fl_hz(out, fl_ring_hz(v));
        else if (i == 1u) fl_put(out, FL_DIVN[v < FL_NDIV ? v : 0u]);
        else if (i == 3u) fl_put(out, FL_RSHAPE[v & 1u]);
        else fl_num(out, (int32_t)v, "");
        return 1;
    default:                                        /* DRIVE */
        if (i == 0u) fl_put(out, FL_DTYPE[v < 5u ? v : 0u]);
        else if (i == 2u) fl_hz(out, fl_cut_hz(64.f + (float)v * 0.5f));
        else if (i == 3u) {
            if (!v) fl_put(out, "OFF");
            else fl_hz(out, 20.f * fm_exp2f((float)v * (5.64f / 127.f)));   /* 20 Hz .. 1 kHz */
        } else fl_num(out, (int32_t)v, "");
        return 1;
    }
}

void dn_fxl_init(dn_fx_s_t *s)
{
    dn_fxl_t *l = &s->u.l;
    l->rng = 0x2545F491u;
    if (s->type == DN_FX_CHORUS)
        l->buf = (int16_t *)s->mem;
}

/* the LFO, 0..1 phase, a 32-sample block on: its value (-1..1) */
static float fl_lfo(dn_fxl_t *l, uint32_t shape, float ph)
{
    switch (shape) {
    case 1: return ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph;
    case 2: return 2.f * ph - 1.f;
    case 3: return ph < 0.5f ? 1.f : -1.f;
    case 4: return l->shv;
    default: return fm_sin_turns(ph);
    }
}
static float fl_advance(dn_fxl_t *l, uint32_t div, uint32_t bpm, uint32_t n)   /* the LFO moved on n samples */
{
    float inc = (float)(bpm ? bpm : 120u) / (FL_DIVB[div < FL_NDIV ? div : 0u] * 2646000.f);   /* turns a sample */
    l->lph += inc * (float)n;
    if (l->lph >= 1.f) {
        l->lph -= fm_floorf(l->lph);
        l->rng = l->rng * 1664525u + 1013904223u;   /* (S&H: a new value each turn) */
        l->shv = (float)(int32_t)l->rng * (1.f / 2147483648.f);
    }
    return l->lph;
}
static float fl_drive(uint32_t type, float x)
{
    switch (type) {
    case 1: return fm_clampf(x, -1.f, 1.f);
    case 2: return fm_sin_pi(fm_clampf(x, -8.f, 8.f) * 0.5f * FM_PI);   /* a fold: back from the top */
    case 3: return x > 0.f ? fm_tanhf(x) : fm_tanhf(x * 0.6f) * 1.2f;     /* asymmetric: even harmonics */
    case 4: return fm_fabsf(x) * 1.4f - 0.7f;                              /* full-wave: an octave up */
    default: return fm_tanhf(x);
    }
}

void dn_fxl_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *st, int32_t *wl, int32_t *wr, uint32_t n,
                uint32_t bpm)
{
    dn_fxl_t *l = &s->u.l;
    const uint8_t *v = p->p;
    uint32_t i, c;
    for (i = 0; i < n; i++)
        wl[i] = wr[i] = 0;
    switch (s->type) {
    case DN_FX_FILTER: {                            /* TPT state-variable filter (Zavalishin), a side each */
        float ph = fl_advance(l, v[3], bpm, n), m = fl_lfo(l, v[6], ph) * ((float)v[4] - 64.f);
        float hz = fm_clampf(fl_cut_hz(fm_clampf((float)v[1] + m, 0.f, 127.f)), 20.f, 18000.f);
        float g = fm_tanf(FM_PI * hz / FL_FS), k = 2.f - 1.96f * (float)v[2] / 127.f;
        float a1 = 1.f / (1.f + g * (g + k)), dg = 1.f + (float)v[5] * (7.f / 127.f);
        uint32_t mode = v[0] & 3u;
        for (c = 0; c < 2u; c++) {
            float s1 = l->s1[c], s2 = l->s2[c];
            for (i = 0; i < n; i++) {
                float x = (float)st[2u * i + c] * (1.f / 32768.f), hp, bp, lp, y;
                if (v[5])
                    x = fm_tanhf(x * dg) / dg * 1.5f;
                hp = (x - (k + g) * s1 - s2) * a1;
                bp = g * hp + s1;
                s1 = g * hp + bp;
                lp = g * bp + s2;
                s2 = g * bp + lp;
                y = mode == 0u ? lp : mode == 1u ? bp : mode == 2u ? hp : x - k * bp;
                (c ? wr : wl)[i] = (int32_t)(fm_clampf(y, -1.5f, 1.5f) * 32768.f);
            }
            l->s1[c] = fm_flush(s1), l->s2[c] = fm_flush(s2);
        }
        break;
    }
    case DN_FX_PHASER: {                            /* first-order allpasses, their corner swept (an octave up and
                                                     * down, two at DEPTH 127) around CENTER */
        float ph = fl_advance(l, v[0], bpm, n), fb = (float)v[2] * (0.85f / 127.f);
        uint32_t ns = 4u + 2u * (v[3] < 3u ? v[3] : 2u), j;
        for (c = 0; c < 2u; c++) {
            float q = ph + (c ? (float)v[5] * (0.5f / 127.f) : 0.f);
            float m = fm_sin_turns(q - fm_floorf(q)) * (float)v[1] * (2.f / 127.f);
            float hz = fm_clampf(fl_cut_hz(40.f + (float)v[4] * 0.6f) * fm_exp2f(m), 30.f, 16000.f);
            float t = fm_tanf(FM_PI * hz / FL_FS), a = (t - 1.f) / (t + 1.f), last = l->fb[c];
            for (i = 0; i < n; i++) {
                float x = (float)st[2u * i + c] * (1.f / 32768.f) + last * fb;
                for (j = 0; j < ns; j++) {          /* y = a x + s; s = x - a y */
                    float y = a * x + l->ap[c][j];
                    l->ap[c][j] = x - a * y;
                    x = y;
                }
                last = x;
                (c ? wr : wl)[i] = (int32_t)(fm_clampf(x, -1.5f, 1.5f) * 32768.f);
            }
            l->fb[c] = fm_flush(last);
            for (j = 0; j < ns; j++)
                l->ap[c][j] = fm_flush(l->ap[c][j]);
        }
        break;
    }
    case DN_FX_CHORUS: {                            /* a swept delay a side; FLANGE: shorter, with FEEDBACK */
        float hz = fl_chorus_hz(v[1]), base, dep, fb = ((float)v[3] - 64.f) * (0.9f / 64.f);
        uint32_t w = l->w;
        if (!l->buf)
            break;
        base = (0.5f + (float)v[4] * (19.5f / 127.f)) * (FL_FS / 1000.f);
        dep = (float)v[2] * (v[0] & 1u ? 4.f : 8.f) / 127.f * (FL_FS / 1000.f);   /* +- 4 / 8 ms */
        if (!(v[0] & 1u))
            fb *= 0.3f;                             /* (a chorus: a little regeneration at most) */
        for (i = 0; i < n; i++) {
            float lph = l->lph + hz / FL_FS * (float)i;
            for (c = 0; c < 2u; c++) {
                float q = lph + (c ? (float)v[5] * (0.5f / 127.f) : 0.f);
                float d = base + dep * 0.5f * (1.f + fm_sin_turns(q - fm_floorf(q)));
                int16_t *b = l->buf + c * FL_CH;
                uint32_t di;
                float f, y, x;
                if (d < 1.f)
                    d = 1.f;
                if (d > (float)(FL_CH - 3u))
                    d = (float)(FL_CH - 3u);
                di = (uint32_t)d, f = d - (float)di;
                y = (float)b[(w - di) & (FL_CH - 1u)] * (1.f - f) + (float)b[(w - di - 1u) & (FL_CH - 1u)] * f;
                x = (float)st[2u * i + c] + y * fb;
                b[w] = (int16_t)dn_fx_c16((int32_t)x);
                (c ? wr : wl)[i] = (int32_t)y;
            }
            w = (w + 1u) & (FL_CH - 1u);
        }
        l->w = w;
        l->lph += hz / FL_FS * (float)n;
        l->lph -= fm_floorf(l->lph);
        break;
    }
    case DN_FX_CRUSH: {                             /* drive, hold (the rate), cut to BITS, a tone after */
        float step = fl_crush_hz(v[1]) / FL_FS, q = (float)(1u << (15u - (v[0] & 15u))) * (1.f / 32768.f);
        float dg = 1.f + (float)v[2] * (7.f / 127.f);
        float a = 1.f - fm_expf(-2.f * FM_PI * fl_cut_hz(64.f + (float)v[3] * 0.5f) / FL_FS);
        for (i = 0; i < n; i++) {
            l->hph += step;
            if (l->hph >= 1.f) {
                l->hph -= 1.f;
                for (c = 0; c < 2u; c++) {
                    float x = (float)st[2u * i + c] * (1.f / 32768.f);
                    if (v[2])
                        x = fm_tanhf(x * dg);
                    l->held[c] = q < 1.f ? fm_floorf(x / q + 0.5f) * q : x > 0.f ? 0.5f : x < 0.f ? -0.5f : 0.f;
                }
            }
            for (c = 0; c < 2u; c++) {
                l->lp[c] += (l->held[c] - l->lp[c]) * a;
                (c ? wr : wl)[i] = (int32_t)(fm_clampf(l->lp[c], -1.5f, 1.5f) * 32768.f);
            }
        }
        l->lp[0] = fm_flush(l->lp[0]), l->lp[1] = fm_flush(l->lp[1]);
        break;
    }
    case DN_FX_RING: {                              /* the send times a carrier, its pitch moved by the LFO */
        float ph = fl_advance(l, v[1], bpm, n), m = fm_sin_turns(ph) * (float)v[2] * (2.f / 127.f);
        float inc = fl_ring_hz(v[0]) * fm_exp2f(m) / FL_FS;
        for (i = 0; i < n; i++) {
            float car = fm_sin_turns(l->rph);
            if (v[3] & 1u)
                car = fm_tanhf(car * 6.f);          /* a soft square */
            l->rph += inc;
            if (l->rph >= 1.f)
                l->rph -= 1.f;
            wl[i] = (int32_t)((float)st[2u * i] * car);
            wr[i] = (int32_t)((float)st[2u * i + 1u] * car);
        }
        break;
    }
    default: {                                      /* DRIVE: low cut, the drive, a DC block, the tone */
        float dg = fm_exp2f((float)v[1] * (6.f / 127.f));   /* x1 .. x64 */
        float a = 1.f - fm_expf(-2.f * FM_PI * fl_cut_hz(64.f + (float)v[2] * 0.5f) / FL_FS);
        float h = v[3] ? 1.f - fm_expf(-2.f * FM_PI * 20.f * fm_exp2f((float)v[3] * (5.64f / 127.f)) / FL_FS) : 0.f;
        float comp = 1.f / (1.f + (float)v[1] * (1.f / 127.f));   /* (louder drives, a little down) */
        for (c = 0; c < 2u; c++)
            for (i = 0; i < n; i++) {
                float x = (float)st[2u * i + c] * (1.f / 32768.f), y;
                if (v[3]) {                         /* two poles: 12 dB an octave under LOW CUT */
                    l->hp[c] += (x - l->hp[c]) * h;
                    x -= l->hp[c];
                    l->held[c] += (x - l->held[c]) * h;
                    x -= l->held[c];
                }
                y = fl_drive(v[0], x * dg);
                l->dc[c] += (y - l->dc[c]) * 0.0015f;   /* (~10 Hz) */
                y -= l->dc[c];
                l->lp[c] += (y - l->lp[c]) * a;
                (c ? wr : wl)[i] = (int32_t)(fm_clampf(l->lp[c] * comp, -1.5f, 1.5f) * 32768.f);
            }
        for (c = 0; c < 2u; c++)
            l->hp[c] = fm_flush(l->hp[c]), l->held[c] = fm_flush(l->held[c]), l->dc[c] = fm_flush(l->dc[c]),
            l->lp[c] = fm_flush(l->lp[c]);
        break;
    }
    }
}
