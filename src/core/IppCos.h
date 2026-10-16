#pragma once

// FL's plugins compute some float cosines through the installation's Intel IPP build (Shared/dsp_ippv2_x64.dll,
// ippsCos_32f_A11, resolved by name). REA_rodada74 §1: our restatement of that routine's two code paths for
// |x| <= 10000, checked bit for bit against the DLL on every float in that range (EXEC):
//  - the FMA path, which IPP's dispatcher picks on AVX2 CPUs (and, by its code, on AVX-512 ones): fused multiply-adds;
//  - the path of older CPUs (SSE2 … AVX): separate multiplies and adds and a three-part pi.
// Both: a quadrant count n from (|x| + pi/2)/pi through the 1.5·2^23 rounding trick, r = |x| - (n - 1/2)·pi,
// sign from n's parity, then an odd degree-5 polynomial. Arguments the DLL routes elsewhere (|x| > 10000, NaN) are
// not decoded; they fall back to the C library's cosine.
// The float operation order matters: includers keep FP contraction off (as the other FL engines do).

#include <cmath>
#include <cstdint>
#include <cstring>

#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace ippcos
{
inline float bitsToFloat (std::uint32_t u) noexcept { float f; std::memcpy (&f, &u, 4); return f; }
inline std::uint32_t floatToBits (float f) noexcept { std::uint32_t u; std::memcpy (&u, &f, 4); return u; }

// a·b + c with a single rounding, without relying on a hardware FMA or on the C library's fmaf: the product is exact
// in double, the sum's error comes from a two-sum, and rounding the double to odd before the float rounding gives
// the correctly rounded float.
inline float fusedMulAdd (float a, float b, float c) noexcept
{
    const double p = (double) a * (double) b, cc = (double) c;
    double s = p + cc;
    const double bb = s - p, e = (p - (s - bb)) + (cc - bb);
    if (e != 0.0)
    {
        std::uint64_t u;
        std::memcpy (&u, &s, 8);
        if ((u & 1) == 0)
        {
            u = ((e > 0.0) == (s > 0.0)) ? u + 1 : u - 1;
            std::memcpy (&s, &u, 8);
        }
    }
    return (float) s;
}

inline float cosFma (float x) noexcept
{
    const float a = std::fabs (x);
    if (! (a <= 10000.0f))
        return (float) std::cos ((double) x);
    const float halfPi = bitsToFloat (0x3fc90fdb), shifter = 12582912.0f; // 1.5·2^23
    const float s0 = a + halfPi;
    if (s0 == halfPi)
        return 1.0f;
    const float s = fusedMulAdd (bitsToFloat (0x3ea2f983), s0, shifter); // 1/pi
    const auto sign = floatToBits (s) << 31;
    const float n = (s - shifter) - 0.5f;
    float r = fusedMulAdd (-bitsToFloat (0x40490fdb), n, a);
    r = fusedMulAdd (-bitsToFloat (0xb3bbbd2e), n, r);
    const float r2 = r * r, rs = bitsToFloat (floatToBits (r) ^ sign);
    const float lin = rs * bitsToFloat (0x3f7ff87c);
    const float q = fusedMulAdd (bitsToFloat (0x3bf84524), r2, bitsToFloat (0xbe29e73f));
    return fusedMulAdd (rs * r2, q, lin);
}

inline float cosNoFma (float x) noexcept
{
    const float a = std::fabs (x);
    if (! (a <= 10000.0f))
        return (float) std::cos ((double) x);
    const float halfPi = bitsToFloat (0x3fc90fdb), shifter = 12582912.0f;
    const float s0 = a + halfPi;
    if (s0 == halfPi)
        return 1.0f;
    float s = s0 * bitsToFloat (0x3ea2f983);
    s = s + shifter;
    const auto sign = floatToBits (s) << 31;
    const float n = (s - shifter) - 0.5f;
    float r = a - bitsToFloat (0x40490000) * n;
    r = r - bitsToFloat (0x3a7da000) * n;
    r = r - bitsToFloat (0x34222169) * n;
    const float r2 = r * r, rs = bitsToFloat (floatToBits (r) ^ sign);
    const float q = bitsToFloat (0x3bf84524) * r2 + bitsToFloat (0xbe29e73f);
    return bitsToFloat (0x3f7ff87c) * rs + q * (rs * r2);
}

// IPP's dispatcher takes the FMA path when the CPU reports AVX2 (EXEC: ippSetCpuFeatures with and without the AVX2
// bit); AVX2 counts only when the OS saves the AVX state, as IPP's own detection requires (inference)
inline bool cpuTakesFmaPath() noexcept
{
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
    static const bool fma = [] {
        unsigned a = 0, b = 0, c = 0, d = 0;
        if (__get_cpuid_max (0, nullptr) < 7 || ! __get_cpuid (1, &a, &b, &c, &d))
            return false;
        if ((c & (1u << 27)) == 0 || (c & (1u << 28)) == 0) // OSXSAVE, AVX
            return false;
        unsigned lo = 0, hi = 0;
        __asm__ volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        if ((lo & 6u) != 6u) // XMM and YMM state enabled by the OS
            return false;
        __cpuid_count (7, 0, a, b, c, d);
        return (b & (1u << 5)) != 0; // AVX2
    }();
    return fma;
#else
    return true;
#endif
}

// ippsCos_32f_A11 as FL gets it on this machine's CPU
inline float cos32fA11 (float x) noexcept { return cpuTakesFmaPath() ? cosFma (x) : cosNoFma (x); }

// The plugin framework's 10 ms end fade (FUN_0071e060 mode 3; REA_rodada42 §3.8): 0.5·(1 + cos((i + 1)·pi/(n + 1)))
// for i < n, with the slope, scale, IPP cosine, offset and scale steps in float
inline void endFadeTable (float* t, long n) noexcept
{
    const auto k = (float) (1.0 / (double) (n + 1) * 3.141592653589793);
    for (long i = 0; i < n; ++i)
        t[i] = (cos32fA11 (((float) i + 1.0f) * k) + 1.0f) * 0.5f;
}
} // namespace ippcos
