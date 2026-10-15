#include "core/Dx10.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

// 1:1 port of our round-42 reference model (r42/py/dxmodel.py). The model emulates the DLL's float32 arithmetic with
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
    const auto version = (std::uint32_t) i32At (d, 0);
    if (version == 0x7fffffff || version >= 3)
        return std::nullopt;
    Params P;
    for (int i = 0; i < paramCount; ++i)
        P.raw[(std::size_t) i] = i32At (d, 4 + 4 * (std::size_t) i);
    return P;
}

// ------------------------------------------------------------------------------------------------------- engine

struct Voice
{
    float fpitch = 0, fvol = 1, fpan = 0, curPitch = 0;
    float phi = 0, D = 0, vibofs = 0;
    float L1 = 0, T1 = 0, c1 = 0, k1 = 1, L2 = 0, T2 = 0, c2 = 0, k2 = 1;
    std::uint32_t ph1 = 0, inc1 = 0, ph2 = 0, inc2 = 0;
    float target = 0, g = 0, a = 0, env = 0, d = 0;
    int cnt = 0, key = 60;
    bool ended = false, released = false, gainsSet = false;
    float gL = 0, gR = 0;
    std::uint64_t serial = 0;
};

class Engine
{
public:
    void init (const Params& params, double sampleRate)
    {
        P = params;
        fs = (int) std::lround (sampleRate);
        update();
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

    // FUN_00735e30 (TriggerVoice); returns the voice record index or −1
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
        v.fpitch = v.curPitch = finalPitch;
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
        order[(std::size_t) count++] = slot;
        return slot;
    }

    // FUN_00736320 (Voice_Release)
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

    // Gen_Render (FUN_00736a10) with the voice processor in its neutral state: mixes into l/r (overwrites)
    void render (float* l, float* r, int n) noexcept
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        if (scratch.size() < (std::size_t) n)
            return; // (prepared for at most `maxBlock`)
        for (int i = 0; i < count; ++i)
        {
            auto& v = voices[(std::size_t) order[(std::size_t) i]];
            auto* x = scratch.data();
            voiceRender (v, x, n);
            float tL, tR;
            panLaw (v.fpan, v.fvol, tL, tR);
            if (! v.gainsSet)
                v.gL = tL, v.gR = tR, v.gainsSet = true; // the first block starts at the target
            const auto sL = rampStep (tL, v.gL, n), sR = rampStep (tR, v.gR, n);
            if (sL == 0.0f && sR == 0.0f)
            {
                for (int j = 0; j < n; ++j)
                {
                    l[j] = x[j] * tL + l[j];
                    r[j] = x[j] * tR + r[j];
                }
            }
            else
                mixRamp (x, l, r, n, v.gL, v.gR, sL, sR);
            v.gL = tL, v.gR = tR;
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
            l[i] = x[i] * a0 + l[i];
            r[i] = x[i] * a1 + r[i];
            l[i + 1] = x[i + 1] * a2 + l[i + 1];
            r[i + 1] = x[i + 1] * a3 + r[i + 1];
            a0 = a0 + s2L, a1 = a1 + s2R, a2 = a2 + s2L, a3 = a3 + s2R;
        }
        for (int i = full; i < n; ++i)
        {
            l[i] = x[i] * a0 + l[i];
            r[i] = x[i] * a1 + r[i];
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

    void prepareBlock (int maxBlock) { scratch.assign ((std::size_t) std::max (maxBlock, 1), 0.0f); }

    Params P;
    int fs = 44100, maxPoly = 0;
    float D0 = 0, ratio1 = 0, init1 = 0, sus1 = 0, vel1 = 0, ratio2 = 0, init2 = 0, sus2 = 0, vel2 = 0, vib = 0;
    float att = 0, dec = 1, rel = 1, time1 = 0, mrel1 = 0, time2 = 0, mrel2 = 0, rich = 0, thru = 0, lfoRate = 0, waveform = 0;
    float lfoS = 0.0f, lfoC = 1.0f, modWheel = 0.0f, bend = 1.0f; // plugin globals after the constructor
    std::array<Voice, maxVoices> voices {};
    std::array<bool, maxVoices> used {};
    std::array<int, maxVoices> order {};
    int count = 0;
    std::uint64_t serials = 0;
    std::vector<float> scratch;
};

// ------------------------------------------------------------------------------------------------------- snapshot

// One articulator record of the snapshot (the channel's 0xDA layout, 17 ints) is neutral when it changes nothing:
// pan (0) and pitch/cutoff/resonance (2, 3, 4) need no envelope amount and no LFO amount; volume (1) needs the envelope
// off with flag 4 (value 1) and no LFO.
bool recordNeutral (const std::uint8_t* r, int index) noexcept
{
    auto at = [&] (int k) {
        std::int32_t v;
        std::memcpy (&v, r + 4 * k, 4);
        return v;
    };
    const auto flags = at (0), enabled = at (1), amount = at (8), lfoAmount = at (11);
    if (lfoAmount != 0)
        return false;
    if (index == 1)
        return enabled == 0 && (flags & 4) != 0;
    if (index == 0)
        return true;
    return enabled == 0 || amount == 0;
}
} // namespace

bool dx10StateUsable (const std::vector<std::uint8_t>& state) { return parseState (state).has_value(); }

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

bool dx10SnapshotNeutral (const std::vector<std::uint8_t>& s)
{
    if (s.size() < (std::size_t) snapshotSize)
        return false;
    for (int k = 0; k < 5; ++k)
        if (! recordNeutral (s.data() + 0x44 * k, k))
            return false;
    // the default "fast LP" filter with Mod X ≥ 256 and Mod Y ≤ 0 is bypassed (FUN_00720cb0/FUN_00722450)
    return i32At (s, 0x168) == 0 && i32At (s, 0x15c) >= 256 && i32At (s, 0x160) <= 0;
}

bool dx10ProcessorNeutral (const std::vector<std::uint8_t>& state)
{
    if (! dx10StateUsable (state))
        return false;
    // version 2 with a complete snapshot: the plugin uses its own (FUN_00734940); otherwise the defaults (neutral)
    if (i32At (state, 0) == 2 && state.size() >= 96 + (std::size_t) snapshotSize)
        return dx10SnapshotNeutral (std::vector<std::uint8_t> (state.begin() + 96, state.begin() + 96 + snapshotSize));
    return true;
}

namespace
{
// ------------------------------------------------------------------------------------------------------- instrument

class Dx10Instrument final : public Instrument
{
public:
    Dx10Instrument() { engine.prepareBlock (blockSize); }

    void prepare (double sr) noexcept override
    {
        rate = sr;
        take();
        engine.init (engine.P, sr);
        engine.count = 0;
        engine.used.fill (false);
        engine.lfoS = 0.0f, engine.lfoC = 1.0f;
    }

    ~Dx10Instrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    // UI thread: the parsed parameters go through a mailbox (the audio thread takes them, this thread frees them)
    void configure (const ChannelDef& channel, double) noexcept override
    {
        delete retired.exchange (nullptr);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        maxPoly.store (channel.synth.mono ? 1 : 0);
        if (channel.pluginState == state)
            return;
        state = channel.pluginState;
        if (auto p = parseState (state))
            delete incoming.exchange (new Params (*p));
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
        if (slot >= 0)
        {
            volFactor[(std::size_t) slot] = initVol;
            notePan[(std::size_t) slot] = nextPan;
        }
        nextPan = 0.0f;
        nextCents = 0.0f;
    }

    void noteOff (int key) noexcept override
    {
        for (int i = 0; i < engine.count; ++i)
        {
            const auto slot = engine.order[(std::size_t) i];
            auto& v = engine.voices[(std::size_t) slot];
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
        for (int done = 0; done < n;)
        {
            const auto len = std::min (blockSize, n - done);
            engine.render (bufL.data(), bufR.data(), len);
            for (int j = 0; j < len; ++j)
            {
                left[done + j] += bufL[(std::size_t) j];
                right[done + j] += bufR[(std::size_t) j];
            }
            done += len;
        }
    }

private:
    void take() noexcept
    {
        if (retired.load() != nullptr)
            return;
        if (auto* p = incoming.exchange (nullptr))
        {
            engine.init (*p, rate);
            retired.store (p);
        }
    }

    static constexpr int blockSize = 512;
    Engine engine;
    std::atomic<Params*> incoming { nullptr }, retired { nullptr };
    std::atomic<float> pitchCents { 0.0f };
    std::atomic<int> maxPoly { 0 };
    std::vector<std::uint8_t> state;
    std::array<float, blockSize> bufL {}, bufR {};
    std::array<float, maxVoices> volFactor {}, notePan {};
    double rate = 44100.0, channelVol = 1.0, channelPan = 0.0;
    float nextPan = 0.0f, nextCents = 0.0f;
};
} // namespace

std::unique_ptr<Instrument> makeDx10Instrument() { return std::make_unique<Dx10Instrument>(); }

namespace dx10
{
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block, int maxPoly)
{
    const auto P = parseState (state);
    if (! P || length <= 0)
        return {};
    auto engine = std::make_unique<Engine>();
    engine->init (*P, sampleRate);
    engine->maxPoly = maxPoly;
    block = std::max (1, block);
    engine->prepareBlock (block);
    std::vector<int> evs;
    for (const auto& nt : notes)
    {
        evs.push_back (nt.start);
        if (nt.release >= 0)
            evs.push_back (nt.release);
    }
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
        while (next < order.size() && order[next].first == n0)
        {
            const auto i = (std::size_t) (order[next].second / 2);
            const auto& nt = notes[i];
            if (order[next].second % 2 == 0)
            {
                slot[i] = engine->trigger (nt.key, (float) nt.initVol, (float) (100.0 * nt.key - 6000.0 + nt.pitchOffset), (float) nt.finalVol,
                                           (float) nt.finalPan);
                serial[i] = slot[i] >= 0 ? engine->voices[(std::size_t) slot[i]].serial : 0;
            }
            else if (slot[i] >= 0 && engine->used[(std::size_t) slot[i]] && engine->voices[(std::size_t) slot[i]].serial == serial[i])
                engine->release (slot[i]);
            ++next;
        }
        while (next < order.size() && order[next].first < n0)
            ++next;
        auto ln = std::min (block, length - n0);
        for (auto e : evs)
            if (n0 < e && e < n0 + ln)
                ln = e - n0;
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
