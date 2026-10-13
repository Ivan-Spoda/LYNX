#pragma once

// Biquad (fórmulas clássicas do "Audio EQ Cookbook" de R. Bristow-Johnson), em double,
// estéreo (estado por canal). Usado pelos efeitos internos e pelo EQ das trilhas do mixer.

#include <algorithm>
#include <cmath>

namespace lynx
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }

    float process (float x, int ch) noexcept
    {
        const auto y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    // passa-baixas/altas/banda (cookbook); o passa-banda tem ganho 0 dB no pico
    void filter (double sr, int type, double hz, double q) noexcept
    {
        constexpr double pi = 3.141592653589793;
        const auto w = 2.0 * pi * std::min (hz, sr * 0.45) / sr;
        const auto cw = std::cos (w);
        const auto alpha = std::sin (w) / (2.0 * q);

        if (type == 0)
            set ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
        else if (type == 1)
            set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
        else
            set (alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
    }

    void peaking (double sr, double hz, double q, double db) noexcept
    {
        constexpr double pi = 3.141592653589793;
        const auto a = std::pow (10.0, db / 40.0);
        const auto w = 2.0 * pi * hz / sr;
        const auto alpha = std::sin (w) / (2.0 * q);
        set (1 + alpha * a, -2 * std::cos (w), 1 - alpha * a, 1 + alpha / a, -2 * std::cos (w), 1 - alpha / a);
    }

    // prateleira; `slope` é o S do cookbook (1 = a mais íngreme sem ressalto)
    void shelf (double sr, double hz, double db, bool high, double slope = 1.0) noexcept
    {
        constexpr double pi = 3.141592653589793;
        const auto a = std::pow (10.0, db / 40.0);
        const auto w = 2.0 * pi * hz / sr;
        const auto cw = std::cos (w);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt ((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
        const auto sq = 2.0 * std::sqrt (a) * alpha;

        if (high)
            set (a * ((a + 1) + (a - 1) * cw + sq), -2 * a * ((a - 1) + (a + 1) * cw), a * ((a + 1) + (a - 1) * cw - sq),
                 (a + 1) - (a - 1) * cw + sq, 2 * ((a - 1) - (a + 1) * cw), (a + 1) - (a - 1) * cw - sq);
        else
            set (a * ((a + 1) - (a - 1) * cw + sq), 2 * a * ((a - 1) - (a + 1) * cw), a * ((a + 1) - (a - 1) * cw - sq),
                 (a + 1) + (a - 1) * cw + sq, -2 * ((a - 1) + (a + 1) * cw), (a + 1) + (a - 1) * cw - sq);
    }
};
} // namespace lynx
