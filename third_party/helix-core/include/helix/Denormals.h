// Flush-to-zero for the duration of a scope (ARMv7 FPSCR / AArch64 FPCR / x86 MXCSR).
#pragma once
#include <cstdint>
#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace helix {

class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals()
    {
#if defined(__aarch64__)
        uint64_t v; __asm__ __volatile__("mrs %0, fpcr" : "=r"(v)); saved_ = v;
        v |= (1ull << 24); __asm__ __volatile__("msr fpcr, %0" : : "r"(v));
#elif defined(__arm__) && defined(__ARM_FP)
        uint32_t v; __asm__ __volatile__("vmrs %0, fpscr" : "=r"(v)); saved_ = v;
        v |= (1u << 24); __asm__ __volatile__("vmsr fpscr, %0" : : "r"(v));
#elif defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
        saved_ = _mm_getcsr(); _mm_setcsr((unsigned)saved_ | 0x8040u);
#endif
    }
    ~ScopedFlushDenormals()
    {
#if defined(__aarch64__)
        uint64_t v = saved_; __asm__ __volatile__("msr fpcr, %0" : : "r"(v));
#elif defined(__arm__) && defined(__ARM_FP)
        uint32_t v = (uint32_t)saved_; __asm__ __volatile__("vmsr fpscr, %0" : : "r"(v));
#elif defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
        _mm_setcsr((unsigned)saved_);
#endif
    }
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;
private:
    uint64_t saved_ = 0;
};

} // namespace helix
