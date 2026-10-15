#include "core/FlangusFastDist.h"

// Fruity Fast Dist and Fruity Flangus, exact engines (docs/analise/REA_rodada55.md). Clean-room: written from the
// behaviour, formulas and data layouts described there; nothing of Image-Line's code or data is copied.
//
// Arithmetic follows the plugins operation by operation: float/double steps in their order with one rounding each
// (no contraction), and the x87 kernels in 80-bit long double under the control word the plugins use (round to
// nearest in Fast Dist's live kernel, toward zero in its render kernel and in Flangus's voices).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Instruments.h"

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
using ld = long double;

// x87 control word for the duration of a kernel: 64-bit precision, nearest or toward zero
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
struct X87Mode
{
    unsigned short saved = 0;
    explicit X87Mode (bool truncate) noexcept
    {
        asm volatile ("fnstcw %0" : "=m"(saved) : : "memory");
        unsigned short cw = (unsigned short) ((saved & ~0x0F00u) | 0x0300u | (truncate ? 0x0C00u : 0u));
        asm volatile ("fldcw %0" : : "m"(cw) : "memory");
    }
    ~X87Mode() { asm volatile ("fldcw %0" : : "m"(saved) : "memory"); }
    X87Mode (const X87Mode&) = delete;
    X87Mode& operator= (const X87Mode&) = delete;
};
// long double → integer in the current x87 rounding mode (the plugins' fist), |v| < 2^63
inline long long x87Int (long double v) noexcept
{
    long long r;
    asm ("fistpll %0" : "=m"(r) : "t"(v) : "st");
    return r;
}
#else
struct X87Mode
{
    explicit X87Mode (bool) noexcept {}
};
inline long long x87Int (long double v) noexcept { return (long long) std::nearbyint (v); }
#endif

inline std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}
inline std::uint32_t bitsOf (float f) noexcept
{
    std::uint32_t u;
    std::memcpy (&u, &f, 4);
    return u;
}
inline float norm (int raw, int lo, int hi) noexcept
{
    return std::clamp ((float) (raw - lo) / (float) (hi - lo), 0.0f, 1.0f);
}
inline int rawFrom (float v01, int lo, int hi) noexcept
{
    return lo + (int) std::lround (std::clamp ((double) v01, 0.0, 1.0) * (hi - lo));
}
// FL's "L11" level law of a control (float steps, the exp in double; ln 11 as a float)
float levelLaw (float n) noexcept
{
    if (n == 1.0f)
        return 1.0f;
    return (float) ((std::exp ((double) (float) (n * 2.39789534f)) - 1.0) * (double) 0.1f);
}
std::string dbText (double g)
{
    char text[32];
    if (g <= 0.0)
        return "-inf dB";
    std::snprintf (text, sizeof (text), "%+.1f dB", 20.0 * std::log10 (g));
    return text;
}
} // namespace

// ===================================================================================================== Fast Dist
namespace fastdistfx
{
namespace
{
constexpr int N = 8191;

struct Curves
{
    // [set][curve − 1][k], k = 0…8192
    std::array<std::array<std::array<std::int16_t, 8193>, 10>, 2> t {};
    Curves()
    {
        // FLEngine's table builder (§2.3): x = k/8191 in dB; set 0 compresses the top 25 dB by (11 − c)/10 and adds
        // 1.6·(c − 1) dB below −1.6·(c − 1); set 1 bends the level by the power 0.8 + 0.02·(10 − c)
        const double x10 = std::log (10.0) * 0.05, c20 = 20.0 / std::log (10.0);
        for (int c = 1; c <= 10; ++c)
        {
            const double x8 = (11 - c) * 0.1, x9 = (c - 1) * 1.6, pw = (10 - c) * 0.02 + 0.8;
            for (int k = 0; k < 8192; ++k)
            {
                const double x = k / 8191.0;
                double d = x != 0.0 ? std::log (x) * c20 : -10000.0;
                if (d >= -25.0)
                    d = (d + 25.0) * x8 + -25.0;
                if (! (-x9 < d))
                    d += x9;
                t[0][(std::size_t) c - 1][(std::size_t) k] = (std::int16_t) std::lrint (std::exp (d * x10) * 32767.0);
                const double e = x != 0.0 ? std::log (x) * c20 : -1000.0;
                const double p = -e > 0.0 ? std::exp (pw * std::log (-e)) : 0.0; // Power (−e, pw)
                t[1][(std::size_t) c - 1][(std::size_t) k] = (std::int16_t) std::lrint (std::exp (-p * x10) * 32767.0);
            }
            for (auto& s : t)
                s[(std::size_t) c - 1][8192] = s[(std::size_t) c - 1][8191];
        }
    }
};
const Curves& curves()
{
    static const Curves c;
    return c;
}

// FL's live kernel: index = x·drive·8191 rounded to nearest (x87), odd symmetry, no interpolation
[[gnu::noinline]] void kernelLive (float* x, int n, const std::int16_t* T, const Gains& g) noexcept
{
    const X87Mode mode (false);
    const ld k = (ld) g.drive * (ld) N, w = (ld) g.wet * 3.0517578125e-05L, a = g.dry;
    for (int i = 0; i < n; ++i)
    {
        const ld X = x[i];
        const ld v = X * k;
        const long long j = std::fabs (v) >= 2147483648.0L ? -2147483648LL : x87Int (v); // out of range: "indefinite"
        const long long aj = std::min<long long> (j < 0 ? -j : j, N);
        const ld t = j < 0 ? -(ld) T[aj] : (ld) T[aj];
        x[i] = (float) (X * a + t * w);
    }
}

// FL's render kernel: truncated index, linear interpolation, every rounding toward zero
[[gnu::noinline]] void kernelRender (float* x, int n, const std::int16_t* T, const Gains& g) noexcept
{
    const X87Mode mode (true);
    const ld k = (ld) g.drive * (ld) N, w = (ld) g.wet * 3.0517578125e-05L, a = g.dry;
    for (int i = 0; i < n; ++i)
    {
        const ld X = x[i];
        const ld v = X * k;
        ld t = 0.0L; // an index out of the 32-bit range gives 0
        if (std::fabs (v) < 2147483648.0L)
        {
            const long long j = x87Int (v);
            const ld fr = v - (ld) j;
            const long long aj = std::min<long long> (j < 0 ? -j : j, N);
            const ld t0 = T[aj], t1 = T[aj + 1];
            t = j >= 0 ? t0 + (t1 - t0) * fr : -(t0 - (t1 - t0) * fr);
        }
        x[i] = (float) (X * a + t * w);
    }
}
} // namespace

std::optional<State> parse (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 20)
        return std::nullopt;
    State s;
    s.pre = i32At (state, 0);
    s.threshold = i32At (state, 4);
    s.type = i32At (state, 8);
    s.mix = i32At (state, 12);
    s.post = i32At (state, 16);
    return s;
}

void toParams (const State& s, float* p) noexcept
{
    p[0] = norm (s.pre, 64, 192);
    p[1] = norm (s.threshold, 1, 10);
    p[2] = norm (s.type, 0, 1);
    p[3] = norm (s.mix, 0, 128);
    p[4] = norm (s.post, 0, 128);
}

State fromParams (const float* p) noexcept
{
    return State { rawFrom (p[0], 64, 192), rawFrom (p[1], 1, 10), rawFrom (p[2], 0, 1), rawFrom (p[3], 0, 128),
                   rawFrom (p[4], 0, 128) };
}

const char* paramName (int param) noexcept
{
    static const char* names[params] { "Pre amp", "Threshold", "Type", "Mix", "Post gain" };
    return param >= 0 && param < params ? names[param] : "";
}

std::string paramText (int param, float v)
{
    char text[32];
    switch (param)
    {
        case 0: return dbText (levelLaw ((float) rawFrom (v, 64, 192) * 0.0078125f));
        case 1: std::snprintf (text, sizeof (text), "%d", rawFrom (v, 1, 10)); return text;
        case 2: return rawFrom (v, 0, 1) != 0 ? "B" : "A";
        case 3: std::snprintf (text, sizeof (text), "%d%%", (int) std::lround (100.0 * rawFrom (v, 0, 128) / 128.0)); return text;
        case 4: return dbText (levelLaw ((float) rawFrom (v, 0, 128) * 0.0078125f));
        default: return {};
    }
}

Gains gains (const State& s) noexcept
{
    const float pre = levelLaw ((float) s.pre * 0.0078125f), post = levelLaw ((float) s.post * 0.0078125f),
                mix = levelLaw ((float) s.mix * 0.0078125f);
    Gains g;
    g.wet = mix * post;
    g.dry = ((post - g.wet) * pre) * post; // post enters the dry path twice, as in FL
    g.drive = pre;
    return g;
}

const std::int16_t* curve (int set, int c) noexcept
{
    // FL indexes its tables with the raw values; out-of-range states are clamped here
    return curves().t[(std::size_t) std::clamp (set, 0, 1)][(std::size_t) std::clamp (c, 1, 10) - 1].data();
}

void hostShaper (float* x, int n, int set, int c, float dry, float wet, float drive, bool render) noexcept
{
    Gains g;
    g.dry = dry;
    g.wet = wet;
    g.drive = drive;
    if (render)
        kernelRender (x, n, curve (set, c), g);
    else
        kernelLive (x, n, curve (set, c), g);
}

void process (float* x, int n, const State& s, bool render) noexcept
{
    const auto g = gains (s);
    const auto* T = curve (s.type, s.threshold);
    if (render)
        kernelRender (x, n, T, g);
    else
        kernelLive (x, n, T, g);
}

namespace
{
class FastDistEffect final : public Effect
{
public:
    FastDistEffect() { curves(); } // builds the tables off the audio thread
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = fromParams (params.data());
        const bool render = offlineRendering();
        fastdistfx::process (left, n, s, render);
        fastdistfx::process (right, n, s, render);
    }
};
} // namespace

std::unique_ptr<Effect> make() { return std::make_unique<FastDistEffect>(); }
} // namespace fastdistfx

// ======================================================================================================= Flangus
namespace flangusfx
{
namespace
{
// the plugin's 20 per-voice spread factors (§1.3)
constexpr float spreadFactor[20] { 0.967f, 1.039f, 1.021f, 0.1013f, 0.977f, 1.033f, 0.997f, 0.971f, 1.061f, 0.947f,
                                   1.031f, 0.1009f, 1.063f, 0.953f, 0.991f, 1.049f, 0.983f, 1.051f, 1.019f, 1.069f };
constexpr int maxVoices = 2 * maxOrder;
constexpr int engineBlock = 512; // the effect's FL-sized engine calls
constexpr int maxBlock = 4096;   // the longest engine call

struct Voice
{
    float d0 = 0, gl = 0, gr = 0, center = 0;
    std::uint32_t phase = 0, start = 0, rate = 0;
    int ch = 0;
    float s8 = 0, s9 = 0, s10 = 0;
};

// framework gain ramp (§1.5): start gain and per-frame step; the state jumps to the (possibly limited) target
void ramp (float& state, float target, float maxStep, int n, float& start, float& step) noexcept
{
    start = state;
    step = target - start;
    if (bitsOf (step) == 0)
        return;
    state = target;
    step = step / (float) n;
    if (std::fabs (step) > maxStep)
    {
        step = std::copysign (maxStep, step);
        state = start + step * (float) n;
        if ((bitsOf (state) & 0x7fffffffu) < 0x33800000u)
        {
            state = 0.0f;
            step = -start / (float) n;
        }
    }
}

// per-frame gains of the framework's SSE ramp: frame pairs step by 2s (lane 0 from g, lane 2 from g + s); frames past
// the last multiple of 8 continue lane 0 by s
void rampGains (float g, float s, int n, float* out) noexcept
{
    const float s2 = s + s;
    float a = g, b = g + s;
    const int m = n / 8 * 8;
    for (int f = 0; f < m; f += 2)
    {
        out[f] = a;
        out[f + 1] = b;
        a = a + s2;
        b = b + s2;
    }
    for (int f = m; f < n; ++f)
    {
        out[f] = a;
        a = a + s;
    }
}

float controlGain (int raw) noexcept
{
    const float x = (float) raw * 0.0078125f;
    const float a = (float) std::fabs ((double) x);
    const float g = levelLaw (a);
    const auto b = bitsOf (x);
    const int sign = (b << 1) == 0 ? 0 : ((b >> 31) != 0 ? -1 : 1);
    return g * (float) sign;
}
} // namespace

struct Engine::Impl
{
    State p {};
    int fs = 44100, block = 0;
    float maxStep = 0.001f;
    float spread = 0.0f, lastSpread = 0.0f;
    long long size = 0; // FL's delay-line size in bytes (its changes clear the line)
    int maxd = 0;
    std::array<Voice, maxVoices> v {};
    int count = 0;
    float dry = 0, wet = 0, dryState = 0, wetL = 0, wetR = 0;
    std::vector<float> line; // interleaved stereo ring
    int cap = 0, w = 0;
    std::array<float, 2 * maxBlock> acc {};
    std::array<float, maxBlock> g1 {}, g2 {};

    void allocate (int sampleRate)
    {
        // the longest delay a voice can reach at this rate (§1.3: 2·(depth + 10) + spread-blended base, ≤ 7245 frames
        // at 44.1 kHz) plus the longest engine call
        const int c = (int) std::ceil (7300.0 * sampleRate / 44100.0) + maxBlock + 64;
        if (c > cap)
        {
            cap = c;
            line.assign (2 * (std::size_t) cap, 0.0f);
            w = 0;
        }
    }

    void clearLine() noexcept
    {
        std::fill (line.begin(), line.end(), 0.0f);
        for (int i = 0; i < count; ++i)
            v[(std::size_t) i].phase = v[(std::size_t) i].start;
    }

    void setup() noexcept
    {
        // the engine values (§1.2)
        const int order = std::max (p[0], 1);
        const float depth = (float) ((double) p[1] * (1.0 / 1024.0)), speed = (float) ((double) p[2] * (1.0 / 1024.0));
        const float delay = (float) p[3] * 0.0078125f, cross = (float) p[5] * 0.0078125f;
        spread = (float) p[4] * 0.0078125f;
        const double ln11 = std::log (11.0), ln101 = std::log (101.0), ln1001 = std::log (1001.0);
        const float D = (float) (std::exp ((double) depth * ln11) - 1.0) * (1092.5f / 10.0f);
        const float a = 1.0f - speed;
        const float P = (float) (std::exp ((double) a * ln101) - 1.0);
        const double R = (2147483648.0 / ((double) P * 75000.0 + 50000.0)) * 500.0;
        const float B = (float) (std::exp ((double) delay * ln11) - 1.0) * (4410.0f / 10.0f);
        const float wsp = (float) (std::exp ((double) spread * ln1001) - 1.0) / 1000.0f;
        const double bw = (double) wsp, bw1 = (double) (1.0f - wsp);
        auto blend = [bw, bw1] (double x, double y) noexcept { return x * bw + y * bw1; };

        const int cnt = std::min (2 * order, maxVoices);
        for (int i = count; i < cnt; ++i)
            v[(std::size_t) i] = Voice {}; // new voices of a longer list start zeroed (their phase too)
        count = cnt;
        float mx = 0.0f, sum = 0.0f;
        const float fsr = (float) (44100.0 / (double) fs);
        for (int i = 0; i < cnt; ++i)
        {
            auto& u = v[(std::size_t) i];
            float fi;
            if (i < order)
            {
                fi = (float) i;
                u.ch = 0;
                u.start = 0;
            }
            else
            {
                fi = (float) (cnt - i - 1);
                u.ch = 1;
                u.start = 0x80000000u;
            }
            const float k = 1.0f - fi / (float) order;
            const float r = spreadFactor[i % 20];
            const float rr = (float) blend ((double) (r * r), 1.0);
            u.gl = 1.0f;
            u.gr = cross * k;
            if (u.ch != 0)
                std::swap (u.gl, u.gr);
            sum = (float) ((double) sum + std::fabs ((double) u.gl) + std::fabs ((double) u.gr));
            u.d0 = D * k + 10.0f;
            const double c0 = R / (double) (D + 10.0f);
            const double rate = blend (R * (double) rr / (double) u.d0, c0);
            u.center = (float) blend ((double) ((B * k) * rr), (double) B);
            const float q = ((k * wsp) * 2147483648.0f) / 256.0f;
            u.start += (std::uint32_t) ((std::uint32_t) std::llrint ((double) q) << 8);
            u.rate = (std::uint32_t) std::llrint (rate * (double) fsr);
            u.d0 = u.d0 / fsr;
            float d3 = u.center / fsr;
            if (! (d3 > 8.0f))
                d3 = 8.0f;
            u.center = u.d0 + d3;
            const float m = u.center + u.d0;
            if (! (mx > m))
                mx = m;
            const double dd = std::sqrt ((double) (u.d0 * 4.0f));
            u.s8 = (float) (dd / 2147483648.0);
            u.s9 = (float) dd;
            u.s10 = (float) (dd / 2.0);
        }
        const float gsum = (float) (std::exp ((double) (1.0f - 1.0f / sum) * ln1001) - 1.0) / 1000.0f;
        const float nm = (1.0f - gsum) * 1.0f;
        for (int i = 0; i < cnt; ++i)
        {
            v[(std::size_t) i].gl = v[(std::size_t) i].gl * nm;
            v[(std::size_t) i].gr = v[(std::size_t) i].gr * nm;
        }
        maxd = (int) std::llrint ((double) mx) + 8;
        const long long sz = ((long long) block + std::max (maxd, 1)) * 8;
        const bool resized = sz != size;
        size = sz;
        if (resized || ! (spread == lastSpread))
        {
            clearLine();
            lastSpread = spread;
        }
    }

    void setParam (int i, int raw) noexcept
    {
        p[(std::size_t) i] = raw;
        if (i < 6)
            setup();
        else if (i == 6)
            dry = controlGain (raw);
        else
            wet = controlGain (raw);
    }

    [[gnu::noinline]] void voices (int n) noexcept
    {
        const X87Mode mode (true);
        const long long reach = cap - n - 2;
        for (int vi = 0; vi < count; ++vi)
        {
            auto& u = v[(std::size_t) vi];
            const ld s8 = u.s8, s9 = u.s9, s10 = u.s10, ctr = u.center, gl = u.gl, gr = u.gr;
            for (int i = 0; i < n; ++i)
            {
                const ld ph = (ld) (std::int32_t) u.phase;
                u.phase += u.rate;
                const ld t = std::fabs (ph * s8) - s10;
                const ld D = t * (s9 - std::fabs (t)) + ctr;
                const long long j = std::clamp (x87Int (D), 0LL, reach); // toward zero (the control word)
                const ld fr = D - (ld) j;
                long long f0 = (long long) w + i - j;
                if (f0 >= cap)
                    f0 -= cap;
                else if (f0 < 0)
                    f0 += cap;
                const long long f1 = f0 > 0 ? f0 - 1 : cap - 1;
                const ld s0 = line[2 * (std::size_t) f0 + (std::size_t) u.ch], s1 = line[2 * (std::size_t) f1 + (std::size_t) u.ch];
                const ld o = s0 + (s1 - s0) * fr;
                auto& L = acc[2 * (std::size_t) i];
                auto& Rr = acc[2 * (std::size_t) i + 1];
                if (vi == 0)
                {
                    L = (float) (o * gl);
                    Rr = (float) (o * gr);
                }
                else
                {
                    L = (float) (o * gl + (ld) L);
                    Rr = (float) (o * gr + (ld) Rr);
                }
            }
        }
    }

    void render (const float* in, float* out, int n) noexcept
    {
        if (n <= 0)
            return;
        n = std::min (n, maxBlock);
        // input into the delay line
        for (int i = 0; i < n; ++i)
        {
            const int f = (w + i) % cap;
            line[2 * (std::size_t) f] = in[2 * i];
            line[2 * (std::size_t) f + 1] = in[2 * i + 1];
        }
        if (count > 0)
            voices (n);
        else
            std::fill (acc.begin(), acc.begin() + 2 * n, 0.0f);
        w = (w + n) % cap;

        // dry: input × ramped gain
        float g0, st;
        ramp (dryState, dry, maxStep, n, g0, st);
        if (bitsOf (st) == 0)
        {
            if (bitsOf (g0) == 0)
                std::fill (out, out + 2 * n, 0.0f);
            else if (g0 == 1.0f && bitsOf (g0) == 0x3f800000u)
            {
                if (out != in)
                    std::memmove (out, in, sizeof (float) * 2 * (std::size_t) n);
            }
            else
                for (int i = 0; i < 2 * n; ++i)
                    out[i] = in[i] * g0;
        }
        else
        {
            rampGains (g0, st, n, g1.data());
            for (int i = 0; i < n; ++i)
            {
                out[2 * i] = in[2 * i] * g1[(std::size_t) i];
                out[2 * i + 1] = in[2 * i + 1] * g1[(std::size_t) i];
            }
        }

        // wet: voice sum × ramped gains, added
        float l0, ls, r0, rs;
        ramp (wetL, wet, maxStep, n, l0, ls);
        ramp (wetR, wet, maxStep, n, r0, rs);
        if (bitsOf (ls) == 0 && bitsOf (rs) == 0)
        {
            if (bitsOf (l0) != 0 || bitsOf (r0) != 0)
            {
                if (bitsOf (l0) == 0x3f800000u && bitsOf (r0) == 0x3f800000u)
                    for (int i = 0; i < 2 * n; ++i)
                        out[i] = acc[(std::size_t) i] + out[i];
                else
                    for (int i = 0; i < n; ++i)
                    {
                        out[2 * i] = acc[2 * (std::size_t) i] * l0 + out[2 * i];
                        out[2 * i + 1] = acc[2 * (std::size_t) i + 1] * r0 + out[2 * i + 1];
                    }
            }
        }
        else
        {
            rampGains (l0, ls, n, g1.data());
            rampGains (r0, rs, n, g2.data());
            for (int i = 0; i < n; ++i)
            {
                out[2 * i] = acc[2 * (std::size_t) i] * g1[(std::size_t) i] + out[2 * i];
                out[2 * i + 1] = acc[2 * (std::size_t) i + 1] * g2[(std::size_t) i] + out[2 * i + 1];
            }
        }
    }
};

Engine::Engine() : d (std::make_unique<Impl>())
{
    d->allocate (44100);
    d->p = defaults; // the constructor stores every control's default, then sends each through ProcessParam
    for (int i = 0; i < params; ++i)
        d->setParam (i, defaults[(std::size_t) i]);
}
Engine::~Engine() = default;

void Engine::prepare (int sampleRate)
{
    d->allocate (std::max (sampleRate, 1));
    setRate (sampleRate);
}
void Engine::setParam (int index, int raw) noexcept
{
    if (index >= 0 && index < params)
        d->setParam (index, raw);
}
void Engine::load (const State& s) noexcept
{
    for (int i = 0; i < params; ++i)
        d->setParam (i, s[(std::size_t) i]);
}
void Engine::setRate (int sampleRate) noexcept
{
    d->fs = std::max (sampleRate, 1);
    d->maxStep = (float) ((44100.0 / (double) d->fs) * 0.001);
    d->setup();
}
void Engine::setBlockSize (int frames) noexcept
{
    d->block = frames;
    d->setup();
}
void Engine::flush() noexcept
{
    d->dryState = d->dry;
    d->wetL = d->wetR = d->wet;
    d->clearLine();
}
int Engine::param (int index) const noexcept { return d->p[(std::size_t) index]; }
void Engine::render (const float* in, float* out, int n) noexcept { d->render (in, out, n); }

std::optional<State> parse (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 32)
        return std::nullopt;
    State s {};
    for (int i = 0; i < params; ++i)
        s[(std::size_t) i] = i32At (state, 4 * (std::size_t) i);
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
    static const char* names[params] { "Order", "Depth", "Speed", "Delay", "Spread", "Cross", "Dry", "Wet" };
    return param >= 0 && param < params ? names[param] : "";
}

std::string paramText (int param, float v)
{
    char text[32];
    const int raw = rawFrom (v, rawMin[(std::size_t) std::clamp (param, 0, params - 1)], rawMax[(std::size_t) std::clamp (param, 0, params - 1)]);
    switch (param)
    {
        case 0: std::snprintf (text, sizeof (text), "%d", raw); return text;
        case 1: // sweep depth of the first voice, ms at 44.1 kHz
            std::snprintf (text, sizeof (text), "%.1f ms", (std::pow (11.0, raw / 1024.0) - 1.0) * 109.25 / 44.1); return text;
        case 3: std::snprintf (text, sizeof (text), "%.1f ms", (std::pow (11.0, raw / 128.0) - 1.0) * 441.0 / 44.1); return text;
        case 6:
        case 7:
        {
            const float g = controlGain (raw);
            return (g < 0.0f ? "-" : "") + dbText (std::fabs ((double) g));
        }
        case 2: std::snprintf (text, sizeof (text), "%d%%", (int) std::lround (100.0 * raw / 1024.0)); return text;
        default: std::snprintf (text, sizeof (text), "%d%%", (int) std::lround (100.0 * raw / 128.0)); return text;
    }
}

namespace
{
class FlangusEffect final : public Effect
{
public:
    FlangusEffect() { engine.setBlockSize (engineBlock); }
    void prepare (double sampleRate) noexcept override
    {
        engine.prepare ((int) std::lround (sampleRate));
        fresh = true;
    }
    // LYNX's reset (transport jumps) = FL's flush: gains at their targets, delay line cleared, LFOs restarted
    void reset() noexcept override { fresh = true; }
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        // parameter changes go through FL's ProcessParam in control order (unchanged controls would not change the
        // engine); a fresh or reset effect then starts from FL's flushed state rather than ramping in from silence
        const auto s = fromParams (params.data());
        for (int i = 0; i < flangusfx::params; ++i)
            if (s[(std::size_t) i] != engine.param (i))
                engine.setParam (i, s[(std::size_t) i]);
        if (fresh)
        {
            engine.flush();
            fresh = false;
        }
        for (int o = 0; o < n; o += engineBlock)
        {
            const int m = std::min (engineBlock, n - o);
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
    std::array<float, 2 * engineBlock> buf {};
    bool fresh = true;
};
} // namespace

std::unique_ptr<Effect> make() { return std::make_unique<FlangusEffect>(); }
} // namespace flangusfx

// ======================================================================================================= Flanger
namespace flangerfx
{
namespace
{
inline std::int32_t wrap32 (long long v) noexcept { return (std::int32_t) (std::uint32_t) (std::uint64_t) v; }
// the plugin's curve of a normalised control: ((k + 1)·v) / (k·v + 1)
float law (float v, float k) noexcept { return (float) ((((double) k + 1.0) * (double) v) / ((double) (k * v) + 1.0)); }

struct Tables
{
    std::array<std::int32_t, 4096> tri {}, sine {}, dtri {}, dsine {};
    std::array<float, 4096> frac {};
    Tables()
    {
        // triangle: 0 → 1 over a quarter, 1 → −1 over half, −1 → 0 over the last quarter; steps of 1/1024 in double
        double d = 0.0;
        const double st = 0.0009765625;
        int i = 0;
        for (; i < 1024; ++i, d += st)
            tri[(std::size_t) i] = (std::int32_t) (d * 32767.0);
        if (d > 1.0)
            d = 2.0 - d;
        for (; i < 3072; ++i, d -= st)
            tri[(std::size_t) i] = (std::int32_t) (d * 32767.0);
        if (d < -1.0)
            d = -2.0 - d;
        for (; i < 4096; ++i, d += st)
            tri[(std::size_t) i] = (std::int32_t) (d * 32767.0);
        for (i = 0; i < 4096; ++i)
            sine[(std::size_t) i] = (std::int32_t) (std::sin (((double) i * 6.283185307) / 4096.0) * 32767.0);
        for (i = 0; i < 4095; ++i)
        {
            dtri[(std::size_t) i] = tri[(std::size_t) i + 1] - tri[(std::size_t) i];
            dsine[(std::size_t) i] = sine[(std::size_t) i + 1] - sine[(std::size_t) i];
        }
        dtri[4095] = tri[0] - tri[4095];
        dsine[4095] = sine[0] - sine[4095];
        for (i = 0; i < 4096; ++i)
            frac[(std::size_t) i] = (float) ((double) i / 4096.0);
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
    State raw = defaults;
    std::array<float, params> v {};
    int fs = 44100;
    // LFO (24-bit cycle in a 32-bit phase)
    float lfoFs = 44100.0f;
    std::uint32_t pl = 0, pr = 0;
    std::int32_t rate = 0;
    std::array<std::int32_t, 4096> tab {}, dtab {};
    // delay lines
    std::vector<float> bl, br;
    std::uint32_t mask = 0, w = 0;
    int size = 0;
    float d0 = 0, d1 = 0, fb = 0, lfoRate = 0, fbl = 0, fbr = 0, damp = 0, dry = 0, wet = 0, cross = 0;
    std::int32_t c140 = 0, c144 = 0;

    void lfoSet (float r, float phase, bool reset) noexcept
    {
        if (reset)
            pl = 0;
        rate = (std::int32_t) std::llrint ((double) (((r * 4096.0f) / lfoFs) * 4096.0f));
        pr = pl + (std::uint32_t) std::llrint ((double) (phase * 16777216.0f));
    }
    void lfoShape (float s) noexcept
    {
        const auto& t = tables();
        const long long a = std::llrint ((double) (s * 4096.0f)), b = std::llrint ((double) (1.0f - s) * 4096.0);
        for (std::size_t i = 0; i < 4096; ++i)
        {
            tab[i] = wrap32 (a * t.tri[i] + b * t.sine[i]);
            dtab[i] = (std::int32_t) ((long long) wrap32 (a * t.dtri[i] + b * t.dsine[i]) >> 12);
        }
    }
    std::int32_t lfoValue (std::uint32_t p) const noexcept
    {
        const auto i = (std::size_t) ((p >> 12) & 0xfffu);
        return (std::int32_t) ((long long) wrap32 ((long long) tab[i] + (long long) (p & 0xfffu) * dtab[i]) >> 12);
    }
    void resize (int frames)
    {
        int b = 0;
        while (! (frames <= (1 << b)))
            ++b;
        if ((1 << b) != size)
        {
            size = 1 << b;
            mask = (std::uint32_t) size - 1;
            bl.assign ((std::size_t) size, 0.0f); // FL keeps the reallocated memory (not modelled: cleared)
            br.assign ((std::size_t) size, 0.0f);
            w = 0;
        }
    }
    void delays() noexcept
    {
        d0 = law (v[0], -0.8f) * 20.0f;
        d1 = law (v[1], -0.8f) * 20.0f;
        c140 = (std::int32_t) (long long) ((((double) d0 + (double) d1 * 0.5) * (double) fs * 4096.0) / 1000.0);
        c144 = (std::int32_t) (long long) (((double) d1 * 0.5 * (double) fs) / 1000.0);
    }
    void rateSet (bool reset) noexcept
    {
        lfoRate = law (v[2], -0.9f) * 5.0f;
        lfoSet (lfoRate, v[3], reset);
    }
    void setParam (int i, int r) noexcept
    {
        raw[(std::size_t) i] = r;
        double x = (double) r * (1.0 / rawMax[(std::size_t) i]);
        x = ! (0.0 <= x) ? 0.0 : (x > 1.0 ? 1.0 : x);
        v[(std::size_t) i] = (float) x;
        if (i < 2)
            delays();
        else if (i < 4)
            rateSet (false);
        else if (i == 4)
            damp = (float) (1.0 - (double) v[4]);
        else if (i == 5)
            lfoShape (v[5]);
        else if (i < 8)
            fb = (float) ((double) (v[7] > 0.5f ? -v[6] : v[6]) * 0.98);
        else
        {
            const bool inv = (double) v[8] > 0.5;
            wet = inv ? -v[10] : v[10];
            cross = inv ? -v[11] : v[11];
            dry = v[9];
        }
    }
    void setRate (int sampleRate)
    {
        fs = std::max (sampleRate, 1);
        resize ((int) std::llrint (fs / 10.0));
        lfoFs = (float) fs;
        rateSet (false);
        delays();
    }
    void render (const float* in, float* out, int n) noexcept
    {
        const auto& F = tables().frac;
        for (int k = 0; k < n; ++k)
        {
            const float il = in[2 * k], ir = in[2 * k + 1];
            const std::int32_t vl = lfoValue (pl);
            pl += (std::uint32_t) rate;
            const std::int32_t vr = lfoValue (pr);
            pr += (std::uint32_t) rate;
            const auto ul = (std::uint32_t) (std::uint64_t) (((long long) wrap32 ((long long) c144 * vl) >> 3) + c140);
            const auto ur = (std::uint32_t) (std::uint64_t) (((long long) wrap32 ((long long) c144 * vr) >> 3) + c140);
            bl[w] = il + fbl * fb;
            br[w] = ir + fbr * fb;
            const std::uint32_t dl = w - (ul >> 12), dr = w - (ur >> 12);
            float s0 = bl[dl & mask], s1 = bl[(dl - 1) & mask];
            const float xl = s0 + (s1 - s0) * F[ul & 0xfffu];
            s0 = br[dr & mask];
            s1 = br[(dr - 1) & mask];
            const float xr = s0 + (s1 - s0) * F[ur & 0xfffu];
            w = (w + 1) & mask;
            fbl = fbl + damp * (xl - fbl);
            fbr = fbr + damp * (xr - fbr);
            out[2 * k] = (fbl * wet + il * dry) + cross * fbr;
            out[2 * k + 1] = (fbr * wet + ir * dry) + cross * fbl;
        }
    }
};

Engine::Engine() : d (std::make_unique<Impl>())
{
    // the plugin's constructor: LFO at 44.1 kHz, sine shape, delay lines for 44.1 kHz, then every default control
    d->lfoShape (0.0f);
    d->resize (4410);
    d->rateSet (true);
    for (int i = 0; i < params; ++i)
        d->setParam (i, defaults[(std::size_t) i]);
}
Engine::~Engine() = default;
void Engine::prepare (int sampleRate) { d->setRate (sampleRate); }
void Engine::setParam (int index, int raw) noexcept
{
    if (index >= 0 && index < params)
        d->setParam (index, raw);
}
void Engine::load (const State& s) noexcept
{
    for (int i = 0; i < params; ++i)
        d->setParam (i, s[(std::size_t) i]);
}
void Engine::enable() noexcept
{
    std::fill (d->bl.begin(), d->bl.end(), 0.0f);
    std::fill (d->br.begin(), d->br.end(), 0.0f);
    d->w = 0;
    d->rateSet (true);
    d->fbl = d->fbr = 0.0f;
}
int Engine::param (int index) const noexcept { return d->raw[(std::size_t) index]; }
void Engine::render (const float* in, float* out, int n) noexcept { d->render (in, out, n); }

std::optional<State> parse (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 4 + 4 * params || i32At (state, 0) >= 2)
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
    static const char* names[params] { "Delay", "Depth", "Rate", "Phase", "Damp", "Shape", "Feed", "Invert feed",
                                       "Invert wet", "Dry", "Wet", "Cross" };
    return param >= 0 && param < params ? names[param] : "";
}
std::string paramText (int param, float value01)
{
    char text[32];
    const int p = std::clamp (param, 0, params - 1);
    const int raw = rawFrom (value01, 0, rawMax[(std::size_t) p]);
    const float x = (float) ((double) raw / rawMax[(std::size_t) p]);
    switch (param)
    {
        case 0:
        case 1: std::snprintf (text, sizeof (text), "%.2f ms", (double) (law (x, -0.8f) * 20.0f)); return text;
        case 2: std::snprintf (text, sizeof (text), "%.3f Hz", (double) (law (x, -0.9f) * 5.0f)); return text;
        case 3: std::snprintf (text, sizeof (text), "%d°", raw); return text;
        case 7:
        case 8: return x > 0.5f ? "on" : "off";
        default: std::snprintf (text, sizeof (text), "%d%%", (int) std::lround (100.0 * x)); return text;
    }
}

namespace
{
class FlangerEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override { engine.prepare ((int) std::lround (sampleRate)); }
    // LYNX's reset = FL's "enabled" notification: delay lines cleared, LFO restarted
    void reset() noexcept override { engine.enable(); }
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = fromParams (params.data());
        for (int i = 0; i < flangerfx::params; ++i)
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
    Engine engine;
    std::array<float, 1024> buf {};
};
} // namespace

std::unique_ptr<Effect> make() { return std::make_unique<FlangerEffect>(); }
} // namespace flangerfx
} // namespace lynx
