#pragma once

// Round 147 (Pro-MB parity, REA_rodada147 §1): Prism Multiband's measured Dynamic phase. Measured black-box on the
// official Pro-MB trial: a band's gain g acts as a MINIMUM-PHASE filter whose magnitude is the linear-phase one,
// |F| = 1 + (g − 1)·A(ω), A = the band's crossover magnitudes (|low-pass of its upper crossover| · |high-pass of its
// lower one|), and the bands are in CASCADE (two adjacent bands at −12 dB give −8.2 dB at their shared crossover,
// the product of the two bands' −4.1 dB, not a flat −12). So at rest (g = 1) the effect is exactly transparent,
// and only a working band shifts the phase.
//
// Construction (LYNX's own, textbook spectral factorisation): with Linkwitz-Riley crossovers of order 2n,
// |L| = 1 / (1 + x_h), |H| = x_l / (1 + x_l), x = (Ω / Ωc)^{2n}, Ω = tan (πf / fs) (the bilinear
// transform's prewarped axis, so the digital magnitudes are exactly those of LYNX's digital crossovers). Then
//   |F| = Num (u) / ((1 + x_l)(1 + x_h)),  u = Ω^{2n},  Num = (1 + x_l)(1 + x_h) + (g − 1)·x_l   (middle band)
// and F = S², |S|² = |F|: S's poles are the two Butterworth sets of the crossovers, its zeros the left-half-plane
// roots of Num (−s²): Num is a quadratic in u, so each of its roots t gives the 2n solutions of s^{2n} = (−1)^n u_t
// on a circle — no general root finder. Low band: Num = g + x_h (a Butterworth set of radius g^{1/2n}·Ω_h); high
// band: Num = 1 + g·x_l (radius Ω_l·g^{−1/2n}). The analog sections go through the bilinear transform, the DC gain
// is set exactly (g for the low band, 1 for the others), and S is run twice.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace lynx::minphase
{
inline constexpr double pi = 3.14159265358979323846;

// the prewarped axis of a frequency at a sample rate (capped like LrSplit: 0.45·fs)
inline double warp (double hz, double sampleRate) noexcept
{
    return std::tan (pi * std::min (hz, sampleRate * 0.45) / sampleRate);
}

// the band's crossover magnitude A at Ω (wl ≤ 0: no lower crossover, wh ≤ 0: no upper one), orders 2·nl / 2·nh
inline double bandShare (double wl, double wh, int nl, int nh, double w) noexcept
{
    double a = 1.0;
    if (wh > 0.0)
        a /= 1.0 + std::pow (w / wh, 2.0 * nh);
    if (wl > 0.0)
    {
        const auto x = std::pow (w / wl, 2.0 * nl);
        a *= x / (1.0 + x);
    }
    return a;
}
inline double bandShare (double wl, double wh, int n, double w) noexcept { return bandShare (wl, wh, n, n, w); }

// the magnitude of one band's filter at Ω: 1 + (g − 1)·A
inline double bandMagnitude (double wl, double wh, int nl, int nh, double g, double w) noexcept
{
    return 1.0 + (g - 1.0) * bandShare (wl, wh, nl, nh, w);
}
inline double bandMagnitude (double wl, double wh, int n, double g, double w) noexcept { return bandMagnitude (wl, wh, n, n, g, w); }

class Band
{
public:
    static constexpr int maxSections = 8; // S: ≤ 2n = 8 roots → ≤ 4 sections, run twice

    // design for gain g (linear) between Ω_l and Ω_h (≤ 0 = absent), crossovers of order 2n (n = 1, 2, 4)
    void design (double wl, double wh, int n, double g) noexcept { design (wl, wh, n, n, g); }

    // round 147: the band's two crossovers may have different orders 2·nl (lower) and 2·nh (upper)
    void design (double wl, double wh, int nl, int nh, double g) noexcept
    {
        const int n = wl <= 0.0 ? nh : nl; // the edge bands have one crossover
        gain = 1.0;
        count = 0;
        if (wl <= 0.0 && wh <= 0.0)
        {
            gain = g; // a single band: a plain gain
            return;
        }
        std::array<std::complex<double>, 8> zeros {}, poles {};
        int nz = 0, np = 0;
        const auto butterworth = [&] (double radius, std::array<std::complex<double>, 8>& out, int& k0, int order) {
            for (int k = 0; k < order; ++k)
                out[(std::size_t) k0++] = std::polar (radius, pi * (2.0 * k + order + 1.0) / (2.0 * order));
        };
        if (wh > 0.0)
            butterworth (wh, poles, np, nh);
        if (wl > 0.0)
            butterworth (wl, poles, np, nl);
        if (wl > 0.0 && wh > 0.0 && nl != nh)
            mixedZeros (wl, wh, nl, nh, g, zeros, nz);
        else if (wl <= 0.0)
            butterworth (wh * std::pow (g, 0.5 / n), zeros, nz, n);        // Num = g + x_h
        else if (wh <= 0.0)
            butterworth (wl * std::pow (g, -0.5 / n), zeros, nz, n);       // Num = 1 + g·x_l
        else
        {
            // ρ t² + (g + ρ) t + 1 = 0, t = x_l, ρ = (Ω_l / Ω_h)^{2n}; the numerically stable quadratic roots
            const auto rho = std::pow (wl / wh, 2.0 * n);
            const std::complex<double> b { g + rho, 0.0 };
            const auto sq = std::sqrt (std::complex<double> (b.real() * b.real() - 4.0 * rho, 0.0));
            const auto q = -0.5 * (b + sq); // b > 0, so b + √D never cancels
            const std::complex<double> t[2] { q / rho, 1.0 / q };
            for (const auto& tr : t)
            {
                // s^{2n} = (−1)^n · t · Ω_l^{2n}: 2n solutions on a circle, the left-half-plane ones kept
                const auto radius = wl * std::pow (std::abs (tr), 0.5 / n);
                const auto phase = std::arg (tr) + (n % 2 != 0 ? pi : 0.0);
                for (int k = 0; k < 2 * n && nz < 8; ++k)
                {
                    const auto s = std::polar (radius, (phase + 2.0 * pi * k) / (2.0 * n));
                    if (s.real() < 0.0)
                        zeros[(std::size_t) nz++] = s;
                }
            }
        }
        // sections: conjugate pairs, then the real roots two by two, a last real root alone
        std::array<Analog, 4> num {}, den {};
        const int sn = sections (zeros.data(), nz, num);
        const int sd = sections (poles.data(), np, den);
        const int m = std::max (sn, sd);
        for (int k = 0; k < m; ++k)
            c[(std::size_t) k] = bilinear (k < sn ? num[(std::size_t) k] : Analog {}, k < sd ? den[(std::size_t) k] : Analog {});
        // the DC gain: g for the low band (no lower crossover), 1 otherwise; S carries its square root
        double dc = 1.0;
        for (int k = 0; k < m; ++k)
        {
            const auto& s = c[(std::size_t) k];
            dc *= (s.b0 + s.b1 + s.b2) / (1.0 + s.a1 + s.a2);
        }
        const auto target = std::sqrt (wl <= 0.0 ? g : 1.0);
        const auto k0 = target / dc;
        c[0].b0 *= k0; c[0].b1 *= k0; c[0].b2 *= k0;
        for (int k = 0; k < m; ++k)
            c[(std::size_t) (m + k)] = c[(std::size_t) k]; // F = S²
        count = 2 * m;
    }

    void reset() noexcept
    {
        for (auto& s : state)
            s = {};
    }

    double process (double x) noexcept
    {
        for (int k = 0; k < count; ++k)
        {
            const auto& q = c[(std::size_t) k];
            auto& z = state[(std::size_t) k];
            const auto y = q.b0 * x + z[0];
            z[0] = q.b1 * x - q.a1 * y + z[1];
            z[1] = q.b2 * x - q.a2 * y;
            x = y;
        }
        return x * gain;
    }

    // the designed digital response at ω (radians per sample)
    std::complex<double> response (double w) const noexcept
    {
        std::complex<double> h { gain, 0.0 };
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        for (int k = 0; k < count; ++k)
        {
            const auto& q = c[(std::size_t) k];
            h *= (q.b0 + q.b1 * z1 + q.b2 * z2) / (1.0 + q.a1 * z1 + q.a2 * z2);
        }
        return h;
    }

private:
    struct Analog { double c2 = 0.0, c1 = 0.0, c0 = 1.0; }; // c2·s² + c1·s + c0 (the default: 1)

    // a middle band whose crossovers have different orders: Num in z = (Ω / Ω_l)² is
    // P (z) = 1 + g·z^nl + ρ·z^nh + ρ·z^(nl+nh), ρ = (Ω_l / Ω_h)^(2·nh), of degree ≤ 8: its roots by Aberth's method,
    // started on the two circles the edge factors give (|z| = g^(−1/nl) and (g / ρ)^(1/nh)); each root z gives the
    // left-half-plane zero s = −Ω_l·√(−z)
    static void mixedZeros (double wl, double wh, int nl, int nh, double g, std::array<std::complex<double>, 8>& zeros, int& nz) noexcept
    {
        using C = std::complex<double>;
        const int d = nl + nh;
        const auto rho = std::pow (wl / wh, 2.0 * nh);
        std::array<double, 9> a {}; // monic coefficients, a[k] for z^k
        a[0] += 1.0;
        a[(std::size_t) nl] += g;
        a[(std::size_t) nh] += rho;
        a[(std::size_t) d] += rho;
        for (auto& x : a)
            x /= rho;
        std::array<C, 8> z {};
        const auto r1 = std::pow (g, -1.0 / nl), r2 = std::pow (g / rho, 1.0 / nh);
        for (int k = 0; k < nl; ++k)
            z[(std::size_t) k] = std::polar (r1, pi * (2.0 * k + 1.0) / nl + 0.31);
        for (int k = 0; k < nh; ++k)
            z[(std::size_t) (nl + k)] = std::polar (r2, pi * (2.0 * k + 1.0) / nh + 0.17);
        for (int it = 0; it < 80; ++it)
        {
            double worst = 0.0;
            for (int i = 0; i < d; ++i)
            {
                C p = 1.0, dp = 0.0; // Horner (monic)
                for (int k = d - 1; k >= 0; --k)
                {
                    dp = dp * z[(std::size_t) i] + p;
                    p = p * z[(std::size_t) i] + a[(std::size_t) k];
                }
                if (std::abs (dp) == 0.0)
                    continue;
                const auto ratio = p / dp;
                C sum = 0.0;
                for (int j = 0; j < d; ++j)
                    if (j != i)
                        sum += 1.0 / (z[(std::size_t) i] - z[(std::size_t) j]);
                const auto step = ratio / (1.0 - ratio * sum);
                z[(std::size_t) i] -= step;
                worst = std::max (worst, std::abs (step) / std::max (1e-300, std::abs (z[(std::size_t) i])));
            }
            if (worst < 1e-14)
                break;
        }
        for (int i = 0; i < d && nz < 8; ++i)
        {
            auto s = -wl * std::sqrt (-z[(std::size_t) i]);
            if (s.real() > 0.0)
                s = -s;
            zeros[(std::size_t) nz++] = s;
        }
    }
    struct Digital { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };

    static int sections (const std::complex<double>* roots, int n, std::array<Analog, 4>& out) noexcept
    {
        int k = 0;
        std::array<double, 8> reals {};
        int nr = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& r = roots[i];
            if (std::abs (r.imag()) > 1e-9 * std::abs (r))
            {
                if (r.imag() > 0.0 && k < 4) // one of each conjugate pair
                    out[(std::size_t) k++] = { 1.0, -2.0 * r.real(), std::norm (r) };
            }
            else
                reals[(std::size_t) nr++] = r.real();
        }
        for (int i = 0; i + 1 < nr && k < 4; i += 2)
            out[(std::size_t) k++] = { 1.0, -(reals[(std::size_t) i] + reals[(std::size_t) (i + 1)]), reals[(std::size_t) i] * reals[(std::size_t) (i + 1)] };
        if (nr % 2 != 0 && k < 4)
            out[(std::size_t) k++] = { 0.0, 1.0, -reals[(std::size_t) (nr - 1)] };
        return k;
    }

    // s = (1 − z⁻¹) / (1 + z⁻¹) (the axis is already prewarped)
    static Digital bilinear (const Analog& n, const Analog& d) noexcept
    {
        const bool second = n.c2 != 0.0 || d.c2 != 0.0;
        const auto map = [second] (const Analog& a, double& c0, double& c1, double& c2) {
            if (! second) // first order: c1·(1 − z⁻¹) + c0·(1 + z⁻¹)
            {
                c0 = a.c1 + a.c0;
                c1 = a.c0 - a.c1;
                c2 = 0.0;
                return;
            }
            c0 = a.c2 + a.c1 + a.c0;
            c1 = 2.0 * (a.c0 - a.c2);
            c2 = a.c2 - a.c1 + a.c0;
        };
        double n0, n1, n2, d0, d1, d2;
        map (n, n0, n1, n2);
        map (d, d0, d1, d2);
        return { n0 / d0, n1 / d0, n2 / d0, d1 / d0, d2 / d0 };
    }

    std::array<Digital, maxSections> c {};
    std::array<std::array<double, 2>, maxSections> state {};
    int count = 0;
    double gain = 1.0;
};
} // namespace lynx::minphase
