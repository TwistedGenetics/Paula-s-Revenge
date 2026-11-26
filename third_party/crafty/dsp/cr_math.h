/* SPDX-License-Identifier: GPL-3.0-only
 * FM1-Craft: the few maths functions the synth needs, single-precision only (doubles go through software helpers on
 * the FM-1 and the build refuses them), no libm. the editor runs this same C, built to WebAssembly.
 * Copyright (C) 2026 Jamie (TwistedGenetics). */
#pragma once
#include <stdint.h>

/* 2^x for x in about -30 .. 30: the integer part by exponent bits, the fraction by a 5th-order polynomial
 * (relative error below 2e-6) */
static inline float cr_exp2f(float x)
{
    union { float f; uint32_t u; } r;
    int32_t i;
    float f, p;
    if (x < -30.f)
        x = -30.f;
    if (x > 30.f)
        x = 30.f;
    i = (int32_t)x;
    if (x < (float)i)
        i--;                                        /* floor */
    f = x - (float)i;
    p = 1.f + f * (0.69314718f + f * (0.24022650f + f * (0.05550411f + f * (0.00961813f + f * 0.00133336f))));
    r.u = (uint32_t)(i + 127) << 23;
    return r.f * p;
}

/* sin(2 pi t) for t in [0, 1): a parabola, corrected (the classic 0.225 refinement: error about 0.1 %) */
static inline float cr_sin_turn(float t)
{
    float u = 2.f * t - 1.f, au = u < 0.f ? -u : u;          /* sin(2 pi t) = -sin(pi u) */
    float y = 4.f * u * (1.f - au), ay = y < 0.f ? -y : y;
    y = 0.225f * (y * ay - y) + y;
    return -y;
}

/* tan(x) for x in [0, 1.45]: sin / cos by their series (enough for the filter's g = tan(pi fc / fs)) */
static inline float cr_tanf(float x)
{
    float x2 = x * x;
    float s = x * (1.f - x2 * (1.f / 6.f - x2 * (1.f / 120.f - x2 * (1.f / 5040.f - x2 * (1.f / 362880.f)))));
    float c = 1.f - x2 * (0.5f - x2 * (1.f / 24.f - x2 * (1.f / 720.f - x2 * (1.f / 40320.f - x2 * (1.f / 3628800.f)))));
    return s / c;
}

static inline float cr_clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

/* a soft clip: linear to about 0.5, 1 at the limit */
static inline float cr_soft(float x)
{
    if (x > 1.5f)
        return 1.f;
    if (x < -1.5f)
        return -1.f;
    return x - (4.f / 27.f) * x * x * x;
}

/* log2(x) for x > 0: the exponent by its bits, the mantissa by a 4th-order polynomial (error about 1e-4) */
static inline float cr_log2f(float x)
{
    union { float f; uint32_t u; } r;
    float m;
    int32_t e;
    if (x < 1e-30f)
        return -100.f;
    r.f = x;
    e = (int32_t)((r.u >> 23) & 0xFFu) - 127;
    r.u = (r.u & 0x007FFFFFu) | 0x3F800000u;       /* 1 <= m < 2 */
    m = r.f - 1.f;
    return (float)e + m * (1.4425449f + m * (-0.7181452f + m * (0.4575485f + m * (-0.2779042f + m * 0.0958178f))));
}

/* tanh, a rational approximation (exact at 0, within 2 % to |x| 3, clamped beyond) */
static inline float cr_tanh(float x)
{
    float x2;
    if (x > 3.f)
        return 1.f;
    if (x < -3.f)
        return -1.f;
    x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

static inline float cr_frac(float x) { return x - (float)(int32_t)x + (x < 0.f && x != (float)(int32_t)x ? 1.f : 0.f); }
