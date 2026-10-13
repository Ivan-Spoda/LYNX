#pragma once

// Liga flush-to-zero/denormals-are-zero na thread enquanto o objeto existe. Caudas de reverb,
// filtros e delays decaem para números denormais, que no x86 custam dezenas de vezes mais
// por operação; zerá-los não muda nada audível.
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
 #include <xmmintrin.h>
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
