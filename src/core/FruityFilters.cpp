#include "core/FruityFilters.h"

// FL's small filters, exact engines (docs/analise/REA_rodada63.md). Clean-room: written from the behaviour, formulas
// and data layouts described there; nothing of Image-Line's code or data is copied. Arithmetic follows the plugins
// operation by operation (float/double steps in their order, one rounding each, no contraction); the x87 kernels run
// in 80-bit long double with 64-bit precision and round-to-nearest; the SSE parts run with FTZ/DAZ as in FL.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
#include "core/Simd.h"
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace lynx::flfilter
{
namespace
{
using ld = long double;
constexpr int engineBlock = 512;
constexpr float tiny = 5.9604644775390625e-08f; // 2^-24

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}
std::uint32_t bitsOf (float f) noexcept
{
    std::uint32_t b = 0;
    std::memcpy (&b, &f, 4);
    return b;
}
int rawFrom (float v, int lo, int hi) noexcept
{
    return lo + (int) std::lround (std::clamp ((double) v, 0.0, 1.0) * (hi - lo));
}
float norm (int raw, int lo, int hi) noexcept { return (float) std::clamp ((double) (raw - lo) / (hi - lo), 0.0, 1.0); }

// a control's value as the plugins' ProcessParam computes it: (raw − min) · (1/range) in double, clamped to 0…1
float unit (int raw, int lo, double scale) noexcept { return (float) std::clamp ((double) (raw - lo) * scale, 0.0, 1.0); }

// flush-to-zero / denormals-are-zero while an engine runs (the plugins' MXCSR)
class Ftz
{
public:
    Ftz() noexcept
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u);
#endif
    }
    ~Ftz()
    {
#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
        _mm_setcsr (saved);
#endif
    }
    Ftz (const Ftz&) = delete;
    Ftz& operator= (const Ftz&) = delete;

private:
    unsigned int saved = 0;
};

// x87 control word: 64-bit precision, round to nearest, for the extended-precision kernels
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
struct X87Mode
{
    unsigned short saved = 0;
    X87Mode() noexcept
    {
        asm volatile ("fnstcw %0" : "=m"(saved) : : "memory");
        unsigned short cw = (unsigned short) ((saved & ~0x0F00u) | 0x0300u);
        asm volatile ("fldcw %0" : : "m"(cw) : "memory");
    }
    ~X87Mode() { asm volatile ("fldcw %0" : : "m"(saved) : "memory"); }
    X87Mode (const X87Mode&) = delete;
    X87Mode& operator= (const X87Mode&) = delete;
};
#else
struct X87Mode
{
};
#endif

// the plugins' end-of-call guard on an x87 state: |s| ≤ 2^-24 (or NaN) → 0, stored as a double
double guard (ld s) noexcept { return (std::fabs (s) > (ld) tiny) ? (double) s : 0.0; }

const float ln601f = (float) std::log (601.0), inv600f = 1.0f / 600.0f;
} // namespace

// ================================================================================================ Fruity Fast LP
std::optional<FastLPState> parseFastLP (const std::vector<std::uint8_t>& d)
{
    // i32 version (the plugin reads states of version < 2), then the two raw controls, unclamped
    if (d.size() < 12 || i32At (d, 0) >= 2)
        return std::nullopt;
    return FastLPState { i32At (d, 4), i32At (d, 8) };
}

void fastLPToParams (const FastLPState& s, float* p) noexcept
{
    p[0] = norm (s.cutoff, 0, 1024);
    p[1] = norm (s.reso, 0, 1024);
}

FastLPState fastLPFromParams (const float* p) noexcept { return { rawFrom (p[0], 0, 1024), rawFrom (p[1], 0, 1024) }; }

std::string fastLPParamText (int param, float v01)
{
    char b[32];
    std::snprintf (b, sizeof b, "%d%%", (int) std::lround (std::clamp (v01, 0.0f, 1.0f) * 100.0f));
    return param == 0 || param == 1 ? std::string (b) : std::string();
}

void FastLPEngine::set (const FastLPState& s) noexcept
{
    raw = s;
    Ftz ftz;
    // cutoff: x' = (x + 1/63)·(63/64), coefficient (601^x' − 1)/600 between 0 and 1
    const float x = (unit (s.cutoff, 0, 1.0 / 1024.0) + 0.0158730168f) * 0.984375f;
    if (x >= 1.0f)
        c = 1.0f;
    else if (! (x > 0.0f))
        c = 0.0f;
    else
        c = (float) ((std::exp ((double) (x * ln601f)) - 1.0) * (double) inv600f);
    // resonance: feedback 1 − (601^(1 − 0.625·y) − 1)/600
    float y = unit (s.reso, 0, 1.0 / 1024.0) * 0.625f;
    if (! (y > 0.0f))
        q = 0.0f;
    else
    {
        if ((double) y > 0.625)
            y = 0.625f;
        q = 1.0f - (float) ((std::exp ((double) ((1.0f - y) * ln601f)) - 1.0) * (double) inv600f);
    }
}

void FastLPEngine::process (float* left, float* right, int n) noexcept
{
    if (n <= 0)
        return;
    float start = cur, slope = 0.0f;
    {
        Ftz ftz;
        if (! (cur == c))
        {
            if (n >= 128)
            {
                slope = (c - cur) / (float) n;
                cur = c;
            }
            else
            {
                slope = (float) ((double) (c - cur) * (1.0 / 128.0));
                cur = cur + slope * (float) n;
                if (tiny > cur)
                {
                    cur = 0.0f;
                    slope = (float) ((double) -start * (1.0 / 128.0));
                }
            }
        }
    }
    if (bitsOf (c) == 0x3f800000u && bitsOf (q) == 0 && bitsOf (slope) == 0)
    {
        // fully open and settled: the input passes untouched and the filter follows it
        lp[0] = (double) left[n - 1];
        lp[1] = (double) right[n - 1];
        return;
    }
    X87Mode mode;
    ld l0 = lp[0], l1 = lp[1], v0 = v[0], v1 = v[1];
    ld cc = start;
    const ld qq = q, ss = slope;
    for (int i = 0; i < n; ++i)
    {
        v0 += ((ld) left[i] - l0) * cc;
        l0 += v0;
        left[i] = (float) l0;
        v0 *= qq;
        v1 += ((ld) right[i] - l1) * cc;
        l1 += v1;
        right[i] = (float) l1;
        v1 *= qq;
        cc += ss;
    }
    v[0] = guard (v0), lp[0] = guard (l0), v[1] = guard (v1), lp[1] = guard (l1);
}

// ================================================================================================ Fruity Free Filter
namespace
{
constexpr double w44 = 0.00014247585730565955; // 2π/44100: the design rate, fixed
const char* const freeTypes[7] = { "Low pass", "Band pass", "High pass", "Notch", "Low shelf", "Peaking", "High shelf" };

// Delphi Power (10, e): exactly 1 for e = 0, otherwise exp(e·ln 10)
double power10 (double e) noexcept { return e == 0.0 ? 1.0 : std::exp (e * std::log (10.0)); }

// x87 round to nearest (even) of a double
int roundEven (double v) noexcept { return (int) std::nearbyint (v); }

float freeFreqHz (float x) noexcept
{
    const double e = std::exp ((double) x * std::log (101.0));
    return (float) ((double) (float) (e - 1.0) * 159.9 + 10.0);
}
} // namespace

std::optional<FreeFilterState> parseFreeFilter (const std::vector<std::uint8_t>& d)
{
    // i32 version (< 3), 4 raw controls, then (version ≥ 2) the CENTER byte; a type below 100 is an old layout
    // (type·10) converted to the current scale
    if (d.size() < 20)
        return std::nullopt;
    const int version = i32At (d, 0);
    if (version >= 3)
        return std::nullopt;
    FreeFilterState s { i32At (d, 4), i32At (d, 8), i32At (d, 12), i32At (d, 16), false };
    if (version > 1)
    {
        if (d.size() < 21)
            return std::nullopt;
        s.center = (d[20] & 1) != 0;
    }
    if (s.type < 100)
        s.type = 100 + roundEven ((((double) (s.type / 10) - 0.5) / 7.0) * 600.0);
    return s;
}

void freeFilterToParams (const FreeFilterState& s, float* p) noexcept
{
    p[0] = norm (s.type, 100, 700);
    p[1] = norm (s.freq, 0, 1024);
    p[2] = norm (s.q, 0, 1024);
    p[3] = norm (s.gain, 0, 1024);
    p[4] = s.center ? 1.0f : 0.0f;
}

FreeFilterState freeFilterFromParams (const float* p) noexcept
{
    return { rawFrom (p[0], 100, 700), rawFrom (p[1], 0, 1024), rawFrom (p[2], 0, 1024), rawFrom (p[3], 0, 1024), p[4] >= 0.5f };
}

std::string freeFilterParamText (int param, float v01)
{
    char b[48];
    const float x = std::clamp (v01, 0.0f, 1.0f);
    switch (param)
    {
        case 0:
        {
            const int t = std::clamp (roundEven ((double) (x * 7.0f) - 0.5) + 1, 1, 7);
            return freeTypes[t - 1];
        }
        case 1:
            std::snprintf (b, sizeof b, "%d Hz", (int) std::lround (freeFreqHz (x)));
            return b;
        case 2:
            std::snprintf (b, sizeof b, "%d%%", (int) std::lround (x * 100.0f));
            return b;
        case 3:
            std::snprintf (b, sizeof b, "%+.1f dB", (double) (x * 36.0f - 18.0f));
            return b;
        case 4:
            return x >= 0.5f ? "On" : "Off";
        default:
            return {};
    }
}

FreeFilterEngine::FreeFilterEngine() noexcept
{
    const FreeFilterState d {};
    raw = d;
    param (0, d.type), param (1, d.freq), param (2, d.q), param (3, d.gain);
}

void FreeFilterEngine::prepare (int sampleRate) noexcept
{
    dcC = (float) (1.0070234999999998 / (double) std::max (1, sampleRate));
    dcQ = 0.96f;
    std::fill (std::begin (dc), std::end (dc), 0.0);
}

void FreeFilterEngine::set (const FreeFilterState& s) noexcept
{
    raw.center = s.center;
    if (s.type != raw.type)
        param (0, s.type);
    if (s.freq != raw.freq)
        param (1, s.freq);
    if (s.q != raw.q)
        param (2, s.q);
    if (s.gain != raw.gain)
        param (3, s.gain);
}

void FreeFilterEngine::param (int k, int v) noexcept
{
    Ftz ftz;
    if (k == 0)
    {
        raw.type = v;
        const float x = unit (v, 100, 1.0 / 600.0);
        const int t = std::clamp (roundEven ((double) (x * 7.0f) - 0.5) + 1, 1, 7);
        if (t != type)
        {
            type = t;
            std::fill (std::begin (st), std::end (st), 0.0f); // a new type starts from a cleared filter
        }
    }
    else if (k == 1)
        raw.freq = v, freq = freeFreqHz (unit (v, 0, 1.0 / 1024.0));
    else if (k == 2)
        raw.q = v, q = unit (v, 0, 1.0 / 1024.0);
    else
        raw.gain = v, gain = unit (v, 0, 1.0 / 1024.0) * 36.0f + -18.0f;
    design();
}

void FreeFilterEngine::design() noexcept
{
    if (freq > 22000.0f)
        freq = 22000.0f;
    double w = (double) freq * w44;
    if (w < 0.0004)
        w = 0.0004;
    const double sn = std::sin (w), cs = std::cos (w);
    double A = 0.0, al;
    if (type == 6)
    {
        A = power10 ((double) gain * 0.025);
        const double e = std::exp ((double) q * std::log (1001.0));
        al = sn / (((double) (float) (e - 1.0) * 0.0999 + 0.1) * 2.0);
    }
    else if (type == 5 || type == 7)
    {
        A = power10 ((double) gain * 0.025);
        al = std::sqrt ((A * A + 1.0) / ((double) q * 1.9 + 0.1) - (A - 1.0) * (A - 1.0));
    }
    else
        al = sn / (((double) q * 18.9 + 0.1) * 2.0);

    float& b0 = co[0]; float& b1 = co[1]; float& b2 = co[2]; float& a1 = co[3]; float& a2 = co[4];
    if (type == 5 || type == 7)
    {
        const double k = (A - 1.0) * cs, be = al * sn;
        if (type == 5) // low shelf
        {
            const double d = 1.0 / (A + 1.0 + k + be);
            a1 = (float) (d * ((A - 1.0) + (A + 1.0) * cs) * -2.0);
            a2 = (float) (d * ((A + 1.0 + k) - be));
            b1 = (float) (d * A * 2.0 * ((A - 1.0) - (A + 1.0) * cs));
            b0 = (float) (d * A * (((A + 1.0) - k) + be));
            b2 = (float) (d * A * (((A + 1.0) - k) - be));
        }
        else // high shelf
        {
            const double d = 1.0 / (((A + 1.0) - k) + be);
            a1 = (float) (d * ((A - 1.0) - (A + 1.0) * cs) * 2.0);
            a2 = (float) (d * (((A + 1.0) - k) - be));
            b1 = (float) (d * A * -2.0 * ((A - 1.0) + (A + 1.0) * cs));
            b0 = (float) (d * A * (A + 1.0 + k + be));
            b2 = (float) (d * A * ((A + 1.0 + k) - be));
        }
        return;
    }
    if (type == 6) // peaking
    {
        const double d = 1.0 / (al / A + 1.0);
        a1 = (float) (d * cs * -2.0);
        a2 = (float) (d * (1.0 - al / A));
        b1 = a1;
        b0 = (float) (d * (al * A + 1.0));
        b2 = (float) (d * (1.0 - al * A));
        return;
    }
    const double d = 1.0 / (al + 1.0);
    a1 = (float) (d * cs * -2.0);
    a2 = (float) (d * (1.0 - al));
    if (type == 1) // low pass
        b1 = (float) (d * (1.0 - cs)), b0 = b1 * 0.5f, b2 = b0;
    else if (type == 2) // band pass
        b1 = 0.0f, b0 = (float) (d * al), b2 = (float) (d * -al);
    else if (type == 3) // high pass
        b1 = (float) (d * -(cs + 1.0)), b0 = -b1 * 0.5f, b2 = b0;
    else // notch
        b1 = a1, b0 = (float) d, b2 = (float) d;
}

void FreeFilterEngine::process (float* left, float* right, int n) noexcept
{
    if (n <= 0)
        return;
    {
        X87Mode mode;
        const ld b0 = co[0], b1 = co[1], b2 = co[2], a1 = co[3], a2 = co[4];
        float* io[2] = { left, right };
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x2 = st[2 + ch], x1 = st[ch];
                st[2 + ch] = x1;
                ld acc = (ld) x1 * b1 + (ld) x2 * b2;
                const float y2 = st[6 + ch], y1 = st[4 + ch];
                st[6 + ch] = y1;
                acc = acc - (ld) y1 * a1;
                acc = acc - (ld) y2 * a2;
                const float x = io[ch][i];
                st[ch] = x;
                acc = acc + (ld) x * b0;
                st[4 + ch] = (float) acc;
                io[ch][i] = st[4 + ch];
            }
        for (auto& v : st) // |v| < 2^-24 → 0
            if (std::fabs (v) < tiny)
                v = 0.0f;
        if (! raw.center)
            return;
        // CENTER: a DC-blocking high-pass after the filter
        ld l0 = dc[0], l1 = dc[1], v0 = dc[2], v1 = dc[3];
        const ld c = dcC, qq = dcQ;
        for (int i = 0; i < n; ++i)
        {
            const ld xl = left[i];
            v0 += (xl - l0) * c;
            l0 += v0;
            v0 *= qq;
            left[i] = (float) (xl - l0);
            const ld xr = right[i];
            v1 += (xr - l1) * c;
            l1 += v1;
            v1 *= qq;
            right[i] = (float) (xr - l1);
        }
        dc[0] = guard (l0), dc[1] = guard (l1), dc[2] = guard (v0), dc[3] = guard (v1);
    }
}

// ================================================================================================ Fruity Filter
namespace
{
const float ramp24 = [] {
    const std::uint32_t b = 0x33800001u; // the smoothers' floor: the float just above 2^-24
    float f;
    std::memcpy (&f, &b, 4);
    return f;
}();

// the plugin's parameter smoother over one call: start value and per-sample step (FL's 128-sample glide)
void glide (float& cur, float target, int n, float& start, float& step) noexcept
{
    start = cur;
    if (cur == target)
        step = 0.0f;
    else if (n < 128)
    {
        step = (float) ((double) (target - cur) * (1.0 / 128.0));
        cur = cur + step * (float) n;
        if (ramp24 > cur)
        {
            cur = 0.0f;
            step = (float) ((double) -start * (1.0 / 128.0));
        }
    }
    else
    {
        step = (target - cur) / (float) n;
        cur = target;
    }
}

const char* const filterNames[7] = { "Cutoff", "Resonance", "Low pass", "Band pass", "High pass", "x2", "Center" };
} // namespace

std::optional<FilterState> parseFilter (const std::vector<std::uint8_t>& d)
{
    // i32 version (< 3), 6 raw controls, then (version ≥ 2) the CENTER byte
    if (d.size() < 28)
        return std::nullopt;
    const int version = i32At (d, 0);
    if (version >= 3)
        return std::nullopt;
    FilterState s;
    for (int i = 0; i < 6; ++i)
        s.raw[(std::size_t) i] = i32At (d, 4 + 4 * (std::size_t) i);
    if (version > 1)
    {
        if (d.size() < 29)
            return std::nullopt;
        s.center = (d[28] & 1) != 0;
    }
    return s;
}

void filterToParams (const FilterState& s, float* p) noexcept
{
    for (int i = 0; i < 5; ++i)
        p[i] = norm (s.raw[(std::size_t) i], 0, 1024);
    p[5] = s.raw[5] == 1 ? 1.0f : 0.0f;
    p[6] = s.center ? 1.0f : 0.0f;
}

FilterState filterFromParams (const float* p) noexcept
{
    FilterState s;
    for (int i = 0; i < 5; ++i)
        s.raw[(std::size_t) i] = rawFrom (p[i], 0, 1024);
    s.raw[5] = p[5] >= 0.5f ? 1 : 0;
    s.center = p[6] >= 0.5f;
    return s;
}

std::string filterParamText (int param, float v01)
{
    char b[48];
    const float x = std::clamp (v01, 0.0f, 1.0f);
    if (param == 0)
    {
        // the SVF coefficient f gives fc = fs/π · asin(f/2), shown at 44.1 kHz
        const double f = (double) (float) ((double) (float) (std::exp ((double) x * std::log (26.0)) - 1.0) * 0.04);
        std::snprintf (b, sizeof b, "%d Hz", (int) std::lround (44100.0 / 3.141592653589793 * std::asin (std::min (1.0, f / 2.0))));
        return b;
    }
    if (param == 1 || (param >= 2 && param <= 4))
    {
        std::snprintf (b, sizeof b, "%d%%", (int) std::lround (x * 100.0f));
        return b;
    }
    if (param == 5 || param == 6)
        return x >= 0.5f ? "On" : "Off";
    (void) filterNames;
    return {};
}

FilterEngine::FilterEngine() noexcept
{
    for (int k = 0; k < 6; ++k)
        param (k, raw.raw[(std::size_t) k]);
    cur = target;
}

void FilterEngine::prepare (int sampleRate) noexcept
{
    dcC = (float) (1.0070234999999998 / (double) std::max (1, sampleRate));
    dcQ = 0.96f;
    std::fill (std::begin (dc), std::end (dc), 0.0);
}

void FilterEngine::set (const FilterState& s) noexcept
{
    raw.center = s.center;
    for (int k = 0; k < 6; ++k)
        if (s.raw[(std::size_t) k] != raw.raw[(std::size_t) k])
            param (k, s.raw[(std::size_t) k]);
}

void FilterEngine::param (int k, int v) noexcept
{
    Ftz ftz;
    raw.raw[(std::size_t) k] = v;
    if (k == 5)
    {
        if (twice != (v == 1))
        {
            twice = v == 1;
            std::fill (std::begin (st), std::end (st), 0.0f);
        }
        return;
    }
    const float x = unit (v, 0, 1.0 / 1024.0);
    if (k == 0) // f = (26^x − 1)·0.04
        target[0] = (float) ((double) (float) (std::exp ((double) x * std::log (26.0)) - 1.0) * 0.04);
    else if (k == 1) // damping 1 − 0.8x
        target[1] = (float) (1.0 - (double) x * 0.8);
    else // output gains (11^x − 1)/10
        target[(std::size_t) k] = (float) (std::exp ((double) x * std::log (11.0)) - 1.0) / 10.0f;
}

void FilterEngine::process (float* left, float* right, int n) noexcept
{
    if (n <= 0)
        return;
    Ftz ftz;
    float c[5], step[5];
    bool moving = false;
    for (int k = 0; k < 5; ++k)
    {
        glide (cur[(std::size_t) k], target[(std::size_t) k], n, c[k], step[k]);
        moving = moving || bitsOf (step[k]) != 0;
    }
    float* io[2] = { left, right };
    for (int i = 0; i < n; ++i)
    {
        if (moving)
            for (int k = 0; k < 5; ++k)
                c[k] = c[k] + step[k];
        const float f = c[0], q = c[1], g1 = c[2], g2 = c[3], g3 = c[4];
        for (int ch = 0; ch < 2; ++ch)
        {
            float& lo = st[ch]; float& ba = st[4 + ch]; float& hi = st[8 + ch];
            lo = lo + f * ba;
            hi = (io[ch][i] - lo) - q * ba;
            ba = f * hi + ba;
            float o = (lo * g1 + ba * g2) + hi * g3;
            if (twice) // a second section, damping 1, on the first one's mix
            {
                float& l2 = st[2 + ch]; float& b2 = st[6 + ch]; float& h2 = st[10 + ch];
                l2 = l2 + f * b2;
                h2 = (o - l2) - b2;
                b2 = f * h2 + b2;
                o = (l2 * g1 + b2 * g2) + h2 * g3;
            }
            io[ch][i] = o;
        }
    }
    for (int k = 0; k < 12; ++k)
        if ((twice || k < 6) && (bitsOf (st[k]) & 0x7fffffffu) < 0x33800000u)
            st[k] = 0.0f;
    if (! raw.center)
        return;
    X87Mode mode;
    ld l0 = dc[0], l1 = dc[1], v0 = dc[2], v1 = dc[3];
    const ld cc = dcC, qq = dcQ;
    for (int i = 0; i < n; ++i)
    {
        const ld xl = left[i];
        v0 += (xl - l0) * cc;
        l0 += v0;
        v0 *= qq;
        left[i] = (float) (xl - l0);
        const ld xr = right[i];
        v1 += (xr - l1) * cc;
        l1 += v1;
        v1 *= qq;
        right[i] = (float) (xr - l1);
    }
    dc[0] = guard (l0), dc[1] = guard (l1), dc[2] = guard (v0), dc[3] = guard (v1);
}

// ================================================================================================ mixer effects
namespace
{
class FastLPEffect final : public Effect
{
public:
    void prepare (double) noexcept override { ready = false; }
    void reset() noexcept override {} // the plugin has no flush
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = fastLPFromParams (params.data());
        if (! ready)
        {
            engine = FastLPEngine(); // a fresh plugin: default controls, then the state's
            engine.set (FastLPState {});
            ready = true;
            engine.set (s);
            current = s;
        }
        else if (s.cutoff != current.cutoff || s.reso != current.reso)
        {
            engine.set (s);
            current = s;
        }
        for (int done = 0; done < n;)
        {
            const int m = std::min (engineBlock, n - done);
            engine.process (left + done, right + done, m);
            done += m;
        }
    }

private:
    FastLPEngine engine;
    FastLPState current {};
    bool ready = false;
};

class FreeFilterEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        rate = (int) std::lround (sampleRate);
        ready = false;
    }
    void reset() noexcept override {} // the plugin's flush leaves the filter as it is
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = freeFilterFromParams (params.data());
        if (! ready)
        {
            engine = FreeFilterEngine(); // a fresh plugin, its rate, then the state's controls
            engine.prepare (rate);
            ready = true;
        }
        engine.set (s);
        for (int done = 0; done < n;)
        {
            const int m = std::min (engineBlock, n - done);
            engine.process (left + done, right + done, m);
            done += m;
        }
    }

private:
    FreeFilterEngine engine;
    int rate = 44100;
    bool ready = false;
};

class FilterEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        rate = (int) std::lround (sampleRate);
        ready = false;
    }
    void reset() noexcept override {} // the plugin's flush leaves the filter as it is
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = filterFromParams (params.data());
        if (! ready)
        {
            engine = FilterEngine(); // a fresh plugin, its rate, then the state's controls (the smoothers glide)
            engine.prepare (rate);
            ready = true;
        }
        engine.set (s);
        for (int done = 0; done < n;)
        {
            const int m = std::min (engineBlock, n - done);
            engine.process (left + done, right + done, m);
            done += m;
        }
    }

private:
    FilterEngine engine;
    int rate = 44100;
    bool ready = false;
};
} // namespace

std::unique_ptr<Effect> makeFastLP() { return std::make_unique<FastLPEffect>(); }
std::unique_ptr<Effect> makeFreeFilter() { return std::make_unique<FreeFilterEffect>(); }
std::unique_ptr<Effect> makeFilter() { return std::make_unique<FilterEffect>(); }
} // namespace lynx::flfilter
