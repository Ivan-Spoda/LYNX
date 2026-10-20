#pragma once

// Enables flush-to-zero/denormals-are-zero on the thread while the object exists. Reverb tails,
// filters and delays decay into denormal numbers, which on x86 cost tens of times more
// per operation; zeroing them changes nothing audible.
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
 #include "core/Simd.h"
#endif

namespace lynx
{
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u); // FTZ (bit 15) + DAZ (bit 6)
#endif
    }

    ~ScopedFlushDenormals()
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr (saved);
#endif
    }

    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
    unsigned int saved = 0;
};
} // namespace lynx
