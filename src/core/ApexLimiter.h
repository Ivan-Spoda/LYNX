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
// [3] release 1..1000 ms (log), [4] style (Clean, Punch, Loud, Safe), [5] true peak (off/on), [6] channel link 0..100 %.

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::apex
{
constexpr int paramCount = 7;
constexpr double maxLookaheadMs = 5.0;

enum class Style { clean, punch, loud, safe };
constexpr int styleCount = 4;

double gainDb (float v) noexcept;
double ceilingDb (float v) noexcept;
double lookaheadMs (float v) noexcept;
double releaseMs (float v) noexcept;
float releaseValue (double ms) noexcept;
Style style (float v) noexcept;
const char* styleName (Style s) noexcept;

const EffectInfo& info();
std::string paramText (int param, float v);

// Constant latency at a sample rate (samples).
int latencyAt (double sampleRate) noexcept;

std::unique_ptr<Effect> make();
} // namespace lynx::apex
