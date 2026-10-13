#pragma once

#include <cmath>

namespace lynx
{
// Master protection until there's a real limiter in the mixer: linear up to 0.9 and
// then a smooth curve that never exceeds 1.0 (avoids the harsh distortion of clipping).
inline void softClip (float* samples, int n) noexcept
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
