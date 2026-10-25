#pragma once

#include <cmath>

namespace lynx
{
// Output protection of the Master (device output and export). Up to round 159 this was a soft clipper that bent
// everything above 0.9 (−0.9 dBFS) with a tanh: a mastered mix peaking at −0.3 dBFS lost 0.07 dB on every peak and
// gained odd harmonics (THD 0.29 % on a −0.3 dBFS sine, 0.65 % at −0.1 dBFS; REA_rodada160 §3). FL leaves the mix untouched up to full
// scale; LYNX now does the same: samples within ±1 pass bit-exact, overs are clamped to ±1 (what the driver or the
// integer file format would do anyway) and NaN/inf become silence.
inline void clipOvers (float* samples, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const auto x = samples[i];
        if (! (std::abs (x) <= 1.0f)) // also catches NaN
            samples[i] = std::isnan (x) ? 0.0f : std::copysign (1.0f, x);
    }
}

// The previous soft clip, kept for reference/measurements only.
inline void legacySoftClip (float* samples, int n) noexcept
{
    constexpr float knee = 0.9f;
    constexpr float headroom = 1.0f - knee;

    for (int i = 0; i < n; ++i)
    {
        const auto x = samples[i];
        const auto a = std::abs (x);

        if (a > knee)
            samples[i] = std::copysign (knee + headroom * std::tanh ((a - knee) / headroom), x);
    }
}
} // namespace lynx
