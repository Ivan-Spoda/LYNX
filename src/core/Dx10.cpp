#include "core/Dx10.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

// 1:1 port of our round-42 reference model (r42/py/dxmodel.py + dxproc.py). The model emulates the DLL's float32 arithmetic with
// numpy float32 scalars and uses Python floats (doubles) where the DLL computes in double; this file keeps those types
// and the operation order of every expression, so fusing a·b + c into one FMA would change the results: contraction
// stays off.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace
{
constexpr int paramCount = 22;
constexpr int snapshotSize = 364;
constexpr int maxVoices = 256; // voice records per instance (FL: unlimited; the oldest is cut beyond this)
constexpr int stateWithSnapshot = 4 + 0x5c + snapshotSize; // the plugin applies a snapshot when ≥ 364 bytes follow the 92

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t at) noexcept
{
    std::uint32_t v = 0;
    std::memcpy (&v, d.data() + at, 4);
    return (std::int32_t) v;
}

std::int32_t bitsOf (float x) noexcept
{
    std::int32_t b;
    std::memcpy (&b, &x, 4);
    return b;
}

// cvtsd2si: round half to even (default rounding mode), 64-bit
std::int64_t roundEven (double x) noexcept { return (std::int64_t) std::nearbyint (x); }

// The host's WaveTables[0]: 16384 floats sin(2i·π/16384) (FLEngine FUN_009cdfc0, REA_rodada42 §1.6)
const std::array<float, 16384>& sineTable() noexcept
{
    static const auto table = [] {
        std::array<float, 16384> t {};
        for (int i = 0; i < 16384; ++i)
            t[(std::size_t) i] = (float) std::sin ((double) (2 * i) * 3.141592653589793 / 16384.0);
        return t;
    }();
    return table;
}

// FUN_005fad20 (x87 path): t = (1 + pan)·π/4 in extended precision; the FPU's fcos/fsin reduce arguments above π/4 with
// its 66-bit π, i.e. cos t = sin(P/2 − t), sin t = cos(P/2 − t) with P = round(π·2^64)/2^64 (EXEC: pan = 1 gives a left
// gain of 6.1230318e-17·vol). P/2 = hi + lo, hi = 0xC90FDAA22168C234·2^-63, lo = 3·2^-65; hi − t is exact (Sterbenz).
void panLaw (float pan, float vol, float& gl, float& gr) noexcept
{
    using ld = long double;
    pan = std::min (std::max (pan, -1.0f), 1.0f);
    const ld quarter = (ld) 0.7853981633974483;
    const ld t = ((ld) 1 + (ld) pan) * quarter;
    ld c, s;
    if (t > quarter)
    {
        const ld hi = std::ldexp ((ld) 0xC90FDAA22168C234ull, -63);
        const ld lo = std::ldexp ((ld) 3, -65);
        const ld r = (hi - t) + lo;
        c = std::sin (r);
        s = std::cos (r);
    }
    else
    {
        c = std::cos (t);
        s = std::sin (t);
    }
    gl = (float) (c * (ld) vol);
    gr = (float) (s * (ld) vol);
}

// FUN_005fac70: per-sample step towards the target, limited to 0.001; a limited ramp that would end below 2^-24 ends
// at 0
float rampStep (float& target, float prev, int n) noexcept
{
    auto step = target - prev;
    if (step != 0.0f)
    {
        step = step / (float) n;
        if (std::fabs (step) > 0.001f)
        {
            step = std::copysign (0.001f, step);
            target = prev + step * (float) n;
            if ((bitsOf (target) & 0x7fffffff) < 864026624)
            {
                target = 0.0f;
                step = -prev / (float) n;
            }
        }
    }
    return step;
}

struct Params
{
    std::array<std::int32_t, paramCount> raw {};

    float p (int i) const noexcept
    {
        const int lo = i == 21 ? -3 : 0, hi = i == 21 ? 3 : 65536;
        return (float) (raw[(std::size_t) i] - lo) / (float) (hi - lo);
    }
};

std::optional<Params> parseState (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4 + 4 * (std::size_t) paramCount)
        return std::nullopt;
    const auto version = i32At (d, 0); // FUN_00734940: any version below 3 (signed) except 0x7fffffff
    if (version == 0x7fffffff || version >= 3)
        return std::nullopt;
    Params P;
    for (int i = 0; i < paramCount; ++i)
        P.raw[(std::size_t) i] = i32At (d, 4 + 4 * (std::size_t) i);
    return P;
}

// ------------------------------------------------------------------------------------------------------- x87

// The processor's filters run on the x87 FPU with 64-bit precision (control word 0x33F, FUN_005a2840): long double
// arithmetic reproduces them where long double is the x87 format; the guard makes sure the precision field is 64-bit.
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
struct X87Extended
{
    unsigned short saved = 0;
    X87Extended() noexcept
    {
        asm volatile ("fnstcw %0" : "=m"(saved));
        unsigned short cw = (unsigned short) (saved | 0x300);
        asm volatile ("fldcw %0" : : "m"(cw));
    }
    ~X87Extended() { asm volatile ("fldcw %0" : : "m"(saved)); }
};
#else
struct X87Extended
{
};
#endif

// ------------------------------------------------------------------------------------------------------- processor

// LFO tables of the plugin's voice processor (FUN_0071f4e0): sine (= the host's), triangle, square
struct LfoTables
{
    std::array<float, 16384> tri {}, sqr {};
    LfoTables()
    {
        for (int i = 0; i < 8192; ++i)
            tri[(std::size_t) ((i + 0x3000) & 0x3fff)] = (float) ((double) (i << 2) / 16384.0 + -1.0);
        for (int i = 8192; i < 16384; ++i)
            tri[(std::size_t) ((i + 0x3000) & 0x3fff)] = (float) (1.0 - (double) ((i - 0x2000) * 4) / 16384.0);
        for (int i = 0; i < 16384; ++i)
            sqr[(std::size_t) i] = i < 8192 ? 1.0f : -1.0f;
    }
};

const float* lfoTable (int shape) noexcept
{
    static const LfoTables t;
    shape = std::min (std::max (shape, 0), 2);
    return shape == 0 ? sineTable().data() : shape == 1 ? t.tri.data() : t.sqr.data();
}

// FUN_0071fe70: knob value → ticks
int toTicks (int v, double E) noexcept
{
    if (v < 0x65)
        return 0;
    const auto x = (float) ((double) v * 1.52587890625e-05);
    const auto d = std::exp ((double) x * std::log (1001.0));
    return (int) roundEven ((double) (float) (d - 1.0) * E);
}

// FUN_0071fdb0 + FUN_005f5ac0: a tension knob → (value, 1/|value|, ln(|value| + 1))
struct Tension
{
    float v = 0.0f, inv = 0.0f, L = 0.0f;
};

Tension makeTension (int t) noexcept
{
    const auto sign = t == 0 ? 0 : (t < 0 ? -1 : 1);
    const auto a = std::fabs ((double) ((float) t * 0.0078125f));
    const auto d = std::exp ((double) (float) a * std::log (6347.0));
    Tension r;
    r.v = ((float) sign * (float) (d - 1.0)) * 0.1f;
    if (r.v != 0.0f)
    {
        r.inv = (float) (1.0 / std::fabs ((double) r.v));
        r.L = (float) std::log (std::fabs ((double) r.v) + 1.0);
    }
    return r;
}

float shape (float x, const Tension& t) noexcept
{
    auto d = (double) x;
    if (bitsOf (t.v) != 0)
    {
        if (bitsOf (t.v) >= 0)
            d = 1.0 - (std::exp ((1.0 - d) * (double) t.L) - 1.0) * (double) t.inv;
        else
            d = (std::exp (d * (double) t.L) - 1.0) * (double) t.inv;
    }
    return (float) d;
}

float unshape (float y, const Tension& t) noexcept
{
    auto d = (double) y;
    if (t.v != 0.0f)
    {
        if (bitsOf (t.v) >= 0)
            d = 1.0 - std::log ((1.0 - d) * std::fabs ((double) t.v) + 1.0) / (double) t.L;
        else
            d = std::log (d * std::fabs ((double) t.v) + 1.0) / (double) t.L;
    }
    return (float) d;
}

// One articulator's settings: a 0xDA record (17 ints) converted by FUN_0071ff00
struct Rec
{
    std::int32_t flags = 0, enabled = 0, attackRaw = 0, amountRaw = 0, lfoAmountRaw = 0;
    int delay = 0, hold = 0, lfoDelay = 0;
    float att = 0, sus = 0, dec = 0, rel = 0, amt = 0, lfoFade = 0, lfoAmt = 0;
    std::uint32_t lfoInc = 0, globalPhase = 0;
    const float* table = nullptr;
    Tension tens[3];

    void build (const std::int32_t* r, int ppq, double tempo) noexcept
    {
        flags = r[0], enabled = r[1], attackRaw = r[3], amountRaw = r[8], lfoAmountRaw = r[11];
        const auto d6 = (((double) (ppq >> 2) / 48.0) * 24576.0) / 1000.0;
        const auto E = (flags & 1) != 0 ? d6 : (d6 * tempo) / 120.0;
        delay = toTicks (r[2], E);
        att = (float) (1.0 / (double) (toTicks (r[3], E) + 1));
        hold = toTicks (r[4], E);
        dec = (float) (-1.0 / (double) (toTicks (r[5], E) + 1));
        sus = (float) r[6] * 0.0078125f;
        rel = (float) (-1.0 / (double) (toTicks (r[7], E) + 1));
        amt = (float) r[8] * 0.0078125f;
        for (int k = 0; k < 3; ++k)
            tens[k] = makeTension (r[14 + k]);
        const auto E2 = (flags & 2) != 0 ? d6 : (d6 * tempo) / 120.0;
        lfoDelay = toTicks (r[9], E2);
        lfoFade = (float) (1.0 / (double) (toTicks (r[10], E2) + 1));
        lfoInc = (std::uint32_t) (0x100000000ull / (std::uint64_t) std::max (toTicks (r[12], E2), 3));
        lfoAmt = (float) r[11] * 0.0078125f;
        table = lfoTable (r[13]);
    }
};

// The 364-byte snapshot: records pan, volume, cutoff, resonance, pitch; Mod X (0x15c), Mod Y (0x160), type (0x168)
struct Settings
{
    std::array<Rec, 5> recs {};
    std::int32_t modX = 256, modY = 0, type = 0;
    bool hasGlobal = false;

    void build (const std::uint8_t* s, int ppq, double tempo) noexcept
    {
        hasGlobal = false;
        for (int k = 0; k < 5; ++k)
        {
            std::int32_t r[17];
            std::memcpy (r, s + 0x44 * k, sizeof (r));
            recs[(std::size_t) k].build (r, ppq, tempo);
            hasGlobal = hasGlobal || (recs[(std::size_t) k].flags & 0x20) != 0;
        }
        std::memcpy (&modX, s + 0x15c, 4);
        std::memcpy (&modY, s + 0x160, 4);
        std::memcpy (&type, s + 0x168, 4);
    }
};

// the plugin's defaults (FUN_00722bc0): neutral records, the volume one with flag 4 and release tension −101
std::array<std::uint8_t, snapshotSize> defaultSnapshot() noexcept
{
    std::array<std::uint8_t, snapshotSize> s {};
    for (int k = 0; k < 5; ++k)
    {
        const std::int32_t r[17] = { k == 1 ? 4 : 0, 0, 100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0, 0, 0, k == 1 ? -101 : 0 };
        std::memcpy (s.data() + 0x44 * k, r, sizeof (r));
    }
    const std::int32_t modX = 256;
    std::memcpy (s.data() + 0x15c, &modX, 4);
    return s;
}

// Per-voice articulator state (FUN_007202c0 / FUN_0071f9b0)
struct Art
{
    int cnt = 0, state = 1, lfoDelay = 0;
    std::uint32_t phase = 0;
    float pos = 0, val = 0, fade = 0, lfo = 0;

    void init (const Rec& s) noexcept
    {
        cnt = 0;
        if (s.delay < 1 || s.enabled == 0)
        {
            state = 1;
            if (((s.flags & 4) == 0 || s.enabled != 0) && (s.enabled == 0 || s.attackRaw != 100))
                pos = 0.0f;
            else
            {
                pos = 1.0f;
                state = s.hold < 1 ? 3 : 2;
            }
        }
        else
            state = 0, pos = 0.0f;
        val = pos;
        lfoDelay = s.lfoDelay;
        phase = 0, fade = 0.0f, lfo = 0.0f;
    }

    void step (const Rec& s) noexcept
    {
        if (s.enabled != 0)
        {
            switch (state)
            {
                case 2:
                    if (s.hold <= ++cnt)
                        state = 3;
                    break;
                case 0:
                    if (s.delay <= ++cnt)
                        state = 1;
                    break;
                case 1:
                    pos = pos + s.att;
                    if (1.0f <= pos)
                    {
                        pos = 1.0f;
                        if (s.hold < 1)
                            state = 3;
                        else
                            cnt = 0, state = 2;
                    }
                    val = shape (pos, s.tens[0]);
                    break;
                case 3:
                    pos = pos + s.dec;
                    val = shape (pos, s.tens[1]);
                    if (val <= s.sus)
                    {
                        val = s.sus;
                        pos = unshape (val, s.tens[1]);
                        cnt = 0, state = 4;
                    }
                    break;
                case 6:
                    pos = pos + s.rel;
                    val = shape (pos, s.tens[2]);
                    break;
                default:
                    break;
            }
            if (state > 2 && val < 5.96046448e-08f)
                val = 0.0f, state = 9;
        }
        if (lfoDelay == 0)
        {
            phase = (s.flags & 0x20) != 0 ? s.globalPhase : phase + s.lfoInc;
            lfo = s.table[phase >> 18];
            if (fade != 1.0f)
            {
                fade = fade + s.lfoFade;
                if (1.0f < fade)
                    fade = 1.0f;
                lfo = lfo * fade;
            }
        }
        else
            --lfoDelay;
    }
};

// filter kinds by type index (DAT_0078cc6c): kind, second stage
constexpr int filterKind[8][2] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 1, 1 }, { 6, 0 }, { 6, 1 } };

const float ln601f = (float) std::log (601.0), inv600f = 1.0f / 600.0f; // 0x79e4b8, 0x77a83c
const float ln51f = (float) std::log (51.0), ln51r = (float) (std::log (51.0) * 80.0 / 128.0); // 0x800624, 0x800628

// FUN_00720cb0: the fast LP's coefficient and feedback
void coefFast (float x, float y, float& c, float& q) noexcept
{
    if (! (0.0f < x))
        c = 0.0f;
    else if (! (x < 1.0f))
        c = 1.0f;
    else
    {
        c = (float) ((std::exp ((double) (x * ln601f)) - 1.0) * (double) inv600f) * 1.0f;
        if (! (1.0f > c))
            c = 1.0f;
    }
    if (! (0.0f < y))
        q = 0.0f;
    else
    {
        if (y > 1.0f)
            y = 1.0f;
        const auto e = std::exp ((double) ((1.0f - y * 0.625f) * ln601f));
        q = 1.0f - (float) ((e - 1.0) * (double) inv600f);
    }
}

// FUN_00720b10 + FUN_007204b0: 12 dB biquads (kinds 1 LP, 2 BP, 3 HP, 4 band stop) → coefficients b0 b1 b2 a1 a2
void coefBiquad (int kind, float x, float y, float& fc, float& fq, float* b) noexcept
{
    fc = (float) (((std::exp ((double) (x * ln51f)) - 1.0) * (double) 0.0596f + 0.02) * 1.0);
    const auto lo = (float) (1.0 * 0.02);
    if (lo > fc)
        fc = lo;
    else if (fc > 3.0f)
        fc = 3.0f;
    if (0.0f < y)
    {
        if (y > 1.0f)
            y = 1.0f;
        fq = (float) ((std::exp ((double) (y * ln51r)) - 1.0) * (double) 0.4f + 0.1);
    }
    else
        fq = 0.1f;
    const auto s = (float) std::sin ((double) fc), co = (float) std::cos ((double) fc);
    const auto alpha = (double) (s / (fq * 2.0f));
    const auto cs = (double) co;
    const auto d = 1.0 / (alpha + 1.0);
    const auto a1 = (float) (d * cs * -2.0), a2 = (float) (d * (1.0 - alpha));
    float b0, b1, b2;
    if (kind == 1)
        b1 = (float) (d * (1.0 - cs)), b0 = b1 * 0.5f, b2 = b0;
    else if (kind == 2)
        b1 = 0.0f, b0 = (float) (d * alpha), b2 = (float) (d * -alpha);
    else if (kind == 3)
        b1 = (float) (d * -(cs + 1.0)), b0 = -b1 * 0.5f, b2 = b0;
    else
        b1 = a1, b0 = (float) d, b2 = (float) d;
    b[0] = b0, b[1] = b1, b[2] = b2, b[3] = a1, b[4] = a2;
}

// FUN_00720de0: state-variable filter frequency and damping
void coefSvf (float x, float y, float& c, float& q) noexcept
{
    if (! (0.0f < x))
        c = 0.0f;
    else
    {
        c = ((float) (std::exp ((double) x * std::log (26.0)) - 1.0) * 0.04f) * 1.0f;
        if (! (1.0f > c))
            c = 1.0f;
    }
    if (! (0.0f < y))
        q = 1.0f;
    else if (! (y < 1.0f))
        q = 0.2f;
    else
        q = 1.0f - y * 0.8f;
}

// FUN_005f91a0: values below 2^-24 in magnitude become 0
void flushSmall (float* v, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        if (((std::uint32_t) bitsOf (v[i]) << 1) < (864026624u << 1))
            v[i] = 0.0f;
}

using ld = long double;

// FUN_005f9290 (x87): one-pole with feedback, coefficient ramped per sample; state yL yR vL vR (doubles)
void renderFast (double* st, float damp, const float* x, float* y, int n, float c0, float dc) noexcept
{
    X87Extended guard;
    ld yL = st[0], yR = st[1], vL = st[2], vR = st[3], c = c0;
    const ld dcl = dc, dm = damp;
    for (int i = 0; i < n; ++i)
    {
        ld t = ((ld) x[2 * i] - yL) * c;
        vL = vL + t;
        yL = yL + vL;
        vL = vL * dm;
        y[2 * i] = (float) yL;
        t = ((ld) x[2 * i + 1] - yR) * c;
        vR = vR + t;
        yR = yR + vR;
        vR = vR * dm;
        y[2 * i + 1] = (float) yR;
        c = c + dcl;
    }
    auto flush = [] (ld v) { return std::fabs (v) <= (ld) 5.96046448e-08f ? 0.0 : (double) v; };
    st[2] = flush (vL), st[0] = flush (yL), st[3] = flush (vR), st[1] = flush (yR);
}

// FUN_00720ee0 (x87): direct-form-I biquad; state x1L x1R x2L x2R y1L y1R y2L y2R (floats)
void renderBiquad (float* st, const float* b, const float* x, float* y, int n) noexcept
{
    X87Extended guard;
    const ld b0 = b[0], b1 = b[1], b2 = b[2], a1 = b[3], a2 = b[4];
    for (int i = 0; i < n; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const ld x2 = st[2 + ch], x1 = st[ch];
            st[2 + ch] = st[ch];
            ld acc = x1 * b1 + x2 * b2;
            const ld y2 = st[6 + ch], y1 = st[4 + ch];
            st[6 + ch] = st[4 + ch];
            acc = acc - y1 * a1;
            acc = acc - y2 * a2;
            const auto in = x[2 * i + ch];
            st[ch] = in;
            const auto out = (float) (acc + (ld) in * b0);
            st[4 + ch] = out;
            y[2 * i + ch] = out;
        }
    flushSmall (st, 8);
}

// FUN_00721000 (x87): state-variable low-pass; state lowL lowR bandL bandR (floats, extended within the block)
void renderSvf (float* st, float fc, float fq, const float* x, float* y, int n) noexcept
{
    X87Extended guard;
    const ld f = fc, q = fq;
    ld lowL = st[0], lowR = st[1], bL = st[2], bR = st[3];
    for (int i = 0; i < n; ++i)
    {
        lowL = lowL + bL * f;
        bL = bL + (-(bL * q + lowL) + (ld) x[2 * i]) * f;
        y[2 * i] = (float) lowL;
        lowR = lowR + bR * f;
        bR = bR + (-(bR * q + lowR) + (ld) x[2 * i + 1]) * f;
        y[2 * i + 1] = (float) lowR;
    }
    st[0] = (float) lowL, st[1] = (float) lowR, st[2] = (float) bL, st[3] = (float) bR;
    flushSmall (st, 8); // (the plugin flushes 8 floats: the second stage's state too)
}

// Per-voice processor (FUN_00722070 …)
struct Proc
{
    std::array<Art, 5> arts {};
    int first = 2; // +0x170: 2 = the volume articulator starts above "attack" (gains start at their target)
    bool gainsSet = false;
    float gL = 0, gR = 0;
    float pan = 0, vol = 1, cut = 0, res = 0, pitch = 0;
    int kind = 0, stage2 = 0;
    bool active = false;
    float fc = 0, fq = 0;
    float b[5] {};
    double fst[4] {};
    float st1[16] {}, st2[16] {};
    int endPos = -1;

    void init (const Settings& S) noexcept
    {
        for (int k = 0; k < 5; ++k)
            arts[(std::size_t) k].init (S.recs[(std::size_t) k]);
        first = arts[1].state > 1 ? 2 : 1;
        gainsSet = false;
        gL = gR = 0.0f;
        pan = cut = res = pitch = 0.0f, vol = 1.0f;
        const auto t = S.type >= 0 && S.type < 8 ? S.type : 0;
        kind = filterKind[t][0], stage2 = filterKind[t][1];
        active = kind > 0;
        fc = fq = 0.0f;
        std::fill (std::begin (b), std::end (b), 0.0f);
        std::fill (std::begin (fst), std::end (fst), 0.0);
        std::fill (std::begin (st1), std::end (st1), 0.0f);
        std::fill (std::begin (st2), std::end (st2), 0.0f);
        endPos = -1;
    }

    // FUN_00722830
    void control (const Settings& S) noexcept
    {
        for (int k = 0; k < 5; ++k)
            arts[(std::size_t) k].step (S.recs[(std::size_t) k]);
        const auto& A = arts;
        const auto& R = S.recs;
        pan = (A[0].lfo * R[0].lfoAmt) * 2.0f;
        vol = A[1].val * ((A[1].lfo * R[1].lfoAmt) + 1.0f);
        if (vol < 0.0f)
            vol = 0.0f;
        cut = A[2].val * R[2].amt + A[2].lfo * R[2].lfoAmt;
        res = A[3].val * R[3].amt + A[3].lfo * R[3].lfoAmt;
        pitch = A[4].val * (float) R[4].amountRaw + A[4].lfo * (float) R[4].lfoAmountRaw;
        if (A[1].state == 9 && endPos < 0)
            endPos = 0;
    }
};

// ------------------------------------------------------------------------------------------------------- engine

struct Voice
{
    float fpitch = 0, fpitchBase = 0, fvol = 1, fpan = 0, curPitch = 0;
    float phi = 0, D = 0, vibofs = 0;
    float L1 = 0, T1 = 0, c1 = 0, k1 = 1, L2 = 0, T2 = 0, c2 = 0, k2 = 1;
    std::uint32_t ph1 = 0, inc1 = 0, ph2 = 0, inc2 = 0;
    float target = 0, g = 0, a = 0, env = 0, d = 0;
    int cnt = 0, key = 60;
    bool ended = false, released = false;
    std::uint64_t serial = 0;
    Proc proc;
};

class Engine
{
public:
    void init (const Params& params, double sampleRate, const std::uint8_t* snapshot, int ppq, double tempo)
    {
        P = params;
        fs = (int) std::lround (sampleRate);
        std::memcpy (snap.data(), snapshot, snap.size());
        timing (ppq, tempo);
        update();
        // end-fade table (FUN_0071e060, mode 3, 10 ms): 0.5·(1 + cos((i + 1)·π/(L + 1))), built by the constructor while
        // the plugin's rate object still holds 44 100 Hz, so L = 441 at every rate (EXEC); FL computes the cosine with
        // IPP's ippsCos_32f_A11, LYNX exactly
        const auto L = roundEven (10.0 * 0.001 * 44100.0);
        endTable.resize ((std::size_t) L);
        const auto k = (float) (1.0 / (double) (L + 1) * 3.141592653589793);
        for (std::int64_t i = 0; i < L; ++i)
            endTable[(std::size_t) i] = ((float) std::cos ((double) (((float) i + 1.0f) * k)) + 1.0f) * 0.5f;
    }

    // ProcessEvent 0 (tempo) / SetTimeSig (PPQ): the articulators' tick counts (FUN_00722d80)
    void timing (int ppq, double tempo) noexcept
    {
        const auto keep = S.hasGlobal;
        std::array<std::uint32_t, 5> phases {};
        for (int k = 0; k < 5; ++k)
            phases[(std::size_t) k] = S.recs[(std::size_t) k].globalPhase;
        S.build (snap.data(), ppq, tempo);
        if (keep)
            for (int k = 0; k < 5; ++k)
                S.recs[(std::size_t) k].globalPhase = phases[(std::size_t) k];
    }

    // FUN_00735760
    void update() noexcept
    {
        const auto inv = (float) (1.0 / fs), r44 = (float) (44100.0 / fs);
        auto p = [&] (int i) { return (double) P.p (i); };
        const auto oct = std::floor (p (21) * 6.9);
        D0 = (float) ((double) inv * 8.175798915644 * std::pow (2.0, oct - 2.0));
        auto coarse = [&] (int i) { return (float) std::floor (p (i) * 40.1 * p (i)); };
        auto fine = [&] (int i) {
            if (p (i) >= 0.5)
            {
                switch (roundEven (p (i) * 8.9))
                {
                    case 4: return 0.25f;
                    case 5: return 0.333333343f;
                    case 6: return 0.5f;
                    case 7: return 0.666666687f;
                    default: return 0.75f;
                }
            }
            return (float) (p (i) * 0.2 * p (i));
        };
        ratio1 = (coarse (3) + fine (4)) * 1073741824.0f;
        init1 = (float) ((double) r44 * 0.0002 * p (5) * p (5));
        sus1 = (float) ((double) r44 * 0.0002 * p (7) * p (7));
        vel1 = P.p (9);
        ratio2 = (coarse (14) + fine (15)) * 1073741824.0f;
        init2 = (float) ((double) r44 * 0.0002 * p (16) * p (16));
        sus2 = (float) ((double) r44 * 0.0002 * p (18) * p (18));
        vel2 = P.p (20);
        vib = (float) (p (10) * 0.001 * p (10));
        const auto ninv = (double) -inv;
        att = (float) (1.0 - std::exp (ninv * std::exp (8.0 - p (0) * 8.0)));
        dec = p (1) > 0.98 ? 1.0f : (float) std::exp (ninv * std::exp (5.0 - p (1) * 8.0));
        rel = (float) std::exp (ninv * std::exp (5.0 - p (2) * 5.0));
        time1 = (float) (1.0 - std::exp (ninv * std::exp (6.0 - p (6) * 7.0)));
        mrel1 = (float) (1.0 - std::exp (ninv * std::exp (5.0 - p (8) * 8.0)));
        time2 = (float) (1.0 - std::exp (ninv * std::exp (6.0 - p (17) * 7.0)));
        mrel2 = (float) (1.0 - std::exp (ninv * std::exp (5.0 - p (19) * 8.0)));
        rich = (float) (0.5 - p (11) * 3.0 * p (11));
        thru = (float) (p (12) * 0.25 * p (12));
        lfoRate = (float) ((double) inv * 15707.499999999998 * p (13) * p (13));
        waveform = P.p (11);
    }

    // FUN_00735d90: past the max polyphony, the quietest unfinished voice (lowest envelope target) is finished
    void steal() noexcept
    {
        if (maxPoly <= 0 || maxPoly > count)
            return;
        int unfinished = 0;
        Voice* best = nullptr;
        float lowest = 2.0f;
        for (int i = 0; i < count; ++i)
        {
            auto& v = voices[(std::size_t) order[(std::size_t) i]];
            if (! v.ended)
            {
                ++unfinished;
                if (v.target < lowest)
                    lowest = v.target, best = &v;
            }
        }
        if (unfinished >= maxPoly && best != nullptr)
            best->ended = true, best->target = 0.0f;
    }

    // FUN_00735e30 (TriggerVoice); returns the voice record index
    int trigger (int key, float initVol, float finalPitch, float finalVol, float finalPan) noexcept
    {
        steal();
        if (count >= maxVoices)
            removeAt (oldest());
        int slot = 0;
        while (used[(std::size_t) slot])
            ++slot;
        used[(std::size_t) slot] = true;
        auto& v = voices[(std::size_t) slot];
        v = Voice {};
        v.key = key;
        v.serial = ++serials;
        v.fpitch = v.fpitchBase = v.curPitch = finalPitch;
        v.fvol = finalVol;
        v.fpan = finalPan;
        const auto k = roundEven ((double) finalPitch);
        const auto q = k / 100; // C division
        const auto frac = (float) (k - 100 * q) * 0.01f;
        auto note = (int) ((finalPitch * 0.01f - frac) + 60.0f);
        note = std::min (std::max (note, 0), 127);
        auto x = initVol * 10.0f;
        const auto lim = 20.5511818f;
        if (x > lim)
            x = lim;
        const auto a = std::log ((double) (x + 1.0f)), b = std::log ((double) (lim + 1.0f));
        auto V = roundEven ((double) ((float) (a / b) * 127.0f));
        V = std::min<std::int64_t> (std::max<std::int64_t> (V, 0), 1270);
        const auto Vf = (float) V;
        auto ratio = (float) std::exp ((double) ((float) note + frac) * 0.05776226505);
        v.D = D0 * bend * ratio;
        if ((double) ratio > 50.0)
            ratio = 50.0f;
        const auto l1 = (float) ((double) ratio * ((double) (vel1 * (Vf - 64.0f)) + 64.0));
        v.L1 = init1 * l1, v.T1 = sus1 * l1, v.c1 = time1;
        v.inc1 = (std::uint32_t) roundEven ((double) (ratio1 * v.D));
        const auto l2 = (float) ((double) ratio * ((double) (vel2 * (Vf - 64.0f)) + 64.0));
        v.L2 = init2 * l2, v.T2 = sus2 * l2, v.c2 = time2;
        v.inc2 = (std::uint32_t) roundEven ((double) (ratio2 * v.D));
        v.target = (float) ((1.5 - (double) waveform) * 0.0035000001080334187 * (double) (Vf + 10.0f));
        v.g = (float) (1.2 / (double) v.target);
        v.a = att, v.env = 0.0f, v.d = dec;
        v.proc.init (S);
        order[(std::size_t) count++] = slot;
        return slot;
    }

    // FUN_00736320 (Voice_Release; the processor's articulators are not released)
    void release (int slot) noexcept
    {
        auto& v = voices[(std::size_t) slot];
        v.released = true;
        v.d = rel, v.target = v.env, v.a = 1.0f;
        v.T1 = 0.0f, v.c1 = mrel1, v.T2 = 0.0f, v.c2 = mrel2;
    }

    // FUN_00736800: Final.Pitch changed
    void repitch (Voice& v) noexcept
    {
        const auto k = roundEven ((double) v.curPitch);
        const auto q = k / 100;
        const auto note = (int) std::min<std::int64_t> (std::max<std::int64_t> (q + 60, 0), 127);
        const auto frac = (float) ((double) (k - 100 * q) / 100.0);
        const auto r = (float) std::exp ((double) ((float) note + frac) * 0.05776226505);
        v.D = D0 * bend * r;
        v.inc1 = (std::uint32_t) roundEven ((double) (ratio1 * v.D));
        v.inc2 = (std::uint32_t) roundEven ((double) (ratio2 * v.D));
    }

    // FUN_007364a0 (Voice_Render): mono output into x
    void voiceRender (Voice& v, float* x, int n) noexcept
    {
        if (v.fpitch != v.curPitch)
        {
            v.curPitch = v.fpitch;
            repitch (v);
        }
        const auto& sine = sineTable();
        auto phi = v.phi;
        auto d = v.D + v.vibofs;
        for (int i = 0; i < n; ++i)
        {
            if (v.cnt == 0)
            {
                lfoS = lfoS + lfoRate * lfoC;
                lfoC = lfoC - lfoRate * lfoS;
                v.vibofs = lfoC * (modWheel + vib);
                d = v.D + v.vibofs;
                v.cnt = 100;
            }
            else
                --v.cnt;
            v.env = v.env + v.a * (v.target - v.env);
            v.target = v.target * v.d;
            const auto m1 = v.k1 * sine[v.ph1 >> 18];
            phi = ((phi + d) + v.L1 * m1) + (v.L2 * v.k2) * sine[v.ph2 >> 18];
            v.L1 = v.L1 + v.c1 * (v.T1 - v.L1);
            v.L2 = v.L2 + v.c2 * (v.T2 - v.L2);
            v.ph1 += v.inc1;
            v.ph2 += v.inc2;
            if (std::fabs (phi) > 1.0f)
            {
                if (phi < 0.0f)
                    while (std::fabs (phi) > 1.0f)
                        phi = phi + 2.0f;
                else
                    while (std::fabs (phi) > 1.0f)
                        phi = phi - 2.0f;
            }
            const auto pp = phi * phi;
            const auto shaped = phi + (pp * phi) * ((rich * (pp - 1.0f)) - 1.0f);
            x[i] = (v.g * v.env) * (thru * m1 + shaped);
        }
        v.phi = phi;
        if (bitsOf (v.target) < 961656599) // envelope target below 0.0002 (bit compare)
            v.d = 1.0f, v.ended = true;
        if (bitsOf (v.L1) < 864026624)
            v.L1 = 0.0f, v.T1 = 0.0f;
        if (bitsOf (v.L2) < 864026624)
            v.L2 = 0.0f, v.T2 = 0.0f;
    }

    // NewTick (FUN_00736b00 → FUN_00721f60): flags the next render's articulator step; global LFOs advance once the
    // plugin has listed them (at the first render with voices after a load, FUN_00721d10/FUN_00721c80)
    void newTick() noexcept
    {
        tickPending = true;
        if (listed)
            for (auto& r : S.recs)
                if ((r.flags & 0x20) != 0)
                    r.globalPhase += r.lfoInc;
    }

    // FUN_00722450: the channel filter on x (interleaved stereo), result in y; may scale the target gains
    bool filter (Proc& pr, const float* x, float* y, int n, float& tL, float& tR) noexcept
    {
        const auto old = pr.fc;
        const auto cx = ((float) S.modX * 0.00390625f + 0.0f) + pr.cut; // + Final.FCut (0)
        const auto cy = ((float) S.modY * 0.00390625f + 0.0f) + pr.res;
        if (pr.kind == 0)
        {
            coefFast (cx, cy, pr.fc, pr.fq);
            if (bitsOf (pr.fc) != 0x3f800000 || bitsOf (pr.fq) != 0)
                pr.active = true;
        }
        else if (pr.kind >= 1 && pr.kind <= 4)
            coefBiquad (pr.kind, cx, cy, pr.fc, pr.fq, pr.b);
        else if (pr.kind == 6)
            coefSvf (cx, cy, pr.fc, pr.fq);
        if (! pr.active)
            return false;
        if (pr.kind == 0)
        {
            const auto start = pr.first > 0 ? pr.fc : old;
            const auto step = rampStep (pr.fc, start, n);
            renderFast (pr.fst, pr.fq, x, y, n, start, step);
        }
        else if (pr.kind >= 1 && pr.kind <= 4)
        {
            renderBiquad (pr.st1, pr.b, x, y, n);
            if (pr.stage2 != 0)
            {
                renderBiquad (pr.st2, pr.b, y, y, n);
                const auto g = (float) (1.0 - (double) pr.fq * (3.2 - (double) pr.fc) * 0.046875);
                tL = tL * g, tR = tR * g;
            }
        }
        else if (pr.kind == 6)
        {
            renderSvf (pr.st1, pr.fc, pr.fq, x, y, n);
            if (pr.stage2 != 0)
                renderSvf (pr.st2, pr.fc, pr.fq + (1.0f - pr.fq) * 0.25f, y, y, n);
        }
        else
            return false;
        return true;
    }

    // Gen_Render (FUN_00736a10): voices through the processor, mixed into l/r (overwrites)
    void render (float* l, float* r, int n) noexcept
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        if (count > 0)
            listed = true;
        const auto tick = tickPending;
        tickPending = false;
        if ((int) mono.size() < n)
            return; // (prepared for at most `prepareBlock`)
        for (int i = 0; i < count; ++i)
        {
            auto& v = voices[(std::size_t) order[(std::size_t) i]];
            auto& pr = v.proc;
            if (tick || pr.first > 0)
            {
                pr.control (S);
                v.fpitch = v.fpitchBase + pr.pitch; // FL's host restores Final.Pitch every tick (inference)
            }
            voiceRender (v, mono.data(), n);
            for (int j = 0; j < n; ++j)
                bufA[2 * (std::size_t) j] = bufA[2 * (std::size_t) j + 1] = mono[(std::size_t) j];
            float tL, tR;
            panLaw (v.fpan + pr.pan, v.fvol * pr.vol, tL, tR);
            if (! pr.gainsSet)
                pr.gL = pr.gR = 0.0f, pr.gainsSet = true;
            if (pr.first > 1)
                pr.gL = tL, pr.gR = tR; // the first block starts at the target
            const float* x = bufA.data();
            if (filter (pr, bufA.data(), bufB.data(), n, tL, tR))
                x = bufB.data();
            if (pr.endPos >= 0) // end fade (FUN_0071e8b0, mode 3): the table, then silence
            {
                auto* w = const_cast<float*> (x);
                const auto L = (int) endTable.size();
                const auto k = std::min (std::max (L - pr.endPos, 0), n);
                for (int j = 0; j < k; ++j)
                {
                    const auto t = endTable[(std::size_t) (pr.endPos + j)];
                    w[2 * j] = w[2 * j] * t;
                    w[2 * j + 1] = w[2 * j + 1] * t;
                }
                std::fill (w + 2 * k, w + 2 * n, 0.0f);
                pr.endPos += n;
                if (pr.endPos >= L)
                    v.ended = true;
            }
            const auto sL = rampStep (tL, pr.gL, n), sR = rampStep (tR, pr.gR, n);
            if (sL == 0.0f && sR == 0.0f)
            {
                for (int j = 0; j < n; ++j)
                {
                    l[j] = x[2 * j] * tL + l[j];
                    r[j] = x[2 * j + 1] * tR + r[j];
                }
            }
            else
                mixRamp (x, l, r, n, pr.gL, pr.gR, sL, sR);
            pr.gL = tL, pr.gR = tR;
            pr.first = 0;
        }
        // the plugin asks the host to kill finished voices, last to first; the host's kill removes them at once
        for (int i = count - 1; i >= 0; --i)
            if (voices[(std::size_t) order[(std::size_t) i]].ended)
                removeAt (i);
    }

    // FUN_005f9760 (SSE path): lanes [L0, R0, L1, R1] start at g and g + s, +2s per pair of frames, single frames +s
    static void mixRamp (const float* x, float* l, float* r, int n, float gL, float gR, float sL, float sR) noexcept
    {
        float a0 = gL + 0.0f, a1 = gR + 0.0f, a2 = gL + sL, a3 = gR + sR;
        const float s2L = sL + sL, s2R = sR + sR;
        const int full = (n >> 3) << 3;
        for (int i = 0; i < full; i += 2)
        {
            l[i] = x[2 * i] * a0 + l[i];
            r[i] = x[2 * i + 1] * a1 + r[i];
            l[i + 1] = x[2 * i + 2] * a2 + l[i + 1];
            r[i + 1] = x[2 * i + 3] * a3 + r[i + 1];
            a0 = a0 + s2L, a1 = a1 + s2R, a2 = a2 + s2L, a3 = a3 + s2R;
        }
        for (int i = full; i < n; ++i)
        {
            l[i] = x[2 * i] * a0 + l[i];
            r[i] = x[2 * i + 1] * a1 + r[i];
            a0 = a0 + sL, a1 = a1 + sR;
        }
    }

    // unordered list: the last voice takes the removed one's place (FUN_005ea710)
    void removeAt (int i) noexcept
    {
        if (i < 0 || i >= count)
            return;
        used[(std::size_t) order[(std::size_t) i]] = false;
        order[(std::size_t) i] = order[(std::size_t) (count - 1)];
        --count;
    }

    int oldest() const noexcept
    {
        int best = 0;
        for (int i = 1; i < count; ++i)
            if (voices[(std::size_t) order[(std::size_t) i]].serial < voices[(std::size_t) order[(std::size_t) best]].serial)
                best = i;
        return best;
    }

    void reset() noexcept
    {
        count = 0;
        used.fill (false);
        lfoS = 0.0f, lfoC = 1.0f;
        listed = false, tickPending = false;
        for (auto& r : S.recs)
            r.globalPhase = 0;
    }

    void prepareBlock (int maxBlock)
    {
        const auto m = (std::size_t) std::max (maxBlock, 1);
        mono.assign (m, 0.0f);
        bufA.assign (2 * m, 0.0f);
        bufB.assign (2 * m, 0.0f);
    }

    Params P;
    Settings S;
    std::array<std::uint8_t, snapshotSize> snap = defaultSnapshot();
    int fs = 44100, maxPoly = 0;
    float D0 = 0, ratio1 = 0, init1 = 0, sus1 = 0, vel1 = 0, ratio2 = 0, init2 = 0, sus2 = 0, vel2 = 0, vib = 0;
    float att = 0, dec = 1, rel = 1, time1 = 0, mrel1 = 0, time2 = 0, mrel2 = 0, rich = 0, thru = 0, lfoRate = 0, waveform = 0;
    float lfoS = 0.0f, lfoC = 1.0f, modWheel = 0.0f, bend = 1.0f; // plugin globals after the constructor
    bool listed = false, tickPending = false;
    std::array<Voice, maxVoices> voices {};
    std::array<bool, maxVoices> used {};
    std::array<int, maxVoices> order {};
    int count = 0;
    std::uint64_t serials = 0;
    std::vector<float> mono, bufA, bufB, endTable;
};

const std::uint8_t* snapshotOf (const std::vector<std::uint8_t>& state, const std::array<std::uint8_t, snapshotSize>& fallback) noexcept
{
    return state.size() >= (std::size_t) stateWithSnapshot ? state.data() + 96 : fallback.data();
}
} // namespace

bool dx10StateUsable (const std::vector<std::uint8_t>& state) { return parseState (state).has_value(); }

std::vector<std::uint8_t> dx10StateWithSnapshot (const std::vector<std::uint8_t>& state, const std::vector<std::uint8_t>& snapshot, bool replace)
{
    if (! parseState (state) || snapshot.size() < (std::size_t) snapshotSize)
        return {};
    if (state.size() >= (std::size_t) stateWithSnapshot && ! replace)
        return state;
    std::vector<std::uint8_t> out (state.begin(), state.begin() + 4 + 4 * paramCount);
    const std::int32_t version = 2;
    std::memcpy (out.data(), &version, 4);
    out.resize (96, 0);
    out.insert (out.end(), snapshot.begin(), snapshot.begin() + snapshotSize);
    return out;
}

bool dx10ProcessorNeutral (const std::vector<std::uint8_t>& state)
{
    if (! dx10StateUsable (state))
        return false;
    static const auto defaults = defaultSnapshot();
    const auto* s = snapshotOf (state, defaults);
    auto at = [&] (int k, int i) {
        std::int32_t v;
        std::memcpy (&v, s + 0x44 * k + 4 * i, 4);
        return v;
    };
    for (int k = 0; k < 5; ++k)
    {
        if (at (k, 11) != 0) // LFO amount
            return false;
        if (k == 1 ? ! (at (k, 1) == 0 && (at (k, 0) & 4) != 0) : (k != 0 && at (k, 1) != 0 && at (k, 8) != 0))
            return false;
    }
    std::int32_t modX, modY, type;
    std::memcpy (&modX, s + 0x15c, 4);
    std::memcpy (&modY, s + 0x160, 4);
    std::memcpy (&type, s + 0x168, 4);
    return type == 0 && modX >= 256 && modY <= 0;
}

std::optional<int> dx10Param (const std::vector<std::uint8_t>& state, int index)
{
    const auto P = parseState (state);
    if (! P || index < 0 || index >= paramCount)
        return std::nullopt;
    return P->raw[(std::size_t) index];
}

bool dx10SetParam (std::vector<std::uint8_t>& state, int index, int value)
{
    if (! parseState (state) || index < 0 || index >= paramCount)
        return false;
    value = index == 21 ? std::clamp (value, -3, 3) : std::clamp (value, 0, 65536);
    const auto v = (std::uint32_t) value;
    std::memcpy (state.data() + 4 + 4 * (std::size_t) index, &v, 4);
    return true;
}

namespace
{
// ------------------------------------------------------------------------------------------------------- instrument

struct Setup
{
    Params P;
    std::array<std::uint8_t, snapshotSize> snap = defaultSnapshot();
    int ppq = 96;
    double tempo = 140.0;
};

class Dx10Instrument final : public Instrument
{
public:
    Dx10Instrument()
    {
        engine.prepareBlock (blockSize);
        engine.init (Params {}, 44100.0, defaultSnapshot().data(), 96, 140.0);
    }

    ~Dx10Instrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    void prepare (double sr) noexcept override
    {
        rate = sr;
        take();
        engine.init (current.P, sr, current.snap.data(), current.ppq, current.tempo);
        engine.reset();
        tickPhase = 0.0;
        playing = false;
        lastTicks = -1.0;
    }

    // UI thread: the parsed state goes through a mailbox (the audio thread takes it, this thread frees it)
    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        delete retired.exchange (nullptr);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        maxPoly.store (channel.synth.mono ? 1 : 0);
        const auto ppq = channel.modelParams.empty() ? 96 : std::clamp ((int) channel.modelParams[0], 1, 1 << 16);
        const auto tempo = bpm > 0.0 ? bpm : 140.0;
        if (channel.pluginState == state && ppq == lastPpq && tempo == lastTempo)
            return;
        state = channel.pluginState, lastPpq = ppq, lastTempo = tempo;
        if (auto p = parseState (state))
        {
            auto s = std::make_unique<Setup>();
            s->P = *p;
            static const auto defaults = defaultSnapshot();
            std::memcpy (s->snap.data(), snapshotOf (state, defaults), snapshotSize);
            s->ppq = ppq, s->tempo = tempo;
            delete incoming.exchange (s.release());
        }
    }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        const auto vel = std::clamp ((double) velocity, 0.0, 1.0);
        const auto initVol = (float) ((std::pow (11.0, 1.28 * vel) - 1.0) / 10.0);
        const auto k = std::clamp (key, 0, 127);
        const auto finalPitch = (float) (100.0 * (k - 60) + (double) pitchCents.load() + (double) nextCents);
        const auto slot = engine.trigger (k, initVol, finalPitch, (float) (channelVol * (double) initVol),
                                          (float) std::clamp ((double) nextPan + channelPan, -1.0, 1.0));
        volFactor[(std::size_t) slot] = initVol;
        notePan[(std::size_t) slot] = nextPan;
        nextPan = 0.0f;
        nextCents = 0.0f;
    }

    void noteOff (int key) noexcept override
    {
        for (int i = 0; i < engine.count; ++i)
        {
            const auto slot = engine.order[(std::size_t) i];
            const auto& v = engine.voices[(std::size_t) slot];
            if (v.key == key && ! v.released)
                engine.release (slot);
        }
    }

    void allNotesOff() noexcept override
    {
        for (int i = 0; i < engine.count; ++i)
        {
            const auto slot = engine.order[(std::size_t) i];
            if (! engine.voices[(std::size_t) slot].released)
                engine.release (slot);
        }
    }

    bool idle() const noexcept override { return engine.count == 0; }

    // while the transport moves, FL's sequencer ticks fall on the song's PPQ grid
    void setSongPosition (double ticks) noexcept override
    {
        playing = lastTicks >= 0.0 && ticks != lastTicks;
        lastTicks = ticks;
        if (playing)
            tickPhase = ticks / (double) ppq * (double) current.ppq;
    }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        take();
        engine.maxPoly = maxPoly.load();
        // FL's channel volume/pan from LYNX's gains (gl = √2·V·cos((1 + p)π/4), gr = √2·V·sin(…)); FL recomputes the
        // voices' FinalLevels when the channel level changes
        const auto vol = std::sqrt (((double) gl * gl + (double) gr * gr) * 0.5);
        auto pan = vol > 0.0 ? std::atan2 ((double) gr, (double) gl) * (4.0 / 3.141592653589793) - 1.0 : 0.0;
        if (std::fabs (pan) < 1e-9)
            pan = 0.0;
        if (vol != channelVol || pan != channelPan)
        {
            channelVol = vol, channelPan = pan;
            for (int i = 0; i < engine.count; ++i)
            {
                const auto slot = engine.order[(std::size_t) i];
                auto& v = engine.voices[(std::size_t) slot];
                v.fvol = (float) (vol * (double) volFactor[(std::size_t) slot]);
                v.fpan = (float) std::clamp ((double) notePan[(std::size_t) slot] + pan, -1.0, 1.0);
            }
        }
        // FL's sequencer ticks (PPQ per beat): a tick boundary starts a new host block, NewTick first
        const auto perSample = current.tempo * (double) current.ppq / (60.0 * rate);
        for (int done = 0; done < n;)
        {
            if (std::fabs (tickPhase - std::round (tickPhase)) < 1e-9)
            {
                tickPhase = std::round (tickPhase);
                engine.newTick();
            }
            const auto next = std::floor (tickPhase) + 1.0;
            const auto toNext = std::max (1, (int) std::ceil ((next - tickPhase) / perSample - 1e-9));
            const auto len = std::min ({ blockSize, n - done, toNext });
            engine.render (bufL.data(), bufR.data(), len);
            for (int j = 0; j < len; ++j)
            {
                left[done + j] += bufL[(std::size_t) j];
                right[done + j] += bufR[(std::size_t) j];
            }
            tickPhase = len == toNext ? next : tickPhase + (double) len * perSample;
            done += len;
        }
    }

private:
    void take() noexcept
    {
        if (retired.load() != nullptr)
            return;
        if (auto* s = incoming.exchange (nullptr))
        {
            current = *s;
            engine.P = current.P;
            std::memcpy (engine.snap.data(), current.snap.data(), snapshotSize);
            engine.timing (current.ppq, current.tempo); // (FL: parameters and tempo reach the plugin while voices play)
            engine.update();
            retired.store (s);
        }
    }

    static constexpr int blockSize = 512;
    Engine engine;
    Setup current;
    std::atomic<Setup*> incoming { nullptr }, retired { nullptr };
    std::atomic<float> pitchCents { 0.0f };
    std::atomic<int> maxPoly { 0 };
    std::vector<std::uint8_t> state;
    int lastPpq = -1;
    double lastTempo = -1.0;
    std::array<float, blockSize> bufL {}, bufR {};
    std::array<float, maxVoices> volFactor {}, notePan {};
    double rate = 44100.0, channelVol = 1.0, channelPan = 0.0, tickPhase = 0.0, lastTicks = -1.0;
    bool playing = false;
    float nextPan = 0.0f, nextCents = 0.0f;
};
} // namespace

std::unique_ptr<Instrument> makeDx10Instrument() { return std::make_unique<Dx10Instrument>(); }

namespace dx10
{
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const RefOptions& opt)
{
    const auto P = parseState (state);
    if (! P || length <= 0)
        return {};
    static const auto defaults = defaultSnapshot();
    auto engine = std::make_unique<Engine>();
    engine->init (*P, sampleRate, snapshotOf (state, defaults), opt.ppq, opt.tempo);
    engine->maxPoly = opt.maxPoly;
    const auto block = std::max (1, opt.block);
    engine->prepareBlock (block);
    std::vector<int> slot (notes.size(), -1);
    std::vector<std::uint64_t> serial (notes.size(), 0);
    std::vector<float> out (2 * (std::size_t) length, 0.0f), l ((std::size_t) block), r ((std::size_t) block);
    // events at a sample in the model's order: sorted by sample, then in list order (stable)
    std::vector<std::pair<int, int>> order; // (sample, 2·note + isOff)
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        order.push_back ({ notes[i].start, 2 * (int) i });
        if (notes[i].release >= 0)
            order.push_back ({ notes[i].release, 2 * (int) i + 1 });
    }
    std::stable_sort (order.begin(), order.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    int n0 = 0;
    std::size_t next = 0;
    while (n0 < length)
    {
        auto ln = std::min (block, length - n0);
        for (const auto& e : order)
            if (n0 < e.first && e.first < n0 + ln)
                ln = e.first - n0;
        if (opt.tick > 0)
        {
            const auto nt = (n0 / opt.tick + 1) * opt.tick;
            if (nt < n0 + ln)
                ln = nt - n0;
            if (n0 % opt.tick == 0)
                engine->newTick();
        }
        while (next < order.size() && order[next].first < n0)
            ++next;
        while (next < order.size() && order[next].first == n0)
        {
            const auto i = (std::size_t) (order[next].second / 2);
            const auto& nt = notes[i];
            if (order[next].second % 2 == 0)
            {
                slot[i] = engine->trigger (nt.key, (float) nt.initVol, (float) (100.0 * nt.key - 6000.0 + nt.pitchOffset), (float) nt.finalVol,
                                           (float) nt.finalPan);
                serial[i] = engine->voices[(std::size_t) slot[i]].serial;
            }
            else if (slot[i] >= 0 && engine->used[(std::size_t) slot[i]] && engine->voices[(std::size_t) slot[i]].serial == serial[i])
                engine->release (slot[i]);
            ++next;
        }
        engine->render (l.data(), r.data(), ln);
        for (int j = 0; j < ln; ++j)
        {
            out[2 * (std::size_t) (n0 + j)] = l[(std::size_t) j];
            out[2 * (std::size_t) (n0 + j) + 1] = r[(std::size_t) j];
        }
        n0 += ln;
    }
    return out;
}
} // namespace dx10
} // namespace lynx
