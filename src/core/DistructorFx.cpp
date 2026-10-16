#include "core/DistructorFx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <complex>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

#include "core/FlangusFastDist.h"
#include "core/FruityDelay.h"
#include "core/WavPack.h"

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

// the amp/cabinet module's impulse list (Module_x64's table: display name, file in the installation's
// Plugins\\Fruity\\Effects\\Distructor\\module\\impulses folder, output level); entries without a file are headings
struct ImpulseInfo
{
    const char* name;
    const char* file;
    float level;
};
constexpr ImpulseInfo impulseTable[59] = {
    { "-Cabinets Stereo", nullptr, 0.0f },
    { "Blues", "IMP Cabinet Stereo Blues.wv", 0.0260000005f },
    { "Guitaro", "IMP Cabinet Stereo Guitaro.wv", 0.100000001f },
    { "Metal", "IMP Cabinet Stereo Metal.wv", 0.0799999982f },
    { "Muffled", "IMP Cabinet Stereo Muffled.wv", 0.0580000021f },
    { "Wide", "IMP Cabinet Stereo Wide.wv", 0.0860000029f },
    { "Combo 01", "IMP Cabinet Stereo Combo 01.wv", 0.150000006f },
    { "Combo 02", "IMP Cabinet Stereo Combo 02.wv", 0.136000007f },
    { "Combo 03", "IMP Cabinet Stereo Combo 03.wv", 0.128000006f },
    { "Combo 04", "IMP Cabinet Stereo Combo 04.wv", 0.100000001f },
    { "Combo 05", "IMP Cabinet Stereo Combo 05.wv", 0.159999996f },
    { "Combo 06", "IMP Cabinet Stereo Combo 06.wv", 0.128000006f },
    { "Combo 07", "IMP Cabinet Stereo Combo 07.wv", 0.120000005f },
    { "Combo 08", "IMP Cabinet Stereo Combo 08.wv", 0.100000001f },
    { "Combo 09", "IMP Cabinet Stereo Combo 09.wv", 0.129999995f },
    { "Combo 10", "IMP Cabinet Stereo Combo 10.wv", 0.120000005f },
    { "Combo 11", "IMP Cabinet Stereo Combo 11.wv", 0.100000001f },
    { "Combo 12", "IMP Cabinet Stereo Combo 12.wv", 0.120000005f },
    { "Combo 13", "IMP Cabinet Stereo Combo 13.wv", 0.101999998f },
    { "Combo 14", "IMP Cabinet Stereo Combo 14.wv", 0.100000001f },
    { "-Cabinets Mono", nullptr, 0.0f },
    { "Closest", "IMP Cabinet Mono Closest.wv", 0.158000007f },
    { "Closer", "IMP Cabinet Mono Closer.wv", 0.129999995f },
    { "Close", "IMP Cabinet Mono Close.wv", 0.129999995f },
    { "Medium", "IMP Cabinet Mono Medium.wv", 0.109999999f },
    { "Far", "IMP Cabinet Mono Far.wv", 0.0960000008f },
    { "Farther", "IMP Cabinet Mono Farther.wv", 0.0879999995f },
    { "-Hardcore Stereo", nullptr, 0.0f },
    { "Model AB", "IMP Cabinet Model AB.wv", 0.0260000005f },
    { "Model CD", "IMP Cabinet Model CD.wv", 0.129999995f },
    { "Model EF", "IMP Cabinet Model EF.wv", 0.120000005f },
    { "Model GH", "IMP Cabinet Model GH.wv", 0.200000003f },
    { "Model IJ", "IMP Cabinet Model IJ.wv", 0.0700000003f },
    { "Model KL", "IMP Cabinet Model KL.wv", 0.172000006f },
    { "Model MN", "IMP Cabinet Model MN.wv", 0.0680000037f },
    { "Model OP", "IMP Cabinet Model OP.wv", 0.0250000004f },
    { "Model QR", "IMP Cabinet Model QR.wv", 0.0560000017f },
    { "Model ST", "IMP Cabinet Model ST.wv", 0.0240000002f },
    { "-Hardcore Mono", nullptr, 0.0f },
    { "Model A", "IMP Cabinet Model A.wv", 0.104000002f },
    { "Model B", "IMP Cabinet Model B.wv", 0.120000005f },
    { "Model C", "IMP Cabinet Model C.wv", 0.101999998f },
    { "Model D", "IMP Cabinet Model D.wv", 0.137999997f },
    { "Model E", "IMP Cabinet Model E.wv", 0.120000005f },
    { "Model F", "IMP Cabinet Model F.wv", 0.0799999982f },
    { "Model G", "IMP Cabinet Model G.wv", 0.120000005f },
    { "Model H", "IMP Cabinet Model H.wv", 0.136000007f },
    { "Model I", "IMP Cabinet Model I.wv", 0.108000003f },
    { "Model J", "IMP Cabinet Model J.wv", 0.148000002f },
    { "Model K", "IMP Cabinet Model K.wv", 0.124000005f },
    { "Model L", "IMP Cabinet Model L.wv", 0.140000001f },
    { "Model M", "IMP Cabinet Model M.wv", 0.0799999982f },
    { "Model N", "IMP Cabinet Model N.wv", 0.109999999f },
    { "Model O", "IMP Cabinet Model O.wv", 0.0560000017f },
    { "Model P", "IMP Cabinet Model P.wv", 0.0520000011f },
    { "Model Q", "IMP Cabinet Model Q.wv", 0.0759999976f },
    { "Model R", "IMP Cabinet Model R.wv", 0.064000003f },
    { "Model S", "IMP Cabinet Model S.wv", 0.0920000002f },
    { "Model T", "IMP Cabinet Model T.wv", 0.064000003f },
};
std::string& impulseFolder()
{
    static std::string f;
    return f;
}

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
    Mt19937() : Mt19937 (19650218u) {}
    explicit Mt19937 (std::uint32_t seed)
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

// the plugins' soft clipper (FUN_005fdb10 / FUN_005df670, x87): |x| above thr bends into
// thr + (1 − thr)·(1 − 2^((thr − |x|)·log2(e)/(1 − thr))), keeping the sign
void softClipBlock (float* y, int count, float thr) noexcept
{
    auto w = 1.0f - thr;
    if (w <= 0.0f)
        w = 1.5258789e-05f;
    const auto c = 1.0f / w;
    X87Precision pc (x87Pc);
    std::uint32_t thrBits;
    std::memcpy (&thrBits, &thr, 4);
    const ld T = thr, Wd = w, C = c;
    for (int i = 0; i < count; ++i)
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
    void softClip (float* y, int n) noexcept // FUN_0071fcd0 → FUN_005fdb10, then the post gain
    {
        softClipBlock (y, 2 * n, scThr);
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

// ------------------------------------------------------------------------------------------------ filter
// the module's trapezoidal state-variable filter (FUN_00716690 design, FUN_00716500 tick), one state per channel
struct Svf
{
    float piOverFs = (float) (3.141592653589793 / 44100.0);
    float a1 = 0, a2 = 0, a3 = 0, m0 = 0, m1 = 0, m2 = 0;
    float ic1[2] {}, ic2[2] {};
    void setRate (double rate) noexcept { piOverFs = (float) (3.141592653589793 / rate); }
    void coef (float g, float k) noexcept
    {
        a1 = 1.0f / (g * (g + k) + 1.0f);
        a2 = g * a1;
        a3 = g * a2;
    }
    // mode: 0 low pass, 1 high pass, 2 band pass, 3 notch, 4 peak, 6 low shelf, 7 high shelf, 8 all pass
    void design (int mode, float f, float k, float gain) noexcept
    {
        const auto w = f * piOverFs;
        switch (mode)
        {
            case 0: coef ((float) std::tan ((double) w), k), m0 = 0, m1 = 0, m2 = gain; break;
            case 1: coef ((float) std::tan ((double) w), k), m0 = gain, m1 = -k * gain, m2 = -gain; break;
            case 2: coef ((float) std::tan ((double) w), k), m0 = 0, m1 = gain, m2 = 0; break;
            case 3: coef ((float) std::tan ((double) w), k), m0 = gain, m1 = -k * gain, m2 = 0; break;
            case 4: coef ((float) std::tan ((double) w), k), m0 = gain, m1 = -k * gain, m2 = gain * -2.0f; break;
            case 6:
            {
                const auto A = (float) dPower (10.0, (double) (gain / 40.0f));
                const auto t = (float) std::tan ((double) w);
                const auto g = (float) ((double) t / std::sqrt ((double) A));
                coef (g, k);
                m0 = 1.0f, m1 = k * (A - 1.0f), m2 = A * A - 1.0f;
                break;
            }
            case 7:
            {
                const auto A = (float) dPower (10.0, (double) (gain / 40.0f));
                const auto t = (float) std::tan ((double) w);
                const auto g = (float) ((double) t * std::sqrt ((double) A));
                coef (g, k);
                m0 = A * A, m1 = k * (1.0f - A) * A, m2 = 1.0f - A * A;
                break;
            }
            case 8: coef ((float) std::tan ((double) w), k), m0 = 1.0f, m1 = (float) ((double) k * -2.0), m2 = 0; break;
            default: break;
        }
    }
    float tick (int c, float x) noexcept
    {
        const auto v3 = x - ic2[c];
        const auto v1 = a1 * ic1[c] + a2 * v3;
        const auto v2 = (ic2[c] + a2 * ic1[c]) + a3 * v3;
        ic1[c] = v1 * 2.0f - ic1[c];
        ic2[c] = v2 * 2.0f - ic2[c];
        return (m0 * x + m1 * v1) + m2 * v2;
    }
};

// the one-pole low/high pass (FUN_007183c0 / FUN_00718300 design, FUN_007184d0 tick)
struct OnePole
{
    float piOverFs = 0, b0 = 0, b1 = 0, a = 0;
    float y[2] {}, x1[2] {};
    void setRate (double rate) noexcept { piOverFs = (float) ((1.0 / rate) * 3.141592653589793); }
    void design (bool high, float f) noexcept
    {
        const auto g = (float) std::tan ((double) (f * piOverFs));
        const auto d = g + 1.0f;
        if (high)
        {
            b0 = (float) (1.0 / (double) d);
            b1 = -b0;
        }
        else
            b0 = b1 = g / d;
        a = (float) (-((double) g - 1.0) / (double) d);
    }
    float tick (int c, float x) noexcept
    {
        const auto v = (x * b0 + x1[c] * b1) + y[c] * a;
        y[c] = v, x1[c] = x;
        return v;
    }
};

// the comb filters' delay lines (FUN_007185e0: 16384 floats, linear interpolation)
struct CombLine
{
    std::vector<float> buf = std::vector<float> (16384, 0.0f);
    int w = 0;
    float read (float d) const noexcept
    {
        const auto i = (int) (double) d;
        const auto fr = d - (float) i;
        const auto a = buf[(std::size_t) ((w - i) & 16383)], b = buf[(std::size_t) ((w - i - 1) & 16383)];
        const auto t = (double) (b * fr);
        return (float) ((double) a * (1.0 - (double) fr) + t);
    }
    void write (float v) noexcept
    {
        buf[(std::size_t) w] = v;
        w = (w + 1) & 16383;
    }
};

// the "Alt" filters (FUN_00716ec0 class): a ×4 oversampled state-variable loop with a soft clipper in its feedback
struct AltSvf
{
    float invFs = 0, step = 0.03125f, res = 0, f = 0, fCur = 0, fStep = 0, drive = 4.0f, clipAmt = 0;
    float wLp = 1, wBp = 1, wHp = 1;
    float x1[2] {}, hp[2] {}, bp[2] {}, lp[2] {};
    void setRate (double rate) noexcept
    {
        invFs = (float) (1.0 / (double) (float) rate);
        step = 0.03125f;
    }
    void design (float freq, float r, bool jump) noexcept // FUN_007176b0
    {
        res = (float) ((double) r * 0.5);
        f = (float) ((double) freq * 6.283 * (double) invFs * 0.25);
        drive = (float) ((double) clipAmt * 4.0 + 4.0);
        if (jump)
            fCur = f;
        fStep = (f - fCur) * step;
    }
    static float clip (float x) noexcept // FUN_00717570
    {
        if ((double) x > 2.0)
            return 1.0f;
        if ((double) x < -2.0)
            return -1.0f;
        const auto y = (float) ((double) x * 0.5);
        return (float) ((double) y * (2.0 - std::fabs ((double) y)));
    }
    float tick (int c, float x) noexcept // FUN_00717090
    {
        const auto g = fCur;
        const auto xp = x1[c];
        const auto d4 = (double) xp * 0.75, d7 = (double) x * 0.25;
        const auto d5 = (double) xp * 0.5, d8 = (double) x * 0.5;
        const auto d6 = (double) xp * 0.25, d9 = (double) x * 0.75;
        x1[c] = x;
        auto& h = hp[c];
        auto& b = bp[c];
        auto& l = lp[c];
        auto sub = [&] (float in) {
            h = (in - l) - b;
            const auto s = clip (b * drive);
            h = h + res * s;
            b = b + g * h;
            l = l + g * b;
        };
        sub (xp);
        sub ((float) (d4 + d7));
        sub ((float) (d5 + d8));
        sub ((float) (d6 + d9));
        return (l * wLp + h * wHp) + b * wBp;
    }
};

// "Low pass 24 Alt" (FUN_00717790 class): a four-pole state-space low pass in double, its transition matrix built
// by squaring a one-step matrix twelve times, a saturating first state and a DC follower on the output
struct Ladder4
{
    double fs = 44100.0;
    double M[5][4] {}; // rows 0…3 act on the states, row 4 on the input
    double s[2][4] {}, dc[2] {};
    void design (float f, float r) noexcept // FUN_00717d30
    {
        if ((double) f > 20000.0)
            f = 20000.0f;
        auto a = ((double) f * 6.25) / (fs * 4096.0);
        a = std::clamp (a, 0.0, 1.0);
        const auto b = 1.0 - a;
        const auto q = (double) - (float) ((double) r * 4.05);
        for (auto& row : M)
            std::fill (std::begin (row), std::end (row), 0.0);
        M[0][0] = b, M[3][0] = q * a, M[4][0] = a, M[0][1] = b * a, M[1][1] = b, M[3][1] = q * a * a, M[4][1] = a * a;
        M[0][2] = b * a * a, M[1][2] = b * a, M[2][2] = b, M[3][2] = q * a * a * a, M[4][2] = a * a * a;
        M[0][3] = b * a * a * a, M[1][3] = b * a * a, M[2][3] = b * a, M[3][3] = b + q * a * a * a * a, M[4][3] = a * a * a * a;
        for (int it = 0; it < 12; ++it)
        {
            double N[5][4];
            for (int j = 0; j < 4; ++j)
            {
                for (int r2 = 0; r2 < 4; ++r2)
                    N[r2][j] = ((M[r2][0] * M[0][j] + M[r2][1] * M[1][j]) + M[r2][2] * M[2][j]) + M[r2][3] * M[3][j];
                N[4][j] = (((M[4][0] * M[0][j] + M[4][1] * M[1][j]) + M[4][2] * M[2][j]) + M[4][3] * M[3][j]) + M[4][j];
            }
            std::memcpy (M, N, sizeof (M));
        }
    }
    float tick (int c, float x) noexcept // FUN_00717920
    {
        const auto u = (double) ((x + 0.349999994f) * 0.200000003f);
        auto& t = s[c];
        const double o0 = t[0], o1 = t[1], o2 = t[2], o3 = t[3];
        t[0] = (((o0 * M[0][0] + o1 * M[1][0]) + o2 * M[2][0]) + o3 * M[3][0]) + u * M[4][0];
        t[1] = (((o0 * M[0][1] + o1 * M[1][1]) + o2 * M[2][1]) + o3 * M[3][1]) + u * M[4][1];
        t[2] = (((o0 * M[0][2] + o1 * M[1][2]) + o2 * M[2][2]) + o3 * M[3][2]) + u * M[4][2];
        t[3] = (((o0 * M[0][3] + o1 * M[1][3]) + o2 * M[2][3]) + o3 * M[3][3]) + u * M[4][3];
        const auto y = t[3] / (double) 0.200000003f;
        t[0] = t[0] / (t[0] * t[0] + 1.0);
        dc[c] = dc[c] + (y - dc[c]) * 0.005;
        return (float) (y - dc[c]);
    }
};

// "Vowel" (FUN_00718ea0 class): eight two-pole formant resonators per channel, their frequencies blended between
// three vowel tables
struct Vowel
{
    struct Res
    {
        float f = -1, q = -1, sr = -1, b0 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void design (float nf, float nq, float nsr) noexcept // FUN_00718d30
        {
            if (nf == f && nq == q && nsr == sr)
                return;
            f = nf, q = nq, sr = nsr;
            const auto w = ((double) f * 6.28318530717959) / (double) sr;
            const auto sn = std::sin (w), cs = std::cos (w);
            b0 = (float) (sn / 2.0);
            a1 = (float) (cs * 2.0);
            a2 = (float) (1.0 - sn / ((double) q * 2.0));
            const auto d = sn / ((double) q * 2.0) + 1.0;
            b0 = (float) ((double) b0 / d);
            a1 = (float) ((double) a1 / d);
            a2 = (float) ((double) a2 / d);
        }
        float tick (float x) noexcept // FUN_00718cc0
        {
            const auto xo = x2, yo = y2;
            x2 = x1, x1 = x, y2 = y1;
            y1 = (b0 * (x - xo) + a1 * y1) - a2 * yo;
            return y1;
        }
    };
    static constexpr float tables[3][8] = { { 250, 700, 2500, 3500, 4200, 8500, 10000, 12000 },
                                            { 700, 1250, 2500, 3500, 4200, 8500, 10000, 12000 },
                                            { 250, 2500, 2700, 3500, 4200, 8500, 10000, 12000 } };
    float sr = 44100.0f, pos = 0, res = 0;
    Res l[8], r[8];
    void tick (float& L, float& R) noexcept // FUN_00719100
    {
        const auto scale = (float) dPower (2.0, ((double) res - 0.5) * 1.7);
        const auto q3 = (double) pos * 3.0;
        auto idx = (int) q3;
        const auto fr = (float) (q3 - (double) idx);
        if (idx > 2)
            idx = 0;
        float wa = 0, wb = 0, wc = 0;
        if (idx >= 0 && idx < 1)
            wa = (float) (1.0 - (double) fr), wb = fr;
        if (idx >= 1 && idx < 2)
            wb = (float) (1.0 - (double) fr), wc = fr;
        if (idx >= 2 && idx < 3)
            wc = (float) (1.0 - (double) fr), wa = fr;
        float al = 0, ar = 0;
        for (int i = 0; i < 8; ++i)
        {
            const auto f = ((wa * tables[0][i] + wb * tables[1][i]) + wc * tables[2][i]) * scale;
            l[i].design (f, 12.0f, sr);
            r[i].design (f, 12.0f, sr);
            const auto g = (float) (1.0 - (double) (float) ((double) idx / 8.0));
            al = al + l[i].tick (L) * g;
            ar = ar + r[i].tick (R) * g;
        }
        L = (float) ((double) L * (1.0 - (double) 1.0f) + (double) (1.0f * al));
        R = (float) ((double) R * (1.0 - (double) 1.0f) + (double) (1.0f * ar));
        L = (float) ((double) L * 0.5);
        R = (float) ((double) R * 0.5);
    }
};

// "Phaser 1/2/3" (FUN_007195b0 class): a chain of all-pass state-variable stages swept by a triangle LFO, a feedback
// tap, a wet/dry blend and a one-pole high pass, with three fixed presets of its fourteen controls
struct Phaser
{
    float sr = 44100.0f;
    float mix = 0.5f, rateV = 0, cut = 0, fb = 0, res = 0, depth = 0, offset = 0, hpV = 0;
    int outStage = 0, fbStage = 4, minStages = 4;
    bool sync = false;
    float inc = 0, phase = 0, q = 0, fbL = 0, fbR = 0;
    Svf stL[6], stR[6];
    OnePole hpL, hpR;
    static float interp (const float* t, float x) noexcept // FUN_00719b80
    {
        const auto i = (int) (double) x;
        const auto fr = x - (float) i;
        return (float) ((double) t[i] * (1.0 - (double) fr) + (double) (t[i + 1] * fr));
    }
    void setRate (double rate) noexcept // FUN_0071a890
    {
        sr = (float) rate;
        setRateCtl (rateV);
        for (int k = 0; k < 6; ++k)
            stL[k].setRate ((double) sr), stR[k].setRate ((double) sr);
        hpL.setRate ((double) sr), hpR.setRate ((double) sr);
        setHp (hpV);
    }
    void setRateCtl (float v) noexcept // FUN_0071a840
    {
        static constexpr float rates[10] = { 576.289978f, 26.9599991f, 8.88000011f, 4.0f, 1.84000003f, 0.879999995f, 0.400000006f, 0.147f, 0.0199999996f, 1.0f };
        rateV = v;
        const auto per = interp (rates, (float) ((double) v * 7.99999));
        inc = (float) (1.0 / (double) (per * sr));
    }
    void setHp (float v) noexcept // FUN_0071a500
    {
        hpV = v;
        const auto f = (float) ((double) (v * v * v * 990.0f) + 10.0);
        hpL.design (true, f), hpR.design (true, f);
    }
    static int stages6 (float v) noexcept { return (int) std::nearbyint (std::clamp ((double) v, 0.0, 1.0) * 6.0); }
    void set (int i, float v) noexcept // FUN_0071a650
    {
        switch (i)
        {
            case 0: mix = v; break;
            case 1: setRateCtl (v); break;
            case 2: cut = v; break;
            case 3: outStage = std::clamp (stages6 (v) + 1, 1, 6) - 1; break;
            case 4: fb = (float) ((double) -0.727999985f + (double) 0.727999985f * 2.0 * (double) v); break;
            case 5: res = v; break;
            case 6: depth = (float) ((double) v * 0.85); break;
            case 9: offset = v; break;
            case 11: setHp (v); break;
            case 12: sync = (double) v >= 0.5; break;
            case 13: fbStage = std::clamp (stages6 (v), 0, 5); break;
            default: break;
        }
    }
    void preset (int which) noexcept // FUN_0071ab70
    {
        static constexpr float p[3][14] = { { 1, 0, 0.462f, 0.436f, 0, 0.5f, 0, 0, 1, 0, 0.839f, 0.19f, 0, 0.63f },
                                            { 1, 0, 0.5065f, 0.4166f, 1, 0.5f, 0.25f, 0, 1, 0, 0.5f, 0.19f, 0, 0.459f },
                                            { 1, 0, 0.5f, 1, 0, 0.5f, 0, 0, 1, 0, 0.37f, 0, 1, 1 } };
        for (int i = 0; i < 14; ++i)
            set (i, p[which][i]);
    }
    Phaser()
    {
        // the class's own start: defaults of controls 0…4, then the wrapper's rate set-up, depth control 44100 and preset
        static constexpr float d[5] = { 0.5f, 0.5f, 0.75f, 0.5f, 0.0f };
        for (int i = 0; i < 5; ++i)
            set (i, d[i]);
        setHp (300.0f);
    }
    float stage (Svf* st, float& fbState, float x, int n) noexcept
    {
        float tap = 0.0f;
        x = x + fbState * fb;
        for (int k = 0; k < n; ++k)
        {
            x = st[k].tick (0, x);
            if (k == outStage)
                tap = x;
            if (k == fbStage)
                fbState = x;
        }
        return tap;
    }
    void tick (float& L, float& R, double bpm) noexcept // FUN_00719c70
    {
        static constexpr float qs[5] = { 3.0f, 1.60000002f, 0.832000017f, 0.5f, 0.349999994f };
        q = interp (qs, (float) ((double) res * 3.9999));
        if (sync)
        {
            // the synced length control (+0xec) is never set by the module: round(0·768) + 1 = 1/192 of a beat
            const int len = (int) std::nearbyint (0.0 * 768.0) + 1;
            phase = (float) ((double) phase + 1.0 / ((1.0 / (bpm / 60.0)) * ((double) (float) len / 192.0) * (double) sr));
        }
        else
            phase = phase + inc;
        phase = phase - (float) (long long) (double) phase;
        auto t = phase + offset;
        t = t - (float) (long long) (double) t;
        const auto tri = (double) t < 0.5 ? (float) ((double) t * 2.0) : (float) (2.0 - (double) t * 2.0);
        const int n = std::max (minStages, fbStage + 1);
        auto sweep = [&] {
            const auto c = (float) ((double) cut + (1.0 - (double) cut) * (double) tri * (double) depth);
            auto f = dPower (2.0, (double) ((float) ((double) c * 0.925) * 10.0208330f)) * 28.5;
            if (f > 22000.0)
                f = 22000.0;
            return (float) f;
        };
        const auto k = (float) (1.0 / (double) q);
        const auto fl = sweep();
        stL[0].design (8, fl, k, 1.0f);
        for (int s2 = 1; s2 < n; ++s2)
            stL[s2].a1 = stL[0].a1, stL[s2].a2 = stL[0].a2, stL[s2].a3 = stL[0].a3, stL[s2].m0 = stL[0].m0, stL[s2].m1 = stL[0].m1, stL[s2].m2 = stL[0].m2;
        auto tap = stage (stL, fbL, L, n);
        auto w = (float) ((double) L * (1.0 - (double) mix) + (double) (tap * mix));
        L = hpL.tick (0, w);
        const auto fr = sweep();
        stR[0].design (8, fr, k, 1.0f);
        for (int s2 = 1; s2 < n; ++s2)
            stR[s2].a1 = stR[0].a1, stR[s2].a2 = stR[0].a2, stR[s2].a3 = stR[0].a3, stR[s2].m0 = stR[0].m0, stR[s2].m1 = stR[0].m1, stR[s2].m2 = stR[0].m2;
        tap = stage (stR, fbR, R, n);
        w = (float) ((double) R * (1.0 - (double) mix) + (double) (tap * mix));
        R = hpR.tick (0, w);
    }
};

struct Filter
{
    int type = 0;
    bool smooth = true;
    double fs = 44100.0;
    float par[5] { 0.0f, 0.5f, 0.5f, 0.0f, 0.0f }; // +0x60: [1] cutoff, [2] resonance
    struct Ent
    {
        bool on = false;
        float target = 0, cur = 0;
    } ent[3];
    double smoothK = 0;
    Svf svf, svfA, svfB;
    OnePole one;
    CombLine combL, combR;
    float combCut = 0.5f, combGain = 0;
    AltSvf alt;
    Ladder4 ladder;
    Vowel vowel;
    Phaser phasers[3];

    void setRate (double rate)
    {
        fs = rate;
        smoothK = 1.0 - dPower (1.0 - 0.9, 1.0 / (0.01 * (rate / 1.0)));
        svf.setRate (rate), svfA.setRate (rate), svfB.setRate (rate);
        one.setRate (rate);
        alt.setRate (rate);
        ladder.fs = rate;
        vowel.sr = (float) rate;
        for (int k = 0; k < 3; ++k)
        {
            auto& ph = phasers[k];
            ph = Phaser {};
            ph.setRate (44100.0);        // the wrapper's creation (FUN_0071a940): rate 44100,
            ph.preset (0);               // its preset 0,
            ph.set (6, 44100.0f);        // a "depth" of 44100 (overwritten by the preset below)
            ph.preset (k);               // then the filter's preset for this phaser (FUN_0071ac50)
            ph.setRate (rate);           // and the module's rate set-up (FUN_0071da10)
        }
    }
    void setParam (int i, float v) noexcept // FUN_0071d940
    {
        par[i] = v;
        if (i == 1 || i == 2)
        {
            auto& e = ent[i];
            if (e.on)
                e.target = v;
            else
                e.on = true, e.target = e.cur = v;
        }
    }
    void stepSmoothers() noexcept // FUN_00715400
    {
        for (auto& e : ent)
            if (e.on && e.cur != e.target)
            {
                e.cur = (float) ((double) e.cur + (double) (e.target - e.cur) * smoothK);
                if (std::fabs ((double) (e.cur - e.target)) < 0.0001)
                    e.cur = e.target;
            }
    }
    float cutValue() const noexcept { return smooth ? ent[1].cur : par[1]; }
    float freq() const noexcept // FUN_0071b740
    {
        return (float) (dPower (2.0, (double) (cutValue() * 10.0f)) * 20.0);
    }
    static float dampLaw (float r) noexcept { return (float) (1.0 - (double) r * 0.99); }   // FUN_0071b6e0
    static float notchLaw (float r) noexcept { return (float) (1.0 - (double) r * 0.792); } // FUN_0071b710
    static float gainLaw (float r) noexcept { return (float) ((double) r * 40.0 + -20.0); } // FUN_0071b6b0

    void runSvf (int mode, float (*law) (float), bool shelf, const float* x, float* y, int n) noexcept // FUN_0071b910
    {
        auto design = [&] {
            const auto v = law (par[2]);
            if (shelf)
                svf.design (mode, freq(), 1.0f, v);
            else
                svf.design (mode, freq(), v, 1.0f);
        };
        if (! smooth)
            design();
        for (int i = 0; i < n; ++i)
        {
            if (smooth)
            {
                stepSmoothers();
                design();
            }
            y[2 * i] = svf.tick (0, x[2 * i]);
            y[2 * i + 1] = svf.tick (1, x[2 * i + 1]);
        }
    }
    void runOnePole (bool low, const float* x, float* y, int n) noexcept // FUN_0071bcb0
    {
        if (! smooth)
            one.design (! low, freq());
        for (int i = 0; i < n; ++i)
        {
            if (smooth)
            {
                stepSmoothers();
                one.design (! low, freq());
            }
            y[2 * i] = one.tick (0, x[2 * i]);
            y[2 * i + 1] = one.tick (1, x[2 * i + 1]);
        }
    }
    void runFourPole (int mode, const float* x, float* y, int n) noexcept // FUN_0071be70
    {
        auto design = [&] (float r) {
            auto q = (float) (1.0 / (double) dampLaw (r));
            q = (float) std::sqrt ((double) q);
            const auto k = (float) (1.0 / (double) q);
            const auto f = freq();
            svfA.design (mode, f, (float) ((double) k * 2.0), 1.0f);
            svfB.design (mode, f, (float) ((double) k * 2.0), 1.0f);
        };
        if (! smooth)
            design (par[2]);
        for (int i = 0; i < n; ++i)
        {
            if (smooth)
            {
                stepSmoothers();
                design (ent[2].cur);
            }
            const auto l = svfA.tick (0, x[2 * i]);
            const auto r = svfA.tick (1, x[2 * i + 1]);
            y[2 * i] = svfB.tick (0, l);
            y[2 * i + 1] = svfB.tick (1, r);
        }
    }
    void runAlt (float wl, float wb, float wh, const float* x, float* y, int n) noexcept // FUN_0071c6e0
    {
        alt.wLp = wl, alt.wBp = wb, alt.wHp = wh;
        if (! smooth)
            alt.design (freq(), par[2] * 1.0f, true);
        for (int i = 0; i < n; ++i)
        {
            if (smooth)
            {
                stepSmoothers();
                alt.design (freq(), ent[2].cur * 1.0f, true);
            }
            y[2 * i] = alt.tick (0, x[2 * i]);
            y[2 * i + 1] = alt.tick (1, x[2 * i + 1]);
        }
    }
    void runComb (bool feedForward, const float* x, float* y, int n) noexcept // FUN_0071cd00
    {
        auto setCoef = [&] (float c, float r) { combCut = c, combGain = (float) ((double) r * 0.95); };
        if (! smooth)
            setCoef (par[1], par[2]);
        for (int i = 0; i < n; ++i)
        {
            if (smooth)
            {
                stepSmoothers();
                setCoef (ent[1].cur, ent[2].cur);
            }
            const auto d = (float) ((double) combCut * fs * (double) 0.05f + 1.0);
            auto l = x[2 * i], r = x[2 * i + 1];
            if (! feedForward)
            {
                l = l + combL.read (d) * combGain;
                r = r + combR.read (d) * combGain;
                combL.write (l);
                combR.write (r);
            }
            else
            {
                combL.write (l);
                combR.write (r);
                l = l + combL.read (d) * combGain;
                r = r + combR.read (d) * combGain;
            }
            y[2 * i] = l, y[2 * i + 1] = r;
        }
    }
    void runLadder (const float* x, float* y, int n) noexcept
    {
        if (! smooth)
            ladder.design (freq(), par[2] * 1.0f);
        for (int i = 0; i < n; ++i)
        {
            if (smooth)
            {
                stepSmoothers();
                ladder.design (freq(), ent[2].cur * 1.0f);
            }
            y[2 * i] = ladder.tick (0, x[2 * i]);
            y[2 * i + 1] = ladder.tick (1, x[2 * i + 1]);
        }
    }
    void runVowel (const float* x, float* y, int n) noexcept
    {
        if (! smooth)
            vowel.pos = par[1], vowel.res = par[2];
        for (int i = 0; i < n; ++i)
        {
            if (smooth)
            {
                stepSmoothers();
                vowel.pos = ent[1].cur, vowel.res = ent[2].cur;
            }
            auto l = x[2 * i], r = x[2 * i + 1];
            vowel.tick (l, r);
            y[2 * i] = l, y[2 * i + 1] = r;
        }
    }
    void runPhaser (int k, const float* x, float* y, int n) noexcept // FUN_0071b800
    {
        auto& ph = phasers[k];
        if (! smooth)
            ph.set (2, par[1]), ph.set (5, par[2]);
        for (int i = 0; i < n; ++i)
        {
            if (smooth)
            {
                stepSmoothers();
                ph.set (2, ent[1].cur), ph.set (5, ent[2].cur);
            }
            auto l = x[2 * i], r = x[2 * i + 1];
            ph.tick (l, r, 60.0);
            y[2 * i] = l, y[2 * i + 1] = r;
        }
    }
    // FUN_0071cf80; returns false for the types LYNX does not run (the signal passes unchanged)
    bool process (const float* x, float* y, int n) noexcept
    {
        std::memcpy (y, x, sizeof (float) * 2 * (std::size_t) n);
        switch (type)
        {
            case 0: runOnePole (true, x, y, n); break;
            case 1: runOnePole (false, x, y, n); break;
            case 2: runSvf (0, dampLaw, false, x, y, n); break;
            case 3: runSvf (1, dampLaw, false, x, y, n); break;
            case 4: runSvf (2, dampLaw, false, x, y, n); break;
            case 5: runFourPole (0, x, y, n); break;
            case 6: runFourPole (1, x, y, n); break;
            case 7: runSvf (3, notchLaw, false, x, y, n); break;
            case 8: runAlt (1.0f, 0.0f, 0.0f, x, y, n); break;
            case 9: runAlt (0.0f, 1.0f, 0.0f, x, y, n); break;
            case 10: runAlt (0.0f, 0.0f, 1.0f, x, y, n); break;
            case 11: runLadder (x, y, n); break;
            case 12: runSvf (4, dampLaw, false, x, y, n); break;
            case 13: runComb (true, x, y, n); break;
            case 14: runComb (false, x, y, n); break;
            case 15: runVowel (x, y, n); break;
            case 16: runPhaser (0, x, y, n); break;
            case 17: runPhaser (1, x, y, n); break;
            case 18: runPhaser (2, x, y, n); break;
            case 19: runSvf (6, gainLaw, true, x, y, n); break;
            case 20: runSvf (7, gainLaw, true, x, y, n); break;
            case 21: runSvf (8, dampLaw, false, x, y, n); break;
            default: return false;
        }
        return true;
    }
};

// ------------------------------------------------------------------------------------------------ amp / cabinet
// The amp/cabinet module (FUN_00734260): input stereo-separation matrix → stereo convolution with the chosen
// impulse response → output matrix (gain = 44100/rate) → the impulse's level. FL convolves with IPP's float FFT
// (a direct 128-tap head and partitioned tails); LYNX uses the same structure in double precision, so the result
// matches FL to float rounding (not bit for bit). The FFT and the zero-latency partitioned convolver below are the
// ones LYNX wrote for Fruity Convolver (core/ConvolverFx.cpp).
using cd = std::complex<double>;
constexpr double pi = 3.14159265358979323846;
// ---------------------------------------------------------------------------------------------- radix-2 FFT
class Fft
{
public:
    explicit Fft (int size) : n (size), tw ((std::size_t) size / 2), rev ((std::size_t) size)
    {
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * pi * (double) k / (double) n;
            tw[(std::size_t) k] = cd (std::cos (a), std::sin (a));
        }
        int bits = 0;
        while ((1 << bits) < n)
            ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            rev[(std::size_t) i] = r;
        }
    }
    int size() const noexcept { return n; }
    // in place; the inverse is unscaled
    void run (cd* a, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(std::size_t) i])
                std::swap (a[i], a[rev[(std::size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    cd w = tw[(std::size_t) (j * step)];
                    if (inverse)
                        w = std::conj (w);
                    const cd u = a[i + j], v = a[i + j + half] * w;
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
        }
    }

private:
    int n;
    std::vector<cd> tw;
    std::vector<int> rev;
};

int pow2AtLeast (std::size_t v) noexcept
{
    int p = 1;
    while ((std::size_t) p < v)
        p <<= 1;
    return p;
}

// ------------------------------------------------------------------------------- partitioned convolution
// Zero-latency stereo convolution: a direct head, then uniformly partitioned overlap-save levels whose partitions
// never start before their own size (so each level's output is ready when its block begins).
class CabConvolver
{
public:
    void setKernel (const std::vector<float>& kl, const std::vector<float>& kr)
    {
        const std::size_t len = std::max (kl.size(), kr.size());
        headL.assign ((std::size_t) head, 0.0f);
        headR.assign ((std::size_t) head, 0.0f);
        for (std::size_t i = 0; i < std::min (len, (std::size_t) head); ++i)
            headL[i] = i < kl.size() ? kl[i] : 0.0f, headR[i] = i < kr.size() ? kr[i] : 0.0f;
        levels.clear();
        static const int sizes[] = { 128, 1024, 8192, 16384 };
        static const int starts[] = { 128, 1024, 8192, 65536 };
        static const int ends[] = { 1024, 8192, 65536, 1 << 30 };
        for (int k = 0; k < 4; ++k)
        {
            if ((std::size_t) starts[k] >= len)
                break;
            Level lv;
            lv.N = sizes[k];
            lv.j0 = starts[k] / sizes[k];
            const std::size_t stop = std::min (len, (std::size_t) ends[k]);
            lv.parts = (int) ((stop - (std::size_t) starts[k] + (std::size_t) lv.N - 1) / (std::size_t) lv.N);
            lv.fft = std::make_shared<Fft> (2 * lv.N);
            const int bins = lv.N + 1;
            lv.hl.assign ((std::size_t) (lv.parts * bins), {});
            lv.hr.assign ((std::size_t) (lv.parts * bins), {});
            std::vector<cd> z ((std::size_t) (2 * lv.N));
            for (int p = 0; p < lv.parts; ++p)
            {
                std::fill (z.begin(), z.end(), cd {});
                for (int i = 0; i < lv.N; ++i)
                {
                    const std::size_t t = (std::size_t) starts[k] + (std::size_t) (p * lv.N + i);
                    if (t < len)
                        z[(std::size_t) i] = cd (t < kl.size() ? kl[t] : 0.0f, t < kr.size() ? kr[t] : 0.0f);
                }
                lv.fft->run (z.data(), false);
                split (z, 2 * lv.N, lv.hl.data() + p * bins, lv.hr.data() + p * bins);
            }
            lv.ages = lv.j0 + lv.parts - 1;
            lv.xl.assign ((std::size_t) (lv.ages * bins), {});
            lv.xr.assign ((std::size_t) (lv.ages * bins), {});
            lv.out.assign ((std::size_t) (2 * lv.N), 0.0f);
            lv.work.assign ((std::size_t) (2 * lv.N), {});
            lv.accL.assign ((std::size_t) bins, {});
            lv.accR.assign ((std::size_t) bins, {});
            levels.push_back (std::move (lv));
        }
        int maxN = 128;
        for (const auto& lv : levels)
            maxN = std::max (maxN, lv.N);
        ring = pow2AtLeast ((std::size_t) (2 * maxN + head));
        histL.assign ((std::size_t) ring, 0.0f);
        histR.assign ((std::size_t) ring, 0.0f);
        clear();
    }

    void clear() noexcept
    {
        std::fill (histL.begin(), histL.end(), 0.0f);
        std::fill (histR.begin(), histR.end(), 0.0f);
        for (auto& lv : levels)
        {
            std::fill (lv.xl.begin(), lv.xl.end(), cd {});
            std::fill (lv.xr.begin(), lv.xr.end(), cd {});
            std::fill (lv.out.begin(), lv.out.end(), 0.0f);
            lv.newest = 0;
        }
        t = 0;
    }

    // in place, planar
    void process (float* l, float* r, int n) noexcept
    {
        const int mask = ring - 1;
        for (int s = 0; s < n; ++s)
        {
            if ((t & 127) == 0)
                for (auto& lv : levels)
                    if (t % lv.N == 0)
                        step (lv);
            const int w = (int) (t & mask);
            histL[(std::size_t) w] = l[s];
            histR[(std::size_t) w] = r[s];
            double yl = 0.0, yr = 0.0;
            for (int k = 0; k < head; ++k)
            {
                const auto q = (std::size_t) ((w - k) & mask);
                yl += (double) headL[(std::size_t) k] * (double) histL[q];
                yr += (double) headR[(std::size_t) k] * (double) histR[q];
            }
            for (auto& lv : levels)
            {
                const auto o = (std::size_t) (t % lv.N);
                yl += (double) lv.out[o];
                yr += (double) lv.out[(std::size_t) lv.N + o];
            }
            l[s] = (float) yl;
            r[s] = (float) yr;
            ++t;
        }
    }

private:
    struct Level
    {
        int N = 0, j0 = 1, parts = 0, ages = 0, newest = 0;
        std::shared_ptr<Fft> fft;
        std::vector<std::complex<float>> hl, hr, xl, xr; // spectra (N + 1 bins) of the partitions / of the inputs
        std::vector<cd> work, accL, accR;
        std::vector<float> out; // [0, N) left, [N, 2N) right: the level's output for the current block
    };

    // spectra of the two real signals packed in z = a + ib (bins 0…N of a 2N transform)
    template <typename T>
    static void split (const std::vector<cd>& z, int size, T* a, T* b) noexcept
    {
        for (int k = 0; k <= size / 2; ++k)
        {
            const cd zk = z[(std::size_t) k], zc = std::conj (z[(std::size_t) ((size - k) % size)]);
            const cd x = (zk + zc) * 0.5, y = (zk - zc) * cd (0.0, -0.5);
            a[k] = T (x);
            b[k] = T (y);
        }
    }

    void step (Level& lv) noexcept
    {
        const int N = lv.N, bins = N + 1, mask = ring - 1;
        // spectrum of the last 2N inputs (times t−2N … t−1)
        for (int i = 0; i < 2 * N; ++i)
        {
            const auto q = (std::size_t) ((t - 2 * N + i) & mask);
            lv.work[(std::size_t) i] = cd (histL[q], histR[q]);
        }
        lv.fft->run (lv.work.data(), false);
        lv.newest = (lv.newest + lv.ages - 1) % lv.ages;
        split (lv.work, 2 * N, lv.xl.data() + lv.newest * bins, lv.xr.data() + lv.newest * bins);
        std::fill (lv.accL.begin(), lv.accL.end(), cd {});
        std::fill (lv.accR.begin(), lv.accR.end(), cd {});
        for (int p = 0; p < lv.parts; ++p)
        {
            const int age = lv.j0 - 1 + p;
            const int slot = (lv.newest + age) % lv.ages;
            const auto* xl = lv.xl.data() + slot * bins;
            const auto* xr = lv.xr.data() + slot * bins;
            const auto* hl = lv.hl.data() + p * bins;
            const auto* hr = lv.hr.data() + p * bins;
            for (int k = 0; k < bins; ++k)
            {
                lv.accL[(std::size_t) k] += cd (xl[k]) * cd (hl[k]);
                lv.accR[(std::size_t) k] += cd (xr[k]) * cd (hr[k]);
            }
        }
        // Z = YL + i·YR over the full spectrum (Hermitian halves rebuilt)
        for (int k = 0; k <= N; ++k)
            lv.work[(std::size_t) k] = lv.accL[(std::size_t) k] + cd (0.0, 1.0) * lv.accR[(std::size_t) k];
        for (int k = N + 1; k < 2 * N; ++k)
            lv.work[(std::size_t) k] = std::conj (lv.accL[(std::size_t) (2 * N - k)]) + cd (0.0, 1.0) * std::conj (lv.accR[(std::size_t) (2 * N - k)]);
        lv.fft->run (lv.work.data(), true);
        const double inv = 1.0 / (double) (2 * N);
        for (int i = 0; i < N; ++i)
        {
            lv.out[(std::size_t) i] = (float) (lv.work[(std::size_t) (N + i)].real() * inv);
            lv.out[(std::size_t) (N + i)] = (float) (lv.work[(std::size_t) (N + i)].imag() * inv);
        }
    }

    static constexpr int head = 128;
    std::vector<float> headL, headR, histL, histR;
    std::vector<Level> levels;
    int ring = 256;
    long long t = 0;
};


// FL's stereo gain matrix (outL = m0·L + m2·R, outR = m1·R + m3·L) for a gain and a separation at centre pan:
// separation > 0 merges ((5^s − 1)/4), < 0 separates
std::array<float, 4> gainMatrix (float gain, float sep) noexcept
{
    if (sep == 0.0f)
        return { gain, gain, 0.0f, 0.0f };
    if (gain == 0.0f)
        return { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> m {};
    const long double a = (1.0L + 0.0L) * (long double) 0.7853981633974483;
    const long double g = (long double) (gain * 1.41421354f);
    m[0] = (float) (cosl (a) * g);
    m[1] = (float) (sinl (a) * g);
    float k = sep;
    if (sep > 0.0f)
        k = (float) (std::exp ((double) sep * std::log (5.0)) - 1.0) * 0.25f;
    const float fa = std::min (0.0f + k, 1.0f), fb = std::min (0.0f + k, 1.0f);
    m[2] = m[0] * fa * 0.5f;
    m[0] = m[0] * (1.0f - fa * 0.5f);
    m[3] = m[1] * fb * 0.5f;
    m[1] = m[1] * (1.0f - fb * 0.5f);
    return m;
}

struct Ramp
{
    float start = 0.0f, inc = 0.0f;
};
Ramp ramp (float& current, float target, int n, float maxStep) noexcept
{
    Ramp r;
    r.start = current;
    r.inc = target - r.start;
    if (r.inc != 0.0f)
    {
        current = target;
        r.inc = r.inc / (float) n;
        if (std::abs (r.inc) > maxStep)
        {
            r.inc = std::copysign (maxStep, r.inc);
            current = r.start + r.inc * (float) n;
            if (std::fabs (current) < 5.96046448e-08f) // (an integer compare of the magnitudes' bits)
            {
                current = 0.0f;
                r.inc = -r.start / (float) n;
            }
        }
    }
    return r;
}

// dst (=, or += when `add`) src × matrix, the matrix ramped per sample in pairs as the plugin's unrolled loop does
void applyMatrix (const float* src, float* dst, int n, const std::array<float, 4>& m0, const std::array<Ramp, 4>& rp, bool add) noexcept
{
    const bool ramping = rp[0].inc != 0.0f || rp[1].inc != 0.0f || rp[2].inc != 0.0f || rp[3].inc != 0.0f;
    auto put = [add] (float& d, float v) { d = add ? v + d : v; };
    std::array<float, 4> m = m0;
    if (! ramping)
    {
        if (m[2] == 0.0f && m[3] == 0.0f)
        {
            if (m[0] == 0.0f && m[1] == 0.0f)
            {
                if (! add)
                    std::fill (dst, dst + 2 * n, 0.0f);
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                put (dst[2 * i], src[2 * i] * m[0]);
                put (dst[2 * i + 1], src[2 * i + 1] * m[1]);
            }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const float a = src[2 * i], b = src[2 * i + 1];
            put (dst[2 * i], a * m[0] + b * m[2]);
            put (dst[2 * i + 1], b * m[1] + a * m[3]);
        }
        return;
    }
    int i = 0;
    for (; i + 1 < n; i += 2)
    {
        std::array<float, 4> m1;
        for (int k = 0; k < 4; ++k)
            m1[(std::size_t) k] = m[(std::size_t) k] + rp[(std::size_t) k].inc;
        float a = src[2 * i], b = src[2 * i + 1];
        put (dst[2 * i], a * m[0] + b * m[2]);
        put (dst[2 * i + 1], b * m[1] + a * m[3]);
        a = src[2 * i + 2], b = src[2 * i + 3];
        put (dst[2 * i + 2], a * m1[0] + b * m1[2]);
        put (dst[2 * i + 3], b * m1[1] + a * m1[3]);
        for (int k = 0; k < 4; ++k)
            m[(std::size_t) k] = m1[(std::size_t) k] + rp[(std::size_t) k].inc;
    }
    if (i < n)
    {
        const float a = src[2 * i], b = src[2 * i + 1];
        put (dst[2 * i], a * m[0] + b * m[2]);
        put (dst[2 * i + 1], b * m[1] + a * m[3]);
    }
}

struct Cab
{
    std::vector<float> irL, irR;
    float level = 0.1f;
    double fs = 44100.0, runFs = 44100.0; // the rate at state load (gains) and the running rate (ramp steps)
    CabConvolver conv;
    bool ready = false;
    std::array<float, 4> mIn {}, mOut {}, mInNow {}, mOutNow {};
    int len = 0, count = 0;
    float dryNow = 0.0f;
    std::vector<float> buf, pl, pr;

    void setRate (double stateRate, double rate) noexcept { fs = stateRate, runFs = rate; }
    void setImpulse (const std::vector<float>& l, const std::vector<float>& r, float lvl)
    {
        irL = l, irR = r.empty() ? l : r;
        level = lvl > 0.0f ? lvl : 0.1f;
        len = (int) std::max (irL.size(), irR.size());
        count = len;
        conv.setKernel (irL, irR);
        ready = len > 0;
    }
    void setSeparation (int which, int raw) noexcept // FUN_00734680
    {
        const auto sep = (float) raw * 0.00100000005f;
        if (which == 0)
            mIn = gainMatrix (1.0f, sep);
        else
            mOut = gainMatrix ((float) (44100.0 / fs), sep); // the host's 44100/rate ratio
    }
    void process (const float* x, float* y, int n) noexcept
    {
        if (! ready)
        {
            std::memcpy (y, x, sizeof (float) * 2 * (std::size_t) n);
            return;
        }
        bool silent = true;
        for (int i = 0; i < 2 * n; ++i)
            if (std::fabs (x[i]) > 9.99999997e-07f)
            {
                silent = false;
                break;
            }
        if (! silent)
            count = len;
        else
            count -= n;
        if (! (-n < count))
        {
            count = 0;
            std::memcpy (y, x, sizeof (float) * 2 * (std::size_t) n);
            mInNow = mIn, mOutNow = mOut;
            return;
        }
        const auto maxStep = (float) ((44100.0 / runFs) * 0.001);
        buf.resize ((std::size_t) n * 2);
        std::array<Ramp, 4> rp;
        std::array<float, 4> st {};
        for (std::size_t k = 0; k < 4; ++k)
            rp[k] = ramp (mInNow[k], mIn[k], n, maxStep), st[k] = rp[k].start;
        applyMatrix (x, buf.data(), n, st, rp, false);
        pl.resize ((std::size_t) n), pr.resize ((std::size_t) n);
        for (int i = 0; i < n; ++i)
            pl[(std::size_t) i] = buf[(std::size_t) (2 * i)], pr[(std::size_t) i] = buf[(std::size_t) (2 * i + 1)];
        conv.process (pl.data(), pr.data(), n);
        for (int i = 0; i < n; ++i)
            buf[(std::size_t) (2 * i)] = pl[(std::size_t) i], buf[(std::size_t) (2 * i + 1)] = pr[(std::size_t) i];
        std::fill (y, y + 2 * n, 0.0f); // the dry path of this module is always at 0
        for (std::size_t k = 0; k < 4; ++k)
            rp[k] = ramp (mOutNow[k], mOut[k], n, maxStep), st[k] = rp[k].start;
        applyMatrix (buf.data(), y, n, st, rp, true);
        for (int i = 0; i < 2 * n; ++i)
            y[i] = y[i] * level;
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
    double stateRate = 0.0; // the host rate when the state is loaded (0: the same as fs)
    double loadRate() const noexcept { return stateRate > 0.0 ? stateRate : fs; }
    Distortion dist;
    Filter filt;
    std::unique_ptr<fldelay::Delay3Engine> chorus;
    Cab cab;
    std::vector<float> chL, chR;
    DcBlocker dc;
    std::vector<HalfbandStage> upSt, downSt;
    std::vector<float> s1, s2, s3, hi, tmp;

    int factor() const noexcept { return 1 << st.oversample; }
    bool overs() const noexcept { return st.oversample > 0 && (st.type == 0 || st.type == 1); }

    void configure()
    {
        upSt.assign ((std::size_t) st.oversample, HalfbandStage {});
        downSt.assign ((std::size_t) st.oversample, HalfbandStage {});
        dc.setRate (fs);
        dist = Distortion {};
        dist.setRate (loadRate(), factor());
        for (int i = 0; i < 36; ++i)
            dist.p[(std::size_t) i] = st.p[(std::size_t) i];
        dist.setAlgorithm (st.algorithm);
        filt = Filter {};
        filt.setRate (loadRate() * factor());
        filt.setParam (1, 0.5f), filt.setParam (2, 0.5f); // the processor's own initial controls
        filt.type = st.filterType;
        filt.smooth = st.smoothFilter;
        filt.setParam (1, (float) ((double) st.p[39] / 10000.0));
        filt.setParam (2, (float) ((double) st.p[40] / 10000.0));
        if (st.type == 2)
            setupChorus();
        else
            chorus.reset();
        cab = Cab {};
        if (st.type == 3)
            setupCab();
    }
    // the chorus module drives an instance of Fruity Delay 3's engine (TChorusProcessor, FUN_0074b000): its 26 controls
    // start from a fixed set, the mode picks the delay type and stereo spread, the six chorus knobs map to six controls
    void setupChorus()
    {
        static constexpr float init[26] = { 1.0f, 0.330000013f, 0.0f, 1.0f, 0.0677966103f, 0.839999974f, 0.5f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f,
                                            1.0f, 0.159999996f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f };
        chorus = std::make_unique<fldelay::Delay3Engine> (44100.0);
        chorus->setTempo (120.0); // the chorus never syncs to the tempo
        for (int i = 0; i < 26; ++i)
            chorus->setParameter (i, init[i]);
        const auto mode = std::clamp (st.chorusMode, 0, 2);
        static constexpr float type1[3] = { 0.330000013f, 0.0f, 0.660000026f }, spread5[3] = { 0.839999974f, 0.5f, 0.5f };
        chorus->setParameter (1, type1[mode]);
        chorus->setParameter (5, spread5[mode]);
        const auto& p = st.p;
        chorus->setParameter (4, (float) ((double) p[43] / 768.0));
        chorus->setParameter (14, (float) ((double) p[44] / 6000.0));
        chorus->setParameter (8, (float) ((double) p[45] / 6000.0));
        chorus->setParameter (9, (float) ((double) p[46] / 6000.0));
        chorus->setParameter (11, (float) ((double) p[47] / 6000.0));
        chorus->setParameter (12, (float) ((double) p[47] / 6000.0));
        chorus->setParameter (24, (float) (((double) p[48] + 3000.0) / 6000.0));
        // the module's rate set-up after the state: the engine behaves as if its rate set-up ran twice, first at the
        // creation rate (as with Fruity Delay 3 itself, REA_rodada52 §2)
        chorus->setSampleRate (44100.0);
        chorus->setSampleRate (fs);
    }
    void setupCab()
    {
        cab.setRate (loadRate(), fs);
        const auto& info = impulseTable[std::clamp (st.impulse, 0, 58)];
        auto bytes = st.impulseFile;
        if (bytes.empty() && info.file != nullptr && ! impulseFolder().empty())
        {
            std::ifstream f (impulseFolder() + "/" + info.file, std::ios::binary);
            bytes.assign (std::istreambuf_iterator<char> (f), {});
        }
        DecodedAudio a;
        if (! bytes.empty() && decodeWavPack (bytes.data(), bytes.size(), a) && ! a.channels.empty())
            cab.setImpulse (a.channels[0], a.channels.size() > 1 ? a.channels[1] : a.channels[0], info.level);
        cab.setSeparation (0, st.p[41]);
        cab.setSeparation (1, st.p[42]);
    }
    void processChorus (const float* x, float* y, int n)
    {
        chL.resize ((std::size_t) n), chR.resize ((std::size_t) n);
        for (int i = 0; i < n; ++i)
            chL[(std::size_t) i] = x[2 * i], chR[(std::size_t) i] = x[2 * i + 1];
        chorus->process (chL.data(), chR.data(), n);
        for (int i = 0; i < n; ++i)
            y[2 * i] = chL[(std::size_t) i], y[2 * i + 1] = chR[(std::size_t) i];
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
        else if (st.type == 1)
            filt.process (s1.data(), w, m);
        else if (st.type == 2 && chorus)
            processChorus (s1.data(), w, m);
        else if (st.type == 3)
            cab.process (s1.data(), w, m);
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
void Module::prepare (double sampleRate, int, double stateRate)
{
    impl->fs = sampleRate;
    impl->stateRate = stateRate;
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
void Module::processInterleaved (float* lr, int n) noexcept { impl->process (lr, n); }
const ModuleState& Module::state() const noexcept { return impl->st; }

// ------------------------------------------------------------------------------------------------ plugin
struct Engine::Impl
{
    State st;
    double fs = 44100.0, stateRate = 0.0;
    int maxBlock = 512;
    std::vector<std::unique_ptr<Module>> mods;
    std::vector<float> lr;
};
Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;
void Engine::prepare (double sampleRate, int maxBlock, double stateRate)
{
    impl->fs = sampleRate, impl->maxBlock = maxBlock, impl->stateRate = stateRate;
    for (auto& m : impl->mods)
        m->prepare (sampleRate, maxBlock, stateRate);
}
void Engine::set (const State& s)
{
    impl->st = s;
    impl->mods.clear();
    for (const auto& ms : s.modules)
    {
        auto m = std::make_unique<Module>();
        m->prepare (impl->fs, impl->maxBlock, impl->stateRate);
        m->set (ms);
        impl->mods.push_back (std::move (m));
    }
}
void Engine::reset() { set (impl->st); }
void Engine::process (float* left, float* right, int n) noexcept
{
    if (n <= 0)
        return;
    auto& lr = impl->lr;
    if (lr.size() < (std::size_t) n * 2)
        lr.resize ((std::size_t) n * 2);
    for (int i = 0; i < n; ++i)
        lr[(std::size_t) (2 * i)] = left[i], lr[(std::size_t) (2 * i + 1)] = right[i];
    // the modules in their stored order (FUN_006a8a10), then the optional final soft clipper above 0.78125
    for (auto& m : impl->mods)
        m->processInterleaved (lr.data(), n);
    if (impl->st.clip)
        softClipBlock (lr.data(), 2 * n, 0.78125f);
    for (int i = 0; i < n; ++i)
        left[i] = lr[(std::size_t) (2 * i)], right[i] = lr[(std::size_t) (2 * i + 1)];
}

std::string moduleName (const ModuleState& m)
{
    static const char* types[] = { "Distortion", "Filter", "Chorus", "Amp/Cab" };
    return types[std::clamp (m.type, 0, 3)];
}
void setImpulseFolder (const std::string& folder) { impulseFolder() = folder; }
} // namespace lynx::distructor
