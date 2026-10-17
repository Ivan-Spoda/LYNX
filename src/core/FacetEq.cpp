#include "core/FacetEq.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace lynx::facet
{
namespace
{
constexpr double pi = 3.14159265358979323846;
using cd = std::complex<double>;

// Analog section in normalized frequency (s = jω/ω0): (n2 s² + n1 s + n0) / (d2 s² + d1 s + d0)
struct Analog { double n2 = 0, n1 = 0, n0 = 1, d2 = 0, d1 = 0, d0 = 1; };

cd evalAnalog (const Analog& a, double w) noexcept // w = ω/ω0
{
    const cd s (0.0, w);
    return (a.n2 * s * s + a.n1 * s + a.n0) / (a.d2 * s * s + a.d1 * s + a.d0);
}

double mag2 (const Analog& a, double w) noexcept { return std::norm (evalAnalog (a, w)); }

// Magnitude-matched digital section for an analog section at w0 = 2π f0 / fs. `ok` reports whether the numerator
// was realisable as asked (false = the magnitude at the match point had to be clamped).
Section matched (const Analog& a, double w0, bool& ok) noexcept
{
    Section s;
    ok = true;
    const double nyq = pi / w0; // digital Nyquist on the normalized analog axis

    if (a.d2 == 0.0)
    {
        // first order: (n1 s + n0) / (d1 s + d0); pole by impulse invariance, zero matched at DC and Nyquist
        const double wp = std::min (a.d0 / a.d1 * w0, 0.999 * pi);
        const double p = std::exp (-wp);
        const double h0 = std::sqrt (mag2 (a, 0.0)), hn = std::sqrt (mag2 (a, nyq));
        const double sum = h0 * (1.0 - p), diff = hn * (1.0 + p);
        s.b0 = 0.5 * (sum + diff);
        s.b1 = 0.5 * (sum - diff);
        s.a1 = -p;
        return s;
    }

    // poles of d2 s² + d1 s + d0: natural frequency wp, damping zeta
    const double wpn = std::sqrt (a.d0 / a.d2);
    const double zeta = a.d1 / (2.0 * std::sqrt (a.d0 * a.d2));
    const double wp = std::min (wpn * w0, 0.999 * pi);
    const double e = std::exp (-zeta * wp);
    s.a2 = e * e;
    s.a1 = zeta <= 1.0 ? -2.0 * e * std::cos (std::sqrt (1.0 - zeta * zeta) * wp)
                       : -2.0 * e * std::cosh (std::sqrt (zeta * zeta - 1.0) * wp);

    // squared magnitude of a digital polynomial as A0 φ0 + A1 φ1 + A2 φ2 (φ0 = cos²(w/2), φ1 = sin²(w/2), φ2 = 4 φ0 φ1)
    const double A0 = (1.0 + s.a1 + s.a2) * (1.0 + s.a1 + s.a2);
    const double A1 = (1.0 - s.a1 + s.a2) * (1.0 - s.a1 + s.a2);
    const double A2 = -4.0 * s.a2;
    auto den = [&] (double w) {
        const double p1 = std::sin (w * 0.5) * std::sin (w * 0.5), p0 = 1.0 - p1;
        return A0 * p0 + A1 * p1 + A2 * 4.0 * p0 * p1;
    };
    const double wm = std::min (w0, 0.9 * pi);
    const double p1 = std::sin (wm * 0.5) * std::sin (wm * 0.5), p0 = 1.0 - p1;
    const double target = mag2 (a, wm / w0) * den (wm);

    if (a.n1 == 0.0 && a.n0 == 0.0)
    {
        // high-pass: double zero at DC, k (1 − z⁻¹)², matched at the band frequency
        const double k = std::sqrt (target) / (4.0 * p1);
        s.b0 = k; s.b1 = -2.0 * k; s.b2 = k;
        return s;
    }
    if (a.n1 == 0.0 && a.n2 != 0.0 && a.n0 != 0.0)
    {
        // notch: zeros on the unit circle at the notch frequency, gain matched at DC
        const double wz = std::min (std::sqrt (a.n0 / a.n2) * w0, 0.999 * pi);
        const double k = std::sqrt (mag2 (a, 0.0)) * (1.0 + s.a1 + s.a2) / (2.0 - 2.0 * std::cos (wz));
        s.b0 = k; s.b1 = -2.0 * k * std::cos (wz); s.b2 = k;
        return s;
    }

    // numerator matched at DC, Nyquist and the match point (the band frequency, kept inside the band)
    const double B0 = A0 * mag2 (a, 0.0);
    const double B1 = A1 * mag2 (a, nyq);
    double B2 = (target - B0 * p0 - B1 * p1) / (4.0 * p0 * p1);

    const double sb0 = std::sqrt (std::max (0.0, B0)), sb1 = std::sqrt (std::max (0.0, B1));
    const double W = 0.5 * (sb0 + sb1);
    if (B2 < -W * W) { B2 = -W * W; ok = false; }
    s.b0 = 0.5 * (W + std::sqrt (W * W + B2));
    s.b1 = 0.5 * (sb0 - sb1);
    s.b2 = s.b0 != 0.0 ? -B2 / (4.0 * s.b0) : 0.0;
    return s;
}

// matched design; when the numerator cannot be realised (cuts near Nyquist), the reciprocal section is designed
// instead and inverted (its zeros become the poles)
Section matched (const Analog& a, double w0) noexcept
{
    bool ok = true;
    const auto s = matched (a, w0, ok);
    if (ok || a.d2 == 0.0 || a.n2 == 0.0 || a.n0 == 0.0)
        return s;
    const Analog inv { a.d2, a.d1, a.d0, a.n2, a.n1, a.n0 };
    bool ok2 = true;
    const auto r = matched (inv, w0, ok2);
    if (! ok2 || r.b0 == 0.0)
        return s;
    Section out;
    out.b0 = 1.0 / r.b0; out.b1 = r.a1 / r.b0; out.b2 = r.a2 / r.b0;
    out.a1 = r.b1 / r.b0; out.a2 = r.b2 / r.b0;
    return out;
}

double bandQ (const EffectParams& p, int b) noexcept { return q (p[(std::size_t) param (b, 3)]); }

// Butterworth section Qs of an order-n filter, ascending
int butterworthQs (int n, double* qs) noexcept
{
    const int m = n / 2;
    for (int k = 0; k < m; ++k)
        qs[k] = 1.0 / (2.0 * std::sin ((2 * k + 1) * pi / (2.0 * n)));
    std::sort (qs, qs + m);
    return m;
}

// analog sections of one band (normalized to the band frequency); returns the count
int analogSections (const EffectParams& p, int b, Analog* out) noexcept
{
    const auto sh = shape (p[(std::size_t) param (b, 0)]);
    const double scale = gainScale (p[(std::size_t) scaleParam]);
    const double g = gainDb (p[(std::size_t) param (b, 2)]) * scale;
    const double Q = bandQ (p, b);
    const int slope = slopeDb (p[(std::size_t) param (b, 4)]);

    switch (sh)
    {
        case Shape::off: return 0;
        case Shape::bell:
        {
            // symmetric bell: (s² + s·A/Qb + 1) / (s² + s/(A·Qb) + 1), Qb = Q/√2 (measured convention)
            if (std::abs (g) < 1e-9) return 0;
            const double A = std::pow (10.0, g / 40.0), qb = Q / std::sqrt (2.0);
            out[0] = { 1.0, A / qb, 1.0, 1.0, 1.0 / (A * qb), 1.0 };
            return 1;
        }
        case Shape::lowShelf:
        case Shape::highShelf:
        case Shape::tiltShelf:
        {
            if (std::abs (g) < 1e-9) return 0;
            const bool tilt = sh == Shape::tiltShelf;
            const double gg = tilt ? 2.0 * g : g;
            const double A = std::pow (10.0, gg / 40.0), sa = std::sqrt (A);
            const double norm = tilt ? std::pow (10.0, -g / 20.0) : 1.0;
            const bool high = sh != Shape::lowShelf;
            if (slope <= 6)
            {
                // first-order shelf, geometric half-gain point at the band frequency: high (A s + 1)/(s/A + 1), low (s + A)/(s + 1/A)
                if (high) out[0] = { 0.0, A * norm, norm, 0.0, 1.0 / A, 1.0 };
                else out[0] = { 0.0, norm, A * norm, 0.0, 1.0, 1.0 / A };
                return 1;
            }
            // second-order cookbook shelf, Qs = 0.7071·Q^0.4697 (measured), gain independent
            const double qs = std::sqrt (0.5) * std::pow (Q, 0.4697);
            if (high) out[0] = { A * A * norm, A * sa / qs * norm, A * norm, 1.0, sa / qs, A };
            else out[0] = { A * norm, A * sa / qs * norm, A * A * norm, A, sa / qs, 1.0 };
            return 1;
        }
        case Shape::lowCut:
        case Shape::highCut:
        {
            const bool hp = sh == Shape::lowCut;
            const int n = std::max (1, slope / 6);
            double qs[16];
            const int m = butterworthQs (n, qs);
            if (m > 0) qs[m - 1] *= Q / 1.0; // the highest-Q section carries the band's resonance (Q 1 = Butterworth)
            int c = 0;
            for (int k = 0; k < m; ++k)
                out[c++] = hp ? Analog { 1.0, 0.0, 0.0, 1.0, 1.0 / qs[k], 1.0 } : Analog { 0.0, 0.0, 1.0, 1.0, 1.0 / qs[k], 1.0 };
            if (n % 2)
                out[c++] = hp ? Analog { 0.0, 1.0, 0.0, 0.0, 1.0, 1.0 } : Analog { 0.0, 0.0, 1.0, 0.0, 1.0, 1.0 };
            return c;
        }
        case Shape::notch:
        {
            const double qb = Q / std::sqrt (2.0);
            out[0] = { 1.0, 0.0, 1.0, 1.0, 1.0 / qb, 1.0 };
            return 1;
        }
        case Shape::bandPass:
        {
            const double qb = Q / std::sqrt (2.0);
            out[0] = { 0.0, 1.0 / qb, 0.0, 1.0, 1.0 / qb, 1.0 };
            return 1;
        }
    }
    return 0;
}

cd evalDigital (const Section& s, double w) noexcept
{
    const cd z1 = std::polar (1.0, -w), z2 = z1 * z1;
    return (s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
}

class FacetEqEffect final : public Effect
{
public:
    void prepare (double sr) noexcept override { sampleRate = sr; cached.fill (-1.0f); reset(); }
    void reset() noexcept override
    {
        for (auto& band : state)
            for (auto& st : band)
                st = {};
    }

    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        bool changed = false;
        for (int i = 0; i < paramCount; ++i)
            if (params[(std::size_t) i] != cached[(std::size_t) i]) { changed = true; break; }
        if (changed)
        {
            for (int i = 0; i < paramCount; ++i) cached[(std::size_t) i] = params[(std::size_t) i];
            for (int b = 0; b < bands; ++b)
            {
                const int before = counts[(std::size_t) b];
                counts[(std::size_t) b] = design (params, b, sampleRate, sections[(std::size_t) b]);
                if (counts[(std::size_t) b] != before) // the structure changed: start this band clean
                    for (auto& st : state[(std::size_t) b]) st = {};
            }
            outGain = (float) std::pow (10.0, outputDb (params[(std::size_t) outputParam]) / 20.0);
        }

        for (int b = 0; b < bands; ++b)
            for (int k = 0; k < counts[(std::size_t) b]; ++k)
            {
                const auto& c = sections[(std::size_t) b][(std::size_t) k];
                auto& st = state[(std::size_t) b][(std::size_t) k];
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* x = ch == 0 ? left : right;
                    double z1 = st.z1[ch], z2 = st.z2[ch];
                    for (int i = 0; i < n; ++i)
                    {
                        const double in = x[i];
                        const double y = c.b0 * in + z1;
                        z1 = c.b1 * in - c.a1 * y + z2;
                        z2 = c.b2 * in - c.a2 * y;
                        x[i] = (float) y;
                    }
                    // denormal guard
                    st.z1[ch] = std::abs (z1) < 1e-30 ? 0.0 : z1;
                    st.z2[ch] = std::abs (z2) < 1e-30 ? 0.0 : z2;
                }
            }

        if (outGain != 1.0f)
            for (int i = 0; i < n; ++i) { left[i] *= outGain; right[i] *= outGain; }
    }

private:
    struct State { double z1[2] {}, z2[2] {}; };
    double sampleRate = 48000.0;
    std::array<float, paramCount> cached {};
    std::array<std::array<Section, maxSections>, bands> sections {};
    std::array<std::array<State, maxSections>, bands> state {};
    std::array<int, bands> counts {};
    float outGain = 1.0f;
};
} // namespace

Shape shape (float v) noexcept { return (Shape) std::clamp ((int) std::lround (v * (shapeCount - 1)), 0, shapeCount - 1); }
float shapeValue (Shape s) noexcept { return (float) (int) s / (float) (shapeCount - 1); }
double hz (float v) noexcept { return 10.0 * std::pow (3000.0, (double) std::clamp (v, 0.0f, 1.0f)); }
float hzValue (double f) noexcept { return (float) std::clamp (std::log (f / 10.0) / std::log (3000.0), 0.0, 1.0); }
double gainDb (float v) noexcept { return ((double) v - 0.5) * 60.0; }
float gainValue (double db) noexcept { return (float) std::clamp (db / 60.0 + 0.5, 0.0, 1.0); }
double q (float v) noexcept { return 0.025 * std::pow (1600.0, (double) std::clamp (v, 0.0f, 1.0f)); }
float qValue (double qq) noexcept { return (float) std::clamp (std::log (qq / 0.025) / std::log (1600.0), 0.0, 1.0); }
int slopeDb (float v) noexcept { return slopes[(std::size_t) std::clamp ((int) std::lround (v * 8.0f), 0, 8)]; }
float slopeValue (int d) noexcept
{
    for (int i = 0; i < 9; ++i)
        if (slopes[(std::size_t) i] >= d) return (float) i / 8.0f;
    return 1.0f;
}
double outputDb (float v) noexcept { return ((double) v - 0.5) * 48.0; }
double gainScale (float v) noexcept { return (double) v * 2.0; }

const char* shapeName (Shape s) noexcept
{
    static const char* names[] = { "Off", "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf" };
    return names[(int) s];
}

const EffectInfo& info()
{
    static const EffectInfo i = [] {
        EffectInfo e { "Facet EQ", {} };
        static std::array<std::array<char, 24>, bands * fieldsPerBand> names {};
        const char* fields[] = { "Shape", "Freq", "Gain", "Q", "Slope" };
        for (int b = 0; b < bands; ++b)
            for (int f = 0; f < fieldsPerBand; ++f)
            {
                auto& n = names[(std::size_t) param (b, f)];
                std::snprintf (n.data(), n.size(), "%d %s", b + 1, fields[f]);
                const float def = f == 0 ? 0.0f : f == 1 ? hzValue (std::array<double, 8> { 50, 120, 300, 700, 1500, 3500, 8000, 15000 }[(std::size_t) b])
                                : f == 2 ? 0.5f : f == 3 ? 0.5f : slopeValue (12);
                e.params.push_back ({ n.data(), def });
            }
        e.params.push_back ({ "Output", 0.5f });
        e.params.push_back ({ "Gain Scale", 0.5f });
        return e;
    }();
    return i;
}

std::string paramText (int p, float v)
{
    char t[48];
    if (p == outputParam) { std::snprintf (t, sizeof (t), "%+.2f dB", outputDb (v)); return t; }
    if (p == scaleParam) { std::snprintf (t, sizeof (t), "%.0f%%", gainScale (v) * 100.0); return t; }
    switch (p % fieldsPerBand)
    {
        case 0: return shapeName (shape (v));
        case 1: { const auto f = hz (v); std::snprintf (t, sizeof (t), f < 1000 ? "%.1f Hz" : "%.2f kHz", f < 1000 ? f : f / 1000.0); return t; }
        case 2: std::snprintf (t, sizeof (t), "%+.2f dB", gainDb (v)); return t;
        case 3: std::snprintf (t, sizeof (t), "%.3f", q (v)); return t;
        default: std::snprintf (t, sizeof (t), "%d dB/oct", slopeDb (v)); return t;
    }
}

int design (const EffectParams& params, int band, double sampleRate, std::array<Section, maxSections>& out) noexcept
{
    Analog a[maxSections];
    const int n = analogSections (params, band, a);
    const double f0 = std::min (hz (params[(std::size_t) param (band, 1)]), 0.4999 * sampleRate);
    const double w0 = 2.0 * pi * f0 / sampleRate;
    for (int k = 0; k < n; ++k)
        out[(std::size_t) k] = matched (a[k], w0);
    return n;
}

double analogDb (const EffectParams& params, int band, double f) noexcept
{
    Analog a[maxSections];
    const int n = analogSections (params, band, a);
    const double w = f / hz (params[(std::size_t) param (band, 1)]);
    double m = 1.0;
    for (int k = 0; k < n; ++k) m *= std::sqrt (mag2 (a[k], w));
    return 20.0 * std::log10 (std::max (m, 1e-15));
}

double responseDb (const EffectParams& params, double sampleRate, double f) noexcept
{
    const double w = 2.0 * pi * f / sampleRate;
    double m = 1.0;
    for (int b = 0; b < bands; ++b)
    {
        std::array<Section, maxSections> s;
        const int n = design (params, b, sampleRate, s);
        for (int k = 0; k < n; ++k) m *= std::abs (evalDigital (s[(std::size_t) k], w));
    }
    return 20.0 * std::log10 (std::max (m, 1e-15)) + outputDb (params[(std::size_t) outputParam]);
}

std::unique_ptr<Effect> make() { return std::make_unique<FacetEqEffect>(); }
} // namespace lynx::facet
