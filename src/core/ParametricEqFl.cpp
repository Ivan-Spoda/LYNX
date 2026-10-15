#include "core/ParametricEqFl.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

// Exact Fruity Parametric EQ 2 / Fruity Parametric EQ engines (docs/analise/REA_rodada50.md). Every float/double
// rounding below is deliberate: it follows the order of operations observed in the plugins (§2–§3).

namespace lynx::peqfl
{
namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr double ln2 = 0.69314718055994529;
constexpr double halfLn2 = 0.34657359027997264;
constexpr double invSqrt2 = 0.70710678118654746;
constexpr double freqStep = 0.00015206580024203624; // log2(1000) / 65536 (§2.1)
constexpr float dbToLn = 0.115129255f;              // ln(10) / 20 as a float
constexpr float flushBelow = 5.9604644775390625e-08f; // 2^-24

// FL's UI type → internal design type (§2.2)
constexpr int internalType[8] = { 0, 1, 2, 4, 3, 6, 7, 8 };

// Per-stage tables of the "steep" orders, row = |order| (§2.4)
constexpr float steepCutQ[4][4] = { { 1, 1, 1, 1 }, { 1.98340166f, 0.749214649f, 1, 1 }, { 2.64303994f, 1.08151865f, 0.694244742f, 1 },
                                    { 3.57645154f, 1.14173281f, 1.04471076f, 0.655118585f } };
constexpr float steepShelfS[4][4] = { { 1, 1, 1, 1 }, { 2.63044715f, 0.615644455f, 1, 1 }, { 5.16122818f, 1.10927105f, 0.509867311f, 1 },
                                      { 10.3191233f, 1.51443529f, 0.694032848f, 0.552996695f } };
constexpr float steepPeakOffset[4][4] = { { 0, 0, 0, 0 }, { -0.25f, 0.25f, 0, 0 }, { -0.475f, 0, 0.475f, 0 },
                                          { -0.775f, -0.3f, 0.3f, 0.775f } };
constexpr float steepPeakWidth[4][4] = { { 1, 1, 1, 1 }, { 1, 1, 1, 1 }, { 1, 1.57079637f, 1, 1 }, { 1, 1.57079637f, 1.57079637f, 1 } };
constexpr float steepPeakGain[4] = { 0.5f, 0.32125f, 0.2442f, 0.214445f };
constexpr float steepPeakScale[4] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f };

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t offset)
{
    std::int32_t v = 0;
    if (offset + 4 <= d.size())
        std::memcpy (&v, d.data() + offset, 4);
    return v;
}

// hyperbolic sine as the plugin's run-time library computes it
double sinhRtl (double x)
{
    if (std::abs (x) <= 1.0e-12)
        return 0.0;
    return (std::exp (x) - std::exp (-x)) / 2.0;
}

// Power(base, e) on floats for a non-integral exponent: exp(e·ln base)
float powerF (float base, float e)
{
    if (e == 0.0f)
        return 1.0f;
    if (base == 0.0f)
        return e > 0.0f ? 0.0f : 1.0f;
    if (std::trunc (e) == e && std::abs (e) < 2147483647.0f)
    {
        // integral exponent: repeated multiplication (never reached by the EQ's 1/(n+1), n ≥ 1)
        double r = 1.0, b = base;
        auto k = (long long) std::abs (e);
        while (k > 0)
        {
            if (k & 1)
                r *= b;
            b *= b;
            k >>= 1;
        }
        return (float) (e < 0 ? 1.0 / r : r);
    }
    return (float) std::exp ((double) e * std::log ((double) base));
}

// Biquad design from (A, alpha, sin ω, cos ω), per internal type (§2.5)
void designStage (Stage& st, int type, double A, double alpha, double sn, double cs)
{
    st.type = type;
    if (type >= 1 && type <= 5)
    {
        const double d = 1.0 / (alpha + 1.0);
        st.a1 = d * cs * -2.0;
        st.a2 = d * (1.0 - alpha);
        switch (type)
        {
            case 1: st.b1 = d * (1.0 - cs); st.b0 = st.b1 * 0.5; st.b2 = st.b0; break;
            case 2: st.b1 = 0.0; st.b0 = d * alpha; st.b2 = d * -alpha; break;
            case 3: st.b1 = st.a1; st.b0 = d; st.b2 = d; break;
            case 4: st.b1 = d * -(cs + 1.0); st.b0 = -st.b1 * 0.5; st.b2 = st.b0; break;
            default: st.b1 = st.a1; st.b0 = st.a2; st.b2 = 1.0; break;
        }
        return;
    }
    if (type == 6 || type == 8)
    {
        const double k = (A - 1.0) * cs;
        const double beta = alpha * sn;
        if (type == 6)
        {
            const double d = 1.0 / (A + 1.0 + k + beta);
            st.a1 = d * ((A - 1.0) + (A + 1.0) * cs) * -2.0;
            st.a2 = d * ((A + 1.0 + k) - beta);
            st.b1 = d * A * 2.0 * ((A - 1.0) - (A + 1.0) * cs);
            st.b0 = d * A * (((A + 1.0) - k) + beta);
            st.b2 = d * A * (((A + 1.0) - k) - beta);
        }
        else
        {
            const double d = 1.0 / (((A + 1.0) - k) + beta);
            st.a1 = d * ((A - 1.0) - (A + 1.0) * cs) * 2.0;
            st.a2 = d * (((A + 1.0) - k) - beta);
            st.b1 = d * A * -2.0 * ((A - 1.0) + (A + 1.0) * cs);
            st.b0 = d * A * (A + 1.0 + k + beta);
            st.b2 = d * A * ((A + 1.0 + k) - beta);
        }
        return;
    }
    if (type == 7)
    {
        const double d = 1.0 / (alpha / A + 1.0);
        st.a1 = d * cs * -2.0;
        st.a2 = d * (1.0 - alpha / A);
        st.b1 = st.a1;
        st.b0 = d * (alpha * A + 1.0);
        st.b2 = d * (1.0 - alpha * A);
        return;
    }
    st.b0 = 1.0, st.b1 = st.b2 = st.a1 = st.a2 = 0.0;
}

// Parametric EQ 2 band design (§2.3–§2.4)
void designPeq2 (const Settings& s, int b, double rate, Band& band)
{
    const int ui = s.type[(std::size_t) b];
    const int t = ui >= 0 && ui < 8 ? internalType[ui] : 0;
    const auto freq = (float) (std::exp ((double) (float) s.freq[(std::size_t) b] * freqStep * ln2) * 20.0);
    const int order = std::clamp (s.order[(std::size_t) b], -3, 3);
    const int n = std::abs (order);
    const bool steep = order < 0;
    const auto inv = (float) (1.0 / (n + 1));
    const auto kbp = (float) ((double) ((float) std::exp ((double) (dbToLn * 3.0f)) / (float) std::exp ((double) (dbToLn * 6.0f)))
                              * std::sqrt ((double) (n + 1)));
    const auto knotch = 1.0f / kbp;

    band.type = t;
    band.stages = 0;
    if (t < 1)
    {
        band.active = 0;
        return;
    }

    const auto w = (float) s.width[(std::size_t) b] * 1.52587890625e-05f;
    float gain = 0.0f;
    if (t < 6)
        band.active = -1;
    else
    {
        gain = (float) s.gain[(std::size_t) b] * 0.01f;
        band.active = gain == 0.0f ? (int) rate / 5 : -1;
    }

    const double wk = (1.0 / rate) * twoPi;
    band.stages = n + 1;
    for (int k = 0; k <= n; ++k)
    {
        double A = 0.0, alpha = 0.0, sn = 0.0, cs = 0.0;
        if (t == 2 || t == 3)
        {
            const double om = std::min ((double) freq * wk, 3.138451099395752);
            sn = std::sin (om), cs = std::cos (om);
            const double bw = ((double) w * 4.9 + 0.1) * (double) (t == 2 ? kbp : knotch);
            alpha = sn * sinhRtl ((halfLn2 * bw * om) / sn);
        }
        else if (t == 1 || t == 4)
        {
            const double om = std::min ((double) freq * wk, 3.138451099395752);
            sn = std::sin (om), cs = std::cos (om);
            double q = (double) (1.0f - w);
            q = q * q * q * 9.9 + 0.1;
            if (n > 0)
                q = (double) powerF ((float) q, inv);
            if (steep)
                q = q * (double) steepCutQ[n][k];
            alpha = (sn * invSqrt2) / q;
        }
        else if (t == 7)
        {
            double om = std::min ((double) freq * wk, 2.1991147994995117);
            double bw = (double) w * 4.9 + 0.1;
            double g;
            if (n < 1 || ! steep)
                g = (double) gain * (double) inv * 0.5;
            else
            {
                const double scale = steepPeakScale[n];
                const double shifted = (double) freq * std::exp ((double) steepPeakOffset[n][k] * bw * scale * ln2);
                bw = bw * scale * (double) steepPeakWidth[n][k];
                g = n == 1 ? (double) steepPeakGain[1] - (bw - 1.0) * 0.01 : (double) steepPeakGain[n];
                g = (double) gain * g;
                om = std::min ((double) (float) shifted * wk, 2.5132741928100586);
            }
            sn = std::sin (om), cs = std::cos (om);
            A = (double) (float) std::exp ((double) ((float) g * dbToLn));
            alpha = sn * sinhRtl ((halfLn2 * bw * om) / sn);
        }
        else // shelves (and the unused all-pass type)
        {
            const double om = std::min ((double) freq * wk, 3.138451099395752);
            sn = std::sin (om), cs = std::cos (om);
            A = (double) (float) std::exp ((double) (gain * inv * 0.5f * dbToLn));
            double S = (double) (1.0f - w) * 1.7192982456140353 + 0.3333333333333333;
            if (steep)
                S = S * (double) steepShelfS[n][k];
            alpha = std::sqrt (A) * std::sqrt ((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        }
        designStage (band.stage[(std::size_t) k], t, A, alpha, sn, cs);
    }
}

// Parametric EQ (v1): one biquad per band (§3)
void designPeq1 (const Settings& s, int b, double rate, Band& band)
{
    const int ui = s.type[(std::size_t) b];
    const int t = ui >= 0 && ui < 8 ? internalType[ui] : 0;
    band.type = t;
    band.stages = 0;
    if (t < 1)
    {
        band.active = 0;
        return;
    }
    const auto x = (float) ((double) s.freq[(std::size_t) b] * 1.52587890625e-05);
    const auto e = (float) (std::exp ((double) x * std::log (101.0)) - 1.0);
    auto freq = (float) ((double) e * 159.9 + 10.0);
    const auto w = (float) ((double) s.width[(std::size_t) b] * 1.52587890625e-05);
    const auto gain = (float) s.gain[(std::size_t) b] * 0.01f;
    band.active = gain == 0.0f && t > 5 ? (int) rate / 5 : -1;

    const auto nyquist = (float) ((double) (int) rate * 0.4999); // §3.2: the rate object's limit
    if (freq > nyquist)
        freq = nyquist;
    const double om = (double) freq * ((1.0 / rate) * twoPi);
    const double sn = std::sin (om), cs = std::cos (om);
    double A = 0.0, alpha;
    if (t >= 1 && t <= 5)
        alpha = sn / (((double) w * 18.9 + 0.1) * 2.0);
    else
    {
        const double ge = (double) gain * 0.025;
        A = ge == 0.0 ? 1.0 : std::exp (ge * std::log (10.0));
        if (t == 7)
        {
            const auto q = (float) (std::exp ((double) w * std::log ((double) (1000.0f + 1.0f))) - 1.0);
            alpha = sn / (((double) q * 0.0999 + 0.1) * 2.0);
        }
        else
            alpha = std::sqrt ((A * A + 1.0) / ((double) w * 1.9 + 0.1) - (A - 1.0) * (A - 1.0));
    }
    band.stages = 1;
    designStage (band.stage[0], t, A, alpha, sn, cs);
}

void runStage (Stage& st, float* io, int n) noexcept
{
    double x1l = st.s[0], x1r = st.s[1], x2l = st.s[2], x2r = st.s[3];
    double y1l = st.s[4], y1r = st.s[5], y2l = st.s[6], y2r = st.s[7];
    const double b0 = st.b0, b1 = st.b1, b2 = st.b2, a1 = st.a1, a2 = st.a2;
    for (int i = 0; i < n; ++i)
    {
        const double xl = io[2 * i], xr = io[2 * i + 1];
        const double yl = xl * b0 + (((x1l * b1 - y2l * a2) + x2l * b2) - y1l * a1);
        const double yr = xr * b0 + (((x1r * b1 - y2r * a2) + x2r * b2) - y1r * a1);
        x2l = x1l, x2r = x1r, x1l = xl, x1r = xr;
        y2l = y1l, y2r = y1r, y1l = yl, y1r = yr;
        io[2 * i] = (float) yl;
        io[2 * i + 1] = (float) yr;
    }
    const double out[8] = { x1l, x1r, x2l, x2r, y1l, y1r, y2l, y2r };
    // end of call: state values below 2^-24 in magnitude are cleared (§2.6)
    for (int k = 0; k < 8; ++k)
        st.s[k] = std::abs (out[k]) < (double) flushBelow ? 0.0 : out[k];
}

void clearStages (Band& band) noexcept
{
    for (auto& st : band.stage)
        std::fill (std::begin (st.s), std::end (st.s), 0.0);
}

// 2:1 polyphase half-band (§2.7): states per channel pair: outputs A1 A2 A3 B1 B2 B3 (L,R), then input delays
constexpr double hbA[3] = { 0.14737711360104661, 0.48246854276970014, 0.88300502576937312 };
constexpr double hbB[3] = { 0.039151597734460045, 0.30264684832849342, 0.6746159185469639 };

void flushHalfBand (double* s) noexcept
{
    for (int k = 0; k < 12; ++k)
        if (std::abs (s[k]) < (double) flushBelow)
            s[k] = 0.0;
}

void upsample (double* s, const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            double* o = s + c;      // outputs at o[0], o[2], …, o[10]
            double* d = s + 12 + c; // input delays
            const double v = in[2 * i + c];
            const double a1 = d[0] + (v - o[0]) * hbA[0];
            d[0] = v, o[0] = a1;
            const double b1 = d[6] + (v - o[6]) * hbB[0];
            d[6] = v, o[6] = b1;
            const double a2 = d[2] + (a1 - o[2]) * hbA[1];
            d[2] = a1, o[2] = a2;
            const double b2 = d[8] + (b1 - o[8]) * hbB[1];
            d[8] = b1, o[8] = b2;
            const double a3 = d[4] + (a2 - o[4]) * hbA[2];
            o[4] = a3;
            const double b3 = d[10] + (b2 - o[10]) * hbB[2];
            o[10] = b3;
            d[4] = a2, d[10] = b2;
            out[4 * i + c] = (float) b3;
            out[4 * i + 2 + c] = (float) a3;
        }
    flushHalfBand (s);
}

void downsample (double* s, const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < 2; ++c)
        {
            double* o = s + c;
            double* d = s + 12 + c;
            const double e = in[4 * i + c], od = in[4 * i + 2 + c];
            const double a1 = d[0] + (e - o[0]) * hbA[0];
            d[0] = e, o[0] = a1;
            const double b1 = d[6] + (od - o[6]) * hbB[0];
            d[6] = od, o[6] = b1;
            const double a2 = d[2] + (a1 - o[2]) * hbA[1];
            d[2] = a1, o[2] = a2;
            const double b2 = d[8] + (b1 - o[8]) * hbB[1];
            d[8] = b1, o[8] = b2;
            const double a3 = d[4] + (a2 - o[4]) * hbA[2];
            d[4] = a2, o[4] = a3;
            const double b3 = d[10] + (b2 - o[10]) * hbB[2];
            d[10] = b2, o[10] = b3;
            out[2 * i + c] = (float) ((b3 + a3) * 0.5);
        }
    flushHalfBand (s);
}

bool oversampled (const Settings& s) noexcept
{
    return s.law == Law::peq2 && (s.processing == Processing::hq || s.processing == Processing::hqExternal);
}

float peq2MainGain (int v) noexcept
{
    return (float) std::exp ((double) ((float) v * 0.01f * dbToLn));
}

float peq1MainGain (int v) noexcept
{
    const float x = (float) v * 0.0078125f;
    if (x == 1.0f)
        return 1.0f;
    return (float) ((std::exp ((double) (x * 2.39789534f)) - 1.0) * (double) 0.1f);
}

float raw01 (int v, double range) { return (float) ((double) v / range); }
int fromRaw01 (float p, double range) { return (int) std::lround ((double) p * range); }
} // namespace

// ------------------------------------------------------------------------------------------------------- states

std::optional<Settings> parsePeq2 (const std::vector<std::uint8_t>& d)
{
    // §1: version, 36 parameters, A/B copy, UI flags; optional fields by version
    if (d.size() < 0x12D)
        return std::nullopt;
    Settings s;
    s.law = Law::peq2;
    const auto version = i32At (d, 0);
    auto p = [&] (int i) { return i32At (d, 4 + 4 * (std::size_t) i); };
    for (int b = 0; b < 7; ++b)
    {
        s.gain[(std::size_t) b] = p (b);
        s.freq[(std::size_t) b] = p (7 + b);
        s.width[(std::size_t) b] = p (14 + b);
        s.type[(std::size_t) b] = p (21 + b);
        s.order[(std::size_t) b] = p (28 + b);
    }
    s.main = p (35);
    const auto ui = (std::uint32_t) i32At (d, 0x124);
    const bool hq = (ui & 1) != 0;
    const auto design = (std::uint8_t) (ui >> 1);
    std::uint8_t options = 0;
    if (version >= 4 && d.size() >= 0x142)
    {
        options = d[0x13D];
        s.mask = (std::uint32_t) i32At (d, 0x13E) & 0x7F;
    }
    s.linearPhase = (options & 1) != 0;
    if (hq && design == 0)
        s.processing = (options & 2) != 0 ? Processing::hqExternal : Processing::hq;
    else if (hq && design == 1)
        s.processing = Processing::hqAlternative;
    return s;
}

std::optional<Settings> parsePeq1 (const std::vector<std::uint8_t>& d)
{
    // §3.1: 29 parameters (7 gains, 7 frequencies, 7 widths, 7 types, main), no version
    if (d.size() < 4 * 28)
        return std::nullopt;
    Settings s;
    s.law = Law::peq1;
    auto p = [&] (int i) { return i32At (d, 4 * (std::size_t) i); };
    for (int b = 0; b < 7; ++b)
    {
        s.gain[(std::size_t) b] = p (b);
        s.freq[(std::size_t) b] = p (7 + b);
        s.width[(std::size_t) b] = p (14 + b);
        s.type[(std::size_t) b] = p (21 + b);
    }
    s.main = d.size() >= 4 * 29 ? p (28) : 128;
    return s;
}

Law lawOf (const EffectParams& params) noexcept
{
    const auto v = (int) std::lround (params[(std::size_t) lawParam] * 2.0f);
    return v == 1 ? Law::peq2 : v == 2 ? Law::peq1 : Law::lynx;
}

namespace
{
// LYNX's own frequency value of a v1 frequency (the editor's 20 Hz…20 kHz law)
float lynxHzValue (double hz) { return (float) std::clamp (std::log (std::max (hz, 1.0) / 20.0) / std::log (1000.0), 0.0, 1.0); }
double peq1Hz (int raw)
{
    const auto x = (float) ((double) raw * 1.52587890625e-05);
    const auto e = (float) (std::exp ((double) x * std::log (101.0)) - 1.0);
    return (double) (float) ((double) e * 159.9 + 10.0);
}
double peq1MainDb (int raw) { return 20.0 * std::log10 (std::max ((double) peq1MainGain (raw), 1.0e-6)); }
float gainParam (double db) { return (float) std::clamp (db / 36.0 + 0.5, 0.0, 1.0); }
// FL UI type ↔ LYNX band type (core/Effects.h peq::BandType order: off, peak, lowShelf, highShelf, lowCut, highCut,
// bandPass, notch)
constexpr int lynxOfFl[8] = { 0, 5, 6, 4, 7, 2, 1, 3 };
constexpr int flOfLynx[8] = { 0, 6, 5, 7, 3, 1, 2, 4 };
} // namespace

void toParams (const Settings& s, EffectParams& params) noexcept
{
    for (int b = 0; b < 7; ++b)
    {
        const auto ub = (std::size_t) b;
        const int ui = s.type[ub] >= 0 && s.type[ub] < 8 ? s.type[ub] : 0;
        params[(std::size_t) (4 * b)] = (float) lynxOfFl[ui] / 7.0f;
        params[(std::size_t) (4 * b + 2)] = std::clamp ((float) ((double) s.gain[ub] / 3600.0 + 0.5), 0.0f, 1.0f);
        params[(std::size_t) (4 * b + 3)] = std::clamp (raw01 (s.width[ub], 65536.0), 0.0f, 1.0f);
        params[(std::size_t) (29 + b)] = (float) (std::clamp (s.order[ub], -3, 3) + 3) / 6.0f;
        if (s.law == Law::peq2)
            params[(std::size_t) (4 * b + 1)] = std::clamp (raw01 (s.freq[ub], 65536.0), 0.0f, 1.0f);
        else
        {
            params[(std::size_t) (v1FreqParam + b)] = std::clamp (raw01 (s.freq[ub], 65536.0), 0.0f, 1.0f);
            params[(std::size_t) (4 * b + 1)] = lynxHzValue (peq1Hz (s.freq[ub]));
        }
    }
    if (s.law == Law::peq2)
        params[28] = std::clamp ((float) ((double) s.main / 3600.0 + 0.5), 0.0f, 1.0f);
    else
    {
        params[(std::size_t) v1MainParam] = std::clamp (raw01 (s.main, 256.0), 0.0f, 1.0f);
        params[28] = gainParam (peq1MainDb (s.main));
    }
    params[(std::size_t) lawParam] = s.law == Law::peq1 ? 1.0f : 0.5f;
    params[(std::size_t) processingParam] = (float) (int) s.processing / 3.0f;
    params[(std::size_t) linearParam] = s.linearPhase ? 1.0f : 0.0f;
    params[(std::size_t) maskParam] = (float) (s.mask & 0x7F) / 127.0f;
}

Settings fromParams (const EffectParams& params) noexcept
{
    Settings s;
    s.law = lawOf (params) == Law::peq1 ? Law::peq1 : Law::peq2;
    for (int b = 0; b < 7; ++b)
    {
        const auto ub = (std::size_t) b;
        const int lt = std::clamp ((int) std::lround (params[(std::size_t) (4 * b)] * 7.0f), 0, 7);
        s.type[ub] = flOfLynx[lt];
        s.gain[ub] = (int) std::lround (((double) params[(std::size_t) (4 * b + 2)] - 0.5) * 3600.0);
        s.width[ub] = fromRaw01 (params[(std::size_t) (4 * b + 3)], 65536.0);
        s.order[ub] = std::clamp ((int) std::lround (params[(std::size_t) (29 + b)] * 6.0f) - 3, -3, 3);
        const auto f = params[(std::size_t) (4 * b + 1)];
        if (s.law == Law::peq2)
            s.freq[ub] = fromRaw01 (f, 65536.0);
        else
        {
            // FL's own value while the band has not been moved in the editor; else the nearest v1 value
            const int raw = fromRaw01 (params[(std::size_t) (v1FreqParam + b)], 65536.0);
            if (lynxHzValue (peq1Hz (raw)) == f)
                s.freq[ub] = raw;
            else
            {
                const double hz = 20.0 * std::pow (1000.0, std::clamp ((double) f, 0.0, 1.0));
                const double x = std::log ((std::clamp (hz, 10.0, 16000.0) - 10.0) / 159.9 + 1.0) / std::log (101.0);
                s.freq[ub] = std::clamp ((int) std::lround (x * 65536.0), 0, 65536);
            }
            s.order[ub] = 0;
        }
    }
    if (s.law == Law::peq2)
        s.main = (int) std::lround (((double) params[28] - 0.5) * 3600.0);
    else
    {
        const int raw = fromRaw01 (params[(std::size_t) v1MainParam], 256.0);
        if (gainParam (peq1MainDb (raw)) == params[28])
            s.main = raw;
        else
        {
            // nearest raw level to the edited dB value: gain = (11^(raw/128) − 1)/10
            const double g = std::pow (10.0, ((double) params[28] - 0.5) * 36.0 / 20.0);
            s.main = std::clamp ((int) std::lround (128.0 * std::log (g * 10.0 + 1.0) / std::log (11.0)), 0, 256);
        }
    }
    s.processing = (Processing) std::clamp ((int) std::lround (params[(std::size_t) processingParam] * 3.0f), 0, 3);
    s.linearPhase = params[(std::size_t) linearParam] >= 0.5f;
    s.mask = (std::uint32_t) std::clamp ((int) std::lround (params[(std::size_t) maskParam] * 127.0f), 0, 127);
    return s;
}

void designBand (const Settings& s, int b, double rate, Band& band)
{
    if (s.law == Law::peq1)
        designPeq1 (s, b, rate, band);
    else
        designPeq2 (s, b, rate, band);
}

double responseDb (const Settings& s, double sr, double hz)
{
    const double rate = oversampled (s) ? sr * 2.0 : sr;
    const auto w = twoPi * std::clamp (hz, 1.0, sr * 0.499) / rate;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    double total = 20.0 * std::log10 (std::max ((double) (s.law == Law::peq1 ? peq1MainGain (s.main) : peq2MainGain (s.main)), 1.0e-9));
    for (int b = 0; b < 7; ++b)
    {
        if (s.law == Law::peq2 && (s.mask & (1u << b)) == 0)
            continue;
        Band band;
        designBand (s, b, rate, band);
        if (band.active == 0)
            continue;
        for (int k = 0; k < band.stages; ++k)
        {
            const auto& c = band.stage[(std::size_t) k];
            const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
            total += 20.0 * std::log10 (std::max (std::abs (h), 1.0e-9));
        }
    }
    return total;
}

// ------------------------------------------------------------------------------------------------------- engine

double Engine::processingRate() const noexcept
{
    return oversampled (settings) ? sampleRate * 2.0 : sampleRate;
}

void Engine::prepare (double sr) noexcept
{
    sampleRate = sr;
    silenceFrames = (int) sr / 5;
    buffer.assign (2 * 512, 0.0f);
    wide.assign (4 * 512, 0.0f);
    if (configured)
        for (int b = 0; b < 7; ++b)
            designBand (settings, b, processingRate(), bands[(std::size_t) b]);
    reset();
}

void Engine::reset() noexcept
{
    for (auto& band : bands)
        clearStages (band);
    std::fill (std::begin (up), std::end (up), 0.0);
    std::fill (std::begin (down), std::end (down), 0.0);
    silenceCount = 0;
    if (configured)
        for (int b = 0; b < 7; ++b)
            designBand (settings, b, processingRate(), bands[(std::size_t) b]); // restarts the 0 dB countdowns
}

void Engine::set (const Settings& s) noexcept
{
    const bool rateChange = ! configured || oversampled (s) != oversampled (settings);
    const auto old = settings;
    settings = s;
    mainGain = s.law == Law::peq1 ? peq1MainGain (s.main) : peq2MainGain (s.main);
    for (int b = 0; b < 7; ++b)
    {
        auto& band = bands[(std::size_t) b];
        const auto ub = (std::size_t) b;
        const bool same = configured && old.law == s.law && old.gain[ub] == s.gain[ub] && old.freq[ub] == s.freq[ub]
                          && old.width[ub] == s.width[ub] && old.type[ub] == s.type[ub] && old.order[ub] == s.order[ub];
        if (same && ! rateChange)
            continue;
        if (! configured || old.type[ub] != s.type[ub] || old.order[ub] != s.order[ub])
            clearStages (band); // FL clears a band's filters when its type or order changes
        designBand (s, b, processingRate(), band);
    }
    if (rateChange)
    {
        std::fill (std::begin (up), std::end (up), 0.0);
        std::fill (std::begin (down), std::end (down), 0.0);
    }
    configured = true;
}

void Engine::flushFilters() noexcept
{
    // FL's silence reset: only bands still counting down (0 dB) lose their state; the resamplers are cleared
    for (auto& band : bands)
        if (band.active > 0)
            clearStages (band);
    std::fill (std::begin (up), std::end (up), 0.0);
    std::fill (std::begin (down), std::end (down), 0.0);
}

void Engine::block (float* io, int n) noexcept
{
    if (settings.law == Law::peq2)
    {
        // silence gate (§2.8): a block whose samples are all within ±1e-6 counts towards rate/5 frames of silence
        bool silent = true;
        for (int i = 0; i < 2 * n && silent; ++i)
            silent = std::abs (io[i]) <= 9.99999997e-07f;
        if (! silent || settings.linearPhase)
            silenceCount = silenceFrames;
        else
            silenceCount -= n;
        if (silenceCount <= -n)
        {
            if (silenceCount > -0x3FFFFFFF)
                flushFilters();
            silenceCount = (int) 0xC0000001;
            return; // passes the input unchanged
        }
    }

    if (mainGain == 0.0f)
        std::fill (io, io + 2 * n, 0.0f);
    else if (mainGain != 1.0f)
        for (int i = 0; i < 2 * n; ++i)
            io[i] *= mainGain;

    float* x = io;
    int m = n;
    const bool os = oversampled (settings);
    if (os)
    {
        upsample (up, io, wide.data(), n);
        x = wide.data();
        m = 2 * n;
    }
    for (int b = 0; b < 7; ++b)
    {
        auto& band = bands[(std::size_t) b];
        if (band.active == 0 || (settings.law == Law::peq2 && (settings.mask & (1u << b)) == 0))
            continue;
        for (int k = 0; k < band.stages; ++k)
            runStage (band.stage[(std::size_t) k], x, m);
        if (band.active > 0 && (band.active -= m) < 1)
        {
            band.active = 0;
            clearStages (band);
        }
    }
    if (os)
        downsample (down, wide.data(), io, n);
}

void Engine::process (float* left, float* right, int n) noexcept
{
    if (! configured)
        return;
    for (int done = 0; done < n;)
    {
        const int len = std::min (512, n - done);
        for (int i = 0; i < len; ++i)
        {
            buffer[(std::size_t) (2 * i)] = left[done + i];
            buffer[(std::size_t) (2 * i + 1)] = right[done + i];
        }
        block (buffer.data(), len);
        for (int i = 0; i < len; ++i)
        {
            left[done + i] = buffer[(std::size_t) (2 * i)];
            right[done + i] = buffer[(std::size_t) (2 * i + 1)];
        }
        done += len;
    }
}
} // namespace lynx::peqfl
