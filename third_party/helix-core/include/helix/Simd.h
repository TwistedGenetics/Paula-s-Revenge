// A 4-wide float vector with NEON (ARMv7/AArch64), SSE and scalar back ends.
// Used where four independent lanes exist: unison voices, stereo pairs, buffer maths.
#pragma once
#include <cstdint>

#if defined(HELIX_FORCE_SCALAR)
  #define HELIX_SIMD_SCALAR 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
  #include <arm_neon.h>
  #define HELIX_SIMD_NEON 1
#elif defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
  #include <emmintrin.h>
  #define HELIX_SIMD_SSE 1
#else
  #define HELIX_SIMD_SCALAR 1
#endif

namespace helix {

inline const char* simdBackendName()
{
#if defined(HELIX_SIMD_NEON)
    return "neon";
#elif defined(HELIX_SIMD_SSE)
    return "sse2";
#else
    return "scalar";
#endif
}

struct alignas(16) float4
{
#if defined(HELIX_SIMD_NEON)
    float32x4_t v;
    float4() : v(vdupq_n_f32(0.0f)) {}
    float4(float32x4_t x) : v(x) {}
    explicit float4(float s) : v(vdupq_n_f32(s)) {}
    float4(float a, float b, float c, float d) { alignas(16) float t[4] = {a, b, c, d}; v = vld1q_f32(t); }
    static float4 load(const float* p) { return float4(vld1q_f32(p)); }
    void store(float* p) const { vst1q_f32(p, v); }
    friend float4 operator+(float4 a, float4 b) { return vaddq_f32(a.v, b.v); }
    friend float4 operator-(float4 a, float4 b) { return vsubq_f32(a.v, b.v); }
    friend float4 operator*(float4 a, float4 b) { return vmulq_f32(a.v, b.v); }
    static float4 min(float4 a, float4 b) { return vminq_f32(a.v, b.v); }
    static float4 max(float4 a, float4 b) { return vmaxq_f32(a.v, b.v); }
    // a + b * c
    static float4 madd(float4 a, float4 b, float4 c) { return vmlaq_f32(a.v, b.v, c.v); }
    float sum() const
    {
        float32x2_t s = vadd_f32(vget_low_f32(v), vget_high_f32(v));
        return vget_lane_f32(vpadd_f32(s, s), 0);
    }
#elif defined(HELIX_SIMD_SSE)
    __m128 v;
    float4() : v(_mm_setzero_ps()) {}
    float4(__m128 x) : v(x) {}
    explicit float4(float s) : v(_mm_set1_ps(s)) {}
    float4(float a, float b, float c, float d) : v(_mm_setr_ps(a, b, c, d)) {}
    static float4 load(const float* p) { return float4(_mm_load_ps(p)); }
    void store(float* p) const { _mm_store_ps(p, v); }
    friend float4 operator+(float4 a, float4 b) { return _mm_add_ps(a.v, b.v); }
    friend float4 operator-(float4 a, float4 b) { return _mm_sub_ps(a.v, b.v); }
    friend float4 operator*(float4 a, float4 b) { return _mm_mul_ps(a.v, b.v); }
    static float4 min(float4 a, float4 b) { return _mm_min_ps(a.v, b.v); }
    static float4 max(float4 a, float4 b) { return _mm_max_ps(a.v, b.v); }
    static float4 madd(float4 a, float4 b, float4 c) { return _mm_add_ps(a.v, _mm_mul_ps(b.v, c.v)); }
    float sum() const { alignas(16) float t[4]; _mm_store_ps(t, v); return (t[0] + t[1]) + (t[2] + t[3]); }
#else
    float v[4];
    float4() : v{0, 0, 0, 0} {}
    explicit float4(float s) : v{s, s, s, s} {}
    float4(float a, float b, float c, float d) : v{a, b, c, d} {}
    static float4 load(const float* p) { return float4(p[0], p[1], p[2], p[3]); }
    void store(float* p) const { for (int i = 0; i < 4; ++i) p[i] = v[i]; }
    friend float4 operator+(float4 a, float4 b) { return float4(a.v[0]+b.v[0], a.v[1]+b.v[1], a.v[2]+b.v[2], a.v[3]+b.v[3]); }
    friend float4 operator-(float4 a, float4 b) { return float4(a.v[0]-b.v[0], a.v[1]-b.v[1], a.v[2]-b.v[2], a.v[3]-b.v[3]); }
    friend float4 operator*(float4 a, float4 b) { return float4(a.v[0]*b.v[0], a.v[1]*b.v[1], a.v[2]*b.v[2], a.v[3]*b.v[3]); }
    static float4 min(float4 a, float4 b) { float4 r; for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] < b.v[i] ? a.v[i] : b.v[i]; return r; }
    static float4 max(float4 a, float4 b) { float4 r; for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i]; return r; }
    static float4 madd(float4 a, float4 b, float4 c) { return a + b * c; }
    float sum() const { return (v[0] + v[1]) + (v[2] + v[3]); }
#endif
};

// x - trunc(x) for x >= 0 (phase wrap), and trunc(x) as int32 lanes.
inline float4 fracPos(float4 x)
{
#if defined(HELIX_SIMD_NEON)
    return float4(vsubq_f32(x.v, vcvtq_f32_s32(vcvtq_s32_f32(x.v))));
#elif defined(HELIX_SIMD_SSE)
    return float4(_mm_sub_ps(x.v, _mm_cvtepi32_ps(_mm_cvttps_epi32(x.v))));
#else
    float4 r; for (int i = 0; i < 4; ++i) r.v[i] = x.v[i] - (float)(int)x.v[i]; return r;
#endif
}
inline void truncToInt(float4 x, int32_t* out)
{
#if defined(HELIX_SIMD_NEON)
    vst1q_s32(out, vcvtq_s32_f32(x.v));
#elif defined(HELIX_SIMD_SSE)
    _mm_storeu_si128((__m128i*)out, _mm_cvttps_epi32(x.v));
#else
    for (int i = 0; i < 4; ++i) out[i] = (int32_t)x.v[i];
#endif
}

// Buffer helpers (n need not be a multiple of 4; pointers need not be aligned).
inline void bufScale(float* x, float g, int n)
{
    int i = 0;
    const float4 G(g);
    for (; i + 4 <= n; i += 4)
    {
#if defined(HELIX_SIMD_NEON)
        vst1q_f32(x + i, vmulq_f32(vld1q_f32(x + i), G.v));
#elif defined(HELIX_SIMD_SSE)
        _mm_storeu_ps(x + i, _mm_mul_ps(_mm_loadu_ps(x + i), G.v));
#else
        for (int k = 0; k < 4; ++k) x[i + k] *= g;
#endif
    }
    for (; i < n; ++i) x[i] *= g;
}

// dst += src * g
inline void bufMixIn(float* dst, const float* src, float g, int n)
{
    int i = 0;
    const float4 G(g);
    for (; i + 4 <= n; i += 4)
    {
#if defined(HELIX_SIMD_NEON)
        vst1q_f32(dst + i, vmlaq_f32(vld1q_f32(dst + i), vld1q_f32(src + i), G.v));
#elif defined(HELIX_SIMD_SSE)
        _mm_storeu_ps(dst + i, _mm_add_ps(_mm_loadu_ps(dst + i), _mm_mul_ps(_mm_loadu_ps(src + i), G.v)));
#else
        for (int k = 0; k < 4; ++k) dst[i + k] += src[i + k] * g;
#endif
    }
    for (; i < n; ++i) dst[i] += src[i] * g;
}

inline void bufClear(float* x, int n) { for (int i = 0; i < n; ++i) x[i] = 0.0f; }

} // namespace helix
