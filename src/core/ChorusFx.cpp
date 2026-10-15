#include "core/ChorusFx.h"

// FL's chorus family, exact engines (docs/analise/REA_rodada59.md). Clean-room: written from the behaviour, formulas
// and data layouts described there; nothing of Image-Line's code or data is copied.
//
// Arithmetic follows the plugins operation by operation: float/double steps in their order with one rounding each
// (no contraction), in the SSE rounding and denormal modes each engine runs in.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#define LYNX_CHORUS_MXCSR 1
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#pragma STDC FENV_ACCESS ON
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off", "rounding-math")
#endif

namespace lynx
{
namespace
{
// SSE mode for the duration of a render: `bits` are or-ed into MXCSR (0x8040 = flush-to-zero + denormals-are-zero,
// 0xE040 = the same with rounding toward zero)
struct CsrMode
{
#ifdef LYNX_CHORUS_MXCSR
    unsigned saved;
    explicit CsrMode (unsigned bits) noexcept : saved (_mm_getcsr()) { _mm_setcsr ((saved & ~0x6000u) | bits); }
    ~CsrMode() { _mm_setcsr (saved); }
#else
    explicit CsrMode (unsigned) noexcept {}
#endif
    CsrMode (const CsrMode&) = delete;
    CsrMode& operator= (const CsrMode&) = delete;
};

inline std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::int32_t v;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}
inline float norm (int raw, int lo, int hi) noexcept
{
    return std::clamp ((float) (raw - lo) / (float) (hi - lo), 0.0f, 1.0f);
}
inline int rawFrom (float v01, int lo, int hi) noexcept
{
    return lo + (int) std::lround (std::clamp ((double) v01, 0.0, 1.0) * (hi - lo));
}
// FL's control value: (raw − min) · (1 / (max − min)) in double, clamped to 0…1, as float
inline float flValue (int raw, int lo, int hi) noexcept
{
    const double v = (double) (raw - lo) * (1.0 / (double) (hi - lo));
    return (float) std::clamp (v, 0.0, 1.0);
}
inline int truncI (double v) noexcept { return (int) (long long) v; }
} // namespace

// ================================================================================================= Fruity Chorus (§1)
namespace fruitychorusfx
{
namespace
{
constexpr int lineSize = 4096, lineMask = lineSize - 1;

// the four LFO tables (§1.3): parabola, its cube, the three-partial sine, its cube; 0…32766
struct Tables
{
    std::array<std::array<std::int32_t, 4096>, 4> t {};
    Tables() noexcept
    {
        auto value = [] (float v) { return truncI ((double) ((v + 1.0f) * 16383.0f)); };
        const double norm3 = 3.0 / (std::sqrt (2.0) * 2.0 + 1.0);
        for (int i = 0; i < 4096; ++i)
        {
            const float f = (float) ((double) i / 2048.0);
            const float p = 1.0 > (double) f ? (f * 4.0f) * (1.0f - f) : ((f - 1.0f) * -4.0f) * (2.0f - f);
            t[0][(std::size_t) i] = value (p);
            t[1][(std::size_t) i] = value ((p * p) * p);
            const double fd = f;
            const double s = std::sin (fd * 3.1415) + (std::sin (fd * 9.4245) + std::sin (fd * 6.283)) * 0.3333333333333333;
            const float sf = (float) (s * norm3);
            t[2][(std::size_t) i] = value (sf);
            t[3][(std::size_t) i] = value ((sf * sf) * sf);
        }
    }
};
const Tables& tables()
{
    static const Tables t;
    return t;
}
} // namespace

struct Engine::Impl
{
    const Tables& tab = tables();
    State raw = defaults;
    std::array<float, params> x {};
    int fs = 44100;
    std::uint32_t phase[3] {}, inc[3] {};
    int wave[3] {};
    int stereo = 0, delay = 0, depth = 0;
    bool lpLine = false, wetOnly = false;
    float a = 0.0f, b = 0.0f;
    float st[2] {}, prev[2] {};
    std::array<float, lineSize> line[2] {};
    int w = 0;

    void cutoff() noexcept
    {
        const double note = 0.0 + (127.0 - 0.0) * (double) x[10];
        double f = (double) (float) (std::pow (2.0, (note - 69.0) / 12.0) * 440.0);
        if ((double) fs / 4.1 < f)
            f = (double) fs / 4.1;
        const double om = (f / (double) fs) * 6.283184;
        a = (float) ((1.0 - std::sin (om)) / std::cos (om));
        b = (float) ((double) (1.0f - a) * 0.5);
    }
    void set (int k, int r) noexcept
    {
        raw[(std::size_t) k] = r;
        const float v = flValue (r, 0, rawMax[(std::size_t) k]);
        x[(std::size_t) k] = v;
        switch (k)
        {
            case 0: delay = truncI (((double) v * 29.95 + 0.05) * (double) fs * 4.096); break;
            case 1: depth = truncI ((double) ((v * 5.0f) * (float) fs) * 4.096 / 64.0); break;
            case 2: stereo = truncI ((double) (v * 4096.0f) * 0.5) - 1; break;
            case 3:
            case 4:
            case 5: inc[k - 3] = (std::uint32_t) truncI ((double) (((v * v) * 83886080.0f) / (float) fs)); break;
            case 6:
            case 7:
            case 8: wave[k - 6] = std::clamp (truncI ((double) (v * 3.0f)), 0, 3); break;
            case 9: lpLine = 0.25 < (double) v; break;
            case 10: cutoff(); break;
            case 11: wetOnly = 0.25 < (double) v; break;
            default: break;
        }
    }
    void reset (std::uint32_t p1, std::uint32_t p2, std::uint32_t p3) noexcept
    {
        st[0] = st[1] = prev[0] = prev[1] = 0.0f;
        line[0].fill (0.0f);
        line[1].fill (0.0f);
        w = 0;
        phase[0] = p1, phase[1] = p2, phase[2] = p3;
    }
    void render (const float* in, float* out, int n) noexcept
    {
        CsrMode mode (0x8040u);
        for (int i = 0; i < n; ++i)
        {
            const float xin[2] { (float) ((double) in[2 * i] * 0.7), (float) ((double) in[2 * i + 1] * 0.7) };
            std::uint32_t off[3][2];
            for (int v = 0; v < 3; ++v)
            {
                const auto& t = tab.t[(std::size_t) wave[v]];
                const std::uint32_t j = (phase[v] >> 12) & 0xfffu;
                const auto lL = (std::uint32_t) t[j], lR = (std::uint32_t) t[(j + (std::uint32_t) stereo) & 0xfffu];
                off[v][0] = (lL * (std::uint32_t) depth) >> 9;
                off[v][1] = (lR * (std::uint32_t) depth) >> 9;
            }
            for (int v = 0; v < 3; ++v)
                phase[v] += inc[v];
            for (int c = 0; c < 2; ++c)
            {
                st[c] = b * (xin[c] + prev[c]) + a * st[c];
                prev[c] = xin[c];
                line[c][(std::size_t) w] = lpLine ? st[c] : xin[c] - st[c];
            }
            const auto base = (std::uint32_t) (w * 4096 - delay);
            float sum[2];
            for (int c = 0; c < 2; ++c)
            {
                const auto& L = line[c];
                float acc = 0.0f;
                for (int v = 0; v < 3; ++v)
                {
                    const std::uint32_t u = base - off[v][c], j = (u >> 12) & 0xfffu, fr = u & 0xfffu;
                    const float dlt = L[(j + 1) & 0xfffu] - L[j];
                    const auto val = (float) ((double) L[j] + ((double) dlt * (double) fr) * 0.000244140625);
                    acc = v == 0 ? val : acc + val;
                }
                sum[c] = acc;
            }
            w = (w + 1) & lineMask;
            for (int c = 0; c < 2; ++c)
            {
                const auto o = (float) ((double) sum[c] * 0.6);
                out[2 * i + c] = wetOnly ? o : o + xin[c];
            }
        }
        for (auto& s : st)
            if (std::fabs ((double) s) < 1e-11)
                s = 0.0f;
    }
};

Engine::Engine() : d (std::make_unique<Impl>())
{
    load (defaults);
    d->reset (0, 0, 0);
}
Engine::~Engine() = default;
void Engine::setRate (int sampleRate) noexcept { d->fs = std::max (1, sampleRate); }
void Engine::setParam (int index, int raw) noexcept
{
    if (index >= 0 && index < params)
        d->set (index, raw);
}
void Engine::load (const State& s) noexcept
{
    for (int i = 0; i < params; ++i)
        d->set (i, s[(std::size_t) i]);
}
void Engine::reset (std::uint32_t p1, std::uint32_t p2, std::uint32_t p3) noexcept { d->reset (p1, p2, p3); }
int Engine::param (int index) const noexcept { return d->raw[(std::size_t) index]; }
void Engine::render (const float* in, float* out, int n) noexcept { d->render (in, out, n); }

std::optional<State> parse (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 4 + 4 * params || i32At (state, 0) > 1)
        return std::nullopt;
    State s {};
    for (int i = 0; i < params; ++i)
        s[(std::size_t) i] = i32At (state, 4 + 4 * (std::size_t) i);
    return s;
}
void toParams (const State& s, float* p) noexcept
{
    for (int i = 0; i < params; ++i)
        p[i] = norm (s[(std::size_t) i], 0, rawMax[(std::size_t) i]);
}
State fromParams (const float* p) noexcept
{
    State s {};
    for (int i = 0; i < params; ++i)
        s[(std::size_t) i] = rawFrom (p[i], 0, rawMax[(std::size_t) i]);
    return s;
}
const char* paramName (int param) noexcept
{
    static const char* names[params] { "Delay",      "Depth",      "Stereo",     "LFO 1 freq", "LFO 2 freq",  "LFO 3 freq",
                                       "LFO 1 wave", "LFO 2 wave", "LFO 3 wave", "Cross type", "Cross cutoff", "Wet only" };
    return param >= 0 && param < params ? names[param] : "";
}
std::string paramText (int param, float value01)
{
    char text[32];
    const int p = std::clamp (param, 0, params - 1);
    const int raw = rawFrom (value01, 0, rawMax[(std::size_t) p]);
    const double x = (double) raw / rawMax[(std::size_t) p];
    switch (p)
    {
        case 0: std::snprintf (text, sizeof (text), "%.2f ms", x * 29.95 + 0.05); return text;
        case 1: std::snprintf (text, sizeof (text), "%.2f ms", x * 5.0 * 4.096 / 64.0 * 32766.0 / 512.0); return text;
        case 3:
        case 4:
        case 5: std::snprintf (text, sizeof (text), "%.3f Hz", x * x * 5.0); return text;
        case 6:
        case 7:
        case 8:
        {
            static const char* waves[4] { "parabola", "sharp", "sine", "sharp sine" };
            return waves[std::clamp ((int) (x * 3.0), 0, 3)];
        }
        case 9: return x > 0.25 ? "low" : "high";
        case 10: std::snprintf (text, sizeof (text), "%.0f Hz", 440.0 * std::pow (2.0, (127.0 * x - 69.0) / 12.0)); return text;
        case 11: return x > 0.25 ? "on" : "off";
        default: std::snprintf (text, sizeof (text), "%d%%", (int) std::lround (100.0 * x)); return text;
    }
}

namespace
{
class FruityChorusEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        engine.setRate ((int) std::lround (sampleRate));
        engine.load (engine.raw()); // the rate-dependent values for this rate (as if FL had set it before the state)
    }
    // LYNX's reset = FL's "enabled" notification; the start phases FL draws at random are taken as 0
    void reset() noexcept override { engine.reset(); }
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = fromParams (params.data());
        for (int i = 0; i < fruitychorusfx::params; ++i)
            if (s[(std::size_t) i] != engine.param (i))
                engine.setParam (i, s[(std::size_t) i]);
        for (int o = 0; o < n; o += 512)
        {
            const int m = std::min (512, n - o);
            for (int i = 0; i < m; ++i)
            {
                buf[2 * (std::size_t) i] = left[o + i];
                buf[2 * (std::size_t) i + 1] = right[o + i];
            }
            engine.render (buf.data(), buf.data(), m);
            for (int i = 0; i < m; ++i)
            {
                left[o + i] = buf[2 * (std::size_t) i];
                right[o + i] = buf[2 * (std::size_t) i + 1];
            }
        }
    }

private:
    struct Wrapped : Engine
    {
        State raw() const noexcept
        {
            State s {};
            for (int i = 0; i < params; ++i)
                s[(std::size_t) i] = param (i);
            return s;
        }
    } engine;
    std::array<float, 1024> buf {};
};
} // namespace

std::unique_ptr<Effect> make() { return std::make_unique<FruityChorusEffect>(); }
const EffectInfo& info()
{
    static const EffectInfo i = [] {
        EffectInfo e { "Fruity Chorus", {} };
        for (int k = 0; k < params; ++k)
            e.params.push_back ({ paramName (k), norm (defaults[(std::size_t) k], 0, rawMax[(std::size_t) k]) });
        return e;
    }();
    return i;
}
} // namespace fruitychorusfx

// ================================================================================================== Hyper Chorus (§2)
namespace hyperchorusfx
{
namespace
{
inline float bitsF (std::uint32_t u) noexcept
{
    float f;
    std::memcpy (&f, &u, 4);
    return f;
}
// The engine's math library (§2.5), evaluated like it in the current SSE rounding mode (the engine's parameter
// changes run inside its render, in "toward zero"): the vector sine (reduction by π in four parts, odd polynomial of
// degree 15; the cosine is the sine of |x| + π/2) and powf for base 2 (ln 2 · y, then 2^(N/64) table and a cubic).
inline std::uint64_t bitsOf (double v) noexcept
{
    std::uint64_t u;
    std::memcpy (&u, &v, 8);
    return u;
}
inline double fromBits (std::uint64_t u) noexcept
{
    double v;
    std::memcpy (&v, &u, 8);
    return v;
}
double crtSin (double x) noexcept
{
    const double ax = std::fabs (x);
    double k = ax * 0.3183098861837907;
    k = k + 6755399441055744.0;
    const std::uint64_t parity = bitsOf (k) << 63;
    const double n = k - 6755399441055744.0;
    const double p1 = n * 3.141592502593994, p2 = n * 1.5099578831723193e-07, p3 = n * 1.078060505991553e-14,
                 p4 = n * 6.564007085747001e-22;
    double r = ax - p1;
    r = r - p2;
    r = r - p3;
    r = r - p4;
    const double r2 = r * r;
    r = fromBits (bitsOf (r) ^ parity);
    double q = -7.373344533110665e-13 * r2 + 1.6048095404959637e-10;
    q = q * r2 + -2.505188027386288e-08;
    q = q * r2 + 2.7557316600552628e-06;
    q = q * r2 + -0.00019841269825056256;
    q = q * r2 + 0.008333333333285542;
    q = q * r2 + -0.16666666666666186;
    const double t = (r2 * q) * r;
    return fromBits (bitsOf (r + t) ^ (bitsOf (x) & 0x8000000000000000ull));
}
void crtSinCos (double x, double& s, double& c) noexcept
{
    const double ax = std::fabs (x);
    if (ax < 7.450580596923828e-09)
    {
        s = x;
        c = 1.0 - (ax * ax) * 0.5;
        return;
    }
    s = crtSin (x);
    c = crtSin (ax + 1.5707963267948966);
}
constexpr double exp2Table[64] {
    0x1.0000000000000p+0, 0x1.02c9a3e778061p+0, 0x1.059b0d3158574p+0, 0x1.0874518759bc8p+0,
    0x1.0b5586cf9890fp+0, 0x1.0e3ec32d3d1a2p+0, 0x1.11301d0125b51p+0, 0x1.1429aaea92de0p+0,
    0x1.172b83c7d517bp+0, 0x1.1a35beb6fcb75p+0, 0x1.1d4873168b9aap+0, 0x1.2063b88628cd6p+0,
    0x1.2387a6e756238p+0, 0x1.26b4565e27cddp+0, 0x1.29e9df51fdee1p+0, 0x1.2d285a6e4030bp+0,
    0x1.306fe0a31b715p+0, 0x1.33c08b26416ffp+0, 0x1.371a7373aa9cbp+0, 0x1.3a7db34e59ff7p+0,
    0x1.3dea64c123422p+0, 0x1.4160a21f72e2ap+0, 0x1.44e086061892dp+0, 0x1.486a2b5c13cd0p+0,
    0x1.4bfdad5362a27p+0, 0x1.4f9b2769d2ca7p+0, 0x1.5342b569d4f82p+0, 0x1.56f4736b527dap+0,
    0x1.5ab07dd485429p+0, 0x1.5e76f15ad2148p+0, 0x1.6247eb03a5585p+0, 0x1.6623882552225p+0,
    0x1.6a09e667f3bcdp+0, 0x1.6dfb23c651a2fp+0, 0x1.71f75e8ec5f74p+0, 0x1.75feb564267c9p+0,
    0x1.7a11473eb0187p+0, 0x1.7e2f336cf4e62p+0, 0x1.82589994cce13p+0, 0x1.868d99b4492edp+0,
    0x1.8ace5422aa0dbp+0, 0x1.8f1ae99157736p+0, 0x1.93737b0cdc5e5p+0, 0x1.97d829fde4e50p+0,
    0x1.9c49182a3f090p+0, 0x1.a0c667b5de565p+0, 0x1.a5503b23e255dp+0, 0x1.a9e6b5579fdbfp+0,
    0x1.ae89f995ad3adp+0, 0x1.b33a2b84f15fbp+0, 0x1.b7f76f2fb5e47p+0, 0x1.bcc1e904bc1d2p+0,
    0x1.c199bdd85529cp+0, 0x1.c67f12e57d14bp+0, 0x1.cb720dcef9069p+0, 0x1.d072d4a07897cp+0,
    0x1.d5818dcfba487p+0, 0x1.da9e603db3285p+0, 0x1.dfc97337b9b5fp+0, 0x1.e502ee78b3ff6p+0,
    0x1.ea4afa2a490dap+0, 0x1.efa1bee615a27p+0, 0x1.f50765b6e4540p+0, 0x1.fa7c1819e90d8p+0,
}; // 2^(j/64), correctly rounded
// double → int32 in the current rounding mode (the library's cvtpd2dq)
inline int roundCurrent (double v) noexcept
{
#ifdef LYNX_CHORUS_MXCSR
    return _mm_cvtsd_si32 (_mm_set_sd (v));
#else
    return (int) std::nearbyint (v);
#endif
}
float crtPow2f (float e) noexcept // powf (2, e) for 0 ≤ e < 128
{
    const double t = (double) e * 0.6931471805599453;
    const int N = roundCurrent (t * 92.33248261689366);
    const double r = t - (double) N * 0.010830424696249145;
    const double q = (r * r) * ((r * 0.16666666666666666) + 0.5) + r;
    const double T = exp2Table[N & 63];
    const double v = q * T + T;
    return (float) fromBits (bitsOf (v) + ((std::uint64_t) (std::int64_t) (N >> 6) << 52));
}

// per voice of a chorus core: delay range (ms), modulation rate range (Hz), modulation amount range, movement speed
// range (Hz); the first core's four voices, then the ArachnaChor core's (§2.3)
constexpr float voiceTable[8][8] {
    { 2.0f, 6.0f, 1.5f, 2.47f, 20.0f, 80.0f, 0.33f, 3.56f },   { 3.5f, 8.25f, 1.57f, 3.41f, 20.0f, 80.0f, 0.29f, 2.66f },
    { 6.5f, 10.0f, 0.38f, 3.64f, 20.0f, 80.0f, 0.26f, 3.78f }, { 5.0f, 12.8f, 1.29f, 3.68f, 20.0f, 80.0f, 0.14f, 2.98f },
    { 7.1f, 16.6f, 3.41f, 5.03f, 20.0f, 80.0f, 0.44f, 5.06f }, { 9.1f, 15.1f, 2.47f, 5.37f, 20.0f, 80.0f, 0.66f, 8.08f },
    { 8.3f, 16.8f, 3.01f, 6.02f, 20.0f, 80.0f, 0.93f, 6.02f }, { 9.6f, 17.7f, 3.3f, 6.5f, 20.0f, 80.0f, 0.41f, 10.22f },
};
const float voiceGain = bitsF (0x3FB50BF3u); // 1.41442716

std::uint32_t randStep (std::uint32_t s) noexcept { return s * 214013u + 2531011u; }

struct Core
{
    std::array<float, 8> o {}; // voice outputs: left voices 0…3, right voices 4…7
    float d[4] {}, fb[4] {}, amt[4] {}, phM[4] {}, incM[4] {}, phd[4] {}, phV[4] {}, incV[4] {}, mv[4] {};
    std::vector<float> line = std::vector<float> ((std::size_t) 8 * 65536, 0.0f);
    std::uint16_t w = 0;
    double fs = 44100.0;
    int first = 0; // first row of voiceTable

    void set (int k, float x) noexcept
    {
        for (int l = 0; l < 4; ++l)
        {
            const float* T = voiceTable[first + l];
            switch (k)
            {
                case 0: d[l] = (((T[1] - T[0]) * x + T[0]) * 0.001f) * (float) fs; break;
                case 1: fb[l] = x * 0.7f; break;
                case 2: incM[l] = ((T[3] - T[2]) * x + T[2]) / (float) fs; break;
                case 3:
                {
                    const float v = (T[5] - T[4]) * x + T[4];
                    amt[l] = (v * v) * bitsF (0x3851B717u); // 5e-5
                    break;
                }
                case 4: mv[l] = x * bitsF (0x3EFF7CEEu); break; // 0.499
                case 5: incV[l] = ((T[7] - T[6]) * x + T[6]) / (float) fs; break;
                case 10: phd[l] = ((x * x) * x) * 0.25f; break;
                default: break;
            }
        }
    }
    static float shape (float t) noexcept
    {
        const float a = t < 1.0f ? t : 1.0f, b = (t > 1.0f ? t : 1.0f) - 1.0f;
        return (a - a * a) - (b - b * b);
    }
    static float frac (float v) noexcept { return v - (float) (int) v; }
    float tap (const float* s, float delay) const noexcept
    {
        const auto i = (std::uint16_t) (w - (std::uint16_t) (int) delay);
        const float y0 = s[i], y1 = s[(std::uint16_t) (i - 1)], y2 = s[(std::uint16_t) (i - 2)], y3 = s[(std::uint16_t) (i - 3)];
        const float c = (y2 - y0) * 0.5f, e = (y3 - y1) * 0.5f, A = y1 - y2, B = A + c, D = e + (B + A);
        const float f = delay - (float) (int) delay;
        return ((((f * D) - (D + B)) * f) + c) * f + y1;
    }
    void process (float inL, float inR) noexcept
    {
        float dl[4], dr[4], m[4];
        for (int l = 0; l < 4; ++l)
        {
            const float pR = frac (phM[l] + phd[l]);
            m[l] = ((shape (phV[l] + phV[l]) * 4.0f) * mv[l]) + 0.5f;
            const float dA = d[l] * amt[l];
            dl[l] = ((shape (phM[l] + phM[l]) * 4.0f) * dA) + d[l];
            dr[l] = ((shape (pR + pR) * 4.0f) * dA) + d[l];
        }
        float* L = line.data();
        for (int l = 0; l < 4; ++l)
            L[(std::size_t) l * 65536 + w] = ((std::sqrt (1.0f - m[l]) * voiceGain) * inL) + fb[l] * o[(std::size_t) l];
        for (int l = 0; l < 4; ++l)
            o[(std::size_t) l] = tap (L + (std::size_t) l * 65536, dl[l]);
        for (int l = 0; l < 4; ++l)
            L[(std::size_t) (4 + l) * 65536 + w] = ((std::sqrt (m[l]) * voiceGain) * inR) + o[(std::size_t) (4 + l)] * fb[l];
        for (int l = 0; l < 4; ++l)
            o[(std::size_t) (4 + l)] = tap (L + (std::size_t) (4 + l) * 65536, dr[l]);
        ++w;
        for (int l = 0; l < 4; ++l)
        {
            phM[l] = frac (incM[l] + phM[l]);
            phV[l] = frac (incV[l] + phV[l]);
        }
    }
    void clear() noexcept
    {
        o.fill (0.0f);
        w = 0;
        std::fill (line.begin(), line.end(), 0.0f);
    }
};

// RBJ low-/high-pass with α = sin ω (§2.4); designed in double, stored as float in the current rounding mode
void design (bool highPass, double f, double fs, float* c) noexcept
{
    const double om = (f * 6.283185307179586) * (1.0 / fs);
    double S, C;
    crtSinCos (om, S, C);
    const double inv = 1.0 / (S + 1.0);
    const double h = highPass ? (C + 1.0) * 0.5 : (1.0 - C) * 0.5, m = highPass ? -(C + 1.0) : 1.0 - C;
    c[0] = (float) (inv * h);
    c[1] = (float) (inv * m);
    c[2] = (float) (inv * h);
    c[3] = -(float) (inv * (C * -2.0));
    c[4] = -(float) (inv * (1.0 - S));
}
struct Biquad
{
    float c[5] {};
    float y1[2] {}, y2[2] {}, x1[2] {}, x2[2] {};
    float run (int ch, float x) noexcept
    {
        const float y = ((c[3] * y1[ch] + c[4] * y2[ch]) + c[2] * x2[ch]) + (c[1] * x1[ch] + c[0] * x);
        x2[ch] = x1[ch], x1[ch] = x, y2[ch] = y1[ch], y1[ch] = y;
        return y;
    }
    void clear() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
            y1[ch] = y2[ch] = x1[ch] = x2[ch] = 0.0f;
    }
};
} // namespace

struct Engine::Impl
{
    Core core[2];
    float value[params] { 0.4f, 0.5f, 0.4f, 0.4f, 0.3f, 0.3f, 0.0f, 1.0f, 0.0f, 0.5f, 0.1f };
    struct Pending
    {
        int id;
        float cur, target;
    };
    std::vector<Pending> pending;
    float smooth = 1.0f;
    bool arachna = false;
    double fs = 44100.0;
    int fade = 0, fadeLen = 441;
    float fadeStep = 0.0f, dry = 1.0f, wet = 0.0f;
    Biquad lp, hp;

    explicit Impl (int instance)
    {
        pending.reserve (32);
        core[1].first = 4;
        std::uint32_t s = 1;
        for (int k = 0; k < 16 * std::max (0, instance); ++k)
            s = randStep (s);
        auto next = [&s] {
            s = randStep (s);
            const auto f = (float) ((double) (int) ((s >> 16) & 0x7fffu) * 3.051850947599719e-05);
            return std::clamp (f, 0.0f, 1.0f);
        };
        for (auto& c : core)
        {
            for (auto& p : c.phM)
                p = next();
            for (auto& p : c.phV)
                p = next();
        }
    }
    void apply (int k, float x) noexcept // the engine's parameter handler
    {
        switch (k)
        {
            case 6: arachna = x >= 0.5f; break;
            case 7: design (false, (double) (crtPow2f (bitsF (0x409E4102u) * x) * 714.0f), fs, lp.c); break;
            case 8: design (true, (double) (crtPow2f (bitsF (0x40D02A30u) * x) * 12.0f), fs, hp.c); break;
            case 9:
            {
                float g;
                if (x >= 0.5f)
                {
                    const float t = 1.0f - (((x - 0.5f) + x) - 0.5f);
                    g = 1.0f - (t * t) * 0.5f;
                }
                else
                    g = ((x + x) * (x + x)) * 0.5f;
                wet = std::sqrt (g);
                dry = std::sqrt (1.0f - g);
                break;
            }
            default:
                core[0].set (k, x);
                core[1].set (k, x);
                break;
        }
    }
    void setRate (double rate) noexcept
    {
        fs = (double) (float) rate;
        fadeLen = (int) (fs * 0.01);
        fadeStep = 1.0f / (float) fadeLen;
        core[0].fs = core[1].fs = fs;
        smooth = (float) (1.0 - std::pow (0.09999999999999998, 1.0 / (fs * 0.05)));
    }
    void setParam (int k, float x) noexcept
    {
        value[k] = x;
        for (auto& p : pending)
            if (p.id == k)
            {
                p.target = x;
                return;
            }
        pending.push_back ({ k, value[k], x });
    }
    float hpFade (int ch, float x) noexcept
    {
        const float y = hp.run (ch, x), f = (float) fade * fadeStep;
        return x * (1.0f - f) + y * f;
    }
    void render (const float* in, float* out, int n) noexcept
    {
        CsrMode mode (0xE040u);
        for (int i = 0; i < n; ++i)
        {
            for (int j = (int) pending.size() - 1; j >= 0; --j)
            {
                auto& p = pending[(std::size_t) j];
                const float diff = p.target - p.cur;
                if (std::fabs (diff) >= bitsF (0x38D1B717u)) // 1e-4
                {
                    p.cur = diff * smooth + p.cur;
                    apply (p.id, p.cur);
                }
                else
                {
                    apply (p.id, p.target);
                    pending.erase (pending.begin() + j);
                }
            }
            const float inL = in[2 * i], inR = in[2 * i + 1];
            core[0].process (inL, inR);
            const auto& a = core[0].o;
            float sL = ((a[0] + a[1]) + a[2]) + a[3], sR = ((a[4] + a[5]) + a[6]) + a[7];
            if (arachna)
            {
                core[1].process (inL, inR);
                const auto& b = core[1].o;
                sR = (((sR + b[7]) + b[6]) + (b[5] + b[4])) * bitsF (0x3E8FF594u);
                sL = (((sL + b[3]) + b[2]) + (b[1] + b[0])) * bitsF (0x3E8FF594u);
            }
            else
            {
                sR = sR * bitsF (0x3F0FF594u);
                sL = sL * bitsF (0x3F0FF594u);
            }
            float yL = lp.run (0, sL), yR = lp.run (1, sR);
            if (value[8] == 0.0f)
            {
                if (fade > 0)
                {
                    yL = hpFade (0, yL), yR = hpFade (1, yR);
                    --fade;
                }
            }
            else if (fade < fadeLen)
            {
                yL = hpFade (0, yL), yR = hpFade (1, yR);
                ++fade;
            }
            else
                yL = hp.run (0, yL), yR = hp.run (1, yR);
            out[2 * i] = inL * dry + yL * wet;
            out[2 * i + 1] = inR * dry + yR * wet;
        }
    }
};

Engine::Engine (int instance) : d (std::make_unique<Impl> (instance)) {}
Engine::~Engine() = default;
void Engine::setRate (double sampleRate) noexcept { d->setRate (sampleRate); }
void Engine::setParam (int index, float value) noexcept
{
    if (index >= 0 && index < params)
        d->setParam (index, value);
}
float Engine::param (int index) const noexcept { return d->value[index]; }
void Engine::reset() noexcept
{
    d->lp.clear();
    d->hp.clear();
    d->fade = 0;
    d->core[0].clear();
}
void Engine::render (const float* in, float* out, int n) noexcept { d->render (in, out, n); }

std::uint32_t randState (int calls) noexcept
{
    std::uint32_t s = 1;
    for (int k = 0; k < calls; ++k)
        s = randStep (s);
    return s;
}

std::optional<State> parse (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 4 + 4 * params || i32At (state, 0) >= 3)
        return std::nullopt;
    State s {};
    for (int i = 0; i < params; ++i)
        s[(std::size_t) i] = i32At (state, 4 + 4 * (std::size_t) i);
    return s;
}
void toParams (const State& s, float* p) noexcept
{
    for (int i = 0; i < params; ++i)
        p[i] = norm (s[(std::size_t) i], rawMin[(std::size_t) i], rawMax[(std::size_t) i]);
}
State fromParams (const float* p) noexcept
{
    State s {};
    for (int i = 0; i < params; ++i)
        s[(std::size_t) i] = rawFrom (p[i], rawMin[(std::size_t) i], rawMax[(std::size_t) i]);
    return s;
}
const char* paramName (int param) noexcept
{
    static const char* names[params] { "Delay",          "Feedback", "Mod rate",  "Mod amount", "Movement",          "Movement speed",
                                       "ArachnaChor mode", "Low pass", "High pass", "Dry/wet",    "LFO phase difference" };
    return param >= 0 && param < params ? names[param] : "";
}
std::string paramText (int param, float value01)
{
    char text[40];
    const int p = std::clamp (param, 0, params - 1);
    const int raw = rawFrom (value01, rawMin[(std::size_t) p], rawMax[(std::size_t) p]);
    const double x = (double) (raw - rawMin[(std::size_t) p]) / (rawMax[(std::size_t) p] - rawMin[(std::size_t) p]);
    switch (p)
    {
        case 6: return x >= 0.5 ? "on" : "off";
        case 7: std::snprintf (text, sizeof (text), "%.0f Hz", 714.0 * std::pow (2.0, 4.9454355 * x)); return text;
        case 8:
            if (x == 0.0)
                return "off";
            std::snprintf (text, sizeof (text), "%.0f Hz", 12.0 * std::pow (2.0, 6.50515 * x));
            return text;
        case 9: std::snprintf (text, sizeof (text), "%.1f%% wet", x * 100.0); return text;
        default: std::snprintf (text, sizeof (text), "%.1f", x * 100.0); return text;
    }
}

namespace
{
class HyperChorusEffect final : public Effect
{
public:
    explicit HyperChorusEffect (int instance) : engine (instance) {}
    void prepare (double sampleRate) noexcept override
    {
        engine.setRate (sampleRate);
        for (int i = 0; i < hyperchorusfx::params; ++i) // re-applied at this rate (FL loads the state after the rate)
            engine.setParam (i, engine.param (i));
    }
    void reset() noexcept override { engine.reset(); }
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = fromParams (params.data());
        for (int i = 0; i < hyperchorusfx::params; ++i)
        {
            const float v = flValue (s[(std::size_t) i], rawMin[(std::size_t) i], rawMax[(std::size_t) i]);
            if (v != engine.param (i) || ! started)
                engine.setParam (i, v);
        }
        started = true;
        for (int o = 0; o < n; o += 512)
        {
            const int m = std::min (512, n - o);
            for (int i = 0; i < m; ++i)
            {
                buf[2 * (std::size_t) i] = left[o + i];
                buf[2 * (std::size_t) i + 1] = right[o + i];
            }
            engine.render (buf.data(), buf.data(), m);
            for (int i = 0; i < m; ++i)
            {
                left[o + i] = buf[2 * (std::size_t) i];
                right[o + i] = buf[2 * (std::size_t) i + 1];
            }
        }
    }

private:
    Engine engine;
    bool started = false;
    std::array<float, 1024> buf {};
};
} // namespace

std::unique_ptr<Effect> make (int instance) { return std::make_unique<HyperChorusEffect> (instance); }
const EffectInfo& info()
{
    static const EffectInfo i = [] {
        EffectInfo e { "Hyper Chorus", {} };
        for (int k = 0; k < params; ++k)
            e.params.push_back ({ paramName (k), norm (defaults[(std::size_t) k], rawMin[(std::size_t) k], rawMax[(std::size_t) k]) });
        return e;
    }();
    return i;
}
} // namespace hyperchorusfx

// ============================================================================================================ FL import
int chorusControls (const std::string& pluginName) noexcept
{
    if (pluginName == "Fruity Chorus")
        return fruitychorusfx::params;
    return 0;
}
std::optional<EffectSlot> chorusFromFl (const std::string& pluginName, const std::vector<std::uint8_t>& state)
{
    if (pluginName == "Fruity Chorus")
        if (const auto s = fruitychorusfx::parse (state))
        {
            auto slot = makeEffectSlot (EffectKind::fruityChorus);
            fruitychorusfx::toParams (*s, slot.params.data());
            return slot;
        }
    return std::nullopt;
}
} // namespace lynx
