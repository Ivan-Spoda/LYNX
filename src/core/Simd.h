#pragma once

// SSE/AVX intrinsics for the engines that reproduce FL Studio's x86 arithmetic bit for bit (MXCSR rounding and
// flush-to-zero, cvtsd2si, packed float order). On x86 they are the compiler's own; on ARM64 (Apple Silicon) the SSE
// subset is translated to NEON by sse2neon (third_party/sse2neon, MIT), with its precise min/max/div/sqrt options so
// the results stay IEEE-exact. AVX/FMA x86 paths stay behind LYNX_X86 (ARM64 has FMA natively: std::fma).

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
 #include <immintrin.h>
 #define LYNX_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
 #define SSE2NEON_PRECISE_MINMAX 1
 #define SSE2NEON_PRECISE_DIV 1
 #define SSE2NEON_PRECISE_SQRT 1
 #define SSE2NEON_PRECISE_DP 1
 #include "sse2neon.h"
 #define LYNX_X86 0
#else
 #error "LYNX needs x86-64 or ARM64"
#endif
