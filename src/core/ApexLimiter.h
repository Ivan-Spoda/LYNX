#pragma once

// Apex Limiter — LYNX's mastering brickwall limiter.
//
// Gain → look-ahead peak limiter with a guaranteed ceiling: the required gain of every sample (ceiling / peak, the
// peak optionally including inter-sample peaks estimated by 4× interpolation) is held for the look-ahead window and
// smoothed by a moving average of the same length, so the gain has fully reached its target when the peak leaves the
// delay line; a program-dependent two-stage release (fast + slow, mixed by style) brings it back. A final safety
// clip at the ceiling catches what is left. The latency is constant (maximum look-ahead + interpolator) so the
// mixer's delay compensation never changes while look-ahead moves. Design targets come from our black-box
// measurements of a reference mastering limiter (docs/analise/MEASURE_fabfilter.md §3).
//
// Parameters (0..1, EffectKind::apexLimiter): [0] gain 0..30 dB, [1] ceiling −30..0 dB, [2] look-ahead 0..5 ms,
// [3] release 1..1000 ms (log), [4] style (8, see styleName), [5] true peak (off/on), [6] channel link (transients)
// 0..100 %. Round 128 (parity with the reference limiter, appended so saved projects keep their meaning; every
// default leaves the output unchanged): [7] attack 0..10 s (how slowly the slow release stage dips: 0 = at once),
// [8] channel link of the release stage 0..100 % (the release is linked at least as much as the transients: only
// a value above [6] adds linking; default 0 = as before), [9] dither (off, 16, 18, 20, 22, 24 bits), [10] noise shaping
// (none, basic, optimized, weighted), [11] unity gain (monitor at the input's level), [12] audition limiting (hear
// what the limiter removes), [13] DC offset filter, [14] meter scale (editor only: −16 / −32 / −48 dB, K-12 / K-14 /
// K-20).

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::apex
{
constexpr int paramCount = 15;
enum Param { pGain, pCeiling, pLookahead, pRelease, pStyle, pTruePeak, pLink, pAttack, pLinkRelease, pDither, pShaping,
             pUnity, pAudition, pDcFilter, pMeterScale };
constexpr double maxLookaheadMs = 5.0;

// Round 128: 8 styles. The four original styles keep the positions their saved values round to (0, 1/3, 2/3, 1 of
// the old 4-step list → 0, 2, 5, 7 of 8), the new ones sit between them.
enum class Style { clean, glue, punch, dynamic, dense, loud, bus, safe };
constexpr int styleCount = 8;

double gainDb (float v) noexcept;
double ceilingDb (float v) noexcept;
double lookaheadMs (float v) noexcept;
double releaseMs (float v) noexcept;
float releaseValue (double ms) noexcept;
double attackMs (float v) noexcept;   // 0 .. 10 000 ms (v^4)
int ditherBits (float v) noexcept;    // 0 = off, else 16 .. 24
int noiseShaping (float v) noexcept;  // 0 none, 1 basic, 2 optimized, 3 weighted
int meterScale (float v) noexcept;    // 0 −16 dB, 1 −32 dB, 2 −48 dB, 3 K-12, 4 K-14, 5 K-20
Style style (float v) noexcept;
const char* styleName (Style s) noexcept;

const EffectInfo& info();
std::string paramText (int param, float v);

// Constant latency at a sample rate (samples).
int latencyAt (double sampleRate) noexcept;

std::unique_ptr<Effect> make();
} // namespace lynx::apex
