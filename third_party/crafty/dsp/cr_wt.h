/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: a wavetable bank made ready to play: its 5 waves band-limited into mip levels (level L keeps
 * 128 >> L harmonics), so the 8 oscillators can read any of them at any pitch without aliasing. Built from a bank's
 * 5 x 256 base samples (factory or user) in the main loop, a few milliseconds. Copyright (C) 2026 Jamie
 * (TwistedGenetics). */
#pragma once
#include <stdint.h>

#define CR_WT_N 256u
#define CR_WT_LV 8u                    /* 128, 64, 32, 16, 8, 4, 2, 1 harmonics */
typedef struct {
    int16_t w[5][CR_WT_LV][CR_WT_N];
} cr_wtbank_t;                         /* 20 KB */

/* base[5][256], peak CR_WT_PEAK (any int16 is fine: it is scaled) */
void cr_wt_build(cr_wtbank_t *dst, const int16_t base[5][CR_WT_N]);
/* the factory bank b's base waves (b < 9) */
const int16_t (*cr_wt_factory(uint32_t b))[CR_WT_N];
float cr_wt_scale(void);               /* int16 -> -1..1 (1 / CR_WT_PEAK) */
