#include "core/DirectWave.h"

#include "core/BassDrum.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Behaviour per docs/analise/REA_rodada72.md. Every float operation keeps the order and precision the plugin uses
// (single precision unless noted), so that the output matches the DLL's bit for bit on the same data.

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace lynx
{
namespace dw
{
namespace
{
// ------------------------------------------------------------------------------------------------- bits
float fbits (std::uint32_t b) { float f; std::memcpy (&f, &b, 4); return f; }
std::uint32_t bitsf (float f) { std::uint32_t b; std::memcpy (&b, &f, 4); return b; }

const float kTwoPi = fbits (0x40c90fdbu);     // 6.2831855f
const float kPi = fbits (0x40490fdbu);
const float kInvPi = fbits (0x3ea2f983u);
const float kTwoInvPi = fbits (0x3f22f983u);
const float kInt16 = fbits (0x38000100u);     // 1/32767
const float kTiny = fbits (0x283424dcu);      // 1e-14 (added against denormals)
const float kRand15 = fbits (0x38000000u);    // 1/32768
const float kRandLfo = fbits (0x38800000u);   // 1/16384
const float kQBase = fbits (0x3f3504f3u);     // 0.70710677f
const float kQRes = fbits (0x3f8147aeu);      // 1.01f
const double kFrac = 2.3283064365386963e-10;  // 2^-32
const double kOne32 = 4294967296.0;

// ------------------------------------------------------------------------------------------------- reading
struct Chunk { std::uint32_t id; const std::uint8_t* data; std::size_t size; };

template <typename Fn>
void forEachChunk (const std::uint8_t* p, std::size_t n, Fn&& fn)
{
    std::size_t o = 0;
    while (o + 12 <= n)
    {
        std::uint32_t id; std::uint64_t sz;
        std::memcpy (&id, p + o, 4);
        std::memcpy (&sz, p + o + 4, 8);
        if (sz > n - o - 12)
            return;
        fn (Chunk { id, p + o + 12, (std::size_t) sz });
        o += 12 + (std::size_t) sz;
    }
}

float rf (const std::uint8_t* p) { float f; std::memcpy (&f, p, 4); return f; }
std::int32_t ri (const std::uint8_t* p) { std::int32_t v; std::memcpy (&v, p, 4); return v; }
std::uint16_t ru16 (const std::uint8_t* p) { std::uint16_t v; std::memcpy (&v, p, 2); return v; }

void readZone (const Chunk& z, Zone& zone)
{
    int filters = 0, envs = 0, lfos = 0, mods = 0;
    forEachChunk (z.data, z.size, [&] (const Chunk& c) {
        const auto* b = c.data;
        switch (c.id)
        {
            case 0x1F4:
                if (c.size >= 25)
                {
                    zone.root = b[0]; zone.lowKey = b[1]; zone.highKey = b[2]; zone.lowVel = b[3]; zone.highVel = b[4];
                    zone.exclusiveGroup = b[5]; zone.output = b[6]; zone.parallelFilters = b[8] != 0;
                    zone.gain = rf (b + 9); zone.pan = rf (b + 13); zone.altGroup = ri (b + 17);
                }
                break;
            case 0x1F5: break;
            case 0x1F6: zone.path.assign ((const char*) b, c.size); break;
            case 0x1F7:
                if (c.size >= 40)
                {
                    std::memcpy (&zone.frames, b, 8);
                    zone.channels = ri (b + 8); zone.beats = ri (b + 12); zone.rate = rf (b + 16);
                    zone.loopMode = ri (b + 20); zone.loopA = ri (b + 24); zone.loopB = ri (b + 28);
                    zone.offset = zone.frames < (std::int64_t) (std::uint32_t) ri (b + 32) ? 0 : ri (b + 32);
                    zone.bits = ri (b + 36);
                }
                break;
            case 0x1F8:
                if (c.size >= 8)
                {
                    zone.pitch = rf (b); zone.coarse = (std::int8_t) b[4]; zone.fine = (std::int8_t) b[5];
                    zone.keyTrack = (std::int16_t) ru16 (b + 6);
                }
                break;
            case 0x1F9:
                if (c.size >= 14)
                {
                    zone.stretchMode = b[0]; zone.stretchSync = b[1];
                    zone.stretch[0] = rf (b + 2); zone.stretch[1] = rf (b + 6); zone.stretch[2] = rf (b + 10);
                }
                break;
            case 0x1FA:
                if (c.size >= 16)
                    for (int i = 0; i < 4; ++i)
                        zone.sends[i] = rf (b + 4 * i);
                break;
            case 0x1FB:
                if (filters < 2 && c.size >= 20)
                {
                    auto& f = zone.filter[filters++];
                    f.type = b[0]; f.cutoff = rf (b + 4); f.res = rf (b + 8); f.shape = rf (b + 12); f.extra = rf (b + 16);
                }
                break;
            case 0x1FC:
                if (c.size >= 2) { zone.syncTempo = b[0] != 0; zone.sliceKeys = b[1] != 0; }
                break;
            case 0x1FD:
                if (c.size >= 16)
                    for (int i = 0; i < 4; ++i)
                        zone.amp[i] = rf (b + 4 * i);
                break;
            case 0x1FE: case 0x1FF: case 0x200: case 0x201:
                if (c.size >= 9)
                {
                    const auto k = (int) c.id - 0x1FE;
                    zone.fxOn[k] = b[0]; zone.fx[k][0] = rf (b + 1); zone.fx[k][1] = rf (b + 5);
                }
                break;
            case 0x202:
                if (envs < 2 && c.size >= 16)
                {
                    for (int i = 0; i < 4; ++i)
                        zone.modEnv[envs][i] = rf (b + 4 * i);
                    ++envs;
                }
                break;
            case 0x203:
                if (lfos < 2 && c.size >= 20)
                {
                    auto& l = zone.lfo[lfos++];
                    l.type = b[0]; l.sync = b[1]; l.rate = rf (b + 4); l.phase = rf (b + 8); l.fade = rf (b + 12); l.extra = rf (b + 16);
                }
                break;
            case 0x204:
                if (mods < 16 && c.size >= 8)
                {
                    auto& m = zone.mods[(std::size_t) mods++];
                    m.src = ru16 (b); m.dst = ru16 (b + 2); m.amount = rf (b + 4);
                }
                break;
            case 0x205:
                zone.embedded.assign (b, b + c.size);
                break;
            default: break;
        }
    });
}

void readProgram (const Chunk& top, Program& p)
{
    int lfos = 0, values = 0;
    forEachChunk (top.data, top.size, [&] (const Chunk& c) {
        const auto* b = c.data;
        switch (c.id)
        {
            case 0x64:
                if (c.size >= 30)
                {
                    p.index = ri (b);
                    p.voiceMode = b[4];
                    p.glideLegatoOnly = b[5] != 0;
                    p.gain = rf (b + 6);
                    p.glide = rf (b + 10);
                }
                break;
            case 0x66: p.name.assign ((const char*) b, c.size); break;
            case 0x68:
                if (c.size >= 10) { p.shape1 = b[0] != 0; p.shape2 = b[1] != 0; p.shape1Amount = rf (b + 2); p.shape2Amount = rf (b + 6); }
                break;
            case 0x69:
                if (c.size >= 18)
                {
                    p.delayOn = b[0] != 0; p.delayCross = b[1] != 0;
                    for (int i = 0; i < 4; ++i) p.delay[i] = rf (b + 2 + 4 * i);
                }
                break;
            case 0x6A:
                if (c.size >= 17) { p.reverbOn = b[0] != 0; for (int i = 0; i < 4; ++i) p.reverb[i] = rf (b + 1 + 4 * i); }
                break;
            case 0x6B:
                if (c.size >= 17) { p.chorusOn = b[0] != 0; for (int i = 0; i < 4; ++i) p.chorus[i] = rf (b + 1 + 4 * i); }
                break;
            case 0x6C:
                if (lfos < 2 && c.size >= 20)
                {
                    auto& l = p.lfo[lfos++];
                    l.type = b[0]; l.sync = b[1]; l.rate = rf (b + 4); l.phase = rf (b + 8); l.fade = rf (b + 12); l.extra = rf (b + 16);
                }
                break;
            case 0x6D:
                if (values < 4 && c.size == 4)
                    p.modValue[values++] = rf (b);
                break;
            case 0x6E:
                if (c.size >= 13)
                {
                    const auto i = ri (b);
                    if (i >= 1 && i <= 99)
                    {
                        auto& g = p.groups[(std::size_t) i];
                        g.mode = b[4] > 3 ? 0 : b[4];
                        g.probability = rf (b + 5);
                        g.velTolerance = rf (b + 9);
                    }
                }
                break;
            case 3:
            {
                Zone z;
                readZone (c, z);
                p.zones.push_back (std::move (z));
                break;
            }
            default: break;
        }
    });
}
} // namespace

std::optional<State> parseState (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 16)
        return std::nullopt;
    std::uint32_t version = 0;
    std::memcpy (&version, d.data(), 4);
    if (version < 36 || version > 38)
        return std::nullopt;
    State s;
    forEachChunk (d.data() + 4, d.size() - 4, [&] (const Chunk& c) {
        if (c.id == 6 && c.size >= 4)
            s.flags = (std::uint32_t) ri (c.data);
        else if (c.id == 1)
        {
            Program p;
            readProgram (c, p);
            if (! p.zones.empty())
                s.programs.push_back (std::move (p));
        }
    });
    if (s.programs.empty())
        return std::nullopt;
    return s;
}

// ================================================================================================= CRT math
// The plugin's run-time library (AMD-derived, FMA3 code paths on the machines FL runs on): see REA_rodada72 §CRT.
namespace
{
double dbits (std::uint64_t b) { double v; std::memcpy (&v, &b, 8); return v; }
std::uint64_t bitsd (double v) { std::uint64_t b; std::memcpy (&b, &v, 8); return b; }

// polynomial coefficients of the run-time's single-precision sine and cosine (on |r| ≤ π/4, in double)
const double kS1 = -0.16666666666666666, kS2 = 0.0083333333333333332, kS3 = -0.00019841269841269841, kS4 = 2.7557319223985893e-06;
const double kC1 = 0.041666666666666664, kC2 = -0.0013888888888888887, kC3 = 2.4801587301587298e-05, kC4 = -2.7557319223985888e-07;
const double kPio2Hi = 1.5707963267341256, kPio2Lo = 6.0771005065061922e-11, kTwoOverPi = 0.63661977236758138;

double sinPoly (double r)
{
    const double x2 = r * r;
    double p = std::fma (x2, kS4, kS3);
    p = std::fma (p, x2, kS2);
    p = std::fma (p, x2, kS1);
    return std::fma (p, r * x2, r);
}

// cosine polynomial; `fusedHead`: 1 − x²/2 as one fused step (sine's reduced path) or rounded first (cosine's)
double cosPoly (double r, bool fusedHead)
{
    const double x2 = r * r;
    const double head = fusedHead ? std::fma (x2, -0.5, 1.0) : 1.0 - x2 * 0.5;
    double p = std::fma (x2, kC4, kC3);
    p = std::fma (p, x2, kC2);
    p = std::fma (p, x2, kC1);
    return std::fma (p, x2 * x2, head);
}

// Cody–Waite reduction by π/2 for |x| below 2^20·π/2: region and remainder
bool reduce (double ax, int& region, double& r)
{
    if (ax >= 1.0e6)
        return false;
    const int n = (int) std::fma (ax, kTwoOverPi, 0.5);
    const double dn = (double) n;
    const double hi = std::fma (-dn, kPio2Hi, ax);
    const double t = dn * kPio2Lo;
    r = hi - t;
    region = n & 3;
    return true;
}
} // namespace

float sinfCrt (float x)
{
    if (! std::isfinite (x))
        return (float) std::sin ((double) x);
    const double xd = (double) x;
    const double ax = std::fabs (xd);
    if (ax <= 0.78539816339744828)
    {
        if (ax < 0.0078125)
        {
            if (ax < 0.0001220703125)
                return x;
            return (float) std::fma (-(xd * xd * xd), 0.16666666666666666, xd);
        }
        return (float) sinPoly (xd);
    }
    int region; double r;
    if (! reduce (ax, region, r))
        return (float) std::sin (xd);
    double y = (region & 1) ? cosPoly (r, true) : sinPoly (r);
    if (region >= 2) y = -y;
    if (xd < 0.0) y = -y;
    return (float) y;
}

float cosfCrt (float x)
{
    if (! std::isfinite (x))
        return (float) std::cos ((double) x);
    const double xd = (double) x;
    const double ax = std::fabs (xd);
    if (ax <= 0.78539816339744828)
    {
        if (ax < 0.0078125)
        {
            if (ax < 0.0001220703125)
                return 1.0f;
            return (float) std::fma (-(xd * 0.5), xd, 1.0);
        }
        return (float) cosPoly (xd, false);
    }
    int region; double r;
    if (! reduce (ax, region, r))
        return (float) std::cos (xd);
    double y = (region & 1) ? sinPoly (r) : cosPoly (r, false);
    if (((region + 1) >> 1) & 1) y = -y;
    return (float) y;
}

float expfCrt (float x)
{
    static const auto table = [] {
        std::array<double, 64> t {};
        for (int i = 0; i < 64; ++i)
            t[(std::size_t) i] = std::exp2 (i / 64.0);
        return t;
    }();
    const double xd = (double) x;
    const double t = xd * 92.332482616893657;
    if (! std::isfinite (xd) || t >= 8192.0 || t < -9600.0)
        return (float) std::exp (xd);
    const auto k = (int) std::nearbyint (t);
    const double r = std::fma (-(double) k, 0.010830424696249145, xd);
    const double p = std::fma (std::fma (0.16666666666666666, r, 0.5), r * r, r);
    const double e = table[(std::size_t) (k & 63)];
    const double q = std::fma (p, e, e);
    return (float) (q * dbits ((std::uint64_t) ((k - (k & 63)) / 64 + 0x3ff) << 52));
}

float powfCrt (float x, float y) { return bassdrum::powfMsvc (x, y); }
double powCrt (double x, double y) { return std::pow (x, y); }

// ================================================================================================= engine
namespace
{
std::uint32_t crtRand (std::uint32_t& s)
{
    s = s * 0x343fdu + 0x269ec3u;
    return (s >> 16) & 0x7fffu;
}

// rate per step of a time value v (0…1): 1/(v⁴·r·10 + r·0.001), 1 when v ≤ 0
float rateOf (float v, float r)
{
    if (v <= 0.0f)
        return 1.0f;
    return 1.0f / (v * v * v * v * r * 10.0f + r * fbits (0x3a83126fu));
}

struct Env
{
    int state = 0;
    float level = 0.0f;

    void start() { state = 1; level = 0.0f; }
    void release() { if (state > 0) state = 4; }
    float tick (float a, float dcy, float s, float r)
    {
        if (state == 4)
        {
            level = level - r;
            if (! (level > 0.0f)) { state = 0; level = 0.0f; return level * level; }
        }
        else if (state == 2)
        {
            level = level - dcy;
            if (! (level > s)) { level = s; state = 3; return level * level; }
        }
        else if (state == 1)
        {
            level = a + level;
            if (! (level < 1.0f)) { level = 1.0f; state = 2; }
        }
        return level * level;
    }
};

struct LfoState
{
    float phase = 0.0f, fade = 0.0f, held = 0.0f, smooth = 0.0f;

    void reset (float startPhase)
    {
        fade = 0.0f;
        if (startPhase < 1.0f)
            phase = kTwoPi * startPhase;
    }

    float tick (int type, float inc, float fadeInc, std::uint32_t& rnd)
    {
        float v = 0.0f;
        switch (type)
        {
            case 0: v = sinfCrt (phase); break;
            case 1: v = std::fabs (sinfCrt (phase)); break;
            case 2: v = phase < kPi ? phase * kTwoInvPi - 1.0f : 3.0f - phase * kTwoInvPi; break;
            case 3: v = phase < kPi ? -1.0f : 1.0f; break;
            case 4: v = kInvPi * phase - 1.0f; break;
            case 5: v = 1.0f - kInvPi * phase; break;
            case 6: v = held; break;
            case 7: smooth = (held - smooth) * inc + smooth; v = smooth; break;
            default: v = 0.0f; break; // (an uninitialised register in the plugin)
        }
        phase = inc + phase;
        if (! (phase < kTwoPi))
        {
            const auto r = (int) crtRand (rnd);
            phase = phase - kTwoPi;
            held = (float) (r - 0x4000) * kRandLfo;
        }
        float f = fadeInc + fade;
        fade = f;
        if (f > 1.0f)
        {
            fade = 1.0f;
            f = 1.0f;
        }
        return f * v;
    }
};

struct Biquad
{
    float c[5] { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }; // b0, b1, b2, a1, a2

    void design (int type, float hz, float q, float sr)
    {
        if (type == 0)
        {
            c[0] = 1.0f; c[1] = c[2] = c[3] = c[4] = 0.0f;
            return;
        }
        const float w = (kTwoPi * hz) / sr;
        const float s = sinfCrt (w), cs = cosfCrt (w);
        const float alpha = s / (q + q);
        const float a0 = alpha + 1.0f;
        const float inv = 1.0f / a0;
        const float a1 = (cs * -2.0f) * inv;
        const float a2 = (1.0f - alpha) * inv;
        switch (type)
        {
            case 1:
                c[1] = (1.0f - cs) * inv;
                c[0] = c[2] = ((1.0f - cs) * 0.5f) * inv;
                c[3] = a1; c[4] = a2;
                break;
            case 2:
            {
                const float p = cs + 1.0f;
                c[0] = c[2] = (p * 0.5f) * inv;
                c[1] = (-p) * inv;
                c[3] = a1; c[4] = a2;
                break;
            }
            case 3:
                c[0] = (s * 0.5f) * inv;
                c[1] = 0.0f;
                c[2] = ((-s) * 0.5f) * inv;
                c[3] = a1; c[4] = a2;
                break;
            case 4:
                c[0] = c[2] = inv;
                c[1] = c[3] = a1;
                c[4] = a2;
                break;
            case 5:
                c[0] = c[4] = a2;
                c[2] = a0 * inv;
                c[1] = c[3] = a1;
                break;
            default: break;
        }
    }
};

// two passes of the same biquad, then an optional clip at ±(1 − shape)
struct BiquadState
{
    float s[8] {};

    void clear() { std::fill (std::begin (s), std::end (s), 0.0f); }

    float run (float x, float shape, const float* c)
    {
        const float t = ((x * c[0] + s[2] * c[1]) + c[2] * s[3] - s[0] * c[3]) - s[1] * c[4];
        s[3] = s[2]; s[1] = s[0]; s[2] = x; s[0] = t;
        const float u = ((t * c[0] + s[6] * c[1]) + c[2] * s[7] - s[4] * c[3]) - c[4] * s[5];
        s[7] = s[6]; s[6] = t; s[5] = s[4]; s[4] = u;
        if (shape > 0.0f)
        {
            const float lim = 1.0f - shape;
            float y = u <= lim ? u : lim;
            const float nl = -lim;
            return nl <= y ? y : nl;
        }
        return u;
    }
};

constexpr int kMaxBlock = 256;
constexpr int kVoices = 256;

struct Voice
{
    // host values
    float pan = 0.0f, vel = 0.0f, cents = 0.0f;
    std::uint64_t id = 0;
    int group = 0, zone = 0, prevNote = 0, note = 0;
    bool start = false, ended = false, legato = false, released = false;
    float glide = 0.0f, random = 0.0f, follow = 0.0f;
    float cutA = 0.0f, cutB = 0.0f, gainL = 0.0f, gainR = 0.0f;
    float lim = 1.0f, lim2 = 1.0f;
    std::uint64_t pos = 0; // high 32 bits: frame, low 32 bits: fraction
    std::int64_t step = 0;
    double speed = 0.0;
    int bounceDir = 0, loopState = 0;
    int loopStart = 0, loopEnd = 0, loopLen = 0, end = 0;
    Env amp, menv[2];
    LfoState lfo[2];
    BiquadState fa[2], fb[2];

    std::uint32_t frame() const { return (std::uint32_t) (pos >> 32); }
    std::uint32_t frac() const { return (std::uint32_t) pos; }
    void setFrame (std::uint32_t f) { pos = ((std::uint64_t) f << 32) | (pos & 0xffffffffu); }
    void setFrac (std::uint32_t f) { pos = (pos & 0xffffffff00000000ull) | f; }
    bool playing() const { return amp.state > 0 || start; }
};
} // namespace

struct Engine::Impl
{
    Program prog;
    std::vector<std::shared_ptr<const ZoneSamples>> samples;
    EngineOptions opt;
    float sr = 44100.0f, invSr = 1.0f / 44100.0f, bpm = 120.0f;
    float smoothK = 0.0f, limUp = 0.0f, limDown = 0.0f, twoPiOverSr = 0.0f;
    std::array<Voice, kVoices> voices;
    LfoState progLfo[2];
    float progLfoOut[2] {};
    int lastNote = 0;
    // slot (MIDI channel 0) controllers
    float bend = 0.0f, wheel = 0.0f, volume = 100.0f / 127.0f, slotPan = 0.5f, expression = 1.0f;
    int bendRange = 2;
    // per block
    float buf[2][kMaxBlock] {};
    float bus[8][kMaxBlock] {}; // dry L/R, delay L/R, chorus L/R, reverb L/R
    std::uint32_t* rnd = nullptr;

    void setRate (float r)
    {
        sr = r;
        invSr = r != 0.0f ? 1.0f / r : 0.0f;
        smoothK = expfCrt (-1.0f / (r * fbits (0x3ba3d70au)));
        limUp = expfCrt (-1000.0f / r);
        limDown = expfCrt (-10.0f / r);
        twoPiOverSr = kTwoPi / r;
    }

    // ----------------------------------------------------------------------------------------- one voice, one block
    void renderVoice (Voice& v, int n, float blockRate, float lfoScale, float blockTempo)
    {
        if (v.zone < 0 || v.zone >= (int) prog.zones.size())
        {
            v.amp.state = 0; v.start = false;
            return;
        }
        const Zone& z = prog.zones[(std::size_t) v.zone];
        const ZoneSamples* zs = samples.size() > (std::size_t) v.zone ? samples[(std::size_t) v.zone].get() : nullptr;
        if (z.channels == 0 || z.frames == 0)
        {
            v.amp.state = 0; v.start = false;
            return;
        }
        if (v.start)
        {
            v.pos = 0;
            v.follow = 0.0f;
            v.menv[0].start();
            v.menv[1].start();
            v.lfo[0].reset (z.lfo[0].phase);
            v.lfo[1].reset (z.lfo[1].phase);
            v.amp.start();
            v.random = (float) (int) crtRand (*rnd) * kRand15;
            if (! (0.0f < prog.glide) || (prog.glideLegatoOnly && ! v.legato))
                v.glide = (float) v.note;
            else
                v.glide = (float) v.prevNote;
        }
        if (v.amp.state < 1)
            return;
        const auto frames = (std::uint32_t) z.frames;
        if (frames <= v.frame())
            v.setFrame (frames);

        // modulation sources
        const float me0 = v.menv[0].tick (rateOf (z.modEnv[0][0], blockRate), rateOf (z.modEnv[0][1], blockRate), z.modEnv[0][2],
                                          rateOf (z.modEnv[0][3], blockRate));
        const float me1 = v.menv[1].tick (rateOf (z.modEnv[1][0], blockRate), rateOf (z.modEnv[1][1], blockRate), z.modEnv[1][2],
                                          rateOf (z.modEnv[1][3], blockRate));
        float lfoOut[2];
        for (int k = 0; k < 2; ++k)
        {
            const auto& L = z.lfo[k];
            const float fadeInc = rateOf (L.fade, blockRate);
            const float r2 = L.rate * L.rate;
            float inc = lfoScale * r2;
            if (L.sync)
            {
                const auto beats = (int) ((double) r2 * 127.5 + 1.0);
                inc = (float) ((double) kTwoPi / ((double) beats * (double) blockTempo));
            }
            lfoOut[k] = v.lfo[k].tick (L.type, inc, fadeInc, *rnd);
        }

        // destinations
        float D[48] {};
        D[1] = z.pitch;
        D[2] = opt.velocityAsInitVol ? z.gain : 1.0f;
        {
            const float p = v.pan * 0.5f + z.pan;
            D[3] = 0.0f > p ? 0.0f : std::min (1.0f, p);
        }
        D[7] = z.filter[0].cutoff; D[8] = z.filter[0].res; D[9] = z.filter[0].shape;
        D[10] = z.filter[1].cutoff; D[11] = z.filter[1].res; D[12] = z.filter[1].shape;
        for (int i = 0; i < 4; ++i) D[13 + i] = z.amp[i];
        D[17] = z.fx[0][0]; D[18] = z.fx[0][1]; D[19] = z.fx[1][0]; D[20] = z.fx[1][1];
        D[21] = z.fx[2][0]; D[22] = z.fx[2][1]; D[23] = z.fx[3][0]; D[24] = z.fx[3][1];
        for (int i = 0; i < 4; ++i) D[25 + i] = z.sends[i];
        D[29] = z.stretch[0]; D[30] = z.stretch[1]; D[31] = z.stretch[2];
        for (int k = 0; k < 16; ++k) D[32 + k] = z.mods[(std::size_t) k].amount;
        bool modStart = false, modLoopA = false, modLoopB = false, follows = false;
        for (const auto& m : z.mods)
        {
            if (m.dst == 4) modStart = true;
            else if (m.dst == 5) modLoopA = true;
            else if (m.dst == 6) modLoopB = true;
        }
        const auto fr = (int) frames;
        if (modStart) D[4] = (float) z.offset / (float) fr;
        if (modLoopA) D[5] = (float) z.loopA / (float) fr;
        if (modLoopB) D[6] = (float) z.loopB / (float) fr;

        auto source = [&] (int src, float a, float& dst, bool& added) {
            // returns the term to add in `a` (already multiplied), or handles additive forms itself
            added = false;
            switch (src)
            {
                case 0: case 7: case 8: dst = dst + 0.0f; added = true; return a;
                case 1: dst = dst + ((float) v.note / 127.0f) * a; added = true; return a;
                case 2: return a * v.vel;
                case 3: return a * wheel;
                case 4: return a * bend;
                case 5: return a * progLfoOut[0];
                case 6: return a * progLfoOut[1];
                case 9: return a * me0;
                case 10: return a * me1;
                case 11: return a * lfoOut[0];
                case 12: return a * lfoOut[1];
                case 13: follows = true; return a * v.follow;
                case 14: dst = dst + ((float) (std::int64_t) v.frame() / (float) fr) * a; added = true; return a;
                case 15: return a * v.random;
                case 16: case 17: case 18: case 19: return a * prog.modValue[src - 16];
                default: added = true; return a; // no change
            }
        };
        // pass 1: slots that modulate the amount of an earlier slot, from the last slot down
        for (int k = 15; k >= 0; --k)
        {
            const auto& m = z.mods[(std::size_t) k];
            const int dst = m.dst;
            if (dst <= 31 || dst - k >= 32)
                continue;
            if (dst >= 48)
                continue;
            float a = (D[32 + k] - 0.5f);
            a = a + a;
            a = a * a * a;
            float x = D[dst];
            bool added;
            const float t = source (m.src, a, x, added);
            if (! added)
                x = x + t;
            D[dst] = std::min (1.0f, std::max (0.0f, x));
        }
        // pass 2: the others, from the first slot up
        for (int k = 0; k < 16; ++k)
        {
            const auto& m = z.mods[(std::size_t) k];
            const int dst = m.dst;
            if (dst > 31 && dst - k < 32)
                continue;
            if (dst >= 48)
                continue;
            float a = (D[32 + k] - 0.5f);
            a = a + a;
            a = a * a * a;
            float x = D[dst];
            bool added = false;
            if (m.src == 2 && ! opt.velocityAsInitVol && dst == 2)
            {
                x = x * (a * v.vel);
                added = true;
            }
            else
            {
                const float t = source (m.src, a, x, added);
                if (! added)
                    x = x + t;
            }
            if (! (m.src == 2 && ! opt.noGainClamp && dst == 2))
                x = std::min (1.0f, std::max (0.0f, x));
            D[dst] = x;
        }

        // sample points
        int startFrame = z.offset, la = z.loopA, lb = z.loopB;
        if (modStart) startFrame = (int) ((double) D[4] * (double) fr);
        if (modLoopA) la = (int) ((double) D[5] * (double) fr);
        if (modLoopB) lb = (int) ((double) D[6] * (double) fr);
        v.loopEnd = std::max (la, lb);
        v.loopStart = std::min (la, lb);
        v.loopLen = v.loopEnd - v.loopStart;
        v.end = fr;

        // pitch, glide, speed
        {
            const float target = (float) v.note;
            const float dist = std::fabs ((float) (v.note - v.prevNote));
            const float g = rateOf (prog.glide, blockRate) * dist;
            if (v.glide > target)
            {
                v.glide = v.glide - g;
                if (! (target < v.glide)) v.glide = target;
            }
            if (target > v.glide)
            {
                const float ng = g + v.glide;
                v.glide = ng;
                if (! (ng < target)) v.glide = target;
            }
        }
        float cents = (v.glide - (float) z.root) * (float) z.keyTrack;
        cents = cents + (float) z.coarse * 100.0f;
        cents = cents + (float) z.fine;
        cents = cents + (D[1] * 2400.0f - 1200.0f);
        cents = cents + (float) bendRange * bend;
        cents = cents + v.cents;
        double speed = powCrt (2.0, (double) cents / 1200.0) * (double) z.rate / (double) sr;
        v.speed = speed;
        v.step = (std::int64_t) (speed * kOne32);

        const bool filters = z.filter[0].type != 0 || z.filter[1].type != 0;
        const float relTime = v.ended ? 0.25f : D[16];
        const float aRate = rateOf (D[13], sr), dRate = rateOf (D[14], sr), rRate = rateOf (relTime, sr);

        // gains
        float g = D[2];
        if (! opt.velocityAsInitVol)
            g = g * z.gain;
        g = g * prog.gain;
        g = g * volume;
        g = g * expression;
        float gl = std::sqrt ((1.0f - slotPan) * (1.0f - D[3]));
        float gr = std::sqrt (slotPan * D[3]);
        if (opt.velocityAsInitVol)
        {
            gl = gl * 2.0f * g;
            gr = gr * 2.0f * g;
        }
        const float targetL = gl * g, targetR = gr * g;

        if (v.start)
        {
            v.lim = 1.0f; v.lim2 = 1.0f;
            v.fa[0].clear(); v.fa[1].clear(); v.fb[0].clear(); v.fb[1].clear();
            v.cutA = D[7]; v.cutB = D[10];
            v.gainL = targetL; v.gainR = targetR;
            v.setFrame ((std::uint32_t) startFrame);
            v.bounceDir = 0; v.loopState = 0;
            if (z.sliceKeys && z.beats > 0 && v.note >= z.root)
                v.setFrame ((std::uint32_t) (int) (std::int64_t) ((double) ((v.note - z.root) % z.beats) * ((double) fr / (double) z.beats)));
        }
        v.start = false;

        // ------------------------------------------------------------------ sample reading
        float* L = buf[0];
        float* R = buf[1];
        const bool stereo = z.channels == 2;
        auto sampleAt = [&] (int ch, std::uint32_t i) -> float {
            if (! zs)
                return 0.0f;
            const auto idx = (std::size_t) i + 256;
            if (zs->bits == 32)
            {
                const auto& a = zs->f32[(std::size_t) ch];
                return idx < a.size() ? a[idx] : 0.0f;
            }
            const auto& a = zs->i16[(std::size_t) ch];
            return idx < a.size() ? (float) a[idx] * kInt16 : 0.0f;
        };
        auto rawAt = [&] (int ch, std::uint32_t i) -> float { // 16-bit: the integer value as float; float: the value
            if (! zs)
                return 0.0f;
            const auto idx = (std::size_t) i + 256;
            if (zs->bits == 32)
            {
                const auto& a = zs->f32[(std::size_t) ch];
                return idx < a.size() ? a[idx] : 0.0f;
            }
            const auto& a = zs->i16[(std::size_t) ch];
            return idx < a.size() ? (float) a[idx] : 0.0f;
        };
        auto readOneShot = [&] (int i) {
            const auto f = v.frame();
            const float x = (float) ((double) v.frac() * kFrac);
            if (! stereo)
            {
                const float s0 = sampleAt (0, f), s1 = sampleAt (0, f + 1);
                L[i] = R[i] = (s1 - s0) * x + s0;
                return;
            }
            const float w = 1.0f - x;
            if (zs && zs->bits != 32)
            {
                L[i] = (rawAt (0, f) * w + rawAt (0, f + 1) * x) * kInt16;
                R[i] = (rawAt (1, f) * w + rawAt (1, f + 1) * x) * kInt16;
            }
            else
            {
                L[i] = w * sampleAt (0, f) + x * sampleAt (0, f + 1);
                R[i] = w * sampleAt (1, f) + x * sampleAt (1, f + 1);
            }
        };
        auto readLoop = [&] (int i) {
            const auto f = v.frame();
            const float x = (float) ((double) v.frac() * kFrac);
            const bool wrap = f == (std::uint32_t) (v.loopEnd - 1) && v.loopState < 2;
            if (! stereo)
            {
                if (wrap)
                {
                    const float s0 = sampleAt (0, f), s1 = sampleAt (0, (std::uint32_t) v.loopStart);
                    L[i] = R[i] = (s1 - s0) * x + s0;
                }
                else
                {
                    const float s0 = sampleAt (0, f), s1 = sampleAt (0, f + 1);
                    L[i] = R[i] = (s1 - s0) * x + s0;
                }
                return;
            }
            if (wrap)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float s0 = sampleAt (ch, f), s1 = sampleAt (ch, (std::uint32_t) v.loopStart);
                    (ch == 0 ? L : R)[i] = (s1 - s0) * x + s0;
                }
                return;
            }
            const float w = 1.0f - x;
            if (zs && zs->bits != 32)
            {
                L[i] = (rawAt (0, f + 1) * x + rawAt (0, f) * w) * kInt16;
                R[i] = (rawAt (1, f + 1) * x + rawAt (1, f) * w) * kInt16;
            }
            else
            {
                L[i] = x * sampleAt (0, f + 1) + w * sampleAt (0, f);
                R[i] = x * sampleAt (1, f + 1) + w * sampleAt (1, f);
            }
        };
        auto endCheck = [&] {
            if ((std::uint32_t) v.end <= v.frame())
            {
                v.setFrame ((std::uint32_t) v.end);
                v.setFrac (0);
                v.ended = true;
                v.amp.release();
            }
        };
        auto loopWrap = [&] {
            const auto f = v.frame();
            if ((std::uint32_t) v.loopEnd <= f)
            {
                int nf = v.loopStart;
                if (v.loopLen > 0)
                {
                    int r = ((int) f - v.loopStart) % v.loopLen;
                    if (r < 0) r += v.loopLen;
                    nf = r + v.loopStart;
                }
                v.setFrame ((std::uint32_t) nf);
                v.loopState = 1;
            }
        };
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        if (z.stretchMode == 0)
        {
            const int mode = z.loopMode;
            const bool releasedSustain = mode == 3 && v.amp.state > 3;
            if (mode < 2 || releasedSustain)
            {
                v.loopState = 2;
                for (int i = 0; i < n; ++i)
                {
                    readOneShot (i);
                    v.pos += (std::uint64_t) v.step;
                    endCheck();
                }
            }
            if (mode == 2 || (mode == 3 && ! releasedSustain))
            {
                for (int i = 0; i < n; ++i)
                {
                    readLoop (i);
                    v.pos += (std::uint64_t) v.step;
                    loopWrap();
                }
            }
            if (mode == 4)
            {
                for (int i = 0; i < n; ++i)
                {
                    readOneShot (i);
                    auto back = [&] {
                        const auto p = (std::int64_t) v.pos;
                        v.pos = (std::uint64_t) (p - v.step);
                        if (v.frame() <= (std::uint32_t) v.loopStart || p - v.step < 0)
                            v.bounceDir = 0;
                    };
                    if (v.bounceDir == 0)
                    {
                        v.pos += (std::uint64_t) v.step;
                        if ((std::uint32_t) v.loopEnd <= v.frame())
                        {
                            v.bounceDir = 1;
                            back();
                        }
                    }
                    else if (v.bounceDir == 1)
                        back();
                }
            }
        }

        // envelope follower (source 13)
        if (follows)
        {
            float f = v.follow;
            for (int i = 0; i < n; ++i)
            {
                const float p = std::max (std::fabs (L[i]), std::fabs (R[i]));
                if (f < p) f = p;
                f = f * smoothK;
            }
            v.follow = f;
        }

        // ------------------------------------------------------------------ filters
        if (filters)
        {
            const float k = expfCrt (-1.0f / (blockRate * fbits (0x3ba3d70au)));
            Biquad ca, cb;
            const float hzA = flCutoff (v.cutA);
            if (z.filter[0].type < 6)
                ca.design (z.filter[0].type, hzA, kQBase / (kQRes - D[8]), sr);
            const float hzB = flCutoff (v.cutB);
            if (z.filter[1].type < 6)
                cb.design (z.filter[1].type, hzB, kQBase / (kQRes - D[11]), sr);
            if (std::getenv ("DWDBG"))
                std::fprintf (stderr, "#f hzA %.9g q %.9g c %.9g %.9g %.9g %.9g %.9g type %d res %.9g\n", (double) hzA, (double) (kQBase / (kQRes - D[8])), (double) ca.c[0], (double) ca.c[1],
                              (double) ca.c[2], (double) ca.c[3], (double) ca.c[4], z.filter[0].type, (double) D[8]);
            v.cutA = (v.cutA - D[7]) * k + D[7];
            v.cutB = (v.cutB - D[10]) * k + D[10];
            const float shA = D[9], shB = D[12];
            for (int i = 0; i < n; ++i)
            {
                const float noise = (float) (int) crtRand (*rnd) * kTiny;
                const float xl = noise + L[i];
                const float xr = noise + R[i];
                if (std::getenv ("DWDBG")) std::fprintf (stderr, "#x %d %.9g %.9g\n", i, (double) xl, (double) noise);
                float yl, yr;
                if (! z.parallelFilters)
                {
                    yl = v.fa[0].run (xl, shA, ca.c);
                    yr = v.fa[1].run (xr, shA, ca.c);
                    yl = v.fb[0].run (yl, shB, cb.c);
                    yr = v.fb[1].run (yr, shB, cb.c);
                }
                else
                {
                    yl = v.fa[0].run (xl, shA, ca.c);
                    yr = v.fa[1].run (xr, shA, ca.c);
                    yl = yl + v.fb[0].run (xl, shB, cb.c);
                    yr = yr + v.fb[1].run (xr, shB, cb.c);
                }
                L[i] = yl;
                R[i] = yr;
                limiter (L[i], R[i], v.lim);
            }
        }

        if (std::getenv ("DWDBG") && filters)
            std::fprintf (stderr, "#fs %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", (double) v.fa[0].s[0], (double) v.fa[0].s[1], (double) v.fa[0].s[2], (double) v.fa[0].s[3],
                          (double) v.fa[0].s[4], (double) v.fa[0].s[5], (double) v.fa[0].s[6], (double) v.fa[0].s[7]);
        // ------------------------------------------------------------------ amplitude
        for (int i = 0; i < n; ++i)
        {
            const float e = v.amp.tick (aRate, dRate, D[15], rRate);
            L[i] = e * v.gainL * L[i];
            R[i] = e * v.gainR * R[i];
            v.gainL = (v.gainL - targetL) * smoothK + targetL + kTiny;
            v.gainR = (v.gainR - targetR) * smoothK + targetR + kTiny;
        }

        if (std::getenv ("DWDBG"))
            std::fprintf (stderr, "#v zone %d gainL %.9g gainR %.9g cutA %.9g cutB %.9g lim %.9g amp %.9g glide %.9g vel %.9g speed %.17g D2 %.9g D3 %.9g\n", v.zone,
                          (double) v.gainL, (double) v.gainR, (double) v.cutA, (double) v.cutB, (double) v.lim, (double) v.amp.level, (double) v.glide, (double) v.vel, v.speed, (double) D[2], (double) D[3]);
        // ------------------------------------------------------------------ buses
        auto send = [&] (int b, float gain) {
            for (int i = 0; i < n; ++i)
            {
                bus[b][i] = L[i] * gain + bus[b][i];
                bus[b + 1][i] = R[i] * gain + bus[b + 1][i];
            }
        };
        if (prog.delayOn && D[25] > 0.0f) send (2, D[25] * D[25]);
        if (prog.chorusOn && D[26] > 0.0f) send (4, D[26] * D[26]);
        if (prog.reverbOn && D[27] > 0.0f) send (6, D[27] * D[27]);
        if (! (1.0f <= D[28]))
            send (0, D[28] * D[28]);
        else
            for (int i = 0; i < n; ++i)
            {
                bus[0][i] = L[i] + bus[0][i];
                bus[1][i] = R[i] + bus[1][i];
            }
    }

    static float flCutoff (float v) { return powfCrt (2.0f, (v * 127.0f) / 12.0f - 5.0f) * 440.0f; }

    void limiter (float& l, float& r, float& state) const
    {
        float p = std::fabs (l);
        const float q = std::fabs (r);
        if (! (q < p)) p = q;
        const float k = p <= state ? limDown : limUp;
        const float s = (state - p) * k + p;
        state = s;
        if (1.0f < s)
        {
            const float g = powfCrt (s, -1.0f);
            l = g * l;
            r = g * r;
        }
    }
};

Engine::Engine() : d (std::make_unique<Impl>()) { d->rnd = &randState; d->setRate (44100.0f); }
Engine::~Engine() = default;

void Engine::setProgram (const Program& program, std::vector<std::shared_ptr<const ZoneSamples>> samples, const EngineOptions& o)
{
    d->prog = program;
    d->samples = std::move (samples);
    d->opt = o;
    reset();
}

void Engine::setSampleRate (double sampleRate) { d->setRate ((float) sampleRate); }
void Engine::setTempo (double bpm) { d->bpm = (float) bpm; }

void Engine::reset()
{
    for (auto& v : d->voices)
        v = Voice();
    d->progLfo[0] = d->progLfo[1] = LfoState();
}

bool Engine::active() const
{
    for (const auto& v : d->voices)
        if (v.playing())
            return true;
    return false;
}

void Engine::noteOn (std::uint64_t id, int key, double velocity, double finalVol, double finalPan, double cents)
{
    auto& P = d->prog;
    const int note = std::clamp (key, 0, 127);
    const int vel = std::clamp ((int) ((float) velocity * 127.0f), 1, 127);
    bool held = false;
    for (const auto& v : d->voices)
        if (v.playing() && ! v.released)
            held = true;
    d->progLfo[0].reset (P.lfo[0].phase);
    d->progLfo[1].reset (P.lfo[1].phase);
    for (int zi = 0; zi < (int) P.zones.size(); ++zi)
    {
        const auto& z = P.zones[(std::size_t) zi];
        if (note < z.lowKey || note > z.highKey)
            continue;
        const auto g = (std::size_t) std::clamp (z.altGroup, 0, 99);
        const float tol = (float) ((double) P.groups[g].velTolerance * 127.0);
        if (! ((float) z.lowVel - tol <= (float) vel && (float) vel <= (float) z.highVel + tol))
            continue;
        if (z.exclusiveGroup != 0)
            for (auto& v : d->voices)
                if (v.amp.state >= 1 && v.amp.state <= 3 && v.group == z.exclusiveGroup)
                    v.amp.release(), v.menv[0].release(), v.menv[1].release();
        for (auto& v : d->voices)
        {
            if (v.amp.state == 0 && ! v.start)
            {
                v = Voice();
                v.start = true;
                v.zone = zi;
                v.prevNote = d->lastNote;
                v.note = note;
                v.legato = held;
                v.group = z.exclusiveGroup;
                v.id = id;
                v.vel = (float) finalVol;
                v.pan = (float) finalPan;
                v.cents = (float) cents;
                break;
            }
        }
    }
    d->lastNote = note;
}

void Engine::noteOff (std::uint64_t id)
{
    for (auto& v : d->voices)
    {
        if (v.id != id || ! v.playing())
            continue;
        v.released = true;
        const auto& z = d->prog.zones[(std::size_t) v.zone];
        if (v.amp.state >= 1 && v.amp.state <= 3 && z.loopMode != 1)
        {
            v.amp.release();
            v.menv[0].release();
            v.menv[1].release();
        }
        if (v.start)
        {
            v.start = false;
            v.amp.state = 0;
        }
    }
}

void Engine::setLevels (std::uint64_t id, double finalVol, double finalPan, double cents)
{
    for (auto& v : d->voices)
        if (v.id == id && v.playing())
        {
            v.vel = (float) finalVol;
            v.pan = (float) finalPan;
            v.cents = (float) cents;
        }
}

void Engine::render (float* out, int n)
{
    auto& I = *d;
    const int maxBlock = I.opt.fineBlocks ? 8 : kMaxBlock;
    int done = 0;
    while (done < n)
    {
        const int m = std::min (n - done, maxBlock);
        const float blockRate = I.sr / (float) m;
        const float blockTempo = (float) ((double) blockRate * 15.0 / (double) I.bpm);
        const float lfoScale = (kTwoPi * 20.0f) / blockRate;
        // program LFOs
        for (int k = 0; k < 2; ++k)
        {
            const auto& L = I.prog.lfo[k];
            const float fadeInc = rateOf (L.fade, blockRate);
            const float r2 = L.rate * L.rate;
            float inc = lfoScale * r2;
            if (L.sync)
            {
                const auto beats = (int) ((double) r2 * 127.5 + 1.0);
                inc = (float) ((double) kTwoPi / ((double) beats * (double) blockTempo));
            }
            I.progLfoOut[k] = I.progLfo[k].tick (L.type, inc, fadeInc, randState);
        }
        for (auto& b : I.bus)
            std::fill (b, b + m, 0.0f);
        for (auto& v : I.voices)
            if (v.amp.state > 0 || v.start)
                I.renderVoice (v, m, blockRate, lfoScale, blockTempo);
        // program effects: §6 (delay, chorus, reverb, shapers) — not yet
        for (int i = 0; i < m; ++i)
        {
            out[2 * (done + i)] = I.bus[0][i] + out[2 * (done + i)];
            out[2 * (done + i) + 1] = I.bus[1][i] + out[2 * (done + i) + 1];
        }
        done += m;
    }
}

std::vector<float> renderReference (const Program& program, const std::vector<std::shared_ptr<const ZoneSamples>>& samples,
                                    const EngineOptions& options, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block, std::uint32_t randSeed)
{
    Engine e;
    e.setSampleRate (sampleRate);
    e.setProgram (program, samples, options);
    e.randState = randSeed;
    std::vector<int> evs;
    for (const auto& nt : notes)
    {
        evs.push_back (nt.start);
        if (nt.release >= 0)
            evs.push_back (nt.release);
    }
    block = std::max (1, block);
    std::vector<float> out (2 * (std::size_t) std::max (0, length), 0.0f);
    int t = 0;
    while (t < length)
    {
        for (std::size_t k = 0; k < notes.size(); ++k)
        {
            if (notes[k].start == t)
                e.noteOn (k + 1, notes[k].key, notes[k].velocity, notes[k].finalVol, notes[k].finalPan, notes[k].cents);
            if (notes[k].release == t)
                e.noteOff (k + 1);
        }
        int len = std::min (block, length - t);
        for (const auto ev : evs)
            if (ev > t && ev < t + len)
                len = ev - t;
        e.render (out.data() + 2 * (std::size_t) t, len);
        t += len;
    }
    return out;
}
} // namespace dw
} // namespace lynx
