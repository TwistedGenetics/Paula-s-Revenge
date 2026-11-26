/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the effects of 0.4, after its author's other projects:
 *   ERACRUSH  EraCrush's sampler converters (SP-1200, MPC60, S950, S1000, Amiga): rate, bits, companding, the
 *             machine's output filter; CRUSH takes the rate down to a twentieth
 *   PHASER    a 4-stage phaser (Small Stone style), sweeping 200 Hz - 3.2 kHz, a little feedback
 *   CHORUS    a two-tap chorus, the taps swept in opposite directions (Juno style): mono in, stereo out
 *   REVERB    Horizon (Super-Massive) in small: an 8-line feedback delay network with its modes (Room, Hall, Halo,
 *             Swell, Abyss, Dub), SIZE, DECAY, WARP (clean ratios -> smeared), DENSITY (echoes -> wash), MOD
 *   OTT       three bands, each pushed up when quiet and down when loud, by one DEPTH
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>

#define CR_VLINE 4096u                 /* a reverb line: 93 ms at 44.1 kHz (a power of two) */
#define CR_VDIFF 1024u                 /* an input diffuser */
#define CR_CHO_LEN 2048u
#define CR_FX_MEM (8u * CR_VLINE + 4u * CR_VDIFF + CR_CHO_LEN)   /* int16 samples: 76 KB */

typedef struct {
    /* EraCrush */
    uint32_t era;
    float er_inc, er_ph, er_hold, er_q, er_mu, er_munorm, er_g, er_post_g, er_post_k, er_s1, er_s2, er_t1, er_t2, er_mk;
    /* phaser */
    float ph_lfo, ph_inc, ph_mix, ph_ap[4], ph_fb;
    /* chorus */
    int16_t *cho;
    uint32_t cho_w;
    float ch_lfo, ch_inc, ch_depth, ch_mix;
    /* reverb */
    int16_t *line, *diff;
    uint32_t vw, dw;
    uint32_t vmode;
    float vlen[8], vlen_t[8], vg, vgl[8], vdense, vcos, vsin, vdiff, vmix, vlp, vhp, vlpz[8], vhpz[8], vmod_ph, vmod_inc, vmod_d;
    float vout_g, vdrive, vmo[8];
    uint32_t vtick;
    /* OTT */
    float ott, ot_lo[2], ot_hi[2], ot_env[3], ot_g[3], ot_mk;
    uint32_t ot_tick;
} cr_fx_t;

void cr_fx_init(cr_fx_t *f, int16_t *mem /* CR_FX_MEM */);
/* every control block: the controls, 0..1 (era: 0 off, 1..5 the machines; vmode 0..5) */
void cr_fx_set(cr_fx_t *f, uint32_t era, float crush, float phmix, float phrate, float chmix, float chrate,
               uint32_t vmode, float size, float decay, float vmix, float warp, float dens, float vmod, float ott);
float cr_fx_era(cr_fx_t *f, float x);                  /* mono, before the amp */
float cr_fx_phaser(cr_fx_t *f, float x);               /* mono */
void cr_fx_chorus(cr_fx_t *f, float x, float *l, float *r);
void cr_fx_reverb(cr_fx_t *f, float *l, float *r);     /* in place */
void cr_fx_ott(cr_fx_t *f, float *l, float *r);        /* in place */
