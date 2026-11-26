/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: band-limiting a bank (see cr_wt.h). A real DFT of each wave (harmonics 1..128), then each mip level
 * summed back from its harmonics: exact brick-wall band limits, no FFT code. About a million multiply-adds a bank.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#include "cr_wt.h"
#include "cr_wavetables.h"

const int16_t (*cr_wt_factory(uint32_t b))[CR_WT_N] { return CR_WT_FACTORY[b < 9u ? b : 0u]; }
float cr_wt_scale(void) { return 1.f / (float)CR_WT_PEAK; }

void cr_wt_build(cr_wtbank_t *dst, const int16_t base[5][CR_WT_N])
{
    static float re[129], im[129], acc[CR_WT_N];
    uint32_t w, k, i, lv;
    for (w = 0; w < 5u; w++) {
        float peak = 0.f, g;
        for (k = 1; k <= 128u; k++) {
            float sr = 0.f, si = 0.f;
            uint32_t idx = 0;
            for (i = 0; i < CR_WT_N; i++) {
                float x = (float)base[w][i];
                sr += x * CR_SIN256[(idx + 64u) & 255u];   /* cos */
                si += x * CR_SIN256[idx & 255u];
                idx += k;
            }
            re[k] = sr * (2.f / (float)CR_WT_N);
            im[k] = si * (2.f / (float)CR_WT_N);
        }
        re[128] *= 0.5f;                                   /* Nyquist: counted once */
        im[128] = 0.f;
        for (i = 0; i < CR_WT_N; i++) {
            float x = (float)base[w][i];
            if (x < 0.f)
                x = -x;
            if (x > peak)
                peak = x;
        }
        g = peak > 1.f ? (float)CR_WT_PEAK / peak : 0.f;  /* every bank to the same peak (user waves too) */
        for (lv = 0; lv < CR_WT_LV; lv++) {
            uint32_t h = 128u >> lv;
            for (i = 0; i < CR_WT_N; i++)
                acc[i] = 0.f;
            for (k = 1; k <= h; k++) {
                float a = re[k], b = im[k];
                uint32_t idx = 0;
                if (a * a + b * b < 1e-3f)
                    continue;
                for (i = 0; i < CR_WT_N; i++) {
                    acc[i] += a * CR_SIN256[(idx + 64u) & 255u] + b * CR_SIN256[idx & 255u];
                    idx += k;
                }
            }
            for (i = 0; i < CR_WT_N; i++) {
                float y = acc[i] * g;
                y = y > 32767.f ? 32767.f : y < -32767.f ? -32767.f : y;
                dst->w[w][lv][i] = (int16_t)(y + (y < 0.f ? -0.5f : 0.5f));
            }
        }
    }
}
