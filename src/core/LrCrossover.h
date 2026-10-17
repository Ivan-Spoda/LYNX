#pragma once

// Linkwitz-Riley crossovers (S. Linkwitz, JAES 24(1), 1976): low = (Butterworth low-pass)², high = (Butterworth
// high-pass)² at the same frequency, so |low + high| = 1 (an all-pass). 12 dB/oct = LR2 (one section of Q 0.5 per
// side, its high side polarity-flipped), 24 = LR4 (two of Q 0.7071), 48 = LR8 (four: the 4th-order Butterworth twice). Used by Prism Multiband and
// Ember's multiband mode (REA_rodada108). Stereo, no allocation.

#include <algorithm>
#include <array>
#include <complex>

#include "core/Biquad.h"

namespace lynx
{
struct LrSplit
{
    static constexpr int maxSections = 4;
    std::array<Biquad, maxSections> lp, hp;
    int sections = 2;

    void design (double sr, double hz, int slopeDbOct) noexcept
    {
        hz = std::min (hz, sr * 0.45);
        static constexpr double q8[] { 0.54119610, 1.30656296, 0.54119610, 1.30656296 };
        sections = slopeDbOct <= 12 ? 1 : (slopeDbOct <= 24 ? 2 : 4);
        for (int k = 0; k < sections; ++k)
        {
            const auto q = sections == 1 ? 0.5 : (sections == 2 ? 0.70710678 : q8[k]);
            lp[(std::size_t) k].filter (sr, 0, hz, q);
            hp[(std::size_t) k].filter (sr, 1, hz, q);
        }
    }

    void reset() noexcept
    {
        for (auto& b : lp)
            b.reset();
        for (auto& b : hp)
            b.reset();
    }

    void run (float x, int channel, float& lo, float& hi) noexcept
    {
        lo = x;
        hi = x;
        for (int k = 0; k < sections; ++k)
        {
            lo = lp[(std::size_t) k].process (lo, channel);
            hi = hp[(std::size_t) k].process (hi, channel);
        }
        if (sections == 1) // LR2's high side has the opposite polarity: flip it so low + high is the all-pass
            hi = -hi;
    }

    // the split's all-pass (low + high), for keeping the other bands in phase
    float allpass (float x, int channel) noexcept
    {
        float lo, hi;
        run (x, channel, lo, hi);
        return lo + hi;
    }

    std::complex<double> response (bool high, double w) const noexcept
    {
        std::complex<double> h { 1.0, 0.0 };
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto& s = high ? hp : lp;
        for (int k = 0; k < sections; ++k)
        {
            const auto& q = s[(std::size_t) k];
            h *= (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
        }
        return high && sections == 1 ? -h : h;
    }
};
} // namespace lynx
