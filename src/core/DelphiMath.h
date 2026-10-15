#pragma once

// The double-precision math routines of the Delphi runtime that Image-Line's Delphi plugins are built with (Exp, Ln,
// Sin, Cos, SinCos, the exp-minus-one behind Tanh, and Power/Sinh/Tanh on top of them), re-created from their
// behaviour as measured in REA_rodada66.md §7: the range reductions, polynomial degrees, operation order and rounding
// steps, with the published polynomial constants. Clean-room: no Embarcadero or Image-Line code; the e^(j/64) − 1 table
// is computed here (and matches theirs). They differ from the C library's routines in the last bit on a few percent of
// inputs; the exact plugin engines (core/LovePhilter, core/FruityVocoderFx) use them so their results do not depend on the
// platform's libm. Out-of-range arguments (overflow, subnormals, |x| ≥ 4194304 for the trigonometric functions) fall
// back to the C library.
//
// Built without contraction (the arithmetic must stay one rounding per operation).

#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

namespace lynx::delphi
{
namespace detail
{
inline std::uint64_t bits (double x) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &x, 8);
    return b;
}
inline double fromBits (std::uint64_t b) noexcept
{
    double x;
    std::memcpy (&x, &b, 8);
    return x;
}
// e^(j/64) − 1, j = −45…45 (Exp uses −32…32, ExpM1 up to ±44), as an unevaluated sum hi + lo
inline const double (&expTable())[91][2]
{
    static const double t[91][2] = {
    { -0.5049641030738015, 4.409662838955128e-17 },
    { -0.49716842202905903, -8.614289810091639e-18 },
    { -0.4892499768709893, 2.113950921442185e-18 },
    { -0.48120683434611067, 2.420865986714763e-17 },
    { -0.473037030756629, -2.163248626592348e-18 },
    { -0.4647385714810098, 1.8721259361862832e-17 },
    { -0.45630943048699957, -8.326348733999239e-18 },
    { -0.4477475498369796, -1.5752952405025438e-17 },
    { -0.4390508391855292, -2.266458316145364e-17 },
    { -0.430217175269077, -9.276604382700606e-20 },
    { -0.4212444013875158, 1.4082533445359833e-17 },
    { -0.4121303268776535, -9.2616241499393e-18 },
    { -0.4028727265783726, -1.424163462407499e-19 },
    { -0.3934693402873666, -6.593178415491414e-19 },
    { -0.3839178722093218, 7.609376052156984e-18 },
    { -0.37421599039540887, -7.658883125910196e-18 },
    { -0.364361326173948, -1.4420373617975653e-17 },
    { -0.35435147357210794, -1.8153089476490168e-17 },
    { -0.3441839887284984, -2.2954950495149666e-17 },
    { -0.33385638929651223, 9.30198404641242e-18 },
    { -0.32336615383827105, -1.01268391978119e-17 },
    { -0.3127107212090278, 1.842314816988643e-17 },
    { -0.30188748993187414, -1.1720028602344369e-17 },
    { -0.2908938175626016, -1.2868055655346304e-17 },
    { -0.27972702004456024, 1.813706295077313e-17 },
    { -0.2683843710533582, 8.35576468031604e-18 },
    { -0.2568631013312417, -9.001102395673582e-19 },
    { -0.24516039801099265, -9.844076038651084e-18 },
    { -0.23327340392917992, -2.0729828135323382e-18 },
    { -0.22119921692859512, -1.0231869534531498e-17 },
    { -0.20893488914970398, -1.2452907836084123e-18 },
    { -0.19647742631093926, -8.86329269357526e-18 },
    { -0.18382378697766022, 6.554697808700811e-18 },
    { -0.17097088181959966, 1.5116689608969005e-19 },
    { -0.15791557285661764, -1.1212311825056607e-17 },
    { -0.14465467269257745, -1.0550675610571318e-17 },
    { -0.13118494373715683, 6.146598011714697e-19 },
    { -0.1175030974154046, 3.2658820639011965e-18 },
    { -0.10360579336484958, -5.827134285622915e-18 },
    { -0.08948963861996587, -5.494907630146725e-18 },
    { -0.07515118678379516, -3.2635260492015698e-18 },
    { -0.06058693718652421, -7.077887227488846e-19 },
    { -0.045793334030811685, 7.6989787849942455e-19 },
    { -0.03076676552365592, 5.607402565184088e-19 },
    { -0.015503562994591593, -6.554927149823924e-19 },
    { 0.0, 0.0 },
    { 0.015747708586685748, -2.862138367894185e-19 },
    { 0.03174340749910267, 7.614433403626514e-19 },
    { 0.0479910020166327, 2.232142242481688e-18 },
    { 0.06449445891785943, -2.2934210303960824e-18 },
    { 0.0812578074490396, 4.627898188856025e-18 },
    { 0.09828514030782586, -6.438065156763691e-18 },
    { 0.11558061464248076, -2.5290380495681964e-18 },
    { 0.13314845306682632, -5.370737708558031e-18 },
    { 0.15099294469117644, 9.857598007072166e-18 },
    { 0.16911844616950442, -1.3811845173682628e-17 },
    { 0.1875293827631006, 6.415816207759217e-19 },
    { 0.2062302494209807, 1.1540139455476613e-17 },
    { 0.22522561187730758, -4.729368350680563e-19 },
    { 0.24452010776609515, 8.861603894276184e-18 },
    { 0.2641184477534664, -1.541497933603795e-17 },
    { 0.2840254166877415, -2.133257464457841e-17 },
    { 0.30424587476763776, 1.7093578107981658e-17 },
    { 0.3247847587288656, -1.6795478687091983e-17 },
    { 0.34564708304941055, -2.1352609134867508e-17 },
    { 0.36683794117379637, -4.061704634846282e-18 },
    { 0.3883625067566268, 1.1408485340934206e-17 },
    { 0.4102260349257107, 1.3752340957573633e-17 },
    { 0.4324338635650781, -9.875070032961977e-18 },
    { 0.45499141461820136, -2.5843071672554946e-17 },
    { 0.47790419541173845, -2.493763123963775e-17 },
    { 0.5011778000001228, -4.5384955300459954e-17 },
    { 0.5248179105313268, -5.4326124239829844e-17 },
    { 0.5488302986341331, -3.110014802562223e-17 },
    { 0.5732208268272528, 4.7879381875011736e-17 },
    { 0.5979954499506333, 1.6864630310268093e-17 },
    { 0.6231602166193057, -4.1334891230883785e-17 },
    { 0.6487212707001282, -4.731568479435833e-17 },
    { 0.674684852811784, -3.893813664473529e-17 },
    { 0.7010573018484006, 3.029262610307366e-17 },
    { 0.7278450565271632, -1.957322111748573e-17 },
    { 0.7550546569602986, -5.1870043558940924e-17 },
    { 0.7826927462518151, -3.7860767805828396e-18 },
    { 0.8107660721193871, 4.850736548749221e-17 },
    { 0.8392814885417809, -1.5418120240632379e-18 },
    { 0.8682459574322224, -3.0653808836464123e-17 },
    { 0.8976665503381187, 4.349228803307586e-17 },
    { 0.9275504501675447, -6.540853129828781e-18 },
    { 0.9579049529429181, -2.3016190565838558e-17 },
    { 0.9887374695822918, 2.530335469581869e-17 },
    { 1.0200555277086967, -1.0065290130142814e-16 },
    };
    return t;
}
inline const double* expEntry (int j) noexcept { return expTable()[j + 45]; }

// kernels on [−π/4, π/4] with a tail y
inline double ksin (double x, double y) noexcept
{
    const double z = x * x, w = z * z, v = z * x;
    double a = 1.5917962022219061e-10 * w + 2.7557316088605576e-06;
    double b = -2.5051129263514983e-08 * w + -0.0001984126983672781;
    a = a * w + -7.004157575623186e-19;
    a = a + 0.008333333333330925;
    b = b * w + 1.1473333947459469e-17;
    double t = a * z + b;
    t = t + -0.16666666666666666;
    t = t * v;
    const double u = (1.0 - z * 0.5) * y;
    t = t + u;
    return x + t;
}
inline double kcos (double x, double y) noexcept
{
    const double z = x * x, w = z * z;
    double a = -1.1358764907666355e-11 * w + -2.7557314219061697e-07;
    double b = 2.0875705690062727e-09 * w + 2.48015872890081e-05;
    a = a * w + -0.0013888888888873348;
    b = b * w + 0.041666666666666595;
    a = a * z;
    double r = (b + a) * w;
    const double hz = z * 0.5;
    const double w1 = 1.0 - hz;
    const double t = hz + (w1 - 1.0);
    r = r - x * y;
    r = r - t;
    return w1 + r;
}
// x = n·π/2 + (y0 + y1); returns n & 3, or −1 when the argument is out of the handled range
inline int remPio2 (double x, double& y0, double& y1) noexcept
{
    const double ax = std::abs (x);
    y0 = x, y1 = 0.0;
    if (0.7853981633974483 >= ax)
        return 0;
    long long n;
    if (3.9269908169872414 >= ax)
    {
        n = 2.356194490192345 >= ax ? 1 : 2;
        if (0.0 > x)
            n = -n;
        const double a = (double) n * 1.5707963267948966, b = (double) n * 6.123233995736762e-17,
                     c = (double) n * 3.548047002737576e-32;
        const double s = x - a;
        y0 = s - b;
        y1 = -b - (y0 - s);
        y1 = y1 - c;
    }
    else if (4194304.0 > ax)
    {
        n = (long long) ((ax - 0.7853981633974483) * 0.6366197723675814) + 1;
        if (0.0 > x)
            n = -n;
        const double a = (double) n * -1.5707963267341256, b = (double) n * -6.077100506303966e-11,
                     c = (double) n * -2.0222662487959506e-21;
        const double s = x + a;
        double a0 = b + s;
        double bv = a0 - b;
        double e = (b - (a0 - bv)) + (s - bv);
        const double hi = a0 + e;
        const double e2 = e - (hi - a0);
        const double a1 = c + hi;
        bv = a1 - c;
        double e3 = (c - (a1 - bv)) + (hi - bv);
        e3 = e3 + e2;
        y0 = a1 + e3;
        y1 = e3 - (y0 - a1);
    }
    else
        return -1;
    return (int) (n & 3);
}
} // namespace detail

inline double exp (double x) noexcept
{
    using namespace detail;
    const std::uint64_t b = bits (x);
    const unsigned hw = (unsigned) (b >> 48);
    if ((hw & 0x7ff0u) == 0)
        return 1.0;
    double fn, scale;
    if ((b >> 63) == 0)
    {
        if (hw >= 0x4087u || b > 0x40862e42fefa39efull)
            return std::exp (x);
        const int n = (int) (x * 1.4426950408889634 + 0.5);
        if (n > 1023)
            return std::exp (x);
        fn = (double) n;
        scale = fromBits ((std::uint64_t) (std::int64_t) (n + 1023) << 52);
    }
    else
    {
        if (hw >= 0xc090u || b > 0xc0874910d52d3051ull)
            return std::exp (x);
        const int n = (int) (x * 1.4426950408889634 - 0.5);
        if (n < -1022)
            return std::exp (x);
        fn = (double) n;
        scale = fromBits ((std::uint64_t) (std::int64_t) (n + 1023) << 52);
    }
    double r = x - fn * 0.6931471805601177;
    const double lo = fn * 1.7239444525614835e-13;
    double a = r + lo;
    const double half = std::copysign (0.5, a);
    const int j = (int) (a * 64.0 + half);
    r = r - (double) j * 0.015625;
    a = r + lo;
    const double z = a * a;
    double p = 0.1666666666665743 + 0.008333345443492927 * z;
    const double q = (0.04166666666662047 + 0.00138889115954389 * z) * a;
    p = p + q;
    p = a * p;
    p = lo + z * (0.5 + p);
    const double* t = expEntry (j);
    const double s = t[0] + 1.0;
    double tl = (t[0] - (s - 1.0)) + t[1];
    const double S = s + tl;
    tl = tl - (S - s);
    double res = p * tl;
    res = res + r * tl;
    res = res + tl;
    res = res + p * S;
    res = res + r * S;
    res = res + S;
    return scale * res;
}

inline double expm1 (double x) noexcept
{
    using namespace detail;
    if (x == 0.0)
        return 0.0;
    const unsigned e = (unsigned) (bits (x) >> 48) & 0x7ff0u;
    if (e == 0)
        return x;
    if (e == 0x7ff0u || std::abs (x) >= 0.6931471805599453)
        return delphi::exp (x) - 1.0;
    const int j = (int) (x * 64.0 + std::copysign (0.5, x));
    const double r = x - (double) j * 0.015625;
    const double z = r * r;
    double a = 0.00138889115954389 * z + 0.04166666666662047;
    double b = 0.008333345443492927 * z + 0.1666666666665743;
    a = a * z;
    b = b * r;
    a = a + b;
    a = a + 0.5;
    a = a * z;
    const double* t = expEntry (j);
    const double lo = t[1] + a, rr = r + a;
    return ((t[0] * rr + lo) + r) + t[0];
}

inline double ln (double x) noexcept
{
    using namespace detail;
    const std::uint64_t b = bits (x);
    if ((b >> 63) != 0 || b == 0 || b >= 0x7ff0000000000000ull || ((b >> 52) & 0x7ffu) == 0)
        return std::log (x);
    std::uint64_t m = b & 0xfffffffffffffull;
    int k = (int) ((b >> 52) & 0x7ffu) - 0x3ff;
    double xr;
    if (m <= 0x6a09e667f3bccull)
        xr = fromBits (m + 0x3ff0000000000000ull);
    else
    {
        xr = fromBits (m + 0x3fe0000000000000ull);
        ++k;
    }
    const double f = xr - 1.0;
    const double s = f / (f + 2.0);
    const double hf = f * 0.5;
    const double hfsq = f * hf;
    const double c3 = hfsq * hf;
    const double z = s * s, w = z * z;
    double A = 0.06555496093966279 * w + 0.05905179857823471;
    double B = 0.04987082245921066 * w + 0.06665539663763492;
    A = A * w + 0.07692341923031541;
    B = B * w + 0.09090908460171164;
    A = A * w + 0.11111111117771544;
    B = B * w + 0.14285714285678625;
    A = A * w + 7.017193254278944e-18;
    A = A + 0.20000000000000073;
    B = B * w + 1.8250737837400312e-17;
    A = A * z;
    B = B + A;
    B = B + 0.3333333333333333;
    B = B * 2.0;
    B = B * z;
    B = B * s;
    const double sc = s * c3;
    B = B + c3;
    B = B - hfsq;
    B = B - sc;
    B = B + f;
    double r = B + (double) k * 5.5459262969660605e-09;
    r = r + (double) k * 0.693147175014019;
    return r;
}

inline void sincos (double x, double& s, double& c) noexcept
{
    using namespace detail;
    if (0.7853981633974483 > std::abs (x))
    {
        s = ksin (x, 0.0);
        c = kcos (x, 0.0);
        return;
    }
    double y0, y1;
    switch (remPio2 (x, y0, y1))
    {
        case 0: s = ksin (y0, y1), c = kcos (y0, y1); return;
        case 1: s = kcos (y0, y1), c = -ksin (y0, y1); return;
        case 2: s = -ksin (y0, y1), c = -kcos (y0, y1); return;
        case 3: s = -kcos (y0, y1), c = ksin (y0, y1); return;
        default: s = std::sin (x), c = std::cos (x); return;
    }
}
inline double sin (double x) noexcept
{
    double s, c;
    sincos (x, s, c);
    return s;
}
inline double cos (double x) noexcept
{
    double s, c;
    sincos (x, s, c);
    return c;
}

// Power (fractional exponents through Exp(y·Ln x), integral ones by repeated squaring), Sinh and Tanh of the runtime
inline double intPower (double x, long long n) noexcept
{
    double r = 1.0, b = x;
    auto k = n < 0 ? -n : n;
    while (k > 0)
    {
        if (k & 1)
            r = r * b;
        b = b * b;
        k >>= 1;
    }
    return n < 0 ? 1.0 / r : r;
}
inline double power (double x, double y) noexcept
{
    if (y == 0.0)
        return 1.0;
    if (x == 0.0 && y > 0.0)
        return 0.0;
    if (std::trunc (y) == y && std::abs (y) <= 2147483647.0)
        return intPower (x, (long long) y);
    return delphi::exp (y * delphi::ln (x));
}
inline double sinh (double x) noexcept
{
    if (std::abs (x) <= 1e-12)
        return 0.0;
    return (delphi::exp (x) - delphi::exp (-x)) / 2.0;
}
inline double tanh (double x) noexcept
{
    const bool neg = x < 0.0;
    const double a = neg ? -x : x;
    double r = 1.0;
    if (a <= 23.0)
    {
        if (a < 1.81458605194507e-05)
            r = a - a * a * a * 0.3333333333333333;
        else if (0.34657359027997264 <= a)
            r = 1.0 - 2.0 / (delphi::exp (a * 2.0) + 1.0);
        else
        {
            const double e = delphi::expm1 (a * 2.0);
            r = e / (e + 2.0);
        }
    }
    return neg ? -r : r;
}
} // namespace lynx::delphi

#if defined(__GNUC__) && ! defined(__clang__)
#pragma GCC pop_options
#endif
