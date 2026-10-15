// FL Studio's Fruit Kick (REA_rodada43 §1): a 1:1 port of the round-43 reference model. Float arithmetic follows the
// plugin's operation order; doubles where the plugin computes in double; long double for FL's x87 pan law.

#include "core/FruitKick.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

// the float/double operation order follows the plugin's, so fusing a·b + c into one FMA would change the results:
// contraction stays off.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

namespace lynx
{
namespace
{
constexpr double kPi = 3.1415926535897931;

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    std::int32_t v = 0;
    if (o + 4 <= d.size())
        std::memcpy (&v, d.data() + o, 4);
    return v;
}

long rint64 (double x) { return std::lrint (x); } // cvtsd2si: round half to even

// ---- tables: FL's host sine (WaveTables[0]), the plugin's LFO triangle/square, the host waveshaper curve ----
struct Tables
{
    std::array<float, 16384> sine {}, tri {}, square {};
    std::array<float, 8193> shaper {}; // curve set 0, curve 10, elements 0…8192 (8192 repeats 8191)

    Tables()
    {
        for (int i = 0; i < 16384; ++i)
            sine[(std::size_t) i] = (float) std::sin ((double) (2 * i) * kPi / 16384.0);
        for (int i = 0; i < 0x2000; ++i)
            tri[(std::size_t) ((i + 0x3000) & 0x3fff)] = (float) ((double) (i << 2) / 16384.0 + -1.0);
        for (int i = 0x2000; i < 0x4000; ++i)
            tri[(std::size_t) ((i + 0x3000) & 0x3fff)] = (float) (1.0 - (double) ((i - 0x2000) * 4) / 16384.0);
        for (int i = 0; i < 16384; ++i)
            square[(std::size_t) i] = i < 0x2000 ? 1.0f : -1.0f;
        // FLEngine's table builder (REA_rodada43 §1.4): x = k/8191 → dB, compressed above −25 dB, +14.4 dB below −14.4 dB
        const double x8 = 0.1, x9 = 9 * 1.6, x10 = std::log (10.0) * 0.05, c20 = 20.0 / std::log (10.0);
        for (int k = 0; k < 8192; ++k)
        {
            const double x = k / 8191.0;
            double d = x != 0.0 ? std::log (x) * c20 : -10000.0;
            if (d >= -25.0)
                d = (d + 25.0) * x8 + -25.0;
            if (! (-x9 < d))
                d += x9;
            shaper[(std::size_t) k] = (float) (std::int16_t) rint64 (std::exp (d * x10) * 32767.0);
        }
        shaper[8192] = shaper[8191];
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

// Delphi Power(single, single) as the plugin calls it (FUN_005ea930)
float fpower (float x, float y)
{
    if (y == 0.0f)
        return 1.0f;
    if (x == 0.0f && y > 0.0f)
        return 0.0f;
    if ((float) (long long) y == y && std::fabs (y) < 2147483648.0f)
    {
        const auto n = (long long) y;
        double r = 1.0;
        for (long long i = 0; i < (n < 0 ? -n : n); ++i)
            r *= (double) x;
        return (float) (n >= 0 ? r : 1.0 / r);
    }
    return (float) std::exp ((double) y * std::log ((double) x));
}

struct Params
{
    int p[6] {};
    float e4 = 0.0f, r154 = 1.0f, fdec = 1.0f, adec = 1.0f, dry = 1.0f, wet = 0.0f;
    std::int32_t floor = 0;

    long incOf (double cents) const { return rint64 ((double) e4 * std::exp (cents * (std::log (2.0) / 1200.0))); }

    void init (const std::vector<std::uint8_t>& state, double fs)
    {
        for (int i = 0; i < 6; ++i)
            p[i] = i32At (state, 4 + 4 * (std::size_t) i);
        e4 = (float) (2247346493527.166 / fs); // 2³² · 523.25 Hz / fs
        r154 = (float) (44100.0 / fs);
        const double ln1001 = std::log (1001.0);
        floor = (std::int32_t) incOf ((double) (p[1] - 3600));
        auto expPart = [&] (int v) { return std::exp ((double) (1.0f - (float) v * 0.00390625f) * ln1001); };
        fdec = fpower (0.9997f - (float) (expPart (p[2]) - 1.0) * 9.7e-6f, r154);
        adec = fpower ((float) (0.99998 - (double) (float) (expPart (p[3]) - 1.0) * 4.98e-6), r154);
        const float d = (float) p[5] * 0.0078125f;
        dry = 1.0f - d;
        wet = (float) ((double) d * 3.3);
    }
};

// ---- the plugin voice framework's channel envelopes (FUN_0071fd90 / FUN_0071f840) ----
struct Tension
{
    float a = 0.0f, c = 0.0f, k = 0.0f;
};

Tension tension (int t)
{
    if (t == 0)
        return {};
    const float sg = t > 0 ? 1.0f : -1.0f;
    const auto m = (float) std::fabs ((double) ((float) t * 0.0078125f));
    Tension r;
    r.a = sg * (float) (std::exp ((double) m * std::log (6347.0)) - 1.0) * 0.1f;
    r.c = (float) (1.0 / std::fabs ((double) r.a));
    r.k = (float) std::log (std::fabs ((double) r.a) + 1.0);
    return r;
}

float curve (float x, const Tension& t)
{
    if (t.a == 0.0f && ! std::signbit (t.a))
        return x;
    if (t.a > 0.0f)
        return (float) (1.0 - (std::exp ((1.0 - (double) x) * (double) t.k) - 1.0) * (double) t.c);
    return (float) ((std::exp ((double) x * (double) t.k) - 1.0) * (double) t.c);
}

float inverse (float y, const Tension& t)
{
    if (t.a == 0.0f && ! std::signbit (t.a))
        return y;
    if (t.a > 0.0f)
        return (float) (1.0 - std::log ((1.0 - (double) y) * std::fabs ((double) t.a) + 1.0) / (double) t.k);
    return (float) (std::log ((double) y * std::fabs ((double) t.a) + 1.0) / (double) t.k);
}

struct Record
{
    std::array<int, 17> raw {};
    int flags = 0, enabled = 0, delay = 0, hold = 0, ldelay = 0;
    float ainc = 1.0f, dinc = -1.0f, rinc = -1.0f, sus = 0.0f, lfade = 1.0f, lamt = 0.0f;
    std::uint32_t linc = 0;
    Tension ten[3];
    const float* shape = nullptr;

    void init (const std::array<int, 17>& r, int ppq, double tempo)
    {
        raw = r;
        flags = r[0];
        enabled = r[1];
        const double e0 = ((double) (ppq >> 2) / 48.0 * 24576.0) / 1000.0;
        auto ticks = [] (int v, double e) -> int {
            if (v < 101)
                return 0;
            return (int) rint64 ((double) (float) (std::exp ((double) (float) (v * 1.52587890625e-05) * std::log (1001.0)) - 1.0) * e);
        };
        double e = (flags & 1) != 0 ? e0 : e0 * tempo / 120.0;
        delay = ticks (r[2], e);
        ainc = (float) (1.0 / (ticks (r[3], e) + 1));
        sus = (float) r[6] * 0.0078125f;
        hold = ticks (r[4], e);
        dinc = (float) (-1.0 / (ticks (r[5], e) + 1));
        rinc = (float) (-1.0 / (ticks (r[7], e) + 1));
        for (int i = 0; i < 3; ++i)
            ten[i] = tension (r[(std::size_t) (14 + i)]);
        e = (flags & 2) != 0 ? e0 : e0 * tempo / 120.0;
        ldelay = ticks (r[9], e);
        lfade = (float) (1.0 / (ticks (r[10], e) + 1));
        const auto period = std::max (ticks (r[12], e), 3);
        linc = (std::uint32_t) (4294967296ULL / (std::uint64_t) period);
        lamt = (float) r[11] * 0.0078125f;
        const auto& t = tables();
        const int s = std::clamp (r[13], 0, 2);
        shape = s == 0 ? t.sine.data() : s == 1 ? t.tri.data() : t.square.data();
    }
};

struct EnvState
{
    int cnt = 0, stage = 1, ldel = 0;
    float x = 0.0f, y = 0.0f, fade = 0.0f, lfo = 0.0f;
    std::uint32_t ph = 0;

    void start (const Record& r)
    {
        cnt = 0;
        if (r.delay < 1 || r.enabled == 0)
        {
            stage = 1;
            x = 0.0f;
            if (((r.flags & 4) != 0 && r.enabled == 0) || (r.enabled != 0 && r.raw[3] == 100))
            {
                x = 1.0f;
                stage = r.hold < 1 ? 3 : 2;
            }
        }
        else
        {
            stage = 0;
            x = 0.0f;
        }
        y = x;
        ldel = r.ldelay;
        ph = 0;
        fade = 0.0f;
        lfo = 0.0f;
    }

    void step (const Record& r)
    {
        if (r.enabled != 0)
        {
            switch (stage)
            {
                case 2:
                    if (r.hold <= ++cnt)
                        stage = 3;
                    break;
                case 0:
                    if (r.delay <= ++cnt)
                        stage = 1;
                    break;
                case 1:
                    x = x + r.ainc;
                    if (1.0f <= x)
                    {
                        x = 1.0f;
                        if (r.hold < 1)
                            stage = 3;
                        else
                            cnt = 0, stage = 2;
                    }
                    y = curve (x, r.ten[0]);
                    break;
                case 3:
                    x = x + r.dinc;
                    y = curve (x, r.ten[1]);
                    if (y <= r.sus)
                    {
                        y = r.sus;
                        x = inverse (y, r.ten[1]);
                        cnt = 0;
                        stage = 4;
                    }
                    break;
                case 6:
                    x = x + r.rinc;
                    y = curve (x, r.ten[2]);
                    break;
                default:
                    break;
            }
            if (stage > 2 && y < 5.96046448e-08f)
            {
                y = 0.0f;
                stage = 9;
            }
        }
        if (ldel == 0)
        {
            ph += r.linc; // (the global-LFO flag 0x20 is not modelled)
            lfo = r.shape[ph >> 18];
            if (fade != 1.0f)
            {
                fade = fade + r.lfade;
                if (1.0f < fade)
                    fade = 1.0f;
                lfo = lfo * fade;
            }
        }
        else
            --ldel;
    }

    void release (const Record& r)
    {
        if (stage < 7)
        {
            x = inverse (y, r.ten[2]);
            cnt = 0;
            stage = 6;
        }
    }
};

// FL's x87 sin/cos pan law (law A): L = vol·cos((1 + pan)·π/4), R = vol·sin(…), in extended precision
void panLaw (float pan, float vol, float& l, float& r)
{
    pan = std::clamp (pan, -1.0f, 1.0f);
    const long double a = (1.0L + (long double) pan) * (long double) 0.78539816339744828;
    l = (float) (std::cos (a) * (long double) vol);
    r = (float) (std::sin (a) * (long double) vol);
}

// FUN_005fa150: per-sample step towards a new level, at most 0.001 per sample (the reached target is returned)
float rampStep (float& target, float prev, int n)
{
    float step = target - prev;
    std::uint32_t bits;
    std::memcpy (&bits, &step, 4);
    if (bits == 0)
        return 0.0f;
    step = step / (float) n;
    if (std::fabs (step) > 0.001f)
    {
        step = std::copysign (0.001f, step);
        target = prev + step * (float) n;
        if (std::fabs (target) < 5.96046448e-08f)
        {
            target = 0.0f;
            step = -prev / (float) n;
        }
    }
    return step;
}

struct Voice
{
    std::uint32_t phase = 0;
    float inc = 0.0f, amp = 1.5f;
    int key = 60;
    double initVol = 1.0, notePan = 0.0;
    float fvol = 1.0f, fpan = 0.0f;
    float L = -1.0f, R = -1.0f, volf = 1.0f, panf = 0.0f;
    int first = 2, fadepos = -1;
    bool released = false; // the framework's "released" flag (+0x174)
    int id = 0;
    EnvState env[5];
};

class Engine
{
public:
    static constexpr int maxBlock = 4096, maxVoices = 64, hostBlock = 512;

    bool init (const std::vector<std::uint8_t>& state, double fs, double tempo, int ppq)
    {
        if (! fruitKickStateUsable (state))
            return false;
        P.init (state, fs);
        const auto n = std::max (1L, rint64 (10.0 * 0.001 * fs));
        fade.assign ((std::size_t) n, 0.0f);
        const auto c = (float) ((1.0 / (double) (n + 1)) * kPi);
        for (long i = 0; i < n; ++i)
            fade[(std::size_t) i] = ((float) std::cos ((double) ((float) (i + 1) * c)) + 1.0f) * 0.5f;
        // envelope snapshot (version 1, ≥ 364 B) or the framework's defaults
        std::array<std::array<int, 17>, 5> recs {};
        const bool snap = i32At (state, 0) == 1 && state.size() >= 32 + 364;
        for (int k = 0; k < 5; ++k)
            for (int j = 0; j < 17; ++j)
            {
                static const int def[17] = { 0, 0, 100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0, 0, 0, 0 };
                recs[(std::size_t) k][(std::size_t) j] = snap ? i32At (state, 32 + 68 * (std::size_t) k + 4 * (std::size_t) j) : def[j];
            }
        if (! snap)
            recs[1][0] |= 4, recs[1][16] = -101;
        for (int k = 0; k < 5; ++k)
            rec[k].init (recs[(std::size_t) k], ppq, tempo);
        voices.reserve (maxVoices);
        return true;
    }

    Voice* trigger (int key, double initVol, float fvol, float fpan, double pitchCents)
    {
        // max polyphony (FUN_0072e400): the first voice not yet released gets the end fade
        if (maxPoly > 0 && (int) voices.size() >= maxPoly)
        {
            Voice* v = nullptr;
            for (auto& w : voices)
            {
                v = &w;
                if (! w.released)
                    break;
            }
            if (v != nullptr)
                startFade (*v);
        }
        if ((int) voices.size() >= maxVoices)
            voices.erase (voices.begin());
        Voice v;
        v.id = nextId++;
        v.key = key;
        v.initVol = initVol;
        v.phase = (std::uint32_t) P.p[4] << 24;
        const auto fpitch = (float) (100.0 * key - 6000.0 + pitchCents);
        const float c = ((float) P.p[0] + fpitch * 0.5f) + 1200.0f;
        const auto lo = (float) (P.p[1] - 3600);
        float st = lo;
        if (lo <= c)
            st = 4800.0f < c ? 4800.0f : c;
        v.inc = (float) P.incOf ((double) st);
        v.amp = 1.5f;
        v.fvol = fvol;
        v.fpan = fpan;
        for (int k = 0; k < 5; ++k)
            v.env[k].start (rec[k]);
        v.first = 1 < v.env[1].stage ? 2 : 1;
        voices.push_back (v);
        return &voices.back();
    }

    void release (Voice& v)
    {
        v.released = true;
        for (int k = 1; k < 5; ++k)
            v.env[k].release (rec[k]);
        if (rec[1].enabled == 0)
            startFade (v);
    }

    void releaseKey (int key)
    {
        for (auto& v : voices)
            if (v.key == key && ! v.released)
                release (v);
    }

    void killAll() { voices.clear(); }
    bool quiet() const { return voices.empty(); }

    void setLevels (double chanVol, double chanPan)
    {
        for (auto& v : voices)
        {
            v.fvol = (float) (v.initVol * chanVol);
            v.fpan = (float) (v.notePan + chanPan);
        }
    }

    // one host block (n ≤ maxBlock): overwrites l/r (silence when no voice is alive)
    void block (float* l, float* r, int n)
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        if (voices.empty())
            return;
        float mono[maxBlock];
        for (std::size_t vi = 0; vi < voices.size();)
        {
            auto& v = voices[vi];
            if (tick || v.first > 0)
            {
                for (int k = 0; k < 5; ++k)
                    v.env[k].step (rec[k]);
                v.panf = (v.env[0].lfo * rec[0].lamt) * 2.0f;
                v.volf = v.env[1].y * ((v.env[1].lfo * rec[1].lamt) + 1.0f);
                if (v.volf < 0.0f)
                    v.volf = 0.0f;
                if (v.env[1].stage == 9)
                    startFade (v);
            }
            const bool finished = renderVoice (v, mono, n);
            int k = 0;
            if (v.fadepos >= 0)
            {
                const int N = (int) fade.size();
                k = v.fadepos < N ? std::min (n, N - v.fadepos) : 0;
                for (int i = 0; i < k; ++i)
                    mono[i] = mono[i] * fade[(std::size_t) (v.fadepos + i)];
                for (int i = k; i < n; ++i)
                    mono[i] = 0.0f;
                v.fadepos += n;
            }
            float tl, tr;
            panLaw (v.fpan + v.panf, v.fvol * v.volf, tl, tr);
            if (v.L == -1.0f)
                v.L = 0.0f;
            if (v.R == -1.0f)
                v.R = 0.0f;
            if (v.first > 1)
                v.L = tl, v.R = tr;
            v.first = 0;
            const float dl = rampStep (tl, v.L, n), dr = rampStep (tr, v.R, n);
            if (dl == 0.0f && dr == 0.0f)
            {
                for (int i = 0; i < n; ++i)
                {
                    l[i] = l[i] + mono[i] * tl;
                    r[i] = r[i] + mono[i] * tr;
                }
            }
            else
            {
                // FL's SSE mixer: gains (L, R, L + dL, R + dR) stepped by 2·d every two frames, then a per-frame tail
                const int m8 = n / 8 * 8;
                float g0 = v.L, g1 = v.R, g2 = v.L + dl, g3 = v.R + dr;
                const float d2l = dl + dl, d2r = dr + dr;
                for (int j = 0; j < m8; j += 2)
                {
                    l[j] = l[j] + mono[j] * g0;
                    r[j] = r[j] + mono[j] * g1;
                    l[j + 1] = l[j + 1] + mono[j + 1] * g2;
                    r[j + 1] = r[j + 1] + mono[j + 1] * g3;
                    g0 = g0 + d2l, g1 = g1 + d2r, g2 = g2 + d2l, g3 = g3 + d2r;
                }
                for (int j = m8; j < n; ++j)
                {
                    l[j] = l[j] + mono[j] * g0;
                    r[j] = r[j] + mono[j] * g1;
                    g0 = g0 + dl, g1 = g1 + dr;
                }
            }
            v.L = tl, v.R = tr;
            if (finished || v.fadepos >= (int) fade.size())
                voices.erase (voices.begin() + (std::ptrdiff_t) vi);
            else
                ++vi;
        }
        tick = false;
    }

    Params P;
    Record rec[5];
    std::vector<float> fade;
    std::vector<Voice> voices;
    bool tick = false;
    int maxPoly = 0, nextId = 0;

private:
    void startFade (Voice& v)
    {
        v.released = true;
        if (v.fadepos < 0)
            v.fadepos = 0;
    }

    bool renderVoice (Voice& v, float* buf, int n)
    {
        const auto& t = tables();
        auto ph = v.phase;
        auto inc = v.inc, amp = v.amp;
        const auto floor = (float) P.floor;
        for (int i = 0; i < n; ++i)
        {
            buf[i] = t.sine[ph >> 18] * amp;
            ph += (std::uint32_t) rint64 ((double) inc);
            amp = amp * P.adec;
            if (floor < inc)
                inc = inc * P.fdec;
            else
                inc = floor;
        }
        v.phase = ph, v.inc = inc, v.amp = amp;
        std::int32_t ab, lim;
        const float lo = 0.0002f;
        std::memcpy (&ab, &amp, 4);
        std::memcpy (&lim, &lo, 4);
        if (P.p[5] > 0)
        {
            // FL's host waveshaper (interpolated variant, curve set 0 / curve 10, drive 0.1)
            const auto& T = t.shaper;
            const int N = 0x1fff;
            const float k = 0.1f * (float) N, w = P.wet * 3.05175781e-05f;
            for (int i = 0; i < n; ++i)
            {
                const float x = buf[i];
                const float tt = x * k;
                int j = (int) (double) tt;
                const float fr = tt - (float) j;
                const float d = x * P.dry;
                float y;
                if (j < 0)
                {
                    j = std::min (-j, N);
                    const float a = T[(std::size_t) j], b = T[(std::size_t) j + 1];
                    y = -(a - (b - a) * fr);
                }
                else
                {
                    j = std::min (j, N);
                    const float a = T[(std::size_t) j], b = T[(std::size_t) j + 1];
                    y = a + (b - a) * fr;
                }
                buf[i] = d + y * w;
            }
        }
        return ab < lim;
    }
};

class FruitKickInstrument final : public Instrument
{
public:
    ~FruitKickInstrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    void prepare (double sr) noexcept override
    {
        delete retired.exchange (nullptr);
        rate.store (sr);
        rebuild();
        take();
    }

    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        delete retired.exchange (nullptr);
        channelCents.store (channel.sampler.pitch * 100.0f);
        mono.store (channel.synth.mono);
        const auto b = bpm > 0.0 ? bpm : 120.0;
        if (channel.pluginState == state && b == tempo.load())
            return;
        state = channel.pluginState;
        tempo.store (b);
        rebuild();
    }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        if (engine == nullptr)
            return;
        engine->maxPoly = mono.load() ? 1 : 0;
        const auto vel = std::clamp ((double) velocity, 0.0, 1.0);
        const auto initVol = (std::pow (11.0, 1.28 * vel) - 1.0) / 10.0; // FL's Init.Vol for a velocity (REA_rodada28 §4.1)
        auto* v = engine->trigger (std::clamp (key, 0, 127), initVol, (float) (initVol * channelVol),
                                   (float) ((double) nextPan + channelPan), (double) channelCents.load() + (double) nextCents);
        v->notePan = nextPan;
        nextPan = 0.0f, nextCents = 0.0f;
    }

    void noteOff (int key) noexcept override
    {
        take();
        if (engine != nullptr)
            engine->releaseKey (key);
    }

    void allNotesOff() noexcept override
    {
        take();
        if (engine != nullptr)
            for (auto& v : engine->voices)
                if (! v.released)
                    engine->release (v);
    }

    bool idle() const noexcept override { return engine == nullptr || engine->quiet(); }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        take();
        if (engine == nullptr || n <= 0)
            return;
        const auto vol = std::sqrt (((double) gl * gl + (double) gr * gr) * 0.5);
        auto pan = vol > 0.0 ? std::atan2 ((double) gr, (double) gl) * (4.0 / kPi) - 1.0 : 0.0;
        if (std::fabs (pan) < 1e-9)
            pan = 0.0;
        if (vol != channelVol || pan != channelPan)
        {
            channelVol = vol, channelPan = pan;
            engine->setLevels (vol, pan);
        }
        // the framework steps its envelopes once per sequencer tick (96 PPQ of the song tempo)
        const auto perTick = rate.load() * 60.0 / (tempo.load() * 96.0);
        int done = 0;
        while (done < n)
        {
            int m = std::min (Engine::hostBlock, n - done);
            if (tickIn <= 0.0)
            {
                engine->tick = true;
                tickIn += perTick;
            }
            const auto untilTick = (int) std::ceil (tickIn);
            if (untilTick > 0 && untilTick < m)
                m = untilTick;
            if (! engine->quiet())
            {
                engine->block (bufL, bufR, m);
                for (int i = 0; i < m; ++i)
                {
                    left[done + i] += bufL[i];
                    right[done + i] += bufR[i];
                }
            }
            tickIn -= m;
            done += m;
        }
    }

private:
    void rebuild() noexcept
    {
        try
        {
            auto e = std::make_unique<Engine>();
            if (! e->init (state, rate.load(), tempo.load(), 96))
                e.reset();
            delete incoming.exchange (e ? e.release() : new Engine());
        }
        catch (...)
        {
        }
    }

    void take() noexcept
    {
        if (retired.load() != nullptr)
            return;
        if (auto* e = incoming.exchange (nullptr))
        {
            // a new state or rate starts a new engine (FL recreates the plugin's voices on a state load)
            retired.store (engine);
            engine = e->fade.empty() ? nullptr : e; // an empty engine stands for an unusable state
            if (engine == nullptr)
                delete e;
        }
    }

    Engine* engine = nullptr;
    std::atomic<Engine*> incoming { nullptr }, retired { nullptr };
    std::atomic<double> rate { 44100.0 }, tempo { 120.0 };
    std::atomic<float> channelCents { 0.0f };
    std::atomic<bool> mono { false };
    std::vector<std::uint8_t> state;
    double channelVol = 1.0, channelPan = 0.0, tickIn = 0.0;
    float nextPan = 0.0f, nextCents = 0.0f;
    float bufL[Engine::maxBlock] {}, bufR[Engine::maxBlock] {};
};
} // namespace

bool fruitKickStateUsable (const std::vector<std::uint8_t>& state)
{
    const auto v = i32At (state, 0);
    return (v == 0 || v == 1) && state.size() >= 4 + 6 * 4;
}

std::vector<std::uint8_t> fruitKickWithChannelEnvelope (const std::vector<std::uint8_t>& state,
                                                        const std::vector<std::vector<std::uint8_t>>& envelopeRecords,
                                                        int cutoff, int resonance, int filterType)
{
    if (i32At (state, 0) != 0 || state.size() < 28 || envelopeRecords.size() < 5)
        return state;
    for (int k = 0; k < 5; ++k)
        if (envelopeRecords[(std::size_t) k].size() < 68)
            return state;
    std::vector<std::uint8_t> out (400, 0);
    const std::int32_t one = 1;
    std::memcpy (out.data(), &one, 4);
    std::memcpy (out.data() + 4, state.data() + 4, 24);
    for (int k = 0; k < 5; ++k)
        std::memcpy (out.data() + 32 + 68 * k, envelopeRecords[(std::size_t) k].data(), 68);
    std::memcpy (out.data() + 32 + 0x15c, &cutoff, 4);
    std::memcpy (out.data() + 32 + 0x160, &resonance, 4);
    std::memcpy (out.data() + 32 + 0x168, &filterType, 4);
    return out;
}

std::vector<std::string> fruitKickFallbacks (const std::vector<std::uint8_t>& state)
{
    std::vector<std::string> r;
    if (! fruitKickStateUsable (state))
        return { "unreadable state" };
    if (i32At (state, 0) == 1 && state.size() >= 32 + 364)
    {
        const auto cut = i32At (state, 32 + 0x15c), res = i32At (state, 32 + 0x160), type = i32At (state, 32 + 0x168);
        bool filterEnv = false, global = false;
        for (int k = 0; k < 5; ++k)
        {
            const auto flags = i32At (state, 32 + 68 * (std::size_t) k);
            global = global || (flags & 0x20) != 0;
            if ((k == 2 || k == 3) && (i32At (state, 32 + 68 * (std::size_t) k + 4) != 0 || i32At (state, 32 + 68 * (std::size_t) k + 44) != 0))
                filterEnv = true;
        }
        if (cut != 256 || res != 0 || type != 0 || filterEnv)
            r.push_back ("channel filter (not modelled)");
        if (global)
            r.push_back ("global LFO phase (per-voice here)");
    }
    return r;
}

int fruitKickParam (const std::vector<std::uint8_t>& state, int index)
{
    return fruitKickStateUsable (state) && index >= 0 && index < 6 ? i32At (state, 4 + 4 * (std::size_t) index) : 0;
}

bool fruitKickSetParam (std::vector<std::uint8_t>& state, int index, int value)
{
    if (! fruitKickStateUsable (state) || index < 0 || index >= 6)
        return false;
    std::memcpy (state.data() + 4 + 4 * index, &value, 4);
    return true;
}

std::unique_ptr<Instrument> makeFruitKickInstrument() { return std::make_unique<FruitKickInstrument>(); }

namespace fruitkick
{
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block, const std::vector<int>& ticks, double tempo, int ppq)
{
    Engine e;
    if (length <= 0 || ! e.init (state, sampleRate, tempo, ppq))
        return {};
    std::vector<int> evs (ticks);
    for (const auto& nt : notes)
    {
        evs.push_back (nt.start);
        if (nt.release >= 0)
            evs.push_back (nt.release);
    }
    block = std::clamp (block, 1, Engine::maxBlock);
    std::vector<float> out (2 * (std::size_t) length, 0.0f);
    std::vector<int> id (notes.size(), -1);
    float l[Engine::maxBlock], r[Engine::maxBlock];
    int t = 0;
    while (t < length)
    {
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            const auto& nt = notes[i];
            if (nt.start == t)
                id[i] = e.trigger (nt.key, 1.0, (float) nt.finalVol, (float) nt.finalPan, nt.pitchCents)->id;
            if (nt.release == t && id[i] >= 0)
                for (auto& v : e.voices)
                    if (v.id == id[i] && ! v.released)
                        e.release (v);
        }
        for (auto tk : ticks)
            if (tk == t)
                e.tick = true;
        auto m = std::min (block, length - t);
        for (auto ev : evs)
            if (t < ev && ev < t + m)
                m = ev - t;
        if (! e.voices.empty())
        {
            e.block (l, r, m);
            for (int j = 0; j < m; ++j)
            {
                out[2 * (std::size_t) (t + j)] = l[j];
                out[2 * (std::size_t) (t + j) + 1] = r[j];
            }
        }
        t += m;
    }
    return out;
}
} // namespace fruitkick
} // namespace lynx
