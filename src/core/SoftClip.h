#pragma once

#include <cmath>

namespace lynx
{
// Proteção do master até existir um limiter de verdade no mixer: linear até 0,9 e
// depois uma curva suave que nunca passa de 1,0 (evita a distorção dura do clipping).
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
