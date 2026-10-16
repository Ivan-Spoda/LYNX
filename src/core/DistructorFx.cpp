#include "core/DistructorFx.h"

#include <algorithm>
#include <cmath>
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
unsigned short x87Pc = 0x200; // 53-bit

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

struct Distortion
{
    int algo = 0;
    int osFactor = 1;
    double fs = 44100.0;
    std::array<int, 36> p {};
    Glide bias, cross; // +0x10, +0x34
    float fdDry = 0, fdWet = 0, fdPre = 0; // Fast dist
    std::vector<float> scratch;

    void setRate (double rate, int factor)
    {
        fs = rate, osFactor = factor;
        const auto k = (float) std::exp (-4.605170185988091 / ((double) (factor * 15) * 0.001 * fs));
        bias.k = cross.k = k;
    }
    void setParam (int idx, int raw) noexcept
    {
        if (idx < 0 || idx >= 36)
            return;
        p[(std::size_t) idx] = raw;
        switch (idx)
        {
            case 7:
                if (algo == 14)
                    setTarget (cross, (float) ((double) p[7] / 5000.0 - 1.0));
                break;
            case 8:
                if (algo == 14)
                    setTarget (bias, (float) ((double) p[8] / 5000.0 - 1.0));
                break;
            case 11:
                if (algo == 15)
                    setTarget (cross, (float) ((double) p[11] / 5000.0 - 1.0));
                break;
            case 12:
                if (algo == 15)
                    setTarget (bias, (float) ((double) p[12] / 5000.0 - 1.0));
                break;
            case 16:
            case 18:
            case 19:
            {
                fdPre = gainLaw (p[16]);
                const auto post = gainLaw (p[18]);
                const auto mix = gainLaw (p[19]);
                fdWet = mix * post;
                fdDry = (post - fdWet) * fdPre * post;
                break;
            }
            default: break;
        }
    }
    static void setTarget (Glide& g, float v) noexcept { g.target[0] = g.target[1] = v; }
    void setAlgorithm (int a) noexcept
    {
        algo = std::clamp (a, 0, 17);
        for (int i = 0; i < 36; ++i)
            setParam (i, p[(std::size_t) i]);
    }

    // FUN_0071e3a0: the asymmetry glide added to the input (algorithms outside 3…13; there only while…)
    void addBias (float* x, int n)
    {
        if (bias.atRest())
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

    void fastDist (float* y, int n) noexcept
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
            const auto r = (long long) std::nearbyint ((double) 0); // placeholder (see below)
            (void) r;
            ld q = x * A;
            long idx = std::lrint (q);
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

    void aperture (const float* x, float* y, int n) noexcept
    {
        const auto pre = (double) p[10] / 500.0 + 1.0;
        const auto thr = std::fabs ((double) p[13] * 0.5 / 10000.0 + 0.5);
        const auto warp = (double) (p[14] * 2) / 10000.0 - 1.0;
        const auto foldMode = p[15] == 0;
        if (thr <= 0.0 || thr > 1.0 || pre == 0.0)
        {
            std::fill (y, y + 2 * n, 0.0f);
            return;
        }
        for (int i = 0; i < 2 * n; ++i)
        {
            auto d = (double) x[i] * pre;
            if (foldMode)
            {
                if (std::fabs (d) > thr)
                {
                    const auto q = d / thr;
                    d = d - (double) (long long) q * thr; // Trunc
                    d = d - (thr + warp);
                }
            }
            else
            {
                const auto s = thr + warp;
                int it = 0;
                bool done = false;
                for (; it < 8; ++it)
                {
                    if (! (std::fabs (d) > thr))
                    {
                        done = true;
                        break;
                    }
                    d = sgn (d) * (std::fabs (d) - s);
                }
                if (! done && std::fabs (d) > thr)
                    d = sgn (d) * thr;
            }
            y[i] = (float) d;
        }
    }

    // FUN_00720150: the selected algorithm, x → y (both n interleaved frames; x may be modified)
    void process (float* x, float* y, int n)
    {
        std::memcpy (y, x, sizeof (float) * 2 * (std::size_t) n);
        switch (algo)
        {
            case 1: fastDist (y, n); break;
            case 15:
                addBias (x, n);
                addCross (x, n);
                aperture (x, y, n);
                break;
            default: break;
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
        s[0] = (double) sl, s[1] = (double) sr, v[0] = (double) vl, v[1] = (double) vr;
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
                float* dst = (k + 1 == upSt.size()) ? hi.data() : tmp.data();
                upSt[k].up (src, dst, cur);
                if (dst == tmp.data() && k + 1 < upSt.size())
                {
                    // ping-pong through hi for the next stage
                    std::memcpy (hi.data(), tmp.data(), sizeof (float) * 2 * (std::size_t) cur * 2);
                    src = hi.data();
                }
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
                float* dst = (k + 1 == downSt.size()) ? x : tmp.data();
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
