/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: the shared fixed-point DSP building blocks. Copyright (C) 2026 Jamie (TwistedGenetics).
 * The polyBLEP saw, the soft clip and the trapezoidal SVF follow Felucca's dsp.c (Leo Kuroshita, Hugelton
 * Instruments, GPL-3.0-only); written out again here for DREADNOUGHT's own engines. */
#pragma once
#include <stdint.h>
#include "dn_tables.h"

static inline int32_t dn_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int32_t dn_mulq15(int32_t a, int32_t b) { return (a * b) >> 15; }
/* a * k / 65536 without 64-bit: a up to 2^31, k Q16 */
static inline int32_t dn_mulq16(int32_t a, uint32_t k)
{
    return (int32_t)(((a >> 16) * (int32_t)k) + (int32_t)(((uint32_t)(a & 0xFFFF) * k) >> 16));
}

/* sine from the phase (2^32 a cycle), Q15, linearly interpolated */
static inline int32_t dn_sine(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = DN_SINE[i], b = DN_SINE[i + 1u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

/* polyBLEP residual (Q15) around a wrap of a phase accumulator */
static inline int32_t dn_blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = inc >> 15;
    int32_t x;
    if (!d)
        return 0;
    if (ph < inc) {
        x = (int32_t)(ph / d);                       /* 0..32767 */
        return x + x - ((x * x) >> 15) - 32768;
    }
    if (ph > 0xFFFFFFFFu - inc) {
        x = -(int32_t)((0xFFFFFFFFu - ph) / d);       /* -32767..0 */
        return ((x * x) >> 15) + x + x + 32768;
    }
    return 0;
}
static inline int32_t dn_saw(uint32_t ph, uint32_t inc) { return ((int32_t)(ph >> 16) - 32768) - dn_blep(ph, inc); }
static inline int32_t dn_pulse(uint32_t ph, uint32_t inc, uint32_t pw)
{
    return (dn_saw(ph, inc) - dn_saw(ph + pw, inc)) >> 1;
}

/* tanh(x / 32768) * 32767, linearly interpolated */
static inline int32_t dn_softclip(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = DN_TANH[256];
    else
        y = DN_TANH[i] + (((DN_TANH[i + 1] - DN_TANH[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}

static inline uint32_t dn_noise(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}

/* the filters' g for a cutoff 0..127 << 8 (30 Hz .. 16 kHz, log), Q12 */
static inline int32_t dn_svf_g(int32_t cut)
{
    int32_t i, g;
    cut = dn_clamp(cut, 0, 127 << 8);
    i = cut >> 8;
    g = DN_SVF_G[i];
    if (i < 127)
        g += ((DN_SVF_G[i + 1] - g) * (cut & 255)) >> 8;
    return g;
}

/* the same, Q16 (finer at low cutoffs: a self-oscillating filter plays in tune) */
static inline int32_t dn_svf_g16(int32_t cut)
{
    int32_t i, f;
    uint32_t a, b;
    cut = dn_clamp(cut, 0, 127 << 8);
    i = cut >> 8, f = cut & 255;
    a = DN_SVF_G16[i], b = DN_SVF_G16[i < 127 ? i + 1 : 127];
    return (int32_t)(a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 8));
}

/* Trapezoidal SVF (A. Simper), Q13 coefficients; signals within +-150000. Outputs: 0 LP, 1 BP, 2 HP */
typedef struct { int32_t a1, a2, a3, k; } dn_svf_t;
static inline void dn_svf_coef(dn_svf_t *c, int32_t cut, int32_t k)   /* k: damping Q12 (2.0 = 8192, sqrt2 = 5793) */
{
    int32_t g = dn_svf_g(cut), den = 4096 + ((g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (c->a1 * g) >> 12;
    c->a3 = (c->a2 * g) >> 12;
    c->k = k;
}
static inline int32_t dn_svf(const dn_svf_t *c, int32_t in, int32_t *ic1, int32_t *ic2, uint32_t out)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = dn_clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = dn_clamp(2 * v2 - *ic2, -150000, 150000);
    return out == 0u ? v2 : out == 1u ? v1 : in - ((c->k * v1) >> 12) - v2;
}

/* a MIDI note plus a fraction (1/256 semitone) -> phase increment (the note table, interpolated in cents) */
static inline uint32_t dn_pitch_inc(int32_t note256)
{
    int32_t n = dn_clamp(note256, 0, 127 * 256 - 1), i = n >> 8, f = n & 255;
    uint32_t a = DN_NOTE_INC[i], b = DN_NOTE_INC[i + 1];
    return a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 8);
}

/* ---- envelope times locked to the tempo (0.13): a value 0..127 picks one of DN_ESYNC_N note lengths, and the
 * envelope covers ~99 % of its way in that long at the tempo (as DN_ENV_K does for its times; 10 s at most) */
#define DN_ESYNC_N 15
static const uint8_t DN_ESYNC[DN_ESYNC_N][2] = {{1, 16}, {1, 12}, {1, 8}, {1, 6}, {1, 4}, {1, 3}, {1, 2}, {2, 3}, {1, 1},
                                                {4, 3}, {2, 1}, {4, 1}, {8, 1}, {16, 1}, {32, 1}};   /* beats: num / den */
static inline uint32_t dn_esync_idx(int32_t v) { return (uint32_t)dn_clamp(v, 0, 127) * DN_ESYNC_N / 128u; }
static inline int32_t dn_esync_k(int32_t v, uint32_t bpm, int32_t lo)   /* lo: the quickest table index allowed */
{
    const uint8_t *d = DN_ESYNC[dn_esync_idx(v)];
    float ms = (float)d[0] * 60000.0f / ((float)(bpm ? bpm : 120u) * (float)d[1]), t = 1.0f;
    int32_t i = 0;
    while (i < 127 && t * 1.0369f < ms) {           /* the table's time nearest it (1.075254 ^ i ms; 1.0369: half a step) */
        t *= 1.075254f;
        i++;
    }
    return DN_ENV_K[i < lo ? lo : i];
}
