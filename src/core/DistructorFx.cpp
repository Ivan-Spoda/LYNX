#include "core/DistructorFx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/FlangusFastDist.h"

// Distructor (REA_rodada71): see DistructorFx.h. The module keeps FL's own operation order and types (float32 where
// the plugin computes in float, double where it does, the x87 routines in long double with the plugin's precision)
// so that fusing a·b + c into one FMA would change the results: contraction stays off.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::distructor
{
namespace
{
using ld = long double;

std::int32_t i32At (const std::uint8_t* d) noexcept
{
    std::int32_t v;
    std::memcpy (&v, d, 4);
    return v;
}
void putI32 (std::vector<std::uint8_t>& v, std::int32_t x)
{
    std::uint8_t b[4];
    std::memcpy (b, &x, 4);
    v.insert (v.end(), b, b + 4);
}

constexpr std::array<int, moduleParams> moduleDefaults { 0, 5000, 0, 10000, 0, 0, 500, 5000, 5000, 10000, 500, 5000, 5000, 10000, 2000, 0,
                                                          128, 10, 128, 128, 0, 100, 128, 500, 0, 1000, 0, 950, 2000, 5000, 2000, 5000,
                                                          8400, 0, 0, 0, 5000, 10000, 5000, 10000, 0, 0, 0, 108, 2880, 1500, 1995, 0, 0 };

// Delphi's Sign of a double (−0 counts as 0)
double sgn (double x) noexcept { return (x == 0.0) ? 0.0 : (std::signbit (x) ? -1.0 : 1.0); }

#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
// the plugin's x87 routines run with the FPU's precision control as Delphi's run time sets it (53-bit mantissa)
struct X87Precision
{
    unsigned short saved = 0;
    explicit X87Precision (unsigned short pc) noexcept
    {
        asm volatile ("fnstcw %0" : "=m"(saved));
        unsigned short cw = (unsigned short) ((saved & ~0x300) | pc);
        asm volatile ("fldcw %0" : : "m"(cw));
    }
    ~X87Precision() { asm volatile ("fldcw %0" : : "m"(saved)); }
};
#else
struct X87Precision
{
    explicit X87Precision (unsigned short) noexcept {}
};
#endif
unsigned short x87Pc = std::getenv ("DISTRUCTOR_PC") ? (unsigned short) std::atoi (std::getenv ("DISTRUCTOR_PC")) : 0x200; // 53-bit

// ------------------------------------------------------------------------------------------------ smoother
// a block-rate one-pole glide of up to four values (L, R used): each sample d ← d·k, value = target + d
struct Glide
{
    float target[4] {}, cur[4] {}, k = 0.0f;

    bool atRest() const noexcept { return target[0] == 0.0f && target[1] == 0.0f && cur[0] == 0.0f && cur[1] == 0.0f; }
    // snaps when every |cur − target| < 2^-24·… (FUN at 0x713f30); true when still moving
    bool moving() noexcept
    {
        constexpr float eps = 5.96046448e-08f;
        bool mv = false;
        for (int c = 0; c < 4; ++c)
            if (! (std::fabs (cur[c] - target[c]) < eps))
                mv = true;
        if (! mv)
            for (int c = 0; c < 4; ++c)
                cur[c] = target[c];
        return mv;
    }
    // the glide's values for n frames (interleaved L/R)
    void ramp (float* out, int n) noexcept
    {
        auto dl = cur[0] - target[0], dr = cur[1] - target[1];
        for (int i = 0; i < n; ++i)
        {
            dl = dl * k, dr = dr * k;
            out[2 * i] = dl + target[0];
            out[2 * i + 1] = dr + target[1];
        }
        if (n > 0)
            cur[0] = dl + target[0], cur[1] = dr + target[1];
    }
};

// ------------------------------------------------------------------------------------------------ oversampler
// one ×2 stage: two branches of three first-order all-pass sections (double), tiny outputs cleared between blocks
constexpr double apCoef[6] = { 0.14737711360104661, 0.039151597734460045, 0.48246854276970014,
                               0.30264684832849342, 0.88300502576937312, 0.6746159185469639 };
struct HalfbandStage
{
    double o[6][2] {}, s[6][2] {};
    void clear() noexcept { std::memset (this, 0, sizeof (*this)); }
    void flush() noexcept // FUN_005fbcd0 on the six section outputs: tiny values (|v| < 2^-24) become 0
    {
        for (auto& p : o)
            for (auto& v : p)
                if (std::fabs (v) < (double) 5.96046448e-08f)
                    v = 0.0;
    }
    static double sec (double& o, double& s, double x, double c) noexcept
    {
        const auto old = s;
        s = x;
        o = old + (x - o) * c;
        return o;
    }
    // n input frames → 2n output frames
    void up (const float* in, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const auto x = (double) in[2 * i + c];
                const auto a1 = sec (o[0][c], s[0][c], x, apCoef[0]);
                const auto b1 = sec (o[3][c], s[3][c], x, apCoef[1]);
                const auto a2 = sec (o[1][c], s[1][c], a1, apCoef[2]);
                const auto b2 = sec (o[4][c], s[4][c], b1, apCoef[3]);
                const auto a3 = sec (o[2][c], s[2][c], a2, apCoef[4]);
                const auto b3 = sec (o[5][c], s[5][c], b2, apCoef[5]);
                out[4 * i + c] = (float) b3;
                out[4 * i + 2 + c] = (float) a3;
            }
        }
        flush();
    }
    // 2n input frames → n output frames
    void down (const float* in, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const auto x0 = (double) in[4 * i + c], x1 = (double) in[4 * i + 2 + c];
                const auto a1 = sec (o[0][c], s[0][c], x0, apCoef[0]);
                const auto b1 = sec (o[3][c], s[3][c], x1, apCoef[1]);
                const auto a2 = sec (o[1][c], s[1][c], a1, apCoef[2]);
                const auto b2 = sec (o[4][c], s[4][c], b1, apCoef[3]);
                const auto a3 = sec (o[2][c], s[2][c], a2, apCoef[4]);
                const auto b3 = sec (o[5][c], s[5][c], b2, apCoef[5]);
                out[2 * i + c] = (float) ((b3 + a3) * 0.5);
            }
        }
        flush();
    }
};

// ------------------------------------------------------------------------------------------------ distortion
// the control law of Fast dist / soft clipper gains: v = raw/128; (11^v − 1)/10 (exactly 1 at v = 1)
float gainLaw (int raw) noexcept
{
    const auto v = (float) raw * 0.0078125f;
    if (v == 1.0f)
        return 1.0f;
    const auto e = std::exp ((double) (v * 2.39789534f));
    return (float) ((e - 1.0) * (double) 0.1f);
}

// Delphi's Int: toward zero (and +0 for |x| < 1)
double dInt (double x) noexcept
{
    const auto ax = std::fabs (x);
    if (ax >= 4503599627370496.0)
        return x;
    if (ax < 1.0)
        return 0.0;
    return (double) (long long) x;
}
// Delphi's Tanh (x ≥ 0 in this plugin's use; odd symmetry otherwise)
double dTanh (double x) noexcept
{
    const auto neg = x < 0.0;
    auto a = std::fabs (x);
    double r;
    if (a > 23.0)
        r = 1.0;
    else if (a < 1.81458605194507e-05)
        r = a - a * a * a * 0.3333333333333333;
    else if (a < 0.34657359027997264)
    {
        const auto e = std::expm1 (a * 2.0);
        r = e / (e + 2.0);
    }
    else
        r = 1.0 - 2.0 / (std::exp (a * 2.0) + 1.0);
    return neg ? -r : r;
}
// Delphi's Power for a positive base (integral exponents through repeated squaring, else Exp(e·Ln b))
double dPower (double base, double e) noexcept
{
    if (e == 0.0)
        return 1.0;
    if (e == std::trunc (e) && std::fabs (e) < 2147483647.0)
    {
        auto n = (long long) std::fabs (e);
        double r = 1.0, b = base;
        while (n != 0)
        {
            if (n & 1)
                r *= b;
            b *= b;
            n >>= 1;
        }
        return e < 0.0 ? 1.0 / r : r;
    }
    return std::exp (e * std::log (base));
}

// Blood overdrive's shaper: 65536 points of a half sine from −1 to 1 (index 0x8000 + k = sin(kπ/65536))
struct HalfSine
{
    std::array<float, 65536> t {};
    HalfSine()
    {
        for (int k = 0; k < 0x8000; ++k)
        {
            const auto s = (float) std::sin ((double) k * 3.141592653589793 / 65536.0);
            t[(std::size_t) (0x8000 + k)] = s;
            t[(std::size_t) (0x7fff - k)] = -s;
        }
    }
    float operator() (float x) const noexcept
    {
        const auto v = (double) (x * 32767.0f);
        const long long q = std::fabs (v) < 9.2233720368547758e18 ? (long long) v : (long long) (1ULL << 63); // cvttsd2si
        const auto i = (std::int32_t) ((std::uint32_t) q + 0x8000u);
        return t[(std::size_t) std::clamp (i, 0, 0xffff)];
    }
};
const HalfSine& halfSine()
{
    static const HalfSine s;
    return s;
}

// the 2nd-order low pass of the Harmor shapes (FUN_005f86c0 type 1 design, FUN_005f8ee0 run: double precision)
struct Biquad
{
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double st[8] {}; // x1 L/R, x2 L/R, y1 L/R, y2 L/R
    void lowPass (double s, double c) noexcept
    {
        const auto alpha = s * 0.70710678118654746;
        const auto d = 1.0 / (alpha + 1.0);
        a1 = d * c * -2.0;
        a2 = d * (1.0 - alpha);
        b1 = d * (1.0 - c);
        b0 = b1 * 0.5;
        b2 = b0;
    }
    void process (float* x, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const auto in = (double) x[2 * i + c];
                const auto y = in * b0 + (((st[c] * b1 - st[6 + c] * a2) + st[2 + c] * b2) - st[4 + c] * a1);
                st[2 + c] = st[c], st[c] = in, st[6 + c] = st[4 + c], st[4 + c] = y;
                x[2 * i + c] = (float) y;
            }
        for (auto& v : st)
            if (std::fabs (v) < (double) 5.96046448e-08f)
                v = 0.0;
    }
};

// Abrasive's noise: the plugin's Mersenne Twister MT19937 (seeded with 19650218), including a quirk of its refill
struct Mt19937
{
    std::uint32_t mt[624] {};
    int idx = 624;
    explicit Mt19937 (std::uint32_t seed = 19650218u)
    {
        mt[0] = seed;
        for (int i = 1; i < 624; ++i)
            mt[i] = (mt[i - 1] ^ (mt[i - 1] >> 30)) * 0x6c078965u + (std::uint32_t) i;
        idx = 624;
    }
    std::uint32_t next() noexcept
    {
        if (idx >= 624)
        {
            auto mix = [this] (int i, std::uint32_t far) {
                const auto y = (mt[i] & 0x80000000u) | (mt[i + 1] & 0x7fffffffu);
                mt[i] = far ^ (y >> 1) ^ ((y & 1u) ? 0x9908b0dfu : 0u);
            };
            for (int i = 0; i < 227; ++i)
                mix (i, mt[i + 397]);
            // the plugin's second pass starts one element early (at 226), where it reads the word before the table:
            // its own position counter (624 whenever the table is regenerated)
            mix (226, (std::uint32_t) idx);
            for (int i = 227; i < 623; ++i)
                mix (i, mt[i - 227]);
            const auto y = (mt[623] & 0x80000000u) | (mt[0] & 0x7fffffffu);
            mt[623] = mt[396] ^ (y >> 1) ^ ((y & 1u) ? 0x9908b0dfu : 0u);
            idx = 0;
        }
        auto y = mt[idx++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= y >> 18;
        return y;
    }
};

// the noise band pass of Abrasive (FUN_00716690 type 2 design, FUN_0071ea60 run: a trapezoidal state-variable filter)
struct NoiseSvf
{
    float fs = 44100.0f, piOverFs = (float) (3.141592653589793 / 44100.0);
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, m0 = 0, m1 = 0, m2 = 0;
    void setRate (double rate) noexcept
    {
        fs = (float) rate;
        piOverFs = (float) (3.141592653589793 / rate);
    }
    void bandPass (float f, float k, float gain) noexcept
    {
        const auto g = (float) std::tan ((double) (f * piOverFs));
        a1 = 1.0f / (g * (g + k) + 1.0f);
        a2 = g * a1;
        a3 = g * a2;
        m0 = 0.0f, m1 = gain, m2 = 0.0f;
    }
    float tick (float x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = v1 * 2.0f - ic1;
        ic2 = v2 * 2.0f - ic2;
        return m0 * x + m1 * v1 + m2 * v2;
    }
};

// Crusher's sample-rate reducer (FUN_007219c0 / FUN_007218f0)
struct SampleHold
{
    float rateScale = 1.0f, count[2] {}, held[2] {};
    float period (float rate) const noexcept
    {
        const auto f = (float) ((double) rate * 127.0);
        const auto fd = (double) f;
        auto r = (float) ((((fd * 0.0002 * fd * fd) - fd * 0.0171 * fd) + fd * 0.7435 + 1.0) * (double) rateScale);
        if ((double) r < 1.0)
            r = 1.0f;
        return r;
    }
    float tick (float x, float rate, int ch) noexcept
    {
        const auto p = period (rate);
        count[ch] = (float) ((double) count[ch] + 1.0);
        if (p <= count[ch])
        {
            const auto q = (double) (count[ch] / p);
            count[ch] = count[ch] - (float) (long long) q * p;
            held[ch] = x;
        }
        return held[ch];
    }
};

// x87 2^v as the plugin computes it (frndint, f2xm1, fscale)
ld x87Exp2 (ld v) noexcept
{
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
    ld r;
    asm ("fld %%st(0)\n\t"
         "frndint\n\t"
         "fsubr %%st(0), %%st(1)\n\t"
         "fxch %%st(1)\n\t"
         "f2xm1\n\t"
         "fld1\n\t"
         "faddp\n\t"
         "fscale\n\t"
         "fstp %%st(1)"
         : "=t"(r)
         : "0"(v));
    return r;
#else
    return std::exp2 (v);
#endif
}

struct Distortion
{
    int algo = 0;
    int factor = 1;
    double fs = 44100.0;
    std::array<int, 36> p {};
    Glide bias, cross, pre, wet, dry; // +0x10, +0x34, +0x402fc, +0x40344, +0x40320
    bool harmorScale = false;         // +0x40464
    float bitjamLevels = 0, bitjamK = 0, bitjamInvK = 0;
    // Blood overdrive
    float bo[18] {};                  // the plugin's fields 0x25c…0x2a4 (see bloodUpdate)
    float boL[6] {}, boR[6] {}, lp[4] {};
    float g294 = 1, g298 = 1, g29c = 1, g2b4 = 0;
    float c284 = 0, c288 = 0, c28c = 0, c290 = 0, c2a0 = 0, c2a4 = 0, c2b8 = 0;
    // Fast dist / soft clipper
    float fdDry = 0, fdWet = 0, fdPre = 0, scThr = 0, scW = 0, scC = 0, scPost = 1;
    // Harmor low pass
    bool lpOn = false;
    float lpFreq = 0;
    Biquad lpf[2];
    // Abrasive
    Mt19937 mt;
    NoiseSvf nsvf;
    std::vector<float> noise;
    // Crusher
    float crushRate = 0, crushBits = 0, crushLevels = 1;
    SampleHold hold;
    float srHalf = 22045.5898f; // the host's rate·0.4999 (Nyquist guard)
    std::vector<float> scratch, b1c, b24, b2c, b34, b3c;

    void setRate (double rate, int f)
    {
        fs = rate, factor = f;
        const auto k = (float) std::exp (-4.605170185988091 / ((double) (factor * 15) * 0.001 * fs));
        bias.k = cross.k = pre.k = wet.k = dry.k = k;
        srHalf = (float) (rate * 0.4999);
        hold.rateScale = (float) ((double) (int) (rate * factor) / 44100.0);
        nsvf.setRate ((double) (float) (rate * factor));
        designLowPass();
    }
    static void setTarget (Glide& g, float l, float r) noexcept { g.target[0] = l, g.target[1] = r; }

    void bloodUpdate() noexcept // FUN_007203a0
    {
        const auto pb = (float) ((double) p[0] / 10000.0), col = (float) ((double) p[1] / 10000.0),
                   preA = (float) ((double) p[2] / 10000.0), post = (float) ((double) p[3] / 10000.0),
                   filt = (float) ((double) p[4] / 10000.0), x100 = (float) p[5];
        c284 = (preA * preA) * preA;
        c2b8 = ((col * col) * col) * 0.000250000012f;
        const auto sw = (double) x100 <= 0.5 ? 25.0f : 2500.0f;
        c288 = (sw * c284 + 1.0f) * 0.000250000012f;
        c28c = (1.0f - pb) * 0.000250000012f;
        c290 = (post * post) * 0.000250000012f;
        c2a0 = filt * 0.699999988f + 0.200000003f;
        c2a4 = 1.0f - c2a0;
    }
    void fastDistUpdate() noexcept
    {
        fdPre = gainLaw (p[16]);
        const auto post = gainLaw (p[18]);
        const auto mix = gainLaw (p[19]);
        fdWet = mix * post;
        fdDry = (post - fdWet) * fdPre * post;
    }
    void harmorUpdate() noexcept // FUN_00720860, parameters 23…26
    {
        if (algo < 3 || algo > 13)
            return;
        const int sub = algo - 2;
        const float ln3 = 1.09861231f;
        auto fw = (float) p[25] * 0.00100000005f;
        if (fw != 1.0f)
            fw = (float) ((std::exp ((double) (fw * ln3)) - 1.0) * (double) 0.5f);
        const auto fd = (float) p[26] * 0.00100000005f;
        setTarget (dry, fd, fd);
        const auto w = (double) fw * (1.0 - std::fabs ((double) fd));
        harmorScale = true;
        const double ln2 = 0.69314718055994529, pi = 3.141592653589793;
        const auto p23 = (double) p[23];
        double d6 = 0;
        float preV = 0, wetV = 0;
        switch (sub)
        {
            case 7:
                d6 = std::exp ((p23 * 0.002 - 1.0) * ln2) * pi;
                preV = (float) (d6 * d6);
                wetV = (float) (w / std::sqrt (d6));
                break;
            case 1:
            case 2:
            case 3:
            case 8:
                d6 = std::exp ((double) ((float) p[23] * 0.00999999978f - 4.0f) * ln2);
                preV = (float) (d6 * pi);
                wetV = (float) (w / (dTanh (d6) * pi));
                break;
            case 4:
            {
                d6 = std::exp ((double) ((float) p[23] * 0.00999999978f - 2.0f) * ln2);
                const auto a = dTanh (d6 * 4.0);
                preV = (float) (d6 * pi);
                wetV = (float) (w / (a * a * a * pi));
                break;
            }
            case 5:
                d6 = std::exp ((p23 * 0.025 - 5.0) * ln2);
                preV = (float) d6;
                wetV = (float) (w / (std::log (d6 + 1.0) * (double) ((float) p[23] * 0.00100000005f + 1.0f)));
                break;
            case 6:
                d6 = std::exp ((p23 * 0.005 - 3.0) * ln2);
                preV = (float) (d6 * pi);
                wetV = (float) (w / (dTanh (d6) * pi));
                break;
            case 9:
            {
                const auto b = (float) std::exp ((double) p[24] * 0.004 * ln2);
                setTarget (bias, b, b);
                d6 = std::exp ((7.0 - p23 * 0.007) * ln2) * pi;
                preV = (float) d6;
                wetV = (float) (w / (double) (preV * b));
                harmorScale = false;
                break;
            }
            case 10:
                d6 = std::exp ((9.0 - p23 * 0.007) * ln2);
                preV = (float) d6;
                wetV = (float) (w / (double) preV);
                break;
            default:
            {
                bitjamLevels = (float) (std::exp ((double) p[24] * 0.02 * ln2) * 1024.0);
                preV = bitjamLevels;
                d6 = std::exp ((10.0 - p23 * 0.007) * ln2);
                bitjamK = (float) (d6 / std::log ((double) (bitjamLevels + 1.0f)));
                bitjamInvK = 1.0f / bitjamK;
                wetV = (float) (w / (double) bitjamLevels);
                harmorScale = false;
                break;
            }
        }
        setTarget (pre, preV, preV);
        setTarget (wet, wetV, wetV);
        if (! harmorScale)
            return;
        const auto f2 = (float) ((double) p[24] * 0.002);
        float f3;
        if (sub < 10)
            f3 = sub == 8 ? (f2 * f2) * preV : (float) ((double) (f2 * f2) * 0.75);
        else
            f3 = f2 / preV;
        std::int32_t f2bits;
        std::memcpy (&f2bits, &f2, 4);
        if (f2bits > 0) // the sign test is done on the float's bits
            setTarget (bias, f3, f3);
        else
            setTarget (bias, f3, -f3);
        harmorScale = sub != 8;
    }
    void designLowPass() noexcept // FUN_007204d0
    {
        auto top = (double) (srHalf * (float) factor);
        if (factor == 1)
            top = top - 45.0;
        if (top < (double) lpFreq)
            lpFreq = (float) top;
        const auto w = (double) lpFreq * ((6.283185307179586 / fs) / (double) factor);
        const auto s = std::sin (w), c = std::cos (w);
        for (auto& b : lpf)
            b.lowPass (s, c);
    }
    void setParam (int idx, int raw) noexcept // FUN_00720860
    {
        if (idx < 0 || idx >= 36)
            return;
        p[(std::size_t) idx] = raw;
        const auto v5000 = [] (int r) { return (float) ((double) r / 5000.0 - 1.0); };
        switch (idx)
        {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5: bloodUpdate(); break;
            case 7:
                if (algo == 14)
                    setTarget (cross, v5000 (p[7]), v5000 (p[7]));
                break;
            case 8:
                if (algo == 14)
                    setTarget (bias, v5000 (p[8]), v5000 (p[8]));
                break;
            case 11:
                if (algo == 15)
                    setTarget (cross, v5000 (p[11]), v5000 (p[11]));
                break;
            case 12:
                if (algo == 15)
                    setTarget (bias, v5000 (p[12]), v5000 (p[12]));
                break;
            case 16:
            case 18:
            case 19: fastDistUpdate(); break;
            case 21:
            {
                scThr = (float) p[21] * 0.0078125f;
                scW = 1.0f - scThr;
                if (scW <= 0.0f)
                    scW = 1.5258789e-05f;
                scC = 1.0f / scW;
                break;
            }
            case 22: scPost = gainLaw (p[22]); break;
            case 23:
            case 24:
            case 25:
            case 26: harmorUpdate(); break;
            case 27:
            {
                if (p[27] == 1100)
                {
                    lpOn = false;
                    break;
                }
                if (! lpOn)
                {
                    lpOn = true;
                    for (auto& b : lpf)
                        std::fill (std::begin (b.st), std::end (b.st), 0.0);
                }
                const auto a = std::exp ((double) ((float) p[27] * 0.00999999978f) * 0.69314718055994529);
                const auto b = std::exp (0.057762265046662105 * -57.0);
                lpFreq = (float) (a * b * 440.0);
                designLowPass();
                break;
            }
            case 34:
            {
                const auto r = (float) ((double) p[34] / 10000.0);
                crushRate = (r * r) * r;
                break;
            }
            case 35:
            {
                const auto b = (float) ((double) p[35] / 10000.0);
                crushBits = b * b;
                crushLevels = (float) dPower (2.0, (1.0 - std::sqrt (std::sqrt ((double) crushBits))) * 14.0 + 2.0); // fourth root
                break;
            }
            default: break;
        }
    }
    void setAlgorithm (int a) noexcept // FUN_00720300 (+ every parameter re-applied)
    {
        if (algo == 11)
            bias.cur[0] = bias.cur[1] = 0.0f;
        algo = std::clamp (a, 0, 17);
        for (int i = 0; i < 36; ++i)
            setParam (i, p[(std::size_t) i]);
    }

    // FUN_0071e3a0: the asymmetry glide added to the input
    void addBias (float* x, int n)
    {
        auto on = ! bias.atRest();
        if (algo > 2 && algo < 14)
            on = on && harmorScale;
        if (! on)
            return;
        scratch.resize ((std::size_t) n * 2);
        bias.ramp (scratch.data(), n);
        for (int i = 0; i < 2 * n; ++i)
            x[i] = x[i] + scratch[(std::size_t) i];
    }
    // FUN_0071e430: x += sign(x)·crossover glide
    void addCross (float* x, int n)
    {
        scratch.resize ((std::size_t) n * 2);
        cross.ramp (scratch.data(), n);
        for (int i = 0; i < 2 * n; ++i)
            x[i] = x[i] + (float) sgn ((double) x[i]) * scratch[(std::size_t) i];
    }
    // FUN_00713d70: dst = src·glide (zero when the glide rests at 0)
    static void mulGlide (Glide& g, const float* src, float* dst, int n) noexcept
    {
        if (g.moving())
        {
            auto dl = g.cur[0] - g.target[0], dr = g.cur[1] - g.target[1];
            for (int i = 0; i < n; ++i)
            {
                dl = dl * g.k, dr = dr * g.k;
                dst[2 * i] = src[2 * i] * (dl + g.target[0]);
                dst[2 * i + 1] = src[2 * i + 1] * (dr + g.target[1]);
            }
            if (n > 0)
                g.cur[0] = dl + g.target[0], g.cur[1] = dr + g.target[1];
        }
        else if (g.target[0] == 0.0f && g.target[1] == 0.0f)
            std::fill (dst, dst + 2 * n, 0.0f);
        else
            for (int i = 0; i < n; ++i)
                dst[2 * i] = src[2 * i] * g.target[0], dst[2 * i + 1] = src[2 * i + 1] * g.target[1];
    }
    // FUN_00713d00: dst += src·glide
    static void addMulGlide (Glide& g, const float* src, float* dst, int n) noexcept
    {
        if (g.moving())
        {
            auto dl = g.cur[0] - g.target[0], dr = g.cur[1] - g.target[1];
            for (int i = 0; i < n; ++i)
            {
                dl = dl * g.k, dr = dr * g.k;
                dst[2 * i] = src[2 * i] * (dl + g.target[0]) + dst[2 * i];
                dst[2 * i + 1] = src[2 * i + 1] * (dr + g.target[1]) + dst[2 * i + 1];
            }
            if (n > 0)
                g.cur[0] = dl + g.target[0], g.cur[1] = dr + g.target[1];
        }
        else if (g.target[0] != 0.0f || g.target[1] != 0.0f)
            for (int i = 0; i < n; ++i)
                dst[2 * i] = src[2 * i] * g.target[0] + dst[2 * i], dst[2 * i + 1] = src[2 * i + 1] * g.target[1] + dst[2 * i + 1];
    }

    // ---- algorithms
    void blood (const float* x, float* y, int n) noexcept // FUN_0071efe0
    {
        const auto& sh = halfSine();
        const float k = 0.999750018f, clip = 0.999949992f;
        auto chan = [&] (float* st, float in) {
            st[0] = st[0] + g2b4 * st[1];
            st[2] = (in - st[0]) - st[1];
            st[1] = st[1] + g2b4 * st[2];
            st[3] = st[3] + st[4] * 0.25f;
            st[5] = ((((st[2] + st[0]) * g298 + st[1]) * g294) - st[3]) - st[4];
            st[4] = st[4] + st[5] * 0.25f;
            auto v = ((sh (st[3]) + sh (st[4])) + sh (st[5])) * g29c;
            if (v > clip)
                v = clip;
            if (v < -clip)
                v = -clip;
            return v;
        };
        for (int i = 0; i < n; ++i)
        {
            g294 = g294 * k + c288;
            g298 = g298 * k + c28c;
            g29c = g29c * k + c290;
            g2b4 = g2b4 * k + c2b8;
            const auto yl = chan (boL, x[2 * i]);
            lp[0] = yl * c2a4 + lp[0] * c2a0;
            lp[1] = lp[0] * c2a4 + lp[1] * c2a0;
            y[2 * i] = lp[1];
            const auto yr = chan (boR, x[2 * i + 1]);
            lp[2] = yr * c2a4 + lp[2] * c2a0;
            lp[3] = lp[2] * c2a4 + lp[3] * c2a0;
            y[2 * i + 1] = lp[3];
        }
    }
    void fastDist (float* y, int n) noexcept // FUN_0071f590 → FUN_005fdb90 (x87)
    {
        const auto set = std::clamp (p[20], 0, 1), curve = std::clamp (p[17], 1, 10);
        const auto* t = fastdistfx::curve (set, curve);
        X87Precision pc (x87Pc);
        const ld A = (ld) fdPre * (ld) 8191;
        const ld W = (ld) fdWet * (ld) 3.0517578125e-05f;
        const ld D = (ld) fdDry;
        for (int i = 0; i < 2 * n; ++i)
        {
            const ld x = (ld) y[i];
            const ld q = x * A;
            const long idx = std::lrint (q);
            ld v;
            if (idx < 0)
                v = -(ld) t[std::min (-idx, 8191L)];
            else
                v = (ld) t[std::min (idx, 8191L)];
            const ld a = x * D;
            const ld b = v * W;
            y[i] = (float) (a + b);
        }
    }
    void softClip (float* y, int n) noexcept // FUN_0071fcd0 → FUN_005fdb10 (x87), then the post gain
    {
        {
            X87Precision pc (x87Pc);
            std::uint32_t thrBits;
            std::memcpy (&thrBits, &scThr, 4);
            const ld T = scThr, Wd = scW, C = scC;
            for (int i = 0; i < 2 * n; ++i)
            {
                std::uint32_t xb;
                std::memcpy (&xb, &y[i], 4);
                const auto ab = xb & 0x7fffffffu;
                if ((std::int32_t) thrBits < (std::int32_t) ab)
                {
                    float ax;
                    std::memcpy (&ax, &ab, 4);
                    ld v = (T - (ld) ax) * C;
                    v = v * 1.44269504088896340736L;
                    const ld r = (-x87Exp2 (v) + 1.0L) * Wd + T;
                    auto f = (float) r;
                    std::uint32_t fb;
                    std::memcpy (&fb, &f, 4);
                    fb |= xb & 0x80000000u;
                    std::memcpy (&y[i], &fb, 4);
                }
            }
        }
        if (scPost != 1.0f)
            for (int i = 0; i < 2 * n; ++i)
                y[i] = y[i] * scPost;
    }
    void harmor (float* x, float* y, int n) // FUN_0071f650
    {
        const auto m = (std::size_t) n * 2;
        b1c.resize (m), b24.resize (m), b2c.resize (m), b34.resize (m), b3c.resize (m);
        mulGlide (dry, x, b34.data(), n);
        addBias (x, n);
        mulGlide (pre, x, x, n);
        const int sub = algo - 2;
        const auto ftanh = [] (float v) { return (float) std::tanh ((double) v); };
        const auto fexp = [] (float v) { return (float) std::exp ((double) v); };
        const auto signOne = [] (float v) { return std::signbit (v) ? -1.0f : 1.0f; };
        for (std::size_t i = 0; i < m; ++i)
        {
            const auto v = x[i];
            float r = 0;
            switch (sub)
            {
                case 1: r = ftanh (v); break;
                case 2: r = fexp (ftanh (v)) + -1.0f; break;
                case 3: r = ftanh (fexp (v) + -1.0f); break;
                case 4:
                {
                    const auto t = ftanh (v);
                    r = (t * t) * t;
                    break;
                }
                case 5: r = signOne (v) * (float) std::log ((double) (std::fabs (v) + 1.0f)); break;
                case 6:
                {
                    const auto c = (v * v) * v;
                    r = (float) std::cbrt ((double) ftanh (c));
                    break;
                }
                case 7: r = signOne (v) * (float) std::sin ((double) (float) std::sqrt ((double) std::fabs (v))); break;
                case 8: r = ftanh (v); break; // completed below with the bias terms
                case 9: r = (float) std::sin ((double) v); break;
                case 10: r = std::nearbyint (v); break;
                default:
                {
                    auto l = (float) std::log ((double) (std::fabs (v) + 1.0f));
                    l = std::nearbyint (l * bitjamK);
                    l = l * bitjamInvK;
                    r = signOne (v) * (float) std::expm1 ((double) l);
                    break;
                }
            }
            b1c[i] = r;
        }
        if (sub == 8)
        {
            scratch.resize (m);
            bias.ramp (scratch.data(), n);
            for (std::size_t i = 0; i < m; ++i)
            {
                auto s = x[i] + scratch[i];
                b1c[i] = ftanh (s) * b1c[i];
                s = scratch[i] + s;
                b1c[i] = ftanh (s) * b1c[i];
            }
        }
        if (sub == 9)
            addMulGlide (bias, x, b1c.data(), n);
        addMulGlide (wet, b1c.data(), b34.data(), n);
        if (lpOn)
            for (auto& b : lpf)
                b.process (b34.data(), n);
        std::memcpy (y, b34.data(), sizeof (float) * m);
    }
    void waveFolder (const float* x, float* y, int n) noexcept // FUN_005fde90
    {
        const auto preA = (double) p[6] / 500.0 + 1.0;
        const auto thr = std::fabs ((double) p[9] * 0.9 / 10000.0 + 0.1);
        if (thr <= 0.0 || thr > 1.0 || preA == 0.0)
        {
            std::fill (y, y + 2 * n, 0.0f);
            return;
        }
        for (int i = 0; i < 2 * n; ++i)
        {
            const auto d = (double) x[i] * preA;
            auto t = (std::fabs (d) * 0.25) / thr;
            t = t - dInt (t);
            if (t >= 0.25)
                t = t >= 0.75 ? (t - 0.75) * 4.0 + -1.0 : 1.0 - (t - 0.25) * 4.0;
            else
                t = t * 4.0;
            if (d < 0.0)
                t = -t;
            y[i] = (float) (t * thr);
        }
    }
    void aperture (const float* x, float* y, int n) noexcept // FUN_005fe170
    {
        const auto preA = (double) p[10] / 500.0 + 1.0;
        const auto thr = std::fabs ((double) p[13] * 0.5 / 10000.0 + 0.5);
        const auto warp = (double) (p[14] * 2) / 10000.0 - 1.0;
        const auto foldMode = p[15] == 0;
        if (thr <= 0.0 || thr > 1.0 || preA == 0.0)
        {
            std::fill (y, y + 2 * n, 0.0f);
            return;
        }
        for (int i = 0; i < 2 * n; ++i)
        {
            auto d = (double) x[i] * preA;
            if (foldMode)
            {
                if (std::fabs (d) > thr)
                {
                    const auto q = d / thr;
                    d = d - (double) (long long) q * thr;
                    d = d - (thr + warp);
                }
            }
            else
            {
                const auto s = thr + warp;
                int it = 0;
                for (; it < 8; ++it)
                {
                    if (! (std::fabs (d) > thr))
                        break;
                    d = sgn (d) * (std::fabs (d) - s);
                }
                if (it == 8 && std::fabs (d) > thr)
                    d = sgn (d) * thr;
            }
            y[i] = (float) d;
        }
    }
    void abrasive (const float* x, float* y, int n) // FUN_0071ebf0
    {
        const auto wetOn = p[30] != 0;
        const auto preA = (double) p[28] / 5000.0 + 1.0;
        const auto wetG = (double) p[30] / 10000.0;
        const auto gate = (double) p[29] / 10000.0;
        const auto post = (double) p[31] / 5000.0 + 1.0;
        if (wetOn)
        {
            noise.resize ((std::size_t) n);
            for (int i = 0; i < n; ++i)
                noise[(std::size_t) i] = (float) ((double) mt.next() * 4.656612874161595e-10 - 1.0);
            if (p[33] == 1)
            {
                const auto f = (float) (dPower (2.0, (double) p[32] / 1000.0) * 20.0);
                nsvf.bandPass (f, (float) (1.0 - (double) 0.5f * 0.99), 1.0f);
                for (int i = 0; i < n; ++i)
                    noise[(std::size_t) i] = nsvf.tick (noise[(std::size_t) i]);
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const auto nz = wetOn ? (double) noise[(std::size_t) i] : 0.0;
            for (int c = 0; c < 2; ++c)
            {
                const auto xv = (double) x[2 * i + c];
                auto d = xv * preA;
                if (wetOn)
                {
                    const auto g = (double) ((long long) sgn (xv) * (long long) (gate < std::fabs (xv) ? 1 : 0));
                    d = d * (1.0 - wetG) + d * wetG * g * nz;
                }
                d = post * d;
                const auto a = std::fabs (d) < 1.0 ? std::fabs (d) : 1.0;
                y[2 * i + c] = (float) (sgn (d) * a);
            }
        }
    }
    void crusher (const float* x, float* y, int n) noexcept // FUN_0071ef50 → FUN_00721c70
    {
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                auto v = x[2 * i + c];
                if (0.0f < crushRate)
                    v = hold.tick (v, crushRate, c);
                if (0.0f < crushBits)
                    v = (float) dInt ((double) (v * crushLevels)) / crushLevels;
                y[2 * i + c] = v;
            }
    }

    // FUN_00720150: the selected algorithm, x → y (both n interleaved frames; x may be modified)
    void process (float* x, float* y, int n)
    {
        std::memcpy (y, x, sizeof (float) * 2 * (std::size_t) n);
        switch (algo)
        {
            case 0: blood (x, y, n); break;
            case 1: fastDist (y, n); break;
            case 2: softClip (y, n); break;
            case 14:
                addBias (x, n);
                addCross (x, n);
                waveFolder (x, y, n);
                break;
            case 15:
                addBias (x, n);
                addCross (x, n);
                aperture (x, y, n);
                break;
            case 16: abrasive (x, y, n); break;
            case 17: crusher (x, y, n); break;
            default: harmor (x, y, n); break;
        }
    }
};

// ------------------------------------------------------------------------------------------------ DC blocker
struct DcBlocker // FUN_005fbdf0 (x87)
{
    float a = 0, b = 0.96f;
    double s[2] {}, v[2] {};
    void setRate (double fs) noexcept { a = (float) ((44100.0 / fs) * 2.2834999999999997e-05); }
    void process (float* y, int n) noexcept
    {
        X87Precision pc (x87Pc);
        ld sl = s[0], sr = s[1], vl = v[0], vr = v[1];
        const ld A = a, B = b;
        for (int i = 0; i < n; ++i)
        {
            ld x = y[2 * i];
            vl = vl + (x - sl) * A;
            sl = sl + vl;
            vl = vl * B;
            y[2 * i] = (float) (x - sl);
            x = y[2 * i + 1];
            vr = vr + (x - sr) * A;
            sr = sr + vr;
            vr = vr * B;
            y[2 * i + 1] = (float) (x - sr);
        }
        // values within 2^-24 of zero are cleared before the states go back to memory (as doubles)
        const auto tiny = [] (ld z) { return std::fabs (z) <= (ld) 5.96046448e-08f ? 0.0L : z; };
        s[0] = (double) tiny (sl), s[1] = (double) tiny (sr), v[0] = (double) tiny (vl), v[1] = (double) tiny (vr);
    }
};
} // namespace

// ------------------------------------------------------------------------------------------------ state
ModuleState defaultModule (ModuleType type) noexcept
{
    ModuleState m;
    m.type = (int) type;
    m.p = moduleDefaults;
    return m;
}

std::optional<ModuleState> parseModule (const std::uint8_t* d, std::size_t size, std::size_t* used)
{
    if (size < 4)
        return std::nullopt;
    ModuleState m;
    m.version = i32At (d);
    if (m.version < 1 || m.version > 4)
        return std::nullopt;
    const std::size_t need = 4 + 12 + 2 + 4 + 1 + 4 + 196 + (m.version > 1 ? 4 : 0) + (m.version >= 4 ? 4 : 0);
    if (size < need)
        return std::nullopt;
    std::size_t o = 4;
    m.type = std::clamp (i32At (d + o), 0, 3), o += 4;
    m.algorithm = std::clamp (i32At (d + o), 0, 17), o += 4;
    m.oversample = std::clamp (i32At (d + o), 0, 4), o += 4;
    m.removeDc = d[o] != 0, m.bypass = d[o + 1] != 0, o += 2;
    m.filterType = std::clamp (i32At (d + o), 0, 21), o += 4;
    m.smoothFilter = d[o] != 0, o += 1;
    m.impulse = std::clamp (i32At (d + o), 0, 58), o += 4;
    for (int k = 0; k < moduleParams; ++k)
        m.p[(std::size_t) k] = i32At (d + o + 4 * (std::size_t) k);
    o += 196;
    if (m.version > 1)
        o += 4;
    if (m.version >= 4)
        m.chorusMode = i32At (d + o), o += 4;
    if (used != nullptr)
        *used = o;
    return m;
}

std::optional<State> parse (const std::vector<std::uint8_t>& b)
{
    if (b.size() < 8)
        return std::nullopt;
    State s;
    s.version = i32At (b.data());
    const auto n = i32At (b.data() + 4);
    if (s.version < 1 || s.version > 5 || n < 0 || n > maxModules)
        return std::nullopt;
    std::size_t o = 8;
    for (int k = 0; k < n; ++k)
    {
        std::size_t used = 0;
        auto m = parseModule (b.data() + o, b.size() - o, &used);
        if (! m)
            return std::nullopt;
        s.modules.push_back (*m);
        o += used;
    }
    if (o < b.size())
        s.clip = b[o] != 0, o += 1;
    if (s.version > 3 && o + 4 <= b.size())
    {
        const auto cnt = i32At (b.data() + o);
        o += 4;
        for (int k = 0; k < cnt && o + 4 <= b.size(); ++k, o += 4)
            if (k < maxModules)
                s.order[(std::size_t) k] = i32At (b.data() + o);
    }
    return s;
}

std::vector<std::uint8_t> serialize (const State& s)
{
    std::vector<std::uint8_t> v;
    putI32 (v, 5);
    putI32 (v, (int) s.modules.size());
    for (const auto& m : s.modules)
    {
        putI32 (v, 4);
        putI32 (v, m.type);
        putI32 (v, m.algorithm);
        putI32 (v, m.oversample);
        v.push_back (m.removeDc ? 1 : 0);
        v.push_back (m.bypass ? 1 : 0);
        putI32 (v, m.filterType);
        v.push_back (m.smoothFilter ? 1 : 0);
        putI32 (v, m.impulse);
        for (auto x : m.p)
            putI32 (v, x);
        putI32 (v, 0);
        putI32 (v, m.chorusMode);
    }
    v.push_back (s.clip ? 1 : 0);
    putI32 (v, 4);
    for (auto x : s.order)
        putI32 (v, x);
    putI32 (v, 0);
    v.push_back (1);
    return v;
}

// ------------------------------------------------------------------------------------------------ module
struct Module::Impl
{
    ModuleState st;
    double fs = 44100.0;
    Distortion dist;
    DcBlocker dc;
    std::vector<HalfbandStage> upSt, downSt;
    std::vector<float> s1, s2, s3, hi, tmp;

    int factor() const noexcept { return 1 << st.oversample; }
    bool overs() const noexcept { return st.oversample > 0 && (st.type == 0 || st.type == 1); }

    void configure()
    {
        upSt.assign ((std::size_t) st.oversample, HalfbandStage {});
        downSt.assign ((std::size_t) st.oversample, HalfbandStage {});
        dist.setRate (fs, factor());
        dc.setRate (fs);
        dist = Distortion {};
        dist.setRate (fs, factor());
        for (int i = 0; i < 36; ++i)
            dist.p[(std::size_t) i] = st.p[(std::size_t) i];
        dist.setAlgorithm (st.algorithm);
    }
    void process (float* x, int n)
    {
        if (st.bypass || n <= 0)
            return;
        const auto gin = (float) ((double) st.p[36] / 5000.0);
        const auto mix = (double) st.p[37] / 10000.0;
        const auto gout = (float) ((double) st.p[38] / 5000.0);
        const auto ovs = overs();
        float* w = x;
        int m = n;
        if (ovs)
        {
            m = n * factor();
            hi.resize ((std::size_t) m * 2);
            tmp.resize ((std::size_t) m * 2);
            const float* src = x;
            int cur = n;
            for (std::size_t k = 0; k < upSt.size(); ++k)
            {
                // the last stage lands in hi, earlier ones alternate so that the final output is in hi
                float* dst = ((upSt.size() - 1 - k) % 2 == 0) ? hi.data() : tmp.data();
                upSt[k].up (src, dst, cur);
                src = dst;
                cur *= 2;
            }
            w = hi.data();
        }
        s1.resize ((std::size_t) m * 2), s2.resize ((std::size_t) m * 2), s3.resize ((std::size_t) m * 2);
        for (int i = 0; i < 2 * m; ++i)
            s1[(std::size_t) i] = w[i] * gin;
        std::memcpy (s2.data(), s1.data(), sizeof (float) * 2 * (std::size_t) m);
        if (st.type == 0)
            dist.process (s1.data(), w, m);
        const auto dryG = (float) (1.0 - mix), wetG = (float) mix;
        for (int i = 0; i < 2 * m; ++i)
        {
            auto v = 0.0f + s2[(std::size_t) i] * dryG;
            v = w[i] * wetG + v;
            w[i] = v * gout;
        }
        if (ovs)
        {
            int cur = m;
            const float* src = hi.data();
            for (std::size_t k = 0; k < downSt.size(); ++k)
            {
                cur /= 2;
                float* dst = (k + 1 == downSt.size()) ? x : (k % 2 == 0 ? tmp.data() : hi.data());
                downSt[k].down (src, dst, cur);
                src = dst;
            }
        }
        if (st.type == 0 && st.removeDc)
            dc.process (x, n);
    }
};

Module::Module() : impl (std::make_unique<Impl>()) {}
Module::~Module() = default;
void Module::prepare (double sampleRate, int)
{
    impl->fs = sampleRate;
    impl->configure();
}
void Module::set (const ModuleState& s)
{
    impl->st = s;
    impl->configure();
}
void Module::process (float* left, float* right, int n) noexcept
{
    std::vector<float> lr ((std::size_t) n * 2);
    for (int i = 0; i < n; ++i)
        lr[(std::size_t) (2 * i)] = left[i], lr[(std::size_t) (2 * i + 1)] = right[i];
    impl->process (lr.data(), n);
    for (int i = 0; i < n; ++i)
        left[i] = lr[(std::size_t) (2 * i)], right[i] = lr[(std::size_t) (2 * i + 1)];
}
const ModuleState& Module::state() const noexcept { return impl->st; }

// ------------------------------------------------------------------------------------------------ plugin
struct Engine::Impl
{
    State st;
    double fs = 44100.0;
    int maxBlock = 512;
    std::vector<std::unique_ptr<Module>> mods;
};
Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;
void Engine::prepare (double sampleRate, int maxBlock)
{
    impl->fs = sampleRate, impl->maxBlock = maxBlock;
    for (auto& m : impl->mods)
        m->prepare (sampleRate, maxBlock);
}
void Engine::set (const State& s)
{
    impl->st = s;
    impl->mods.clear();
    for (const auto& ms : s.modules)
    {
        auto m = std::make_unique<Module>();
        m->prepare (impl->fs, impl->maxBlock);
        m->set (ms);
        impl->mods.push_back (std::move (m));
    }
}
void Engine::reset() { set (impl->st); }
void Engine::process (float* left, float* right, int n) noexcept
{
    for (auto& m : impl->mods)
        m->process (left, right, n);
}

std::string moduleName (const ModuleState& m)
{
    static const char* types[] = { "Distortion", "Filter", "Chorus", "Amp/Cab" };
    return types[std::clamp (m.type, 0, 3)];
}
void setImpulseFolder (const std::string&) {}
} // namespace lynx::distructor
