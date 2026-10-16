#include "core/DelphiMath.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// The plugin's operation order and roundings are reproduced exactly: no fused multiply-adds (the native-CPU builds
// would contract them otherwise).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::dmath
{
namespace
{
std::uint64_t bitsOf (double v) noexcept
{
    std::uint64_t b;
    std::memcpy (&b, &v, 8);
    return b;
}

double fromBits (std::uint64_t b) noexcept
{
    double v;
    std::memcpy (&v, &b, 8);
    return v;
}

// e^(j/64) − 1 for j = −45…45 (exp uses −24…24, expm1 up to ±44) as a double plus the rounding remainder (checked
// against 70-digit decimal arithmetic)
constexpr double expTable[91][2] = {
    { -0x1.028aa7a8b63f2p-1, 0x1.96b8384f1d3eep-55 }, // -45
    { -0x1.fd19b804dffbfp-2, -0x1.3dcfaabcf5d2cp-57 }, // -44
    { -0x1.f4fdf228eb2adp-2, 0x1.37f6cecd799a3p-59 }, // -43
    { -0x1.ecc17c00835p-2, 0x1.be9229f8beebep-56 }, // -42
    { -0x1.e463d1c396301p-2, -0x1.3f3d38f393576p-59 }, // -41
    { -0x1.dbe46d96cd831p-2, 0x1.5958a5d1795c7p-56 }, // -40
    { -0x1.d342c7833133ap-2, -0x1.333023ef6f763p-57 }, // -39
    { -0x1.ca7e556da7e48p-2, -0x1.229736e5c8d3ap-56 }, // -38
    { -0x1.c1968b0e55333p-2, -0x1.a21677c651b54p-56 }, // -37
    { -0x1.b88ad9e7d52eap-2, -0x1.b61343fc21a3bp-64 }, // -36
    { -0x1.af5ab13e5474fp-2, 0x1.03c6e249a5882p-56 }, // -35
    { -0x1.a6057e0e846a4p-2, -0x1.55b1912202da3p-57 }, // -34
    { -0x1.9c8aab046af7ap-2, -0x1.50456628532fp-63 }, // -33
    { -0x1.92e9a0720d3ecp-2, -0x1.85314b9559e64p-61 }, // -32
    { -0x1.8921c445f4addp-2, 0x1.18bc865c78e3bp-57 }, // -31
    { -0x1.7f327a018ddb2p-2, -0x1.1a901b1ee2bd6p-57 }, // -30
    { -0x1.751b22af608fp-2, -0x1.0a0249fec9629p-56 }, // -29
    { -0x1.6adb1cd9205eep-2, -0x1.4edd8a92eb584p-56 }, // -28
    { -0x1.6071c47d953b2p-2, -0x1.a771b057709c4p-56 }, // -27
    { -0x1.55de73065b4dfp-2, 0x1.572ec15fec83dp-57 }, // -26
    { -0x1.4b207f3d7987p-2, -0x1.759d4ac130208p-57 }, // -25
    { -0x1.40373d42ce2e3p-2, 0x1.53d8db804c224p-56 }, // -24
    { -0x1.3521fe8150d2bp-2, -0x1.b0648a5e34bb6p-57 }, // -23
    { -0x1.29e011a428ec6p-2, -0x1.dabf5975c0c02p-57 }, // -22
    { -0x1.1e70c28b987f3p-2, 0x1.4e91dbb1734bdp-56 }, // -21
    { -0x1.12d35a41ba104p-2, 0x1.3445f7544e0efp-57 }, // -20
    { -0x1.07071eef11388p-2, -0x1.09aa682553231p-60 }, // -19
    { -0x1.f616a79dda3a8p-3, -0x1.6b2eab63020c1p-57 }, // -18
    { -0x1.ddbe7247382afp-3, -0x1.31eb13933e894p-59 }, // -17
    { -0x1.c5041854df7d4p-3, -0x1.797d4686c5393p-57 }, // -16
    { -0x1.abe60e1f21836p-3, -0x1.6f8b82e653e2dp-60 }, // -15
    { -0x1.9262c1c3430a1p-3, -0x1.46ff6ec4a4251p-57 }, // -14
    { -0x1.78789b0a5e0cp-3, 0x1.e3a6bdaece8f9p-58 }, // -13
    { -0x1.5e25fb4fde211p-3, 0x1.64eec82915df3p-63 }, // -12
    { -0x1.43693d679612dp-3, -0x1.9da94a869862ap-57 }, // -11
    { -0x1.2840b5836cf67p-3, -0x1.85405051eb425p-57 }, // -10
    { -0x1.0caab118a1278p-3, 0x1.6ad4c353465bp-61 }, // -9
    { -0x1.e14aed893eef4p-4, 0x1.e1f58934f97afp-59 }, // -8
    { -0x1.a85e8c62d9c13p-4, -0x1.adf7745e77188p-58 }, // -7
    { -0x1.6e8caff341feap-4, -0x1.9573ded7888b2p-58 }, // -6
    { -0x1.33d1bb17df2e7p-4, -0x1.e19c873b1d6a8p-59 }, // -5
    { -0x1.f0540438fd5c3p-5, -0x1.a1ce01f9f6ca7p-61 }, // -4
    { -0x1.7723950130405p-5, 0x1.c677ad8fa478dp-61 }, // -3
    { -0x1.f8152aee9450ep-6, 0x1.4b00abf977627p-61 }, // -2
    { -0x1.fc055004416dbp-7, -0x1.82ef422ab152ap-61 }, // -1
    { 0x0p+0, 0x0p+0 }, // 0
    { 0x1.0202ad5778e46p-6, -0x1.51e6d305beec6p-62 }, // 1
    { 0x1.040ac0224fd93p-5, 0x1.c17a107575019p-61 }, // 2
    { 0x1.89246d053d178p-5, 0x1.4967f31eb2595p-59 }, // 3
    { 0x1.082b577d34ed8p-4, -0x1.5272ff30eed1bp-59 }, // 4
    { 0x1.4cd4fc989cd64p-4, 0x1.557a8671b89e7p-58 }, // 5
    { 0x1.92937074e0cd7p-4, -0x1.db0b9cc915fc5p-58 }, // 6
    { 0x1.d96b0eff0e794p-4, -0x1.75385b2cdf93dp-59 }, // 7
    { 0x1.10b022db7ae68p-3, -0x1.8c4a5df1ec7e5p-58 }, // 8
    { 0x1.353bc9fb00b21p-3, 0x1.6bae618011342p-57 }, // 9
    { 0x1.5a5ac59b963cbp-3, -0x1.fd91307e74c5p-57 }, // 10
    { 0x1.800f67b00d7b8p-3, 0x1.7ab912c69ffebp-61 }, // 11
    { 0x1.a65c0b85ac1a9p-3, 0x1.a9c189196f8cdp-57 }, // 12
    { 0x1.cd4315e9e0833p-3, -0x1.172c31a1781f1p-61 }, // 13
    { 0x1.f4c6f5508ee5dp-3, 0x1.46ef7b808180ap-57 }, // 14
    { 0x1.0e7510fd7c564p-2, -0x1.1c5b2e8735a43p-56 }, // 15
    { 0x1.22d78f0fa061ap-2, -0x1.89843c4964554p-56 }, // 16
    { 0x1.378c3b084798p-2, 0x1.3b5223eca1712p-56 }, // 17
    { 0x1.4c946033eb3dep-2, -0x1.35d267d66dc96p-56 }, // 18
    { 0x1.61f14f169ebc1p-2, -0x1.89e2d87fd0d92p-56 }, // 19
    { 0x1.77a45d8117fd5p-2, -0x1.2bb36e6b3a2afp-58 }, // 20
    { 0x1.8daee6a60c961p-2, 0x1.a4e618fb92468p-57 }, // 21
    { 0x1.a4124b2fe50cbp-2, 0x1.fb5f3020a46f5p-57 }, // 22
    { 0x1.bacff156c79d7p-2, -0x1.6c5366444681ap-57 }, // 23
    { 0x1.d1e944f6fbdaap-2, -0x1.dcb8749a64f6ep-56 }, // 24
    { 0x1.e95fb7a7a88f8p-2, -0x1.cc04a2491ed0ap-56 }, // 25
    { 0x1.009a6068f6a8cp-1, -0x1.a29a322473bb6p-55 }, // 26
    { 0x1.0cb4eee42c98bp-1, -0x1.f511ef22f5ad5p-55 }, // 27
    { 0x1.190048ef6002p-1, -0x1.1ed925f893d67p-55 }, // 28
    { 0x1.257d334137dffp-1, 0x1.b99bfe7ce9f9ep-55 }, // 29
    { 0x1.322c75a963b98p-1, 0x1.3718f70534e8ap-56 }, // 30
    { 0x1.3f0edb1d18acdp-1, -0x1.7d3f40a146f34p-55 }, // 31
    { 0x1.4c2531c3c0d38p-1, -0x1.b4690082a4906p-55 }, // 32
    { 0x1.59704b03ddca9p-1, -0x1.67241360f5dc1p-55 }, // 33
    { 0x1.66f0fb901f2bdp-1, 0x1.176670eb83154p-55 }, // 34
    { 0x1.74a81b74adcacp-1, -0x1.690fec61c7ca4p-56 }, // 35
    { 0x1.82968624ac88dp-1, -0x1.de6aad7622defp-55 }, // 36
    { 0x1.90bd1a87ef9a1p-1, -0x1.175cf7e67dfffp-58 }, // 37
    { 0x1.9f1cbb08eb151p-1, 0x1.bf66c74a26ecdp-55 }, // 38
    { 0x1.adb64da2d9acfp-1, -0x1.c71005d68edd6p-60 }, // 39
    { 0x1.bc8abbf01c781p-1, -0x1.1abb427c68493p-55 }, // 40
    { 0x1.cb9af338d4a9cp-1, 0x1.912542fa88dd4p-55 }, // 41
    { 0x1.dae7e481b8284p-1, -0x1.e2a138ec80097p-58 }, // 42
    { 0x1.ea72849b21ebdp-1, -0x1.a892e30b5188fp-56 }, // 43
    { 0x1.fa3bcc305f191p-1, 0x1.d2c3b6d08c659p-56 }, // 44
    { 0x1.05225beb9ce55p+0, -0x1.d02df8ccf4707p-54 }, // 45
};

// exp: x = k·ln2 + j/64 + r, polynomial for e^r − 1, table for e^(j/64)
constexpr double log2e = 0x1.71547652b82fep+0;
constexpr double ln2Hi = 0x1.62e42fefa4000p-1, ln2Lo = 0x1.8432a1b0e2634p-43;
constexpr double e3 = 0x1.5555555554855p-3, e4 = 0x1.5555555553b54p-5, e5 = 0x1.11112b12b282fp-7, e6 = 0x1.6c16e86e89028p-10;

// ln: x = 2^k·(1 + f), f in [√½ − 1, √2 − 1), s = f / (2 + f), odd series in s split in two interleaved parts
constexpr double l0 = 0x1.0c835bd538f8cp-4, l1 = 0x1.988ab1ef94f29p-5, l2 = 0x1.e3c098f53b5eep-5, l3 = 0x1.11053fca919d4p-4,
                 l4 = 0x1.3b140d1e4050fp-4, l5 = 0x1.745d159460dd7p-4, l6 = 0x1.c71c71cbb0290p-4, l7 = 0x1.249249248f262p-3,
                 l8 = 0x1.02e384367005fp-57, l9 = 0x1.99999999999b4p-3, l10 = 0x1.50aaac32dac90p-56, l11 = 0x1.5555555555555p-2;
constexpr double lnHi = 0x1.62e42fc000000p-1, lnLo = 0x1.7d1cf79abc9e4p-28;

// sin/cos kernels on [−π/4, π/4] with a tail y, and π/2 in three parts
constexpr double s0 = 0x1.5e0a28e7fa626p-33, s1 = -0x1.ae60081aa5e86p-26, s2 = 0x1.71de37936614ap-19, s3 = -0x1.a01a019e80e58p-13,
                 s4 = -0x1.9d73d633765ddp-61, s5 = 0x1.1111111110ba5p-7, s6 = 0x1.a74a934ad37d5p-57, s7 = -0x1.5555555555555p-3;
constexpr double c0 = -0x1.8fa6a8a7d84dfp-37, c1 = 0x1.1ee9dc12c88acp-29, c2 = -0x1.27e4f7f1ee922p-22, c3 = 0x1.a01a019c8f945p-16,
                 c4 = -0x1.6c16c16c15018p-10, c5 = 0x1.555555555554bp-5;
constexpr double pio2a = 0x1.921fb54442d18p+0, pio2b = 0x1.1a62633145c04p-54, pio2c = 0x1.707344a409382p-105;
constexpr double quarterPi = 0x1.921fb54442d18p-1, threeQuarterPi = 2.3561944901923448, fiveQuarterPi = 3.9269908169872414;

double expCore (double x, double kd, double scale) noexcept
{
    double hi = x - kd * ln2Hi;
    const double lo = kd * ln2Lo;
    double r = hi + lo;
    const double half = std::copysign (0.5, r);
    const int j = (int) (r * 64.0 + half); // truncation
    hi = hi - (double) j * 0.015625;
    r = hi + lo;
    const double r2 = r * r;
    const double p = (e3 + e5 * r2) + (e4 + e6 * r2) * r;
    const double q = lo + r2 * (0.5 + r * p);
    const double* t = expTable[j + 45];
    const double a = t[0] + 1.0;
    const double e = (t[0] - (a - 1.0)) + t[1];
    const double b = a + e;
    const double c = e - (b - a);
    double sum = q * c;
    sum = sum + hi * c;
    sum = sum + c;
    sum = sum + q * b;
    sum = sum + hi * b;
    sum = sum + b;
    return scale * sum;
}

double lnKernel (double f) noexcept
{
    const double s = f / (f + 2.0);
    const double h = f * 0.5;
    const double hf = f * h;   // f²/2
    const double q = hf * h;   // f³/4
    const double z = s * s, w = z * z;
    double a = l0 * w + l2;
    double b = l1 * w + l3;
    a = a * w + l4;
    b = b * w + l5;
    a = a * w + l6;
    b = b * w + l7;
    a = a * w + l8;
    a = a + l9;
    b = b * w + l10;
    double r = b + a * z;
    r = r + l11;
    r = r * 2.0 * z * s;
    return ((r + q) - hf) - s * q + f;
}

double sinKernel (double x, double y) noexcept
{
    const double z = x * x, w = z * z, v = z * x;
    double a = s0 * w + s2;
    double b = s1 * w + s3;
    a = a * w + s4;
    a = a + s5;
    b = b * w + s6;
    double t = (a * z + b) + s7;
    t = t * v;
    const double u = (1.0 - z * 0.5) * y;
    return x + (t + u);
}

double cosKernel (double x, double y) noexcept
{
    const double z = x * x, w = z * z;
    double a = c0 * w + c2;
    double b = c1 * w + c3;
    a = a * w + c4;
    b = b * w + c5;
    a = a * z;
    const double p = (b + a) * w;
    const double hz = z * 0.5;
    const double r = 1.0 - hz;
    const double corr = hz + (r - 1.0);
    return r + ((p - x * y) - corr);
}

// x = n·π/2 + (y0 + y1) for π/4 < |x| < 4194304; returns n & 3, or −1 beyond (C library). Up to 5π/4 the three-part
// π/2 split; above, a double-double chain with the second three-part split.
int remPio2 (double x, double ax, double& y0, double& y1) noexcept
{
    long long n;
    if (ax <= fiveQuarterPi)
    {
        n = ax <= threeQuarterPi ? 1 : 2;
        if (x < 0.0)
            n = -n;
        const double nd = (double) n;
        const double t = x - nd * pio2a;
        const double pb = nd * pio2b;
        y0 = t - pb;
        y1 = -pb - (y0 - t);
        y1 = y1 - nd * pio2c;
    }
    else if (ax < 4194304.0)
    {
        n = (long long) ((ax - quarterPi) * 0x1.45f306dc9c883p-1) + 1;
        if (x < 0.0)
            n = -n;
        const double nd = (double) n;
        const double a = nd * -0x1.921fb54400000p+0, b = nd * -0x1.0b4611a600000p-34, c = nd * -0x1.3198a2e037073p-69;
        const double s = x + a;
        const double a0 = b + s;
        double bv = a0 - b;
        const double err0 = (b - (a0 - bv)) + (s - bv);
        const double hi = a0 + err0;
        const double err1 = err0 - (hi - a0);
        const double a1 = c + hi;
        bv = a1 - c;
        double err2 = (c - (a1 - bv)) + (hi - bv);
        err2 = err2 + err1;
        y0 = a1 + err2;
        y1 = err2 - (y0 - a1);
    }
    else
        return -1;
    return (int) (n & 3);
}
} // namespace

double exp (double x) noexcept
{
    const auto bits = bitsOf (x);
    const auto top = (unsigned) (bits >> 48);
    if ((bits & 0x7ff0000000000000ull) == 0)
        return 1.0;
    if ((top & 0x7ff0u) == 0x7ff0u) // inf or NaN
        return x == -std::numeric_limits<double>::infinity() ? 0.0 : (std::isnan (x) ? x : std::numeric_limits<double>::infinity());
    if ((std::int64_t) bits >= 0)
    {
        if (top >= 0x4087u || bits > 0x40862e42fefa39efull)
            return std::numeric_limits<double>::infinity();
        const int k = (int) (x * log2e + 0.5);
        if (k > 0x3ff)
            return expCore (x, 1023.5, fromBits (0x7fe6a09e667f3bcdull));
        return expCore (x, (double) k, fromBits ((std::uint64_t) (k + 0x3ff) << 52));
    }
    if (top >= 0xc090u || bits > 0xc0874910d52d3051ull)
        return 0.0;
    const double t = x * log2e;
    const int k = (int) (t - 0.5);
    if (k >= -0x3fe)
        return expCore (x, (double) k, fromBits ((std::uint64_t) (k + 0x3ff) << 52));
    if (-1075.0 >= t)
        return 0.0;
    if (-1074.5 >= t)
        return expCore (x, -1074.5, fromBits (1));
    return expCore (x, (double) k, fromBits (0x10000000000000ull >> (unsigned) (-0x3fe - k)));
}

double ln (double x) noexcept
{
    auto bits = bitsOf (x);
    if (bits >> 63)
        return bits == 0x8000000000000000ull ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
    if (bits == 0)
        return -std::numeric_limits<double>::infinity();
    if (bits >= 0x7ff0000000000000ull)
        return bits == 0x7ff0000000000000ull ? x : std::numeric_limits<double>::quiet_NaN();
    int e = (int) (bits >> 52);
    std::uint64_t m = bits & 0xfffffffffffffull;
    if (e == 0) // subnormal: normalise the mantissa
    {
        e = -0x3fe;
        if ((m & 0x1fffffffe00000ull) == 0) { m <<= 32; e -= 32; }
        if ((m & 0x1fffe000000000ull) == 0) { m <<= 16; e -= 16; }
        if ((m & 0x1fe00000000000ull) == 0) { m <<= 8; e -= 8; }
        if ((m & 0x1e000000000000ull) == 0) { m <<= 4; e -= 4; }
        if ((m & 0x18000000000000ull) == 0) { m <<= 2; e -= 2; }
        if ((m & 0x10000000000000ull) == 0) { m <<= 1; e -= 1; }
        m &= 0xfffffffffffffull;
    }
    else
        e -= 0x3ff;
    double y;
    if (m <= 0x6a09e667f3bccull)
        y = fromBits (0x3ff0000000000000ull + m);
    else
    {
        y = fromBits (0x3fe0000000000000ull + m);
        ++e;
    }
    double r = lnKernel (y - 1.0);
    r = r + (double) e * lnLo;
    return r + (double) e * lnHi;
}

void sinCos (double x, double& s, double& c) noexcept
{
    const double ax = std::abs (x);
    if (ax <= quarterPi)
    {
        s = sinKernel (x, 0.0);
        c = cosKernel (x, 0.0);
        return;
    }
    double a = 0.0, y = 0.0;
    switch (std::isnan (x) ? -1 : remPio2 (x, ax, a, y))
    {
        case 0: s = sinKernel (a, y); c = cosKernel (a, y); break;
        case 1: s = cosKernel (a, y); c = -sinKernel (a, y); break;
        case 2: s = -sinKernel (a, y); c = -cosKernel (a, y); break;
        case 3: s = -cosKernel (a, y); c = sinKernel (a, y); break;
        default: s = std::sin (x); c = std::cos (x); break;
    }
}

double sin (double x) noexcept
{
    double s, c;
    sinCos (x, s, c);
    return s;
}

double cos (double x) noexcept
{
    double s, c;
    sinCos (x, s, c);
    return c;
}

double expm1 (double x) noexcept
{
    if (x == 0.0)
        return 0.0;
    const auto e = (unsigned) (bitsOf (x) >> 48) & 0x7ff0u;
    if (e == 0)
        return x;
    if (e == 0x7ff0u || std::abs (x) >= 0x1.62e42fefa39efp-1)
        return dmath::exp (x) - 1.0;
    const int j = (int) (x * 64.0 + std::copysign (0.5, x)); // truncation
    const double r = x - (double) j * 0.015625;
    const double z = r * r;
    double a = e6 * z + e4;
    double b = e5 * z + e3;
    a = a * z;
    b = b * r;
    a = a + b;
    a = a + 0.5;
    a = a * z;
    const double* t = expTable[j + 45];
    const double lo = t[1] + a, rr = r + a;
    return ((t[0] * rr + lo) + r) + t[0];
}

double intPower (double x, long long n) noexcept
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

double power (double x, double y) noexcept
{
    if (y == 0.0)
        return 1.0;
    if (x == 0.0 && y > 0.0)
        return 0.0;
    if (std::trunc (y) == y && std::abs (y) <= 2147483647.0)
        return intPower (x, (long long) y);
    return dmath::exp (y * dmath::ln (x));
}

double sinh (double x) noexcept
{
    if (std::abs (x) <= 1e-12)
        return 0.0;
    return (dmath::exp (x) - dmath::exp (-x)) / 2.0;
}

double tanh (double x) noexcept
{
    const bool neg = x < 0.0;
    const double a = neg ? -x : x;
    double r = 1.0;
    if (a <= 23.0)
    {
        if (a < 1.81458605194507e-05)
            r = a - a * a * a * 0.3333333333333333;
        else if (0.34657359027997264 <= a)
            r = 1.0 - 2.0 / (dmath::exp (a * 2.0) + 1.0);
        else
        {
            const double e = dmath::expm1 (a * 2.0);
            r = e / (e + 2.0);
        }
    }
    return neg ? -r : r;
}
} // namespace lynx::dmath
