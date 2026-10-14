#include "core/HarmlessFx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

// Specified as float32 / double operations in the order of the reference model (REA_rodada36); contraction of a·b + c
// into one FMA would change the roundings, so it stays off in this file.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx::hlfx
{
namespace
{
constexpr double pi = 3.141592653589793;
constexpr float flushLevel = 5.960464477539063e-08f; // 2^-24: FL's denormal guard
const double ln001 = std::log (0.01);

inline float f32 (double x) noexcept { return (float) x; }
inline double rint (double x) noexcept { return std::nearbyint (x); } // round half to even (cvtsd2si)
// FUN_00627cc0 (x87): a double state below 2^-24 in magnitude becomes 0; the others are kept as they are (the
// decompiler shows a float conversion that the instructions do not do: fldl / fcmovb / fstpl, REA_rodada40 §2.4)
inline double flushState (double v) noexcept { return std::fabs (v) < (double) flushLevel ? 0.0 : v; }


// (11^x − 1)/10, exactly 1 at x = 1 (ln 11 as the float of the plugin's run-time global)
float g11 (float x) noexcept
{
    if (x == 1.0f)
        return 1.0f;
    return f32 ((std::exp ((double) x * (double) (float) std::log (11.0)) - 1.0) * (double) 0.1f);
}

// 1 − (5^x − 1)/4 (chorus dry and wet gains)
float g5 (float x) noexcept
{
    return f32 (1.0 - (double) f32 (std::exp ((double) x * std::log (5.0)) - 1.0) * 0.25);
}

// FUN_00612db0: start value, per-sample step (limited to maxStep) and new current value
struct Ramp
{
    double start = 0.0, step = 0.0, end = 0.0;
};

Ramp ramp (double cur, double target, int n, double maxStep) noexcept
{
    const auto d = (double) f32 (target - cur);
    if (d == 0.0)
        return { cur, 0.0, cur };
    auto step = (double) f32 (d / n);
    auto end = target;
    if (std::fabs (step) > maxStep)
    {
        step = std::copysign (maxStep, step);
        end = (double) f32 (cur + step * n);
    }
    return { cur, step, end };
}

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t at) noexcept
{
    std::int32_t v = 0;
    if (at + 4 <= d.size())
        std::memcpy (&v, d.data() + at, 4);
    return v;
}
} // namespace

// =================================================================================================================
// Maximus (REA_rodada36 §2)
// =================================================================================================================

namespace
{
// 2·(501^(v/10000) − 1) ms, kept as the float of (501^x − 1)
double tms (int v) noexcept
{
    return (double) f32 (std::exp ((double) f32 ((float) v * 0.0001f) * std::log (501.0)) - 1.0);
}

// L7: (7^x − 1)/6, exactly 1 at x = 1
float l7 (int v) noexcept
{
    const auto x = (float) v * 0.001f;
    if (x == 1.0f)
        return 1.0f;
    return f32 ((std::exp ((double) (x * (float) std::log (7.0))) - 1.0) * (double) (1.0f / 6.0f));
}

double coefFor (double n) noexcept // 1 % in n samples
{
    return n > 0.0 ? std::exp (ln001 / std::max (1.0, n)) : 0.0;
}

struct Point
{
    double dx = 0.0;
    float y = 0.0f, tension = 0.0f, q = 0.0f, b = 0.0f, c = 0.0f;
    int type = 0, sign = 0;
};

// one drawn envelope (the compression curve) and its incremental evaluation (FUN_007885d0)
class Curve
{
public:
    explicit Curve (std::vector<Point> pts) : p (std::move (pts)) {}

    bool supported() const noexcept
    {
        if (p.empty())
            return false;
        for (std::size_t i = 1; i < p.size(); ++i)
            if (p[i].type != 0 && p[i].type != 7)
                return false;
        return true;
    }

    void start() noexcept
    {
        idx = 0;
        y0 = p[0].y;
        dy = 0.0;
        if (p[0].dx == 0.0)
            rate = 1.0, t = 1.0;
        else
            rate = 1.0 / p[0].dx, t = 0.0;
    }

    double step (double dx) noexcept
    {
        const auto n = (int) p.size();
        t += dx * rate;
        while (t >= 1.0)
        {
            const auto old = idx;
            t = rate == 0.0 ? dx : (t - 1.0) / rate;
            y0 = p[(std::size_t) idx].y;
            if (idx < n - 1)
                ++idx;
            dy = (double) p[(std::size_t) idx].y - y0;
            if (old == idx)
                t = 0.0, rate = 0.0;
            else if (const auto d = p[(std::size_t) idx].dx; d <= 0.0)
                rate = 1.0, t += 1.0;
            else
                rate = 1.0 / d, t *= rate;
        }
        auto s = t;
        const auto& pt = p[(std::size_t) idx];
        if (pt.sign != 2)
        {
            if (pt.type == 0)
            {
                if (pt.q > 0.0f)
                    s = 1.0 - (std::exp ((1.0 - s) * (double) pt.c) - 1.0) * (double) pt.b;
                else if (pt.q < 0.0f)
                    s = (std::exp (s * (double) pt.c) - 1.0) * (double) pt.b;
            }
            else // type 7
            {
                const auto u = pt.sign > 0 ? 1.0 - s : s;
                const auto u3 = u * u * u;
                s = s - (u3 * u3 - u) * (double) pt.tension;
            }
        }
        return y0 + s * dy;
    }

private:
    std::vector<Point> p;
    int idx = 0;
    double t = 0.0, rate = 0.0, y0 = 0.0, dy = 0.0;
};
} // namespace

struct MaximusPreset
{
    std::array<int, 59> p {};
    int oversampling = 0; // order: ×2^order
    float scale = 0.975f; // output ceiling of the curves (−0.22 dB)
    struct Band
    {
        std::vector<float> gain;  // static gain (output / input) for input levels i/8192
        int size = 0;             // last entry before the curve reaches the ceiling
        double end = 0.0, peak = 1.0;
        std::vector<float> sat;   // soft-saturation table (mode A), 65 538 entries; empty = none
        float satOut = 0.0f, satIn = 0.0f;
        int satB = 0;             // 1: saturation mode B (REA_rodada40 §2.2) with threshold satT and range satR
        float satT = 0.0f, satR = 1.0f, satK = 1.0f;
        int mode = 3;             // effective mode (solo applied, FUN_008417e0)
        bool rms = false;         // envelope mode RMS (REA_rodada40 §2.1)
        float pre = 1.0f, post = 1.0f, g20 = 1.0f;
        std::array<float, 4> matrix { 1.0f, 1.0f, 0.0f, 0.0f };
    };
    std::array<Band, 4> band {};
    bool valid = false;
    MaximusBuild build = MaximusBuild::shared;
};



namespace
{
std::vector<Point> readCurve (const std::vector<std::uint8_t>& d, std::size_t& pos, bool& ok)
{
    std::vector<Point> pts;
    const auto n = i32At (d, pos + 8);
    if (n < 1 || n > 4096 || pos + 12 + 24 * (std::size_t) n + 20 > d.size())
    {
        ok = false;
        return pts;
    }
    for (int k = 0; k < n; ++k)
    {
        const auto at = pos + 12 + 24 * (std::size_t) k;
        double dx = 0.0, y = 0.0;
        float ten = 0.0f;
        std::memcpy (&dx, d.data() + at, 8);
        std::memcpy (&y, d.data() + at + 8, 8);
        std::memcpy (&ten, d.data() + at + 16, 4);
        Point pt;
        pt.dx = std::isfinite (dx) && dx > 0.0 ? dx : 0.0;
        pt.y = std::isfinite (y) ? (float) y : 0.0f;
        pt.tension = std::isfinite (ten) ? ten : 0.0f;
        pt.type = d[at + 20];
        pt.sign = pt.tension == 0.0f ? 0 : (pt.tension < 0.0f ? -1 : 1);
        if (k > 0)
        {
            if (pt.type < 2)
            {
                auto q = f32 (std::exp ((double) std::fabs (pt.tension) * std::log (1000001.0)) - 1.0);
                if (pt.tension < 0.0f)
                    q = -q;
                pt.q = q;
                if (q != 0.0f)
                {
                    pt.b = f32 (1.0 / std::fabs ((double) q));
                    pt.c = f32 (std::log (std::fabs ((double) q) + 1.0));
                }
            }
            if ((pt.type == 0 || pt.type == 7) && pt.tension == 0.0f)
                pt.sign = 2; // straight segment
        }
        pts.push_back (pt);
    }
    pos += 12 + 24 * (std::size_t) n + 20;
    return pts;
}

// FUN_00845b50
void buildGainTable (Curve& curve, float scale, MaximusPreset::Band& b, bool soundgoodizer)
{
    if (soundgoodizer)
    {
        // Soundgoodizer_x64.dll's older build (FUN_0083b050, REA_rodada40 §3.2): the saturated output snaps to the
        // double 0.975 (not the float), the stored end value is the last output before the snap and the peak is
        // divided by the float 0.975
        curve.start();
        b.gain.assign (32770, 0.0f);
        const auto dv9 = 0.5 / std::log (3.0), dv10 = std::log (3.0) * 2.0, half = 0.48750001192092896;
        double prev = 0.0, out = 0.0, mx = 0.0, last = 0.0;
        int lastOk = 0;
        for (int i = 0; i < 32769; ++i)
        {
            const auto lv = (double) std::max (i, 1);
            const auto c = std::log (lv * 0.0001220703125 * 2.0 + 1.0) * dv9;
            const auto y = curve.step (c - prev);
            prev = c;
            out = (std::exp (y * dv10) - 1.0) * half;
            auto idx = i;
            auto keep = out;
            if (std::fabs (out - 0.975) < 5.960464477539063e-08)
                out = 0.975, idx = lastOk, keep = last;
            mx = std::max (mx, out);
            b.gain[(std::size_t) i] = f32 (out / (lv * 0.0001220703125));
            lastOk = idx;
            last = keep;
        }
        b.gain[(std::size_t) lastOk + 1] = b.gain[(std::size_t) lastOk];
        b.gain.resize ((std::size_t) lastOk + 2);
        b.size = lastOk;
        b.end = last;
        b.peak = mx / 0.9750000238418579;
        return;
    }
    curve.start();
    b.gain.assign (32770, 0.0f);
    const auto dv9 = 0.5 / std::log (3.0), dv10 = std::log (3.0) * 2.0, dv14 = (double) scale * 0.5;
    double prev = 0.0, out = 0.0, mx = 0.0;
    int lastOk = 0;
    for (int i = 0; i < 32769; ++i)
    {
        const auto lv = (double) std::max (i, 1);
        const auto c = std::log (lv * 0.0001220703125 * 2.0 + 1.0) * dv9;
        const auto y = curve.step (c - prev);
        prev = c;
        out = (std::exp (y * dv10) - 1.0) * dv14;
        auto idx = i;
        if (std::fabs (out - (double) scale) < 5.960464477539063e-08)
            out = (double) scale, idx = lastOk;
        mx = std::max (mx, out);
        b.gain[(std::size_t) i] = f32 (out / (lv * 0.0001220703125));
        lastOk = idx;
    }
    b.gain[(std::size_t) lastOk + 1] = b.gain[(std::size_t) lastOk];
    b.gain.resize ((std::size_t) lastOk + 2);
    b.size = lastOk;
    b.end = out;
    b.peak = mx / (double) scale;
}

// FUN_00841380, mode A (threshold < 0)
void buildSaturation (int thr, float ceil, double top, MaximusPreset::Band& b, bool soundgoodizer)
{
    const auto p = (double) f32 ((float) (20.0f - f32 ((float) std::log ((double) f32 (f32 (thr * -0.1) + 1.0f)) / std::log (101.0)) * 19.0f));
    const auto ip = 1.0 / p;
    auto d = 1.0;
    if (ceil > 0.0f)
        d = top / ((double) ceil * 65536.0);
    const auto norm = 1.0 / std::pow (std::atan (std::pow (d * 65536.0, p)), ip);
    b.sat.assign (65538, 0.0f);
    for (int i = 0; i <= 65536; ++i)
    {
        auto v = std::pow (std::atan (std::pow (i * d, p)), ip) * norm;
        if (soundgoodizer)
        {
            // FUN_00837d90: a 16-bit table, round(v·32767) clamped to ±32767, read back ×ceiling/32768
            const auto q = std::clamp (std::nearbyint (v * 32767.0), -32767.0, 32767.0);
            b.sat[(std::size_t) i] = (float) q;
            continue;
        }
        if (v < 5.960464477539063e-08)
            v = 0.0;
        b.sat[(std::size_t) i] = (float) v;
    }
    b.sat[65537] = b.sat[65536];
    b.satOut = soundgoodizer ? f32 ((double) ceil * 3.0517578125e-05) : ceil;
    b.satIn = f32 (1.0 / top);
}
} // namespace

std::optional<std::vector<std::uint8_t>> maximusStateFromFst (const std::vector<std::uint8_t>& b)
{
    if (b.size() < 22 || std::memcmp (b.data(), "FLhd", 4) != 0)
        return std::nullopt;
    auto pos = 8 + (std::size_t) std::max (0, i32At (b, 4));
    if (pos + 8 > b.size() || std::memcmp (b.data() + pos, "FLdt", 4) != 0)
        return std::nullopt;
    pos += 8;
    while (pos < b.size())
    {
        const auto id = b[pos++];
        std::size_t len = 0;
        if (id < 64)
            len = 1;
        else if (id < 128)
            len = 2;
        else if (id < 192)
            len = 4;
        else
        {
            int shift = 0;
            while (pos < b.size())
            {
                const auto c = b[pos++];
                len |= (std::size_t) (c & 0x7f) << shift;
                shift += 7;
                if (! (c & 0x80) || shift > 28)
                    break;
            }
        }
        if (pos + len > b.size())
            return std::nullopt;
        if (id == 0xD5)
            return std::vector<std::uint8_t> (b.begin() + (std::ptrdiff_t) pos, b.begin() + (std::ptrdiff_t) (pos + len));
        pos += len;
    }
    return std::nullopt;
}

std::shared_ptr<const MaximusPreset> makeMaximusPreset (const std::vector<std::uint8_t>& d)
{
    return makeMaximusPreset (d, MaximusBuild::shared);
}

std::shared_ptr<const MaximusPreset> makeMaximusPreset (const std::vector<std::uint8_t>& d, MaximusBuild build)
{
    const auto sgz = build == MaximusBuild::soundgoodizer;
    if (sgz && (std::uint32_t) i32At (d, 0) > 13)
        return nullptr; // that build's loader stops after the fixed block (no v14+ options)
    if (d.size() < 0x1fe + 12)
        return nullptr;
    auto m = std::make_shared<MaximusPreset>();
    m->build = build;
    const auto header = (std::uint32_t) i32At (d, 0);
    const auto version = header > 0xFFFFu ? (int) (header >> 16) : (int) header;
    if (version < 3)
        return nullptr; // the pre-v3 piecewise layout is not used by any preset or demo state
    // fixed block read raw into the object (FUN_0083ef50): 0x1FA bytes for v ≥ 7, 0x1F9 (v6), 0x1F5 (v5), 0x1F1 (v4),
    // 0x1EE (v3); the bytes the block does not reach keep the plugin's reset values (0)
    const std::size_t block = version >= 7 ? 0x1fa : version == 6 ? 0x1f9 : version == 5 ? 0x1f5 : version == 4 ? 0x1f1 : 0x1ee;
    if (d.size() < 4 + block)
        return nullptr;
    auto byteAt = [&] (std::size_t at) { return at < 4 + block ? (int) d[at] : 0; };
    for (int i = 0; i < 59; ++i)
        m->p[(std::size_t) i] = i32At (d, 4 + 4 * (std::size_t) i);
    m->oversampling = byteAt (0x1f4) | (byteAt (0x1f5) << 8) | (byteAt (0x1f6) << 16) | (byteAt (0x1f7) << 24);
    if (m->oversampling < 0 || m->oversampling > 1)
        return nullptr; // only ×1 and ×2 are used by the presets and the demo projects
    // menu options read with the block: "Master Mid mode" (+0x324) and the legacy linear-phase FIR crossover (+0x325)
    // are not decoded for rendering (REA_rodada40 §2.5); solo (+0x317) of the editor's band (+0x308) is
    if (byteAt (0x1fc) != 0 || byteAt (0x1fd) != 0)
        return nullptr;
    const auto solo = byteAt (0x1ef) != 0;
    const auto soloBand = i32At (d, 0x1e0);
    std::size_t pos = 4 + block;
    if (version >= 14)
    {
        if ((d[pos + 8] & 0x40) != 0)
            m->scale = 1.0f;
        if ((d[pos + 8] & 0x01) != 0)
            return nullptr; // linear-phase band split through Frequency Splitter's engine_x64.dll: not decoded
        pos += 9 + 20;
    }
    if (version >= 15)
        pos += 4;
    std::array<std::vector<Point>, 8> env;
    bool ok = true;
    for (auto& e : env)
        if (ok)
            e = readCurve (d, pos, ok);
    if (! ok)
        return nullptr;

    const auto& p = m->p;
    for (int b = 0; b < 4; ++b)
    {
        auto q = [&] (int j) { return p[(std::size_t) (13 * b + j)]; };
        auto& B = m->band[(std::size_t) b];
        const auto mode = q (7);
        B.pre = l7 (q (8));
        B.post = l7 (q (9));
        // stereo separation (FUN_00616320 with pan 0, volume 1): positive narrows toward mono, negative widens
        if (const auto sep = (float) q (10) * 0.001f; sep != 0.0f)
        {
            auto s = sep < 0.0f ? sep : f32 ((double) f32 (std::exp ((double) sep * std::log (5.0)) - 1.0) * 0.25);
            s = std::min (1.0f, s);
            const auto h = s * 0.5f;
            B.matrix = { 1.0f - h, 1.0f - h, h, h };
        }
        Curve curve (env[(std::size_t) (2 * b)]);
        if (! curve.supported())
            return nullptr;
        buildGainTable (curve, m->scale, B, sgz);
        B.g20 = (B.pre * B.post) * B.gain[0];
        // effective mode (FUN_008417e0): with solo on and another band selected, the master is at least
        // "compression off" and the other bands are muted
        B.mode = mode;
        if (solo && soloBand != 3 && b != soloBand)
            B.mode = std::max (B.mode, b == 3 ? 1 : 2);
        B.rms = q (19) != 0;
        const auto thr = q (11);
        if (thr < 0)
        {
            const auto top0 = (q (19) == 0 && mode < 1) ? B.peak : 4.0;
            const auto ceil = l7 (q (12));
            buildSaturation (thr, ceil, std::max (top0 * (double) B.post, (double) ceil), B, sgz);
        }
        else if (thr > 0)
        {
            // mode B (FUN_00841380 → FUN_00629fd0): threshold t = (1 − thr/1000)·ceiling, range = ceiling − t
            const auto ceil = l7 (q (12));
            B.satB = 1;
            B.satT = f32 ((double) (1.0f - (float) thr * 0.001f) * (double) ceil);
            B.satR = ceil - B.satT;
            if (B.satR <= 0.0f)
                B.satR = 1.5258789e-05f;
            B.satK = 1.0f / B.satR;
        }
    }
    m->valid = true;
    return m;
}

namespace
{
// FUN_00624580 (types 1 low-pass, 4 high-pass, 5 all-pass) + FUN_00624e90 (stereo direct form I in double, the
// states flushed below 2^-24 at the end of every block, kept in double)
struct Biquad
{
    int type = 1;
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<double, 8> s {}; // per channel: x1 x2 y1 y2

    void design (int kind, double w, double alpha) noexcept
    {
        type = kind;
        const auto sn = std::sin (w), cs = std::cos (w);
        const auto a = alpha * sn;
        const auto norm = 1.0 / (a + 1.0);
        a1 = norm * cs * -2.0;
        a2 = norm * (1.0 - a);
        if (kind == 1)
            b1 = norm * (1.0 - cs), b0 = b1 * 0.5, b2 = b0;
        else if (kind == 4)
            b1 = norm * -(cs + 1.0), b0 = -b1 * 0.5, b2 = b0;
        else
            b1 = a1, b0 = a2, b2 = 1.0;
    }

    void reset() noexcept { s = {}; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        // both channels in one loop (two independent recursions; the same arithmetic per channel)
        auto lx1 = s[0], lx2 = s[1], ly1 = s[2], ly2 = s[3], rx1 = s[4], rx2 = s[5], ry1 = s[6], ry2 = s[7];
        for (int i = 0; i < n; ++i)
        {
            const auto xl = (double) inL[i], xr = (double) inR[i];
            const auto yl = xl * b0 + (((lx1 * b1 - ly2 * a2) + lx2 * b2) - ly1 * a1);
            const auto yr = xr * b0 + (((rx1 * b1 - ry2 * a2) + rx2 * b2) - ry1 * a1);
            outL[i] = (float) yl, outR[i] = (float) yr;
            lx2 = lx1, lx1 = xl, ly2 = ly1, ly1 = yl;
            rx2 = rx1, rx1 = xr, ry2 = ry1, ry1 = yr;
        }
        s = { flushState (lx1), flushState (lx2), flushState (ly1), flushState (ly2),
              flushState (rx1), flushState (rx2), flushState (ry1), flushState (ry2) };
    }
};

// alpha = sin·k: order 0 (12 dB stage) k = 1, order 1 (24 dB, two stages) k = 1/√2
double crossoverAlpha (int order) noexcept { return order == 1 ? 0.70710678118654746 : 1.0; }

// one 2:1 stage (FUN_0083a9c0 up, FUN_0083ab90 down): two cascades of three first-order all-passes
struct Halfband
{
    static constexpr double ka[3] { 0.14737711360104661, 0.48246854276970014, 0.88300502576937312 };
    static constexpr double kb[3] { 0.039151597734460045, 0.30264684832849342, 0.6746159185469639 };
    std::array<double, 24> s {}; // per channel 12: A outputs, A inputs, B outputs, B inputs

    void reset() noexcept { s = {}; }

    static double chain (double* st, double x, const double* k) noexcept
    {
        const auto y1 = st[3] + (x - st[0]) * k[0];
        st[3] = x;
        const auto y2 = st[4] + (y1 - st[1]) * k[1];
        st[4] = y1;
        const auto y3 = st[5] + (y2 - st[2]) * k[2];
        st[5] = y2;
        st[0] = y1, st[1] = y2, st[2] = y3;
        return y3;
    }

    void endBlock() noexcept
    {
        for (int c = 0; c < 2; ++c)
            for (int k : { 0, 1, 2, 6, 7, 8 })
                s[(std::size_t) (12 * c + k)] = flushState (s[(std::size_t) (12 * c + k)]);
    }

    void up (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        const float* in[2] { inL, inR };
        float* out[2] { outL, outR };
        for (int c = 0; c < 2; ++c)
        {
            auto* st = s.data() + 12 * c;
            for (int i = 0; i < n; ++i)
            {
                const auto v = (double) in[c][i];
                const auto a = chain (st, v, ka);
                const auto b = chain (st + 6, v, kb);
                out[c][2 * i] = (float) b;
                out[c][2 * i + 1] = (float) a;
            }
        }
        endBlock();
    }

    void down (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept // n output frames
    {
        const float* in[2] { inL, inR };
        float* out[2] { outL, outR };
        for (int c = 0; c < 2; ++c)
        {
            auto* st = s.data() + 12 * c;
            for (int i = 0; i < n; ++i)
            {
                const auto a = chain (st, (double) in[c][2 * i], ka);
                const auto b = chain (st + 6, (double) in[c][2 * i + 1], kb);
                out[c][i] = (float) ((b + a) * 0.5);
            }
        }
        endBlock();
    }
};

// FUN_00605410 (x87): one-pole on a control signal, c0 while the input is ≥ the state, c1 otherwise. The state stays
// on the x87 stack (64-bit mantissa) through the block, the coefficients are doubles and only the output samples are
// rounded to float; between blocks the state is kept as a double, 0 below 2^-24 (REA_rodada40 §2.4). long double is
// the 80-bit x87 format on the x86-64 GCC/Clang/MinGW targets LYNX is built for.
struct Smoother
{
    double c0 = 0.0, c1 = 0.0, s = 0.0;

    void run (float* x, int n) noexcept
    {
        long double st = s;
        if (c0 == c1)
        {
            const long double c = c0;
            for (int i = 0; i < n; ++i)
            {
                const long double v = x[i];
                st = (st - v) * c + v;
                x[i] = (float) st;
            }
        }
        else
        {
            const long double k0 = c0, k1 = c1;
            for (int i = 0; i < n; ++i)
            {
                const long double v = x[i];
                const auto c = v >= st ? k0 : k1;
                st = (st - v) * c + v;
                x[i] = (float) st;
            }
        }
        s = std::fabs ((double) st) > (double) flushLevel ? (double) st : 0.0;
    }
};

// FUN_00629710 + FUN_007806b0: max(|L|, |R|), then the running maximum over the last W samples
struct PeakWindow
{
    int W = 1, pos = 0, head = 0, tail = 0, count = 0;
    std::vector<std::int32_t> ring, dq;

    void setLength (int w)
    {
        W = std::max (1, w);
        ring.assign ((std::size_t) W, 0);
        dq.assign ((std::size_t) W, 0);
        pos = 0, head = W > 1 ? 1 : 0, tail = 0, count = 0;
    }

    void run (const float* L, const float* R, float* out, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            out[i] = std::max (std::fabs (L[i]), std::fabs (R[i]));
        if (W <= 1)
            return;
        for (int i = 0; i < n; ++i)
        {
            std::int32_t x = 0;
            std::memcpy (&x, out + i, 4);
            auto next = pos + 1;
            if (next >= W)
                next = 0;
            const auto old = count < 1 ? ring[(std::size_t) pos] : ring[(std::size_t) dq[(std::size_t) head]];
            if (ring[(std::size_t) pos] < x)
            {
                while (count > 0)
                {
                    if (x <= ring[(std::size_t) dq[(std::size_t) tail]])
                    {
                        const auto j = dq[(std::size_t) head] - 1;
                        if (next == j || next == j + W)
                        {
                            --count;
                            if (++head >= W)
                                head = 0;
                        }
                        break;
                    }
                    --count;
                    if (--tail < 0)
                        tail = W - 1;
                }
            }
            else
            {
                if (++tail >= W)
                    tail = 0;
                dq[(std::size_t) tail] = pos;
                const auto j = dq[(std::size_t) head] - 1;
                if (next == j || next == j + W)
                {
                    if (++head >= W)
                        head = 0;
                }
                else
                    ++count;
            }
            ring[(std::size_t) next] = x;
            if (x < old)
                x = old;
            std::memcpy (out + i, &x, 4);
            pos = next;
        }
    }
};

// the RMS detector (envelope mode 1, REA_rodada40 §2.1; FUN_00780b90 with the window kernel FUN_00780a60): L² + R²
// summed over the last W samples in an x87 running sum (kept as a double between blocks), scaled by 1/(2W), clamped
// below at 0 (ippsThreshold_LT_32f_I) and square-rooted (ippsSqrt_32f_I)
struct RmsWindow
{
    int W = 1, idx = 0;
    double sum = 0.0;
    float scale = 0.5f;
    std::vector<float> ring { 0.0f };

    void setLength (int w)
    {
        W = std::max (1, w);
        ring.assign ((std::size_t) W, 0.0f);
        scale = (float) (1.0 / (double) (W * 2));
        idx = 0;
        sum = 0.0;
    }

    void run (const float* L, const float* R, float* out, int n) noexcept
    {
        // the window kernel runs on the x87 stack: the sum keeps a 64-bit mantissa within the block
        long double acc = sum;
        const long double sc = scale;
        auto k = idx;
        for (int i = 0; i < n; ++i)
        {
            const auto x = L[i] * L[i] + R[i] * R[i];
            if (++k >= W)
                k = 0;
            acc = (acc - (long double) ring[(std::size_t) k]) + (long double) x;
            ring[(std::size_t) k] = x;
            out[i] = (float) (sc * acc);
        }
        sum = (double) acc;
        idx = k;
        for (int i = 0; i < n; ++i)
            out[i] = std::sqrt (std::max (out[i], 0.0f));
    }
};

// saturation mode B (FUN_0062a090, x87): above the threshold t, |y| = t + range·(1 − e^((t − |x|)/range)), sign kept
inline float saturateB (float x, float t, float range, float k) noexcept
{
    const auto a = std::fabs (x);
    if (! (a > t))
        return x;
    const auto e = std::exp ((long double) ((long double) t - (long double) a) * (long double) k);
    const auto y = (float) ((1.0L - e) * (long double) range + (long double) t);
    return std::signbit (x) ? -y : y;
}

// FUN_0077f350: L-frame delay (swap ring)
struct FrameDelay
{
    std::vector<float> l, r;
    int L = 0, p = 0;

    void setLength (int n)
    {
        L = std::max (0, n);
        l.assign ((std::size_t) std::max (1, L), 0.0f);
        r.assign ((std::size_t) std::max (1, L), 0.0f);
        p = 0;
    }

    void reset() noexcept
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        p = 0;
    }

    void run (float* xl, float* xr, int n) noexcept
    {
        if (L == 0)
            return;
        for (int i = 0; i < n; ++i)
        {
            std::swap (xl[i], l[(std::size_t) p]);
            std::swap (xr[i], r[(std::size_t) p]);
            if (++p >= L)
                p = 0;
        }
    }
};
} // namespace

class Maximus
{
public:
    static constexpr int maxFrames = Rack::maxBlock * 8;

    void prepare (std::shared_ptr<const MaximusPreset> preset, double rate, int mixParam)
    {
        pr = std::move (preset);
        fs = rate;
        const auto& P = *pr;
        const auto& p = P.p;
        K = 1 << P.oversampling;
        mix = (float) mixParam * 0.001f;
        mixOn = mix != 1.0f;
        std::array<int, 4> mode {};
        for (int b = 0; b < 4; ++b)
            mode[(std::size_t) b] = P.band[(std::size_t) b].mode;
        const auto anyComp = mode[0] < 1 || mode[1] < 1 || mode[2] < 1;
        int latency = 0;
        for (int b = 0; b < 4; ++b)
        {
            auto q = [&] (int j) { return p[(std::size_t) (13 * b + j)]; };
            auto& B = band[(std::size_t) b];
            B.mode = mode[(std::size_t) b];
            const auto k = b == 3 ? 1 : K;
            int L = 0;
            if (B.mode < 2 && (b != 3 || B.mode < 1) && (b > 2 || anyComp))
                L = (int) rint (tms (b == 3 ? q (13) : p[4]) * 2.0 * 0.001 * fs) * k;
            B.L = L;
            if (b < 3)
                latency = std::max (latency, L);
            const auto a = q (13);
            const auto A = a == 0 ? (double) L : tms (a) * 2.0 * 0.001 * fs * k;
            const auto na = std::max (1, q (15));
            const auto attN = (A * 0.800000011920929) / std::sqrt ((double) na);
            const auto rel2N = q (14) == 0 ? attN : (tms (q (14)) * 2.0 * 0.001 * fs * k * 2.0) / std::sqrt ((double) na);
            const auto sus = (int) rint (tms (q (18)) * 2.0 * 0.001 * fs);
            B.det.setLength (std::max (std::max (L, 1), sus * k));
            B.rms = P.band[(std::size_t) b].rms;
            if (B.rms)
                B.rmsDet.setLength (std::max (std::max (L, 1), sus * k));
            B.att.assign ((std::size_t) na, Smoother { coefFor (rel2N), coefFor (attN), 0.0 });
            const auto R = tms (q (16)) * 2.0 * 0.001 * fs * k;
            B.rel.clear();
            if (R > 0.0)
            {
                const auto nr = std::max (1, q (17));
                B.rel.assign ((std::size_t) nr, Smoother { 0.0, coefFor ((R * 2.0) / std::sqrt ((double) nr)), 0.0 });
            }
            B.delay.setLength (L);
        }
        latency = latency / K + band[3].L;
        reportedLatency = latency;
        tailLength = latency + (int) rint (fs * 1.5);

        // crossover (FUN_00844e10)
        auto freq = [&] (int v) { return f32 (std::exp ((double) v * 0.00013822774794088088 * 0.69314718055994529) * 30.0); };
        auto fL = freq (p[0]), fH = freq (p[2]);
        const auto sL = mode[0] != 3 ? std::clamp (p[1], 0, 1) : -1;
        const auto sH = mode[2] != 3 ? std::clamp (p[3], 0, 1) : -1;
        if (sL >= 0 && sH >= 0 && fH < fL)
            std::swap (fL, fH);
        first = 1;
        for (int b = 0; b < 3; ++b)
            if (mode[(std::size_t) b] != 3)
            {
                first = b;
                break;
            }
        auto polarity = [] (int s) { return s < 0 ? 0.0f : s == 0 ? -1.0f : 1.0f; };
        band[0].g18 = polarity (sL), band[1].g18 = 1.0f, band[2].g18 = polarity (sH), band[3].g18 = 1.0f;
        const auto rateB = fs * K, fmax = (double) f32 (0.4999 * rateB), w = 2.0 * pi / rateB;
        auto stage = [&] (int type, float f, int order) {
            Biquad q;
            const auto fc = std::min ((double) f, fmax);
            q.design (type, fc * w, crossoverAlpha (order));
            return q;
        };
        for (auto& s : stages)
            s.clear();
        // the low band's all-pass at the high crossover (phase match with a 24 dB high split) is absent from the
        // older build in Soundgoodizer_x64.dll (FUN_0083a4a0: sL + 1 low-pass stages only)
        const auto allPass = sH > 0 && P.build == MaximusBuild::shared;
        const auto n0 = sL + (allPass ? 1 : 0) + 1;
        for (int i = 0; i < n0; ++i)
            stages[0].push_back (i == n0 - 1 && allPass ? stage (5, fH, 0) : stage (1, fL, sL));
        for (int i = 0; i <= sL; ++i)
            stages[1].push_back (stage (4, fL, sL));
        for (int i = 0; i <= sH; ++i)
            stages[1].push_back (stage (1, fH, sH));
        for (int i = 0; i <= sH; ++i)
            stages[2].push_back (stage (4, fH, sH));
        inPlaceStages = std::min (sL + 1, (int) stages[1].size() - 1);

        // low cut: four high-pass stages at 0.8·f (FUN_00840b40, FUN_0083a5c0)
        lowCut.clear();
        if (p[6] >= 0)
        {
            const auto f = f32 (std::exp ((double) p[6] * 6.103515625e-05 * 0.69314718055994529) * 5.0);
            for (const auto k : { 0.19771184027194977, 0.61932772397995, 0.6768445372581482, 1.0793569087982178 })
            {
                Biquad q;
                q.design (4, ((double) f * 0.8) * (2.0 * pi / fs), k);
                lowCut.push_back (q);
            }
        }
        for (auto& h : up)
            h.reset();
        for (auto& h : down)
            h.reset();
        reset();
        counter = -1073741823;
    }

    void reset() noexcept
    {
        for (int b = 0; b < 4; ++b)
        {
            auto& B = band[(std::size_t) b];
            for (auto& s : B.att)
                s.s = pr->band[(std::size_t) b].g20;
            for (auto& s : B.rel)
                s.s = 0.0f;
            B.delay.reset();
        }
        for (auto& st : stages)
            for (auto& q : st)
                q.reset();
        for (auto& q : lowCut)
            q.reset();
        for (auto& h : up)
            h.reset();
        for (auto& h : down)
            h.reset();
    }

    // silence fed while the rack is idle (Eff_Render's silent-block counter, then the reset after 1.5 s)
    void silence (int n) noexcept
    {
        counter -= n;
        if (! (counter > -n))
        {
            if (counter > -0x3fffffff)
                reset();
            counter = -1073741823;
        }
    }

    void process (float* L, float* R, int n) noexcept
    {
        bool silent = true;
        for (int i = 0; i < n && silent; ++i)
            silent = std::fabs (L[i]) <= 9.999999974752427e-07f && std::fabs (R[i]) <= 9.999999974752427e-07f;
        if (! silent)
            counter = tailLength;
        else
            counter -= n;
        if (! (counter > -n))
        {
            if (counter > -0x3fffffff)
                reset();
            counter = -1073741823;
            return;
        }
        for (auto& q : lowCut)
            q.process (L, R, L, R, n);
        // X: the signal being split (at the band rate)
        const auto nb = n * K;
        float* XL = L;
        float* XR = R;
        if (K > 1)
        {
            up[0].up (L, R, xl.data(), xr.data(), n);
            XL = xl.data(), XR = xr.data();
        }
        bool haveSum = false;
        for (int b = 0; b < 3; ++b)
        {
            auto& st = stages[(std::size_t) b];
            float* yl = bl.data();
            float* yr = br.data();
            if (st.empty())
                std::copy (XL, XL + nb, yl), std::copy (XR, XR + nb, yr);
            else if (b == 1 && b != first)
            {
                const float* sl = XL;
                const float* sr = XR;
                for (int k = 0; k < (int) st.size(); ++k)
                {
                    float* dl = k < inPlaceStages ? XL : yl;
                    float* dr = k < inPlaceStages ? XR : yr;
                    st[(std::size_t) k].process (sl, sr, dl, dr, nb);
                    sl = dl, sr = dr;
                }
                if (st.size() <= (std::size_t) inPlaceStages) // (not reached with the presets: kept literal)
                    std::copy (sl, sl + nb, yl), std::copy (sr, sr + nb, yr);
            }
            else
            {
                const float* sl = XL;
                const float* sr = XR;
                for (auto& q : st)
                {
                    q.process (sl, sr, yl, yr, nb);
                    sl = yl, sr = yr;
                }
            }
            auto& B = band[(std::size_t) b];
            processBand (b, yl, yr, nb);
            if (B.mode < 2)
            {
                if (! haveSum)
                    std::copy (yl, yl + nb, sl.data()), std::copy (yr, yr + nb, sr.data()), haveSum = true;
                else
                    for (int i = 0; i < nb; ++i)
                        sl[(std::size_t) i] = sl[(std::size_t) i] + yl[i], sr[(std::size_t) i] = sr[(std::size_t) i] + yr[i];
            }
            else if (b == first)
            {
                std::fill (sl.begin(), sl.begin() + nb, 0.0f), std::fill (sr.begin(), sr.begin() + nb, 0.0f);
                haveSum = true;
            }
        }
        if (! haveSum)
            std::fill (sl.begin(), sl.begin() + nb, 0.0f), std::fill (sr.begin(), sr.begin() + nb, 0.0f);
        if (K > 1)
            down[0].down (sl.data(), sr.data(), L, R, n);
        else
            std::copy (sl.begin(), sl.begin() + n, L), std::copy (sr.begin(), sr.begin() + n, R);
        processBand (3, L, R, n);
    }

private:
    struct BandState
    {
        int mode = 3, L = 0;
        float g18 = 1.0f;
        PeakWindow det;
        RmsWindow rmsDet;
        bool rms = false;
        std::vector<Smoother> att, rel;
        FrameDelay delay;
    };

    void processBand (int b, float* xl, float* xr, int n) noexcept
    {
        auto& B = band[(std::size_t) b];
        const auto& S = pr->band[(std::size_t) b];
        if (B.mode < 2)
        {
            const auto& m = S.matrix;
            if (m[2] != 0.0f || m[3] != 0.0f)
                for (int i = 0; i < n; ++i)
                {
                    const auto l = xl[i], r = xr[i];
                    xl[i] = l * m[0] + r * m[2];
                    xr[i] = r * m[1] + l * m[3];
                }
            else if (m[0] != 1.0f || m[1] != 1.0f)
                for (int i = 0; i < n; ++i)
                    xl[i] = xl[i] * m[0], xr[i] = xr[i] * m[1];
        }
        float* g = gain.data();
        if (B.mode == 0)
        {
            if (B.rms)
                B.rmsDet.run (xl, xr, g, n);
            else
                B.det.run (xl, xr, g, n);
            for (auto& s : B.rel)
                s.run (g, n);
            const auto k = f32 ((double) S.pre * 8192.25);
            const auto pp = S.pre * S.post;
            const auto hiGain = f32 (S.end * (double) S.post);
            const auto* t = S.gain.data();
            for (int i = 0; i < n; ++i)
            {
                const auto lv = g[i];
                const auto idxf = lv * k;
                auto idx = (int) idxf; // ippsConvert_32f32s_Sfs, rounding toward zero
                if (idx < 0)
                    idx = 0;
                if (idx > S.size)
                    g[i] = hiGain / lv;
                else
                {
                    const auto fr = idxf - (float) idx;
                    g[i] = (t[idx] + (t[idx + 1] - t[idx]) * fr) * pp;
                }
            }
            for (auto& s : B.att)
                s.run (g, n);
        }
        B.delay.run (xl, xr, n);
        const auto keepDry = mixOn && b < 3;
        if (keepDry)
            std::copy (xl, xl + n, dl.data()), std::copy (xr, xr + n, dr.data());
        if (B.mode == 0)
            for (int i = 0; i < n; ++i)
                xl[i] = xl[i] * g[i], xr[i] = xr[i] * g[i];
        else
            for (int i = 0; i < n; ++i)
                xl[i] = xl[i] * S.post, xr[i] = xr[i] * S.post;
        if (! S.sat.empty() && B.mode < 2)
        {
            // FUN_0062a240 (x87, truncating control word): odd table read with linear interpolation
            const auto k = (double) S.satIn * 65536.0;
            const auto* t = S.sat.data();
            auto sat = [&] (float x) {
                const auto v = (double) x * k;
                const auto a = std::fabs (v);
                auto i = (long long) a;
                const auto fr = a - (double) i;
                i = std::min<long long> (i, 65536);
                const auto y = (double) t[i] + ((double) t[i + 1] - (double) t[i]) * fr;
                return (float) ((v < 0.0 ? -y : y) * (double) S.satOut);
            };
            for (int i = 0; i < n; ++i)
                xl[i] = sat (xl[i]), xr[i] = sat (xr[i]);
        }
        else if (S.satB != 0 && B.mode < 2)
            for (int i = 0; i < n; ++i)
                xl[i] = saturateB (xl[i], S.satT, S.satR, S.satK), xr[i] = saturateB (xr[i], S.satT, S.satR, S.satK);
        if (b == 3)
        {
            if (B.mode == 2)
                std::fill (xl, xl + n, 0.0f), std::fill (xr, xr + n, 0.0f);
            return;
        }
        if (B.mode < 2)
        {
            if (mixOn)
            {
                const auto a = mix * B.g18, d = (1.0f - mix) * B.g18;
                for (int i = 0; i < n; ++i)
                {
                    xl[i] = xl[i] * a + dl[(std::size_t) i] * d;
                    xr[i] = xr[i] * a + dr[(std::size_t) i] * d;
                }
            }
            else if (B.g18 == -1.0f)
                for (int i = 0; i < n; ++i)
                    xl[i] = -xl[i], xr[i] = -xr[i];
        }
    }

public:
    // FUN_00840a00: the latency the plugin reports to the host (PDC), max(L0…2)/K + L3
    int latency() const noexcept { return reportedLatency; }

private:
    std::shared_ptr<const MaximusPreset> pr;
    double fs = 44100.0;
    int K = 1, first = 0, inPlaceStages = 0, tailLength = 0, counter = 0, reportedLatency = 0;
    float mix = 1.0f;
    bool mixOn = false;
    std::array<BandState, 4> band {};
    std::array<std::vector<Biquad>, 3> stages {};
    std::vector<Biquad> lowCut;
    std::array<Halfband, 1> up {}, down {};
    std::array<float, maxFrames> xl {}, xr {}, bl {}, br {}, sl {}, sr {}, dl {}, dr {}, gain {};
};

// =================================================================================================================
// Chorus, delay, reverb (REA_rodada36 §1)
// =================================================================================================================

// chorus records (table 0x7ee73c + 0x20·type, DATA): order, depth/1024, speed/1024, delay/128, spread/128, cross/128
struct Rack::Chorus
{
    struct Tap
    {
        int ch = 0;
        float gl = 0, gr = 0, p3 = 0, p8 = 0, p9 = 0, p10 = 0;
        std::uint32_t phase = 0, inc = 0;
    };
    std::vector<Tap> taps;
    float dry = 1.0f;
    int maxDelay = 0;
    std::vector<float> ringL, ringR; // input history, power-of-two length
    int mask = 0, w = 0;

    Chorus (int type, float mixKnob, double fs)
    {
        static constexpr int rec[8][6] { { 4, 512, 768, 8, 128, -32 }, { 5, 1024, 856, 0, 128, 68 }, { 7, 512, 512, 128, 128, 54 },
                                         { 6, 561, 854, 0, 0, 38 },    { 1, 136, 312, 0, 0, 128 },   { 2, 86, 632, 0, 0, 0 },
                                         { 1, 42, 944, 0, 0, -22 },    { 9, 81, 170, 0, 0, -66 } };
        static constexpr float q[20] { 0.967f, 1.039f, 1.021f, 0.1013f, 0.977f, 1.033f, 0.997f, 0.971f, 1.061f, 0.947f,
                                       1.031f, 0.1009f, 1.063f, 0.953f, 0.991f, 1.049f, 0.983f, 1.051f, 1.019f, 1.069f };
        const auto* r = rec[std::clamp (type, 1, 8) - 1];
        const auto N = r[0];
        const auto depth = (float) r[1] * 0.0009765625f, speed = (float) r[2] * 0.0009765625f;
        const auto dly = (float) r[3] * 0.0078125f, spread = (float) r[4] * 0.0078125f, cross = (float) r[5] * 0.0078125f;
        dry = g5 (mixKnob);
        const auto vol = g5 (f32 (1.0 - (double) mixKnob));
        const auto A = f32 ((std::pow (11.0, (double) depth) - 1.0) * (1092.5 / 10.0));
        const auto B = (2147483648.0 / ((double) f32 (std::pow (101.0, (double) f32 (1.0f - speed)) - 1.0) * 75000.0 + 50000.0)) * 500.0;
        const auto C = f32 ((std::pow (11.0, (double) dly) - 1.0) * (4410.0 / 10.0));
        const auto sg = f32 (f32 (std::pow (1001.0, (double) spread) - 1.0) / 1000.0f);
        auto mixf = [&] (double a, double b) { return a * (double) sg + b * (1.0 - (double) sg); };
        const auto fsr = f32 (44100.0 / fs);
        float sumAbs = 0.0f, mx = 0.0f;
        for (int i = 0; i < 2 * N; ++i)
        {
            Tap t;
            float f = 0.0f;
            std::uint32_t ph = 0;
            if (i < N)
                f = (float) i, t.ch = 0;
            else
                f = (float) (2 * N - 1 - i), t.ch = 1, ph = 0x80000000u;
            const auto wv = 1.0f - f / (float) N;
            const auto rr = q[i % 20];
            const auto dV = f32 (mixf ((double) (rr * rr), 1.0));
            float gl = 1.0f, gr = cross * wv;
            if (t.ch)
                std::swap (gl, gr);
            sumAbs = f32 ((double) sumAbs + std::fabs ((double) gl) + std::fabs ((double) gr));
            auto p0 = A * wv + 10.0f;
            const auto d11 = mixf (B * (double) dV / (double) p0, B / (double) (A + 10.0f));
            auto p3 = f32 (mixf ((double) ((C * wv) * dV), (double) C));
            ph += (std::uint32_t) ((std::int64_t) rint ((double) ((wv * sg) * 2147483648.0f) / 256.0) << 8);
            t.phase = ph;
            t.inc = (std::uint32_t) (std::int64_t) rint (d11 * (double) fsr);
            p0 = p0 / fsr;
            auto t3 = p3 / fsr;
            if (! (t3 > 8.0f))
                t3 = 8.0f;
            p3 = p0 + t3;
            if (p3 + p0 > mx)
                mx = p3 + p0;
            const auto qq = std::sqrt ((double) (p0 * 4.0f));
            t.gl = gl, t.gr = gr, t.p3 = p3;
            t.p8 = f32 (qq / 2147483648.0), t.p9 = f32 (qq), t.p10 = f32 (qq / 2.0);
            taps.push_back (t);
        }
        const auto norm = f32 ((double) (1.0f - f32 ((double) f32 (std::pow (1001.0, (double) (1.0f - 1.0f / sumAbs)) - 1.0) / 1000.0)) * (double) vol);
        for (auto& t : taps)
            t.gl = t.gl * norm, t.gr = t.gr * norm;
        maxDelay = (int) rint ((double) mx) + 8;
        int size = 1;
        while (size < maxDelay + Rack::maxBlock + 8)
            size <<= 1;
        ringL.assign ((std::size_t) size, 0.0f);
        ringR.assign ((std::size_t) size, 0.0f);
        mask = size - 1;
    }

    void advance (int n) noexcept
    {
        for (auto& t : taps)
            t.phase += t.inc * (std::uint32_t) n;
    }

    // out = Σ taps (no dry); in: this block of the mix
    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            ringL[(std::size_t) ((w + i) & mask)] = inL[i], ringR[(std::size_t) ((w + i) & mask)] = inR[i];
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);
        bool firstTap = true;
        for (auto& t : taps)
        {
            const auto& ring = t.ch ? ringR : ringL;
            auto ph = t.phase;
            for (int i = 0; i < n; ++i)
            {
                const auto sp = (double) (std::int32_t) ph;
                auto v = std::fabs (sp * (double) t.p8) - (double) t.p10;
                v = ((double) t.p9 - std::fabs (v)) * v + (double) t.p3;
                ph += t.inc;
                const auto k = (int) v; // x87 fistl with the truncating control word (v > 0)
                const auto at = w + i - k;
                const auto a = (double) ring[(std::size_t) (at & mask)], b = (double) ring[(std::size_t) ((at - 1) & mask)];
                const auto s = (b - a) * (v - (double) k) + a;
                if (firstTap)
                    outL[i] = (float) (s * (double) t.gl), outR[i] = (float) (s * (double) t.gr);
                else
                    outL[i] = (float) (s * (double) t.gl + (double) outL[i]), outR[i] = (float) (s * (double) t.gr + (double) outR[i]);
            }
            t.phase = ph;
            firstTap = false;
        }
        w = (w + n) & mask;
    }
};

// delay records (table 0x7ee67c + 8·type, DATA): stereo offset in ms (sign = side), ping-pong
struct Rack::Delay
{
    std::vector<float> l, r;
    int S = 1, rp = 0, D = 1, oL = 0, oR = 0;
    bool pingPong = false;
    float vol = 0.0f, fb = 0.0f;
    double cur = 0.0, maxStep = 0.001;

    Delay (int type, int volKnob, int fbKnob, int time, double fs, double bpm)
    {
        static constexpr int rec[3][2] { { 0, 0 }, { 1, 2 }, { -20, 2 } };
        const auto* rc = rec[std::clamp (type, 1, 3) - 1];
        const auto o = (int) rint (std::abs (rc[0]) * 0.001 * fs);
        if (rc[0] > 0)
            oR = o;
        else
            oL = o;
        pingPong = rc[1] != 0;
        D = std::max (1, (int) rint ((double) time * fs * 0.3125 / std::max (1.0, bpm)));
        vol = g11 ((float) volKnob * 0.001f);
        fb = (float) fbKnob * 0.001f;
        cur = fb; // the feedback ramp starts at its target (EXEC with the plugin's state loaded before the rate is set)
        maxStep = (double) f32 (0.001 * 44100.0 / fs);
        S = D + o + Rack::maxBlock + 8;
        l.assign ((std::size_t) S, 0.0f);
        r.assign ((std::size_t) S, 0.0f);
    }

    void write (const float* inL, const float* inR, int n) noexcept
    {
        const auto w = rp + D;
        for (int i = 0; i < n; ++i)
        {
            auto& a = l[(std::size_t) ((w + oL + i) % S)];
            a = a + inL[i] * vol;
            auto& b = r[(std::size_t) ((w + oR + i) % S)];
            b = b + inR[i] * vol;
        }
    }

    void advance (int n) noexcept { cur = ramp (cur, fb, n, maxStep).end; }

    void process (float* outL, float* outR, int n) noexcept
    {
        const auto rr = ramp (cur, fb, n, maxStep);
        cur = rr.end;
        auto g = rr.start;
        for (int i = 0; i < n; ++i)
        {
            const auto a = l[(std::size_t) rp], b = r[(std::size_t) rp];
            l[(std::size_t) rp] = 0.0f, r[(std::size_t) rp] = 0.0f;
            const auto w = (rp + D) % S;
            float fl = a, fr = b;
            if (pingPong)
                outL[i] = b, outR[i] = a, fl = b, fr = a;
            else
                outL[i] = a, outR[i] = b;
            auto u = (float) ((double) fl * g + (double) l[(std::size_t) w]);
            auto v = (float) ((double) fr * g + (double) r[(std::size_t) w]);
            l[(std::size_t) w] = std::fabs (u) < flushLevel ? 0.0f : u;
            r[(std::size_t) w] = std::fabs (v) < flushLevel ? 0.0f : v;
            g += rr.step;
            if (++rp >= S)
                rp = 0;
        }
    }
};

// reverb records (table 0x7ee67c + 0x20·type, DATA): lowcut Hz, highcut ×100 Hz, predelay, room, diffusion,
// decay ×0.1 s, damping ×100 Hz, colour; DSP of the shared Reverb_x64.dll (REA_rodada31 §7.5)
struct Rack::Reverb
{
    std::array<std::vector<float>, 12> buf;
    std::array<int, 12> L {}, P {};
    std::array<int, 7> tl {}, tr {};
    std::vector<float> pre;
    int pp = 0;
    float cHc = 1, cLc = 0, cDamp = 1, cIn = 0, c1 = 0, c2 = 0, g = 0, c48 = 0, c44 = 0;
    float s10 = 0, s14 = 0, s18 = 0, s3c = 0, s40 = 0, s4c = 0, s50 = 0, s54 = 0, s58 = 0;

    Reverb (int type, double fs)
    {
        static constexpr int rec[6][8] { { 75, 40, 0, 40, 100, 15, 40, 2 },  { 0, 115, 18, 40, 100, 22, 50, 2 },
                                         { 34, 21, 27, 90, 100, 45, 31, 3 }, { 400, 60, 31, 60, 65, 23, 37, 1 },
                                         { 100, 36, 0, 35, 100, 6, 36, 2 },  { 100, 60, 0, 17, 100, 5, 30, 2 } };
        static constexpr int lines[12] { 142, 107, 379, 277, 672, 4453, 1800, 3720, 908, 4217, 2656, 3163 };
        static constexpr int tapsL[7] { 266, 2974, 1913, 2496, 1690, 187, 1566 };
        static constexpr int tapsR[7] { 353, 3627, 1728, 3173, 1811, 335, 621 };
        static constexpr double colour[5] { 0.2, 0.6, 1.0, 1.5, 3.0 };
        const auto* r = rec[std::clamp (type, 1, 6) - 1];
        const auto rate = (long long) fs;
        const auto preS = (int) rint ((double) r[2] * (double) rate / 384.0);
        const auto decay = (double) f32 ((double) r[5] * (double) rate / 10.0);
        const auto f17 = (double) f32 (1.0 / (double) rate), half = (double) f32 ((double) rate * 0.5);
        if (r[0] != 0)
        {
            const auto t = (double) f32 (std::tan (std::min ((double) r[0], half) * pi * f17));
            cLc = f32 ((t - 1.0) / (t + 1.0));
        }
        auto lp = [&] (int v) {
            if (v == 0)
                return 1.0f;
            const auto x = (double) f32 (2.0 - std::cos (std::min ((double) v, half) * 628.3185307179587 * f17));
            return f32 (1.0 - (x - std::sqrt (x * x - 1.0)));
        };
        cHc = lp (r[1]);
        cDamp = lp (r[6]);
        auto muldiv = [] (long long a, long long b, long long c) { return (int) ((a * b) / c); };
        const auto sc = muldiv (rate, r[3] + 15, 115);
        for (int i = 0; i < 12; ++i)
        {
            L[(std::size_t) i] = std::max (1, muldiv (lines[i], sc, 20000));
            buf[(std::size_t) i].assign ((std::size_t) L[(std::size_t) i], 0.0f);
        }
        for (int i = 0; i < 7; ++i)
            tl[(std::size_t) i] = muldiv (tapsL[i], sc, 20000), tr[(std::size_t) i] = muldiv (tapsR[i], sc, 20000);
        const auto dv = (double) f32 (r[4] * 0.01), f13 = (double) f32 (f17 * 0.5);
        if (dv != 0.0)
        {
            auto e = [&] (int a, int b, double k) { return f32 (std::exp (std::log (0.001) * (double) f32 ((a + b) * f13) / (dv * k))); };
            cIn = e (L[0], L[1], 0.07), c1 = e (L[4], L[8], 0.35), c2 = e (L[6], L[10], 0.51);
        }
        const auto ssum = (double) (L[5] + L[7] + L[9] + L[11]);
        g = f32 (std::pow (std::exp (std::log (0.001) * ssum / decay), 0.25));
        auto r2 = (double) f32 ((double) f32 (std::pow (std::exp (std::log (0.001) * ssum / (double) f32 (decay * colour[std::clamp (r[7], 0, 4)])), 0.25)) / (double) g);
        c48 = f32 ((r2 - 1.0) * 0.5);
        if (r2 > 1.0)
            r2 = 1.0;
        const auto t = (double) f32 (std::tan (500.0 * pi * f17));
        c44 = f32 ((t - r2) / (t + r2));
        pre.assign ((std::size_t) std::max (1, preS), 0.0f);
    }

    float ap (int i, float v, float c) noexcept
    {
        auto& p = P[(std::size_t) i];
        auto& b = buf[(std::size_t) i];
        const auto w = b[(std::size_t) p] - c * v;
        b[(std::size_t) p] = c * w + v;
        p = p > 0 ? p - 1 : L[(std::size_t) i] - 1;
        return w;
    }

    float dl (int i, float v) noexcept
    {
        auto& p = P[(std::size_t) i];
        auto& b = buf[(std::size_t) i];
        const auto r = b[(std::size_t) p];
        b[(std::size_t) p] = v;
        p = p > 0 ? p - 1 : L[(std::size_t) i] - 1;
        return r;
    }

    float tap (int i, int off) const noexcept
    {
        const auto k = off + P[(std::size_t) i];
        return buf[(std::size_t) i][(std::size_t) (k >= L[(std::size_t) i] ? k - L[(std::size_t) i] : k)];
    }

    void process (const float* x, float* outL, float* outR, int n) noexcept
    {
        const auto t1 = 1.0f - g;
        const auto w74 = t1 * 0.6f + g, w6c = t1 * 0.25f + g, w78 = t1 * 0.7f + g, w68 = t1 * 0.2f + g, w70 = t1 * 0.5f + g;
        for (int i = 0; i < n; ++i)
        {
            auto a = ((x[i] + 1.0e-30f) - s10) * cHc + s10;
            s10 = a;
            const auto h = (a - s14) * cLc + s18;
            s18 = a, s14 = h;
            if (cLc != 0.0f)
                a = (a - h) * 0.5f + 1.0e-30f;
            for (int k = 0; k < 4; ++k)
                a = ap (k, a, cIn);
            const auto z = pre[(std::size_t) pp];
            pre[(std::size_t) pp] = a;
            pp = pp > 0 ? pp - 1 : (int) pre.size() - 1;
            // half 1
            auto u = buf[11][(std::size_t) P[11]] * g;
            auto f = (u - s50) * c44 + s4c;
            s4c = u, s50 = f;
            auto v = (((f + u) * c48 + u + z) - s3c) * cDamp + s3c;
            s3c = v;
            auto w = ap (4, v, c1);
            auto r = dl (5, w) * g;
            auto w2 = ap (6, r, c2);
            buf[7][(std::size_t) P[7]] = w2;
            P[7] = P[7] > 0 ? P[7] - 1 : L[7] - 1;
            // half 2
            u = buf[7][(std::size_t) P[7]] * g;
            f = (u - s58) * c44 + s54;
            s54 = u, s58 = f;
            v = (((f + u) * c48 + u + z) - s40) * cDamp + s40;
            s40 = v;
            w = ap (8, v, c1);
            r = dl (9, w) * g;
            w2 = ap (10, r, c2);
            buf[11][(std::size_t) P[11]] = w2;
            P[11] = P[11] > 0 ? P[11] - 1 : L[11] - 1;
            outL[i] = (((tap (9, tl[0]) - w6c * tap (9, tl[1])) - w78 * tap (10, tl[2])) - w68 * tap (11, tl[3]))
                      + w74 * tap (5, tl[4]) + tap (6, tl[5]) + w70 * tap (7, tl[6]);
            outR[i] = ((((w6c * tap (5, tr[1]) + tap (5, tr[0])) - w78 * tap (6, tr[2])) - w68 * tap (7, tr[3]))
                       - w74 * tap (9, tr[4])) + tap (10, tr[5]) + w70 * tap (11, tr[6]);
        }
    }
};

Rack::Rack() = default;
Rack::~Rack() = default;

void Rack::prepare (const std::array<int, 100>& p, const std::array<int, 7>& t, double fs, double bpm,
                    std::shared_ptr<const MaximusPreset> preset)
{
    chorus.reset();
    delay.reset();
    reverb.reset();
    comp.reset();
    compPreset.reset();
    if (p[90] > 0)
        chorus = std::make_unique<Chorus> (p[90], (float) p[91] * 0.001f, fs);
    if (p[92] > 0)
        delay = std::make_unique<Delay> (p[92], p[93], p[94], p[95], fs, bpm);
    if (p[96] > 0)
        reverb = std::make_unique<Reverb> (p[96], fs);
    wet = f32 ((double) g11 ((float) p[97] * 0.001f) * 0.5);
    if (t[3] > 0 && preset != nullptr && preset->valid)
    {
        compPreset = std::move (preset);
        comp = std::make_unique<Maximus>();
        comp->prepare (compPreset, fs, p[98]);
    }
    on = chorus || delay || reverb || comp;
    tailLength = (int) rint (fs * 0.004) + (chorus ? maxBlock + chorus->maxDelay : 0);
    tail = 0;
    quietNeeded = (long long) (0.25 * fs) + (chorus ? chorus->maxDelay : 0)
                  + (delay ? (long long) delay->D * 2 + delay->oL + delay->oR : 0)
                  + (reverb ? (long long) reverb->pre.size() + reverb->L[5] + reverb->L[9] : 0);
    quietFor = quietNeeded; // a new rack holds no sound
}

void Rack::process (float* left, float* right, int n, bool voices) noexcept
{
    if (! on || n <= 0)
        return;
    if (voices)
        tail = tailLength;
    else if (tail > 0)
        tail -= n;
    if (chorus)
    {
        if (tail < 1)
            chorus->advance (n);
        else
        {
            chorus->process (left, right, scratchL.data(), scratchR.data(), n);
            if (chorus->dry != 1.0f)
                for (int i = 0; i < n; ++i)
                    left[i] = left[i] * chorus->dry, right[i] = right[i] * chorus->dry;
            for (int i = 0; i < n; ++i)
                left[i] = left[i] + scratchL[(std::size_t) i], right[i] = right[i] + scratchR[(std::size_t) i];
        }
    }
    if (delay)
    {
        if (tail > 0)
            delay->write (left, right, n);
        delay->process (scratchL.data(), scratchR.data(), n);
        for (int i = 0; i < n; ++i)
            left[i] = left[i] + scratchL[(std::size_t) i], right[i] = right[i] + scratchR[(std::size_t) i];
    }
    if (reverb)
    {
        for (int i = 0; i < n; ++i)
            mono[(std::size_t) i] = left[i] + right[i];
        reverb->process (mono.data(), scratchL.data(), scratchR.data(), n);
        for (int i = 0; i < n; ++i)
            left[i] = left[i] + scratchL[(std::size_t) i] * wet, right[i] = right[i] + scratchR[(std::size_t) i] * wet;
    }
    if (comp)
        comp->process (left, right, n);
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::max (std::fabs (left[i]), std::fabs (right[i])));
    if (voices || peak > 1.0e-7f)
        quietFor = 0;
    else
        quietFor += n;
}

void Rack::advance (int n) noexcept
{
    if (! on || n <= 0)
        return;
    if (tail > 0)
        tail -= n;
    if (chorus)
        chorus->advance (n);
    if (delay)
        delay->advance (n);
    if (comp)
        comp->silence (n);
}
} // namespace lynx::hlfx

namespace lynx::hlfx
{
// =================================================================================================================
// The engine on its own (mixer Maximus, Soundgoodizer, Vocodex's SG stage; REA_rodada40)
// =================================================================================================================

MaximusProcessor::MaximusProcessor() = default;
MaximusProcessor::~MaximusProcessor() = default;

void MaximusProcessor::prepare (std::shared_ptr<const MaximusPreset> preset, double sampleRate, int mixParameter)
{
    if (preset == nullptr)
    {
        engine.reset();
        return;
    }
    engine = std::make_unique<Maximus>();
    engine->prepare (std::move (preset), sampleRate, mixParameter);
}

void MaximusProcessor::process (float* left, float* right, int n) noexcept
{
    if (engine == nullptr)
        return;
    for (int done = 0; done < n; done += Rack::maxBlock)
    {
        const auto len = std::min (Rack::maxBlock, n - done);
        engine->process (left + done, right + done, len);
    }
}

void MaximusProcessor::reset() noexcept
{
    if (engine != nullptr)
        engine->reset();
}

int MaximusProcessor::latencySamples() const noexcept { return engine != nullptr ? engine->latency() : 0; }

int maximusMixParameter (const MaximusPreset& preset) noexcept { return preset.p[5]; }
} // namespace lynx::hlfx
