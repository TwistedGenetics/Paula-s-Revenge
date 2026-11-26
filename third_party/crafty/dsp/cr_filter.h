/* SPDX-License-Identifier: GPL-3.0-only
 * Crafty Synth: the filter types (0.4), after the models in Mega Synth. Each takes the same three controls:
 * CUTOFF, RESO and MORPH, and MORPH means what suits the type:
 *   SVF        low pass -> band pass -> high pass (the CRAFTsynth's own)
 *   LADDER     4-pole low pass -> 2-pole -> band pass -> high pass (a Minimoog-style ladder, saturating)
 *   MS-20      low pass -> band pass -> high pass, its resonance screaming (saturated in the loop)
 *   TB-303     4 -> 3 -> 2-pole low pass -> band pass (a mismatched, driven ladder: the acid squelch)
 *   SEM        low pass -> notch -> high pass (the Oberheim SEM's knob)
 *   STEINER    low pass -> band pass -> high pass, its input diode-clipped (Steiner-Parker growl)
 *   POLIVOKS   low pass -> band pass, its integrators clipping hard (the Polivoks' crunch)
 *   FORMANT    the vowels A E I O U (CUTOFF shifts them, 50 % natural; RESO narrows them)
 *   COMB +/-   negative (hollow) -> off -> positive (metallic) feedback, tuned to CUTOFF; RESO is brightness
 *   PHASER     2 to 12 all-pass stages at CUTOFF (RESO is feedback): notches to play
 *   DUAL PEAK  a resonant low pass and a second peak up to 3 octaves above it (talking basses)
 *   DECIMATOR  a resonant low pass whose own states are bit-crushed (16 bits -> 3): gritty resonance
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>

enum { CR_F_SVF, CR_F_LADDER, CR_F_MS20, CR_F_303, CR_F_SEM, CR_F_STEINER, CR_F_POLIVOKS, CR_F_FORMANT, CR_F_COMB,
       CR_F_PHASER, CR_F_DUAL, CR_F_DECIM, CR_NFTYPE };
#define CR_COMB_LEN 2048u

typedef struct {
    uint32_t type;
    /* coefficients (cr_filt_set) */
    float g, k, a1, a2, a3, G, m, w[4], drive, q, crush;
    float fg[3], fa1[3], fa2[3], fa3[3], fk[3], fgain[3];   /* FORMANT's three, DUAL PEAK's second */
    float apc, comb_d, comb_fb, comb_lp;
    uint32_t nap;
    /* state */
    float ic1, ic2, s[4], y4, hp, fic1[3], fic2[3], ap[12], apfb, comb_z;
    int16_t *comb;                     /* CR_COMB_LEN samples */
    uint32_t comb_w;
} cr_filt_t;

void cr_filt_init(cr_filt_t *f, int16_t *comb_mem);
/* type, cutoff (Hz), resonance 0..1, morph 0..1 (every control block) */
void cr_filt_set(cr_filt_t *f, uint32_t type, float hz, float res, float morph);
float cr_filt_run(cr_filt_t *f, float x);
