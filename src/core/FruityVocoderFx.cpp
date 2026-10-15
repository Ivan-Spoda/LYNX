#include "core/FruityVocoderFx.h"

// Fruity Vocoder, exact engine (docs/analise/REA_rodada66.md §6). Clean-room: written from the behaviour, formulas and
// data layouts described there; nothing of Image-Line's code or data is copied. The plugin's float and double steps are
// followed in their order (no contraction); its x87 kernels (envelope follower, band accumulation) run in long double.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/Effects.h"

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace lynx::flvocoder
{
namespace
{
using ld = long double;
inline float F (double v) noexcept { return (float) v; }
constexpr float kTiny = 5.960464477539063e-08f;
const float kLn11f = 2.39789534f;
const double kLn2Half = 0.34657359027997264, kCentLn = 0.00057762265046662107; // ln 2 / 2, ln 2 / 1200
inline std::uint32_t bitsOf (float f) noexcept
{
    std::uint32_t b;
    std::memcpy (&b, &f, 4);
    return b;
}
inline std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}
float curve11 (int raw) noexcept
{
    const float x = (float) raw * 0.0078125f;
    if (x == 1.0f)
        return 1.0f;
    return F ((std::exp ((double) (x * kLn11f)) - 1.0) * (double) 0.1f);
}
double sinhD (double x) noexcept
{
    if (std::abs (x) <= 1e-12)
        return 0.0;
    return (std::exp (x) - std::exp (-x)) / 2.0;
}
inline double flushD (double v) noexcept { return std::abs (v) < (double) kTiny ? 0.0 : v; }

struct Biquad
{
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};
struct Band
{
    Biquad c;
    std::array<std::array<double, 8>, 3> st {}; // per cascade copy: x1, (unused), x2, (unused), y1, (unused), y2, (unused)
    float hz = 0, width = 0, level = 1, gain = 0, env = 0;
    bool active = false, silent = false;
    void clear() noexcept
    {
        for (auto& s : st)
            s.fill (0.0);
    }
};

// band-pass with constant 0 dB peak (the plugin's code 2) on one mono buffer: y = b0·x + (((x1·b1 − y2·a2) + x2·b2) − y1·a1)
void biquad (const Biquad& c, std::array<double, 8>& s, const float* in, float* out, int n) noexcept
{
    double x1 = s[0], x2 = s[2], y1 = s[4], y2 = s[6];
    for (int i = 0; i < n; ++i)
    {
        const double x = in[i];
        const double y = x * c.b0 + (((x1 * c.b1 - y2 * c.a2) + x2 * c.b2) - y1 * c.a1);
        out[i] = (float) y;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
    }
    s[2] = x2, s[0] = x1, s[6] = y2, s[4] = y1;
    for (auto& v : s)
        v = flushD (v);
}
} // namespace

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 17 || i32At (d, 0) <= 1)
        return std::nullopt;
    State s;
    s.bands = i32At (d, 4);
    s.order = i32At (d, 8);
    s.listen = i32At (d, 12);
    s.swap = d[16] != 0;
    if (s.bands < 4 || s.bands > 128 || (s.bands & (s.bands - 1)) != 0 || s.order < 1 || s.order > 3)
        return std::nullopt;
    if (d.size() < 17 + 4 * (std::size_t) s.bands + 48)
        return std::nullopt;
    s.levels.assign (128, 1.0f);
    for (int k = 0; k < s.bands; ++k)
        std::memcpy (&s.levels[(std::size_t) k], d.data() + 17 + 4 * (std::size_t) k, 4);
    for (int i = 0; i < paramCount; ++i)
        s.p[(std::size_t) i] = i32At (d, 17 + 4 * (std::size_t) s.bands + 4 * (std::size_t) i);
    return s;
}

void rawRange (int param, int& lo, int& hi) noexcept
{
    static const int mn[paramCount] = { 0, 0, 0, 0, -1200, 5, 10, 10, 0, 0, 0, 0 };
    static const int mx[paramCount] = { 65536, 65536, 128, 1, 1200, 200, 1000, 1000, 1, 160, 160, 160 };
    const int i = std::clamp (param, 0, paramCount - 1);
    lo = mn[i], hi = mx[i];
}

struct Engine::Impl
{
    State s;
    int fs = 44100;
    std::array<Band, 128> ana, syn;
    float gMod = 1, gCar = 0, wet = 0, kGain = 0;
    float att = 0, rel = 0;
    std::vector<float> mod, car, tmp, tmp2, out;

    explicit Impl (const State& st) : s (st)
    {
        s.levels.resize (128, 1.0f);
        for (int k = 0; k < 128; ++k)
            ana[(std::size_t) k].level = s.levels[(std::size_t) k];
        recompute();
    }

    void recompute()
    {
        const auto& p = s.p;
        if (s.listen == 0)
            gMod = 1.0f, gCar = 0.0f, wet = 0.0f;
        else if (s.listen == 1)
            gMod = 0.0f, gCar = 1.0f, wet = 0.0f;
        else if (s.listen == 2)
        {
            gMod = curve11 (p[9]);
            gCar = curve11 (p[10]);
            wet = curve11 (p[11]);
        }
        float fr[2];
        for (int k = 0; k < 2; ++k)
        {
            const double x = (double) p[(std::size_t) k] * 1.52587890625e-05;
            const double e = std::exp ((double) F (x) * std::log (101.0));
            fr[k] = F ((double) F (e - 1.0) * 159.9 + 10.0);
        }
        if (fr[1] < fr[0])
            fr[0] = fr[1] - 1.0f;
        kGain = (wet * 1500.0f) / (float) p[5];
        const float bwk = ((float) p[5] * (float) s.order) / 120000.0f;
        const double e = std::exp ((double) ((float) p[2] * 0.0078125f) * std::log (10000.0));
        const float scale = (float) ((long long) std::nearbyint ((double) F (e - 1.0)) + 1);
        const float top = (float) ((double) (std::int32_t) ((std::uint32_t) fs * 20000u) / 44100.0); // 32-bit product, as the plugin
        bank (syn, true, fr, scale, bwk, top);
        bank (ana, false, fr, scale, bwk, top);
        const double ms = (double) fs * 0.001;
        const float ln01 = (float) std::log (0.01);
        att = F (std::exp ((double) (ln01 / ((float) p[6] * (float) ms))));
        rel = F (std::exp ((double) (ln01 / ((float) p[7] * (float) ms))));
    }

    void bank (std::array<Band, 128>& b, bool synthesis, const float* fr, float scale, float bwk, float top)
    {
        const int N = s.bands;
        float prev = 0.0f;
        const double lnS = std::log ((double) (scale + 1.0f));
        for (int i = 0; i <= N; ++i)
        {
            const double e = std::exp ((double) ((float) i / (float) N) * lnS);
            const float f = fr[0] + (F (e - 1.0) / scale) * (fr[1] - fr[0]);
            float cents = F (std::log ((double) (f / 440.0f)) / kCentLn + 5700.0);
            int idx = i - 1;
            if (synthesis)
            {
                cents = cents + (float) s.p[4];
                if (s.p[3] > 0)
                    idx = (N - idx) - 1;
            }
            if (i > 0)
            {
                auto& band = b[(std::size_t) idx];
                band.hz = F (std::exp ((double) ((prev + cents) * 0.5f - 5700.0f) * kCentLn) * 440.0);
                bool on = ! (band.hz < 10.0f) && band.hz < top;
                if (! synthesis && ! syn[(std::size_t) idx].active)
                    on = false;
                band.active = on;
                if (! on)
                {
                    band.silent = true;
                    band.gain = 0.0f;
                    band.env = 0.0f;
                    band.clear();
                    syn[(std::size_t) idx].clear();
                }
                else
                {
                    band.width = bwk * (cents - prev);
                    design (band);
                    band.gain = band.level * kGain;
                }
            }
            prev = cents;
        }
    }

    void design (Band& b) const
    {
        const double w = (double) b.hz * (1.0 / (double) fs * 6.283185307179586);
        const double sn = std::sin (w), cs = std::cos (w);
        const double a = sinhD ((kLn2Half * (double) b.width * w) / sn);
        const double alpha = sn * a;
        const double d = 1.0 / (alpha + 1.0);
        b.c.a1 = d * cs * -2.0;
        b.c.a2 = d * (1.0 - alpha);
        b.c.b1 = 0.0;
        b.c.b0 = d * alpha;
        b.c.b2 = d * -alpha;
    }

    void clearAll()
    {
        for (int k = 0; k < 128; ++k)
        {
            syn[(std::size_t) k].clear();
            ana[(std::size_t) k].clear();
            ana[(std::size_t) k].env = 0.0f;
        }
    }

    // x87 follower over the band buffer in place: |y|·gain, attack/release one-pole; returns the samples left non-zero.
    // The plugin's denormal test reads 64 bits (this sample's envelope and the next sample's raw value): the envelope is
    // cleared only when it is below 2^-24 and the next sample is exactly +0 (after the last sample: LYNX reads +0).
    int follower (Band& b, float* y, int n) const noexcept
    {
        ld env = b.env;
        const ld g = b.gain, A = att, R = rel;
        int count = n;
        for (int i = 0; i < n; ++i)
        {
            y[i] = std::abs (y[i]);
            const ld x = (ld) y[i] * g;
            const ld c = x > env ? A : R;
            env = x + (env - x) * c;
            const float st = (float) env;
            y[i] = st;
            const std::uint32_t next = i + 1 < n ? bitsOf (y[i + 1]) : 0u;
            if (next == 0u && bitsOf (st) < bitsOf (kTiny))
            {
                y[i] = 0.0f;
                env = 0.0L;
                --count;
            }
        }
        b.env = (float) env;
        return count;
    }

    void block (const float* x, float* y, int n)
    {
        if ((int) mod.size() < n + 1)
            for (auto* v : { &mod, &car, &tmp, &tmp2, &out })
                v->assign ((std::size_t) n + 1, 0.0f);
        for (int i = 0; i < n; ++i)
        {
            mod[(std::size_t) i] = x[2 * i];
            car[(std::size_t) i] = x[2 * i + 1];
        }
        const float* m = s.swap ? car.data() : mod.data();
        const float* c = s.swap ? mod.data() : car.data();
        std::fill (out.begin(), out.begin() + n, 0.0f);
        const bool hold = s.p[8] != 0;
        for (int k = 0; k < s.bands; ++k)
        {
            auto& b = ana[(std::size_t) k];
            if (b.silent && ! (0.0f < b.gain))
                continue;
            if (! hold)
            {
                const float* src = m;
                for (int o = 0; o < s.order; ++o)
                {
                    biquad (b.c, b.st[(std::size_t) o], src, tmp.data(), n);
                    src = tmp.data();
                }
                b.silent = follower (b, tmp.data(), n) == 0;
                if (! b.silent)
                {
                    synth (k, c, n);
                    for (int i = 0; i < n; ++i)
                        out[(std::size_t) i] = (float) ((ld) tmp2[(std::size_t) i] * (ld) tmp[(std::size_t) i] + (ld) out[(std::size_t) i]);
                }
            }
            else if (! b.silent)
            {
                synth (k, c, n);
                for (int i = 0; i < n; ++i)
                    out[(std::size_t) i] = tmp2[(std::size_t) i] * b.env + out[(std::size_t) i];
            }
        }
        if (0.0f <= gMod)
            for (int i = 0; i < n; ++i)
                out[(std::size_t) i] = m[i] * gMod + out[(std::size_t) i];
        if (0.0f <= gCar)
            for (int i = 0; i < n; ++i)
                out[(std::size_t) i] = c[i] * gCar + out[(std::size_t) i];
        for (int i = 0; i < n; ++i)
            y[2 * i] = y[2 * i + 1] = out[(std::size_t) i];
    }

    void synth (int k, const float* c, int n)
    {
        auto& b = syn[(std::size_t) k];
        const float* src = c;
        for (int o = 0; o < s.order; ++o)
        {
            biquad (b.c, b.st[(std::size_t) o], src, tmp2.data(), n);
            src = tmp2.data();
        }
    }
};

Engine::Engine (const State& s) : d (std::make_unique<Impl> (s)) {}
Engine::~Engine() = default;
void Engine::setRate (int sampleRate) noexcept
{
    d->fs = std::max (1, sampleRate);
    d->recompute();
}
void Engine::setParam (int index, int raw) noexcept
{
    if (index < 0 || index >= paramCount)
        return;
    d->s.p[(std::size_t) index] = raw;
    d->recompute();
}
void Engine::reset() noexcept { d->clearAll(); }
void Engine::render (const float* in, float* out, int n) noexcept
{
    if (n > 0)
        d->block (in, out, n);
}

// ------------------------------------------------------------------------------------------------- LYNX effect
namespace
{
int rawFromValue (int i, float v) noexcept
{
    int lo, hi;
    rawRange (i, lo, hi);
    return lo + (int) std::lround (std::clamp ((double) v, 0.0, 1.0) * (hi - lo));
}
float valueFromRaw (int i, int raw) noexcept
{
    int lo, hi;
    rawRange (i, lo, hi);
    return (float) std::clamp ((double) (raw - lo) / (double) (hi - lo), 0.0, 1.0);
}

class VocoderEffect final : public Effect
{
public:
    explicit VocoderEffect (const std::vector<std::uint8_t>& flState)
    {
        if (auto s = parse (flState))
        {
            engine = std::make_unique<Engine> (*s);
            raw = s->p;
        }
    }
    void prepare (double sr) noexcept override
    {
        if (engine)
        {
            engine->setRate ((int) std::lround (sr));
            engine->reset();
        }
    }
    void reset() noexcept override
    {
        if (engine)
            engine->reset();
    }
    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        if (! engine || n <= 0)
            return;
        for (int i = 0; i < paramCount; ++i)
        {
            const auto v = rawFromValue (i, p[(std::size_t) i]);
            if (v != raw[(std::size_t) i])
            {
                raw[(std::size_t) i] = v;
                engine->setParam (i, v);
            }
        }
        if ((int) io.size() < 2 * n)
            io.resize ((std::size_t) (2 * n));
        for (int i = 0; i < n; ++i)
            io[(std::size_t) (2 * i)] = l[i], io[(std::size_t) (2 * i + 1)] = r[i];
        engine->render (io.data(), io.data(), n);
        for (int i = 0; i < n; ++i)
            l[i] = io[(std::size_t) (2 * i)], r[i] = io[(std::size_t) (2 * i + 1)];
    }

private:
    std::unique_ptr<Engine> engine;
    std::array<int, paramCount> raw {};
    std::vector<float> io;
};
} // namespace

std::optional<EffectSlot> slotFromFl (const std::vector<std::uint8_t>& flState)
{
    const auto s = parse (flState);
    if (! s)
        return std::nullopt;
    auto slot = makeEffectSlot (EffectKind::fruityVocoder);
    slot.state = flState;
    for (int i = 0; i < paramCount; ++i)
        slot.params[(std::size_t) i] = valueFromRaw (i, s->p[(std::size_t) i]);
    return slot;
}

const EffectInfo& info()
{
    static const EffectInfo inf = [] {
        static const char* const names[paramCount] = { "Freq min", "Freq max", "Freq scale", "Invert", "Formant",
                                                       "Bandwidth", "Attack", "Release", "Hold", "Modulator level",
                                                       "Carrier level", "Wet level" };
        const State def;
        EffectInfo e { "Fruity Vocoder", {} };
        for (int i = 0; i < paramCount; ++i)
            e.params.push_back ({ names[i], valueFromRaw (i, def.p[(std::size_t) i]) });
        return e;
    }();
    return inf;
}

std::string paramText (int param, float value01)
{
    const int v = rawFromValue (param, value01);
    char t[48];
    switch (param)
    {
        case 0:
        case 1:
            std::snprintf (t, sizeof t, "%.0f Hz", 10.0 + 159.9 * (std::pow (101.0, v / 65536.0) - 1.0));
            break;
        case 2: std::snprintf (t, sizeof t, "%d", v); break;
        case 3:
        case 8: return v != 0 ? "on" : "off";
        case 4: std::snprintf (t, sizeof t, "%+d cents", v); break;
        case 5: std::snprintf (t, sizeof t, "%d%%", v); break;
        case 6:
        case 7: std::snprintf (t, sizeof t, "%d ms", v); break;
        default: std::snprintf (t, sizeof t, "%.1f dB", 20.0 * std::log10 (std::max (1e-6, (double) curve11 (v)))); break;
    }
    return t;
}

std::unique_ptr<Effect> make (const EffectSlot& slot) { return std::make_unique<VocoderEffect> (slot.state); }
} // namespace lynx::flvocoder
