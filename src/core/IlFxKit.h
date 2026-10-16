#pragma once

// Building blocks shared by plug-ins of one third-party DSP code base that FL Studio ships (Toxic Biohazard's effects
// rack, REA_rodada45 §FX, and Hardcore, REA_rodada71): float re-creations of their statically linked MSVC CRT math
// (SSE2 paths, not correctly rounded), the cookbook biquads with a design cache, and the "tape" delay line. Written
// from observed behaviour (clean-room) and checked bit for bit against the plug-ins by the rounds named above.
//
// The DLLs compute in float32 with explicit double steps; these keep the types and the operation order of every
// expression, so fusing a·b + c into one FMA would change the results: contraction stays off.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::ilfx
{
constexpr float tiny9 = 1e-9f;

inline float tiny0 (float v) noexcept { return (v < tiny9 && v > -tiny9) ? 0.0f : v; }

// cvttss2si / cvttsd2si: truncation toward zero; out of range or NaN → INT_MIN
inline int i32 (double x) noexcept
{
    if (! (x == x) || x >= 2147483648.0 || x < -2147483648.0)
        return (int) 0x80000000u;
    return (int) x;
}

// C round(): half away from zero
inline double cround (double x) noexcept { return x >= 0.0 ? std::floor (x + 0.5) : -std::floor (-x + 0.5); }

inline std::uint64_t bitsOf (double d) noexcept
{
    std::uint64_t b = 0;
    std::memcpy (&b, &d, 8);
    return b;
}
inline double fromBits (std::uint64_t b) noexcept
{
    double d = 0;
    std::memcpy (&d, &b, 8);
    return d;
}
inline std::uint32_t bitsOf (float f) noexcept
{
    std::uint32_t b = 0;
    std::memcpy (&b, &f, 4);
    return b;
}

// ------------------------------------------------------------------------------------------------ CRT float math
// The plug-in's MSVC CRT float functions (SSE2 paths) are not correctly rounded; these reproduce them bit for bit on
// the argument ranges the rack uses (REA_rodada45 §FX.8). Tables are recomputed: 1/c, ln(1 + j/256), 2^(i/64).
struct CrtTables
{
    std::array<double, 257> invC {}, lnC {};
    std::array<double, 64> exp2 {};
    CrtTables()
    {
        for (int j = 0; j < 257; ++j)
        {
            invC[(std::size_t) j] = 1.0 / (0.5 + j / 512.0);
            lnC[(std::size_t) j] = std::log (1.0 + j / 256.0);
        }
        for (int i = 0; i < 64; ++i)
            exp2[(std::size_t) i] = std::exp2 (i / 64.0);
    }
};
inline const CrtTables& crt()
{
    static const CrtTables t;
    return t;
}

// exp(t) of the powf/expf tails: table 2^(k/64), degree-3 polynomial, scaling by 2^(k>>6)
inline double crtExpTail (double t) noexcept
{
    const auto k = (long long) std::nearbyint (t * 92.33248261689366);
    const auto r = t - (double) k * 0.010830424696249145;
    const auto q = (r * r) * (0.16666666666666666 * r + 0.5) + r;
    const auto e = crt().exp2[(std::size_t) (k & 63)];
    return std::ldexp (q * e + e, (int) ((k - (k & 63)) / 64));
}

inline float powfCrt (float x, float y) noexcept // 0x180057f90
{
    const auto ay = bitsOf (y) & 0x7fffffffu, ax = bitsOf (x) & 0x7fffffffu, sx = bitsOf (x);
    if (ay == 0)
        return 1.0f;
    if (y == 1.0f)
        return x;
    if (ay >= 0x7f800000u || ax >= 0x7f800000u || sx >= 0x80000000u || ax == 0)
        return (float) std::pow ((double) x, (double) y); // special values: never reached by the rack (not emulated)
    const auto xd = (double) x, yd = (double) y;
    double lg = 0;
    if (sx < 0x3f880000u && std::fabs (xd - 1.0) < 0.0625)
    {
        const auto u = xd - 1.0;
        const auto s = u / (u + 2.0);
        const auto v = s + s;
        const auto su = s * u;
        const auto v2 = v * v;
        const auto a = v2 * 0.012500000003771751 + 0.08333333333333179;
        const auto b = v2 * 0.0004348877777076146 + 0.0022321399879194482;
        const auto v3 = v * v2;
        const auto v7 = (v2 * v2) * v3;
        lg = u + ((v3 * a + v7 * b) - su);
    }
    else
    {
        const auto bx = bitsOf (xd);
        const auto m = bx & 0xfffffffffffffull;
        const auto j = (m >> 44) + ((m >> 43) & 1);
        const auto e = (int) ((bx >> 52) & 0x7ff) - 0x3ff;
        const auto f = fromBits (m | 0x3fe0000000000000ull);
        const auto c = fromBits ((j | 0x3fe00ull) << 44);
        const auto r = (c - f) * crt().invC[(std::size_t) j];
        const auto p = (r * r) * (0.3333333333333333 * r + 0.5) + r;
        lg = ((double) e * 0.6931471805599453 + crt().lnC[(std::size_t) j]) - p;
    }
    const auto t = yd * lg;
    if (t > 88.72283935546875)
        return INFINITY;
    if (! (t > -103.2789306640625))
        return 0.0f; // underflow path (not decoded in detail)
    return (float) crtExpTail (t);
}

inline float expfCrt (float x) noexcept // 0x18006e440
{
    const auto xd = (double) x;
    const auto t = xd * 92.33248261689366;
    if (! std::isfinite (xd) || t >= 8192.0 || t < -9600.0)
        return (float) std::exp (xd); // overflow/underflow handlers: not emulated
    const auto k = (long long) std::nearbyint (t);
    const auto r = xd - (double) k * 0.010830424696249145;
    const auto q = (r * r) * (r * 0.16666666666666666 + 0.5) + r;
    const auto e = crt().exp2[(std::size_t) (k & 63)];
    return (float) ((q * e + e) * std::ldexp (1.0, (int) ((k - (k & 63)) / 64)));
}

inline double sinPoly (double r) noexcept
{
    const auto x2 = r * r;
    const auto x4 = x2 * x2;
    const auto p = (2.7557319223985893e-06 * x2 + -0.0001984126984126984) * x4 + (0.008333333333333333 * x2 + -0.16666666666666666);
    return r + p * (r * x2);
}
inline double cosPoly (double r) noexcept
{
    const auto x2 = r * r;
    const auto x4 = x2 * x2;
    const auto p = ((-2.755731922398589e-07 * x2 + 2.4801587301587298e-05) * x4 + (-0.0013888888888888887 * x2 + 0.041666666666666664)) * x4;
    return (-0.5 * x2 + 1.0) + p;
}
// |x| → quadrant n, reduced r and the exponent of the first r (Cody-Waite in up to two steps)
inline void reduceQuadrant (double ax, int& n, double& r, int& er) noexcept
{
    n = (int) (ax * 0.6366197723675814 + 0.5);
    const auto nd = (double) n;
    auto head = ax - nd * 1.5707963267341256;
    r = head - nd * 6.077100506506192e-11;
    const auto ex = (int) (bitsOf (ax) >> 52);
    er = (int) ((bitsOf (r) << 1) >> 53);
    if (ex - er > 15)
    {
        const auto t = head;
        const auto p2 = nd * 6.077100506303966e-11;
        head = head - p2;
        const auto tail = nd * 2.0222662487959506e-21 - ((t - head) - p2);
        r = head - tail;
    }
}

inline float sinfCrt (float x) noexcept // 0x18006edd0
{
    const auto xd = (double) x, ax = std::fabs (xd);
    if (! std::isfinite (xd) || ax >= 16000000.0)
        return (float) std::sin (xd); // large-argument path: not emulated
    if (ax <= 0.7853981633974483)
    {
        if (ax < 0.0078125)
        {
            if (ax < 0.0001220703125)
                return x;
            return (float) (xd - ((xd * xd) * xd) * 0.16666666666666666);
        }
        return (float) sinPoly (xd);
    }
    const int neg = xd < 0.0 ? 1 : 0;
    int n = 0, er = 0;
    double r = 0;
    reduceQuadrant (ax, n, r, er);
    double v = 0;
    if (er >= 0x3f2)
        v = (n & 1) ? cosPoly (r) : sinPoly (r);
    else if (er > 0x3de)
        v = (n & 1) ? (1.0 - (r * r) * 0.5) : (r - (0.16666666666666666 * r) * (r * r));
    else
        v = (n & 1) ? 1.0 : r;
    const int h = (n >> 1) & 1;
    if ((((h & neg) | ((~h) & (~neg))) & 1) == 0)
        v = 0.0 - v;
    return (float) v;
}

inline float cosfCrt (float x) noexcept // 0x18006db40
{
    const auto xd = (double) x, ax = std::fabs (xd);
    if (! std::isfinite (xd) || ax >= 500000.0)
        return (float) std::cos (xd); // large-argument path: not emulated
    if (ax <= 0.7853981633974483)
        return (float) cosPoly (xd);
    int n = 0, er = 0;
    double r = 0;
    reduceQuadrant (ax, n, r, er);
    double v = 0;
    if (er >= 0x3f2)
        v = (n & 1) ? sinPoly (r) : cosPoly (r);
    else if (er > 0x3de)
        v = (n & 1) ? (r - (0.16666666666666666 * r) * (r * r)) : (1.0 - (r * r) * 0.5);
    else
        v = (n & 1) ? r : 1.0;
    if ((n + 1) & 2)
        v = 0.0 - v;
    return (float) v;
}

inline float tanfCrt (float x) noexcept // 0x18006fa20, |x| ≤ π/4 (the rack never leaves that range)
{
    const auto xd = (double) x, ax = std::fabs (xd);
    if (! std::isfinite (xd) || ax > 0.7853981633974483)
        return (float) std::tan (xd); // reduction path: not emulated
    if (ax < 7.450580596923828e-09)
        return x;
    if (ax < 0.0001220703125)
        return (float) (((xd * xd) * xd) * 0.3333333333333333 + xd);
    const auto x2 = xd * xd;
    const auto num = -0.017203248047148168 * x2 + 0.3852960712639954;
    const auto den = (0.01844239256901656 * x2 + -0.5139650547885454) * x2 + 1.1558882143468838;
    return (float) (xd + (x2 * xd) * (num / den));
}

// float sine of the DLL's packed kernel (0x18006b520, SSE path; the same constants as GMS's, REA_rodada39 §1.13)
inline float sinK (float x) noexcept
{
    const auto a = std::fabs (x);
    const auto t = a * 0.31830987334251404f + 12582912.0f;
    const auto n = t - 12582912.0f;
    const auto odd = (bitsOf (t) & 1u) != 0;
    auto r = a - 3.140625f * n;
    r = r - 0.0009675025939941406f * n;
    r = r - 1.5099067240953445e-07f * n;
    r = r - 5.126688136514179e-12f * n;
    const auto r2 = r * r;
    if (odd)
        r = -r;
    auto p = 2.605780537123792e-06f * r2 + -0.00019809602235909551f;
    p = p * r2 + 0.008333062753081322f;
    p = p * r2 + -0.16666659712791443f;
    const auto y = r + (r2 * p) * r;
    return x < 0.0f ? -y : y;
}

inline void sinCosK (float w, float& s, float& c) noexcept // FUN_18006b460
{
    const auto a = std::fabs (w);
    if (a < 0.0001220703125f)
    {
        s = w;
        c = 1.0f - (a * a) * 0.5f;
        return;
    }
    s = sinK (w);
    c = sinK (a + 1.5707963705062866f);
}

// The CRT float routines a structure below uses: the plain (SSE2) re-creations above (Toxic Biohazard's rack, REA_rodada45)
// or another set with the same static members (Hardcore uses the FMA-aware ones of core/IldspMath.h, REA_rodada71)
struct PlainCrt
{
    static float powf (float x, float y) noexcept { return powfCrt (x, y); }
    static float sinf (float x) noexcept { return sinfCrt (x); }
    static float cosf (float x) noexcept { return cosfCrt (x); }
    static void sincosf (float x, float& s, float& c) noexcept { sinCosK (x, s, c); }
    static float expf (float x) noexcept { return expfCrt (x); }
    static float tanf (float x) noexcept { return tanfCrt (x); }
};

// low-pass biquad of the lo-fi effect (FUN_18001c3a0 design, skipped when unchanged; FUN_18001c4a0 tick)
template <class Crt>
struct LpBiquadT
{
    float f = 0, q = 0, fs = 0, a = 0, b1 = 0, b2 = 0, x2 = 0, x1 = 0, y2 = 0, y1 = 0;

    LpBiquadT()
    {
        design (100.0f, 1.0f, 44100.0f);
        clear();
    }
    void clear() noexcept { x2 = x1 = y2 = y1 = 0.0f; }
    void design (float nf, float nq, float nfs) noexcept
    {
        if (nf == f && nq == q && nfs == fs)
            return;
        f = nf, q = nq, fs = nfs;
        const auto w = (float) ((double) f * 6.2831853071795862 / (double) fs);
        float s = 0, c = 0;
        Crt::sincosf (w, s, c);
        const auto inv = 1.0f / (s + q);
        const auto den = s / q + 2.0f;
        const auto na = (1.0f - c) / den;
        const auto nb1 = (c * inv) * (q * -2.0f);
        const auto nb2 = (q - s) * inv;
        a = tiny0 (na), b1 = tiny0 (nb1), b2 = tiny0 (nb2);
    }
    float tick (float x) noexcept
    {
        x = tiny0 (x);
        auto y = ((((x1 + x1) + x) + x2) * a - y1 * b1) - b2 * y2;
        y = tiny0 (y);
        x2 = x1, y2 = y1, x1 = x, y1 = y;
        return y;
    }
};

// ------------------------------------------------------------------------------------------------ tape line
// CTapeDelay (REA_rodada45 §FX.3): samples stored with a position stamp in [0, 1) in two halves ("worms"); the read
// head interpolates the previous cycle; a triangle-shaped LFO bends the speed. Storage is reserved by setSampleRate so
// that type changes do not allocate.
struct Tape
{
    struct Worm
    {
        std::vector<float> v;
        std::vector<double> t;
        int count = 0, ridx = 0;
    };
    int n = 4;
    Worm worms[2];
    int w = 0, r = 1;
    double phase0 = 0, minrate = 0.25, rateT = 0, rateS = 0, inc = 0, pos = 0, lph = 0;
    float gainT = 1.0f, gainS = 0.0f, fb = 0.0f, depth = 0.0f;

    void reserve (int maxSize)
    {
        for (auto& wm : worms)
        {
            wm.v.assign ((std::size_t) maxSize + 0x100, 0.0f);
            wm.t.assign ((std::size_t) maxSize + 0x100, 0.0);
        }
    }
    // FUN_180026140 (constructor defaults) on reserved storage
    void init (int size, double ph0) noexcept
    {
        n = std::max (4, size);
        if ((std::size_t) n + 0x100 > worms[0].v.size())
            n = (int) worms[0].v.size() - 0x100; // never with storage reserved for this rate
        w = 0, r = 1;
        phase0 = ph0;
        minrate = 1.0 / n;
        setTime (0.5);
        gainT = 1.0f;
        fb = 0.0f;
        inc = 0.0;
        depth = 0.0f;
        reset();
    }
    void setTime (double d) noexcept // FUN_1800267a0
    {
        if (d >= 0.99)
            d = 0.99;
        if (d <= 0.0)
            d = 0.0;
        auto m = 1.0 / n;
        m = m + m;
        rateT = 1.0 / ((d * (1.0 - m) + m) * n);
    }
    void setGain (double g) noexcept { gainT = (float) g; }
    void setFb (double g) noexcept { fb = (float) g; }
    void setInc (double rr, double fs) noexcept
    {
        if (rr <= 0.0)
            rr = 0.0;
        inc = rr / fs;
    }
    void setDepth (double x) noexcept
    {
        if (x >= 1.0)
            x = 1.0;
        if (x <= 0.0)
            x = 0.0;
        depth = (float) x;
    }
    void reset() noexcept // FUN_180026340
    {
        pos = phase0;
        rateS = rateT;
        gainS = 0.0f;
        lph = 0.0;
        for (auto& wm : worms)
        {
            std::fill (wm.v.begin(), wm.v.begin() + (n + 0x100), 0.0f);
            std::fill (wm.t.begin(), wm.t.begin() + (n + 0x100), 0.0);
            wm.count = wm.ridx = 0;
        }
    }
    float tick (float x) noexcept // FUN_180026400
    {
        if (x < tiny9 && x > -tiny9)
            x = 0.0f;
        const auto ph = lph;
        const auto tr = ph >= 0.5 ? 1.0 - (((ph - 0.5) + ph) - 0.5) : ph + ph;
        const auto nph = ph + inc;
        lph = nph;
        auto mod = ((3.0 - (tr + tr)) * tr) * ((double) depth * tr) + 1.0;
        if (! (mod > 1.0))
            mod = 1.0;
        if (nph >= 1.0)
            lph = nph - (double) i32 (nph);
        auto& rb = worms[r];
        float out = 0.0f;
        if (rb.count != 0)
            for (;;)
            {
                const auto i = rb.ridx;
                if (i >= rb.count + 1)
                    break;
                const auto ti = rb.t[(std::size_t) (10 + i)];
                if (pos > ti)
                {
                    rb.ridx = i + 1;
                    continue;
                }
                const auto t0 = (float) rb.t[(std::size_t) (9 + i)];
                const auto t1 = (float) ti;
                double fr = 0.0;
                if (t0 < t1)
                    fr = (pos - (double) t0) / (double) (t1 - t0);
                const auto v0 = rb.v[(std::size_t) (9 + i)];
                out = tiny0 ((float) ((double) (rb.v[(std::size_t) (10 + i)] - v0) * fr + (double) v0));
                break;
            }
        auto rt = mod * rateS;
        if (! (rt < 0.2))
            rt = 0.2;
        const auto wv = out * fb + x;
        const auto ret = out * gainS;
        if (! (rt > minrate))
            rt = minrate;
        pos = rt + pos;
        if (pos >= 1.0)
        {
            auto& wb = worms[w];
            const auto c = (std::size_t) rb.count;
            wb.v[9] = rb.v[9 + c], wb.t[9] = rb.t[9 + c] - 1.0;
            wb.v[8] = rb.v[8 + c], wb.t[8] = rb.t[8 + c] - 1.0;
            wb.v[7] = rb.v[7 + c], wb.t[7] = rb.t[7 + c] - 1.0;
            wb.v[(std::size_t) (10 + wb.count)] = wv;
            wb.t[(std::size_t) (10 + wb.count)] = pos;
            wb.ridx = 0;
            std::swap (w, r);
            pos = pos - 1.0;
            worms[w].count = 0;
        }
        auto& wb = worms[w];
        if ((std::size_t) (10 + wb.count) < wb.v.size())
        {
            wb.v[(std::size_t) (10 + wb.count)] = wv;
            wb.t[(std::size_t) (10 + wb.count)] = pos;
            ++wb.count;
        }
        gainS = tiny0 ((float) ((double) (gainT - gainS) * 0.01 + (double) gainS));
        rateS = (rateT - rateS) * 0.005 + rateS;
        return ret;
    }
};

// ------------------------------------------------------------------------------------------------ EQ band
// FUN_18001c590 (cookbook design with a dirty flag) / FUN_18001c930 (tick); band 0 low shelf, 1–6 peaking, 7 high shelf
template <class Crt>
struct EqBandT
{
    float fs = 44100.0f, gain = 0.0f, freq = 1000.0f, bw = 1.0f;
    bool dirty = true;
    int type = 1;
    float c[5] {}, x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void init (float f, int t) noexcept
    {
        freq = f, type = t, fs = 44100.0f, gain = 0.0f, bw = 1.0f, dirty = true;
        design (44100.0f);
        clear();
    }
    void clear() noexcept { x1 = x2 = y1 = y2 = 0.0f; }
    void setGain (float gn) noexcept
    {
        if (gn != gain)
            dirty = true;
        gain = gn;
    }
    void setFreq (float f) noexcept
    {
        if (f != freq)
            dirty = true;
        freq = f;
    }
    void setBandwidth (float b) noexcept
    {
        if (b != bw)
            dirty = true;
        bw = b;
    }
    void design (float nfs) noexcept
    {
        if (nfs != fs)
            dirty = true;
        fs = nfs;
        if (! dirty)
            return;
        dirty = false;
        const auto A = Crt::powf (10.0f, gain * 0.05000000074505806f);
        const auto num = (double) freq * 6.2831849999999996;
        auto w = (double) fs != 0.0 ? (float) (num / (double) fs) : INFINITY;
        if (w >= 3.0f)
            w = 3.0f;
        if (w <= 0.0f)
            w = 0.0f;
        const auto sn = Crt::sinf (w);
        const auto sh = std::sinh ((double) bw * 0.34657359999999998 * (double) w / (double) sn);
        const auto al2 = (float) (sh + sh);
        const auto beta = std::sqrt (A) * al2;
        const auto alpha = (sn * al2) * 0.5f;
        const auto one = 1.0f;
        float b0, b1, b2, a0, a1, a2;
        if (type == 0)
        {
            const auto cs = Crt::cosf (w);
            const auto k = sn * beta;
            const auto am1 = A - one, ap1 = A + one;
            const auto t = am1 * cs, u = ap1 - t, v = t + ap1;
            const auto c2 = ap1 * cs;
            a0 = k + v;
            b2 = (u - k) * A;
            b0 = (u + k) * A;
            a1 = am1 * -2.0f - (c2 + c2);
            b1 = (A + A) * (am1 - c2);
            a2 = v - k;
        }
        else if (type == 2)
        {
            const auto cs = Crt::cosf (w);
            const auto k = sn * beta;
            const auto am1 = A - one, ap1 = A + one;
            const auto t = am1 * cs, c2 = ap1 * cs;
            const auto p = t + ap1, q = ap1 - t;
            a0 = q + k;
            b0 = (k + p) * A;
            const auto d = am1 - c2;
            b2 = (p - k) * A;
            a1 = d + d;
            b1 = ((c2 + am1) * A) * -2.0f;
            a2 = q - k;
        }
        else
        {
            const auto aA = alpha * A, aD = alpha / A;
            b0 = aA + one;
            a0 = aD + one;
            const auto cc = Crt::cosf (w) * -2.0f;
            a1 = cc, b1 = cc;
            a2 = one - aD;
            b2 = one - aA;
        }
        const auto inv = one / a0;
        c[0] = tiny0 (inv * b0), c[1] = tiny0 (inv * b1), c[2] = tiny0 (inv * b2), c[3] = tiny0 (inv * a1), c[4] = tiny0 (inv * a2);
    }
    float tick (float x) noexcept
    {
        x = tiny0 (x);
        auto y = (((x1 * c[1] + x * c[0]) + c[2] * x2) - y1 * c[3]) - c[4] * y2;
        y = tiny0 (y);
        x2 = x1, y2 = y1, x1 = x, y1 = y;
        return y;
    }
};

// ------------------------------------------------------------------------------------------------ reverb
// class fxReverb (the plate of GMS's reverb slot, REA_rodada39 §1.13) with its early-reflection tap active, processing
// in place (REA_rodada45 §FX.5; Hardcore's reverb pedal is the same class, REA_rodada71).
template <class Crt>
struct ReverbT
{
    static constexpr double qa[18][2] = { { 421.0, -360.5 }, { 2981.0, 19.5 }, { 216.0, 5.5 }, { 250.0, 0.5 }, { 99.0, 1.5 }, { 100.0, 0.5 },
                                          { 28.0, 2.5 }, { 1975.0, 25.5 }, { 199.0, 1.5 }, { 216.0, 5.5 }, { 421.0, -360.5 }, { 421.0, -360.5 },
                                          { 421.0, -360.5 }, { 200.0, 0.5 }, { 1.0, 0.5 }, { 1.0, 0.5 }, { 1.0, 0.5 }, { 1.0, 0.5 } };
    static constexpr int lines[12] = { 142, 107, 379, 277, 672, 4453, 1800, 3720, 908, 4217, 2656, 3163 };
    static constexpr int taps0[14] = { 266, 2974, 1913, 2496, 1690, 187, 1566, 353, 3627, 1728, 3173, 1811, 335, 621 };
    static constexpr int erTapL = 1102, erTapR = 837; // first entries of the early-reflection tables (the only taps with gain ≠ 0)
    // the whole early-reflection tables (Hardcore's build gives every tap a gain and sums 7 left / 8 right taps)
    static constexpr int erTabL[8] = { 1102, 2904, 3784, 4873, 5878, 7470, 9062, 9263 };
    static constexpr int erTabR[8] = { 837, 1423, 2865, 3938, 5278, 5978, 7532, 9263 };
    bool allTaps = false;
    int erTL[8] {}, erTR[8] {};
    float erGL[8] {}, erGR[8] {};
    static constexpr float tiny = 1e-30f;

    float fs = 44100.0f;
    int pi[18] {};
    std::vector<float> buf[12], pre, er;
    int len[12] {}, pos[12] {}, prepos = 0, prelen = 1, lastsize = 0, erSize = 2, erPos = 0, erTl = 0, erTr = 0;
    int taps[14] {};
    bool dirty = true;
    float lp = 0, hpx = 0, hpy = 0, dL0 = 0, dL1 = 0, dR0 = 0, dR1 = 0, tL = 0, tR = 0, lev = -1000.0f;
    float gin = 0, hpk = 0, lpk = 1, ap1 = 0, ap2 = 0, tap1 = 0, tap2 = 0, decay = 0, shelf = 0, shk = 0, damp = 1, dry = 0;
    float erLev = 0, tank = 0, erGl = 0, erGr = 0;

    static int floorI (float x) noexcept
    {
        auto i = (int) x;
        if ((double) i != (double) x && x < 0.0f)
            --i;
        return i;
    }
    static int cdiv (long long a, long long b) noexcept { return (int) (a / b); }
    static float db (int i) noexcept // FUN_18002c200
    {
        if (i <= -361)
            return 0.0f;
        return Crt::powf (10.0f, (float) i * 0.004999999888241291f);
    }

    void construct()
    {
        static constexpr float preset[18] = { 0.8574821949005127f, 0.018785642459988594f, 0.16203702986240387f, 0.0f, 0.49494948983192444f, 1.0f,
                                              0.2857142984867096f, 0.2405063360929489f, 0.07035175710916519f, 0.16203702986240387f,
                                              0.8574821949005127f, 0.7743467688560486f, 0.5724465847015381f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f };
        lastsize = 0;
        setRate (44100.0f);
        for (int i = 0; i < 18; ++i)
            setp (i, preset[i]);
        reset();
    }
    void setRate (float rate) // FUN_18002a7d0 / FUN_18002a7e0
    {
        fs = rate;
        const auto fsi = (int) fs;
        for (int k = 0; k < 12; ++k)
        {
            const auto n = cdiv ((long long) fsi * lines[k], 20000);
            buf[k].assign ((std::size_t) std::max (1, n), tiny);
            len[k] = std::max (1, n);
            pos[k] = 0;
        }
        prelen = cdiv ((long long) fsi * 250, 1000);
        pre.assign ((std::size_t) std::max (1, prelen), tiny);
        prelen = std::max (1, prelen);
        prepos = 0;
        auto u = (unsigned) (fsi * 0.31632653061224492);
        int bitsN = 0;
        while (u != 0)
            ++bitsN, u >>= 1;
        erSize = 1 << (bitsN + 1);
        er.assign ((std::size_t) erSize, tiny);
        erPos = 0;
        for (int t = 0; t < 14; ++t)
            taps[t] = cdiv ((long long) fsi * taps0[t], 20000);
    }
    void setp (int i, float v) noexcept // FUN_18002b380
    {
        const auto b = (float) qa[i][1];
        const auto x = b >= 0.0f ? v * (float) qa[i][0] + b : v * (float) qa[i][0] - (-b);
        pi[i] = floorI (x);
        dirty = true;
    }
    void reset() noexcept // FUN_18002b170 (positions are kept)
    {
        for (auto& b : buf)
            std::fill (b.begin(), b.end(), tiny);
        std::fill (pre.begin(), pre.end(), tiny);
        std::fill (er.begin(), er.end(), tiny);
        lp = hpx = hpy = 0.0f;
        dL0 = dL1 = dR0 = dR1 = 0.0f;
        tL = tR = 0.0f;
        lev = -1000.0f;
    }
    float onePole (int p) const noexcept
    {
        if ((float) p < 221.0f)
        {
            const auto c = Crt::cosf (((float) p * 628.3185424804688f) / fs);
            const auto t = 2.0f - c;
            const auto s = std::sqrt ((t * t) - 1.0f);
            return 1.0f - (t - s);
        }
        return 1.0f;
    }
    void recalc() noexcept // FUN_18002c230
    {
        const auto fsi = (int) fs;
        const auto* p = pi;
        const auto one = 1.0f;
        gin = p[14] == 0 ? db (p[0]) : 0.0f;
        if ((float) p[1] > 19.0f)
        {
            const auto x = (std::min (fs * 0.5f, (float) p[1]) * 3.1415927410125732f) / fs;
            const auto e = Crt::tanf (x);
            hpk = (e - one) / (e + one);
        }
        else
            hpk = 0.0f;
        lpk = onePole (p[2]);
        prelen = std::max (1, cdiv ((long long) fsi * p[3], 1000));
        prelen = std::min (prelen, (int) pre.size());
        prepos %= prelen;
        const auto base = cdiv ((long long) (p[4] + 15) * fsi, 115);
        for (int k = 0; k < 12; ++k)
        {
            len[k] = std::max (1, cdiv ((long long) base * lines[k], 20000));
            len[k] = std::min (len[k], (int) buf[k].size());
            pos[k] %= len[k];
        }
        for (int t = 0; t < 14; ++t)
            taps[t] = cdiv ((long long) base * taps0[t], 20000);
        const auto sc = (float) cdiv ((long long) (p[4] + 3) * fsi, 103);
        erTl = (int) (((float) erTapL * 3.4013603e-05f) * sc);
        erTr = (int) (((float) erTapR * 3.4013603e-05f) * sc);
        const auto m = erTl < erTr ? (float) erTl : (float) erTr;
        erGl = (m * 0.5f) / (float) erTl;
        if (allTaps)
            for (int k = 0; k < 8; ++k)
            {
                erTL[k] = (int) (((float) erTabL[k] * 3.4013603e-05f) * sc);
                erTR[k] = (int) (((float) erTabR[k] * 3.4013603e-05f) * sc);
                erGL[k] = (m * 0.5f) / (float) erTL[k];
                erGR[k] = (m * 0.36000001430511475f) / (float) erTR[k];
            }
        erGr = (m * 0.36000001430511475f) / (float) erTr;
        if (p[4] != lastsize)
        {
            lastsize = p[4];
            for (int k = 0; k < 12; ++k)
                std::fill (buf[k].begin(), buf[k].begin() + len[k], 0.0f);
            std::fill (er.begin(), er.end(), 0.0f);
        }
        const auto diff = (float) p[5] * 0.01f;
        const auto inv = one / fs;
        if (diff != 0.0f)
        {
            ap1 = Crt::expf (((float) (len[0] + len[1]) * (-49.34111022949219f / diff)) * inv);
            ap2 = Crt::expf (((-31.398887634277344f / diff) * (float) (len[2] + len[3])) * inv);
            auto x = (((float) (len[4] + len[8]) * -3.4538776874542236f) / fs) * (2.857142925262451f / diff);
            tap1 = Crt::expf (x);
            x = ((-3.4538776874542236f / fs) * (float) (len[6] + len[10])) * (1.9607843160629272f / diff);
            tap2 = Crt::expf (x);
        }
        else
            ap1 = ap2 = tap1 = tap2 = 0.0f;
        const auto tsum = ((float) (len[9] + len[7] + len[5] + len[11]) / fs) * -6.907755374908447f;
        const auto t60 = (float) p[8] * 0.10000000149011612f;
        decay = Crt::powf (Crt::expf (tsum / t60), 0.25f);
        auto rr = Crt::powf (Crt::expf ((10.0f / ((float) p[6] * t60)) * tsum), 0.25f);
        rr = rr / decay;
        shelf = (rr - one) * 0.5f;
        const auto rm = std::min (rr, one);
        const auto e = Crt::tanf (((float) p[7] * 3.1415927410125732f) / fs);
        shk = (e - rm) / (rm + e);
        damp = onePole (p[9]);
        dry = p[15] == 0 ? db (p[10]) : 0.0f;
        erLev = p[16] == 0 ? db (p[11]) * 0.5f : 0.0f;
        tank = p[17] == 0 ? db (p[12]) * 0.30000001192092896f : 0.0f;
        dirty = false;
    }
    float ap (int k, float x, float c1, float c2) noexcept
    {
        auto& b = buf[k];
        const auto i = pos[k];
        const auto t = b[(std::size_t) i] - (c1 * x);
        b[(std::size_t) i] = (c2 * t) + x;
        pos[k] = i > 0 ? i - 1 : len[k] - 1;
        return t;
    }
    static int wrap (int i, int n) noexcept { return i >= n ? i - n : i; }
    void process (float* outL, float* outR, int n) noexcept // FUN_18002dda0, in place
    {
        if (dirty)
            recalc();
        const auto d = decay, one = 1.0f, half = 0.5f, dd = one - d;
        const auto g41 = (dd * 0.6000000238418579f) + d, g42 = (dd * 0.25f) + d, g43 = (dd * 0.699999988079071f) + d;
        const auto g56 = (dd * 0.20000000298023224f) + d, g44 = (dd * half) + d;
        const auto width = (float) pi[13] * 0.009999999776482582f;
        const auto erm = erSize - 1;
        auto* B = buf;
        auto* L = len;
        auto* P = pos;
        const auto* T = taps;
        for (int s = 0; s < n; ++s)
        {
            const auto xl = gin * outL[s], xr = gin * outR[s];
            const auto m = ((xr + xl) * half) + tiny;
            auto l = ((m - lp) * lpk) + lp;
            lp = l;
            const auto h = ((l - hpx) * hpk) + hpy;
            hpy = l;
            hpx = h;
            if (hpk != 0.0f)
                l = ((l - h) * half) + tiny;
            float erl = 0.0f, err = 0.0f;
            if (erLev != 0.0f)
            {
                if (allTaps)
                {
                    auto e = [&] (int tap) { return er[(std::size_t) ((tap + erPos) & erm)]; };
                    erl = e (erTL[0]) * erGL[0] + e (erTL[1]) * erGL[1];
                    erl = erl - e (erTL[2]) * erGL[2];
                    erl = e (erTL[3]) * erGL[3] + erl;
                    erl = erl + e (erTL[4]) * erGL[4];
                    erl = erl + e (erTL[5]) * erGL[5];
                    erl = erl + e (erTL[6]) * erGL[6];
                    err = e (erTR[0]) * erGR[0] - e (erTR[1]) * erGR[1];
                    err = err + e (erTR[2]) * erGR[2];
                    err = err + e (erTR[3]) * erGR[3];
                    err = err - e (erTR[4]) * erGR[4];
                    err = err + e (erTR[5]) * erGR[5];
                    err = err + e (erTR[6]) * erGR[6];
                    err = err - e (erTR[7]) * erGR[7];
                }
                else
                {
                    erl = er[(std::size_t) ((erTl + erPos) & erm)] * erGl;
                    err = er[(std::size_t) ((erTr + erPos) & erm)] * erGr;
                }
            }
            auto t = ap (0, l, ap1, ap1);
            auto u = ap (1, t, ap2, ap1);
            er[(std::size_t) erPos] = t;
            erPos = erPos > 0 ? erPos - 1 : erm;
            t = ap (2, u, ap1, ap1);
            u = ap (3, t, ap2, ap1);
            const auto pv = pre[(std::size_t) prepos];
            pre[(std::size_t) prepos] = u;
            prepos = prepos > 0 ? prepos - 1 : prelen - 1;
            const auto tin = pv + 9.99999968e-21f;
            auto v = B[11][(std::size_t) P[11]] * d;
            auto s2 = ((v - dL1) * shk) + dL0;
            dL0 = v, dL1 = s2;
            auto w = (((((s2 + v) * shelf) + v) + tin) - tL) * damp;
            w = w + tL;
            tL = w;
            u = ap (4, w, tap1, tap1);
            auto i = P[5];
            auto o = B[5][(std::size_t) i];
            B[5][(std::size_t) i] = u;
            P[5] = i > 0 ? i - 1 : L[5] - 1;
            o = o * d;
            u = ap (6, o, tap2, tap2);
            i = P[7];
            B[7][(std::size_t) i] = u;
            i = i > 0 ? i - 1 : L[7] - 1;
            P[7] = i;
            v = B[7][(std::size_t) i] * d;
            s2 = ((v - dR1) * shk) + dR0;
            dR0 = v, dR1 = s2;
            w = (((((s2 + v) * shelf) + v) + tin) - tR) * damp;
            w = w + tR;
            tR = w;
            u = ap (8, w, tap1, tap1);
            i = P[9];
            o = B[9][(std::size_t) i];
            B[9][(std::size_t) i] = u;
            P[9] = i > 0 ? i - 1 : L[9] - 1;
            o = o * d;
            u = ap (10, o, tap2, tap2);
            i = P[11];
            B[11][(std::size_t) i] = u;
            P[11] = i > 0 ? i - 1 : L[11] - 1;
            auto at = [&] (int line, int tap) { return B[line][(std::size_t) wrap (tap + P[line], L[line])]; };
            const auto fA = at (9, T[0]), fB = at (9, T[1]), fC = at (10, T[2]), fD = at (11, T[3]);
            const auto fE = at (5, T[4]), fF = at (6, T[5]), fG = at (7, T[6]);
            const auto f7 = at (5, T[8]), f8 = at (5, T[7]), f9 = at (6, T[9]), f10 = at (7, T[10]), f11 = at (9, T[11]);
            const auto f12 = at (10, T[12]), f13 = at (11, T[13]);
            auto tl = (((fA - (g42 * fB)) - (g43 * fC)) - (g56 * fD)) + (g41 * fE);
            tl = (tl + fF) + (g44 * fG);
            auto tr = ((((g42 * f7) + f8) - (g43 * f9)) - (g56 * f10)) - (g41 * f11);
            tr = (tr + f12) + (g44 * f13);
            if (lev < -99.0f)
                lev = tank;
            lev = ((tank - lev) * 0.0010000000474974513f) + lev;
            const auto a = (lev * tl) + (erLev * erl), b = (lev * tr) + (erLev * err);
            const auto sm = b + a, df = (a - b) * width;
            outL[s] = (xl * dry) + (df + sm);
            outR[s] = (xr * dry) + (sm - df);
        }
    }
};

using LpBiquad = LpBiquadT<PlainCrt>;
using Reverb = ReverbT<PlainCrt>;
using EqBand = EqBandT<PlainCrt>;
} // namespace lynx::ilfx
