/* HELIX: the fm_* helpers DREADNOUGHT's effects use (its fastmath.h lives in the x0x-firmware submodule), done with
 * the C library: the MPC's ARM has a hardware FPU, and accuracy over the firmware's approximations costs little here. */
#pragma once
#include <math.h>
#define FM_PI 3.14159265358979323846f
static inline float fm_clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static inline float fm_exp2f(float x) { return exp2f(x); }
static inline float fm_expf(float x) { return expf(x); }
static inline float fm_powf(float a, float b) { return powf(a, b); }
static inline float fm_fabsf(float x) { return fabsf(x); }
static inline float fm_floorf(float x) { return floorf(x); }
static inline float fm_sqrtf(float x) { return sqrtf(x); }
static inline float fm_tanf(float x) { return tanf(x); }
static inline float fm_tanhf(float x) { return tanhf(x); }
static inline float fm_sinf(float x) { return sinf(x); }
static inline float fm_cosf(float x) { return cosf(x); }
static inline float fm_sin_pi(float x) { return sinf(x); }             /* (its argument is already in radians) */
static inline float fm_sin_turns(float t) { return sinf(6.28318530717958647f * t); }
/* flush tiny values to zero (denormals in feedback paths) */
static inline float fm_flush(float x) { return fabsf(x) < 1e-15f ? 0.0f : x; }
