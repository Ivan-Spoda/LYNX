#include "core/FruityPhaser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// FL's Fruity Phaser (docs/analise/REA_rodada105_phaser.md). Single-precision arithmetic in the order FL's plugin
// performs it; the coefficients that FL computes in double are computed in double here too.

namespace lynx::phaserfx
{
namespace
{
inline int rawFrom (float v01, int hi) noexcept { return (int) std::lround (std::clamp ((double) v01, 0.0, 1.0) * hi); }
inline float norm (int raw, int hi) noexcept { return (float) std::clamp ((double) raw / (double) hi, 0.0, 1.0); }
inline int roundEven (double x) noexcept { return (int) (long long) std::nearbyint (x); } // FL's Round (to nearest, ties to even)
inline std::int32_t wrapAdd (std::int32_t a, std::int32_t b) noexcept { return (std::int32_t) ((std::uint32_t) a + (std::uint32_t) b); }

// the LFO: a raised sine of 4096 entries, its slopes and the fractional-position table
struct Tables
{
    std::array<float, 4096> sine {}, slope {}, frac {};
    Tables()
    {
        for (int i = 0; i < 4096; ++i)
        {
            frac[(std::size_t) i] = (float) ((double) i / 4096.0);
            sine[(std::size_t) i] = (float) ((std::sin (((double) i / 4096.0) * 6.283185307) + 1.0) * 0.5);
        }
        for (int i = 0; i < 4095; ++i)
            slope[(std::size_t) i] = sine[(std::size_t) i + 1] - sine[(std::size_t) i];
        slope[4095] = sine[0] - sine[4095];
    }
};
const Tables& tables()
{
    static const Tables t;
    return t;
}

std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o) noexcept
{
    std::uint32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return (std::int32_t) v;
}
} // namespace

float sweepHz (float sweep01, bool large) noexcept
{
    const double a = std::exp ((double) (sweep01 * 4.0f)), b = std::exp (4.0);
    return (float) (((a - 1.0) / (b - 1.0)) * (large ? 10.0 : 2.0));
}

std::optional<State> parse (const std::vector<std::uint8_t>& state)
{
    if (state.size() < 4 + 4 * (std::size_t) params || i32At (state, 0) >= 2)
        return std::nullopt;
    State s {};
    for (int i = 0; i < params; ++i)
        s[(std::size_t) i] = i32At (state, 4 + 4 * (std::size_t) i);
    return s;
}

std::vector<std::uint8_t> serialize (const State& s)
{
    std::vector<std::uint8_t> out (4 + 4 * (std::size_t) params);
    const std::int32_t version = 1;
    std::memcpy (out.data(), &version, 4);
    for (int i = 0; i < params; ++i)
        std::memcpy (out.data() + 4 + 4 * (std::size_t) i, &s[(std::size_t) i], 4);
    return out;
}

void toParams (const State& s, float* p) noexcept
{
    for (int i = 0; i < params; ++i)
        p[i] = norm (s[(std::size_t) i], rawMax[(std::size_t) i]);
}

State fromParams (const float* p) noexcept
{
    State s {};
    for (int i = 0; i < params; ++i)
        s[(std::size_t) i] = rawFrom (p[i], rawMax[(std::size_t) i]);
    return s;
}

const char* paramName (int param) noexcept
{
    static const char* names[params] { "Sweep freq.", "Min depth", "Max depth", "Freq. range", "Stereo",
                                       "Stages", "Feedback", "Dry-wet", "Out gain" };
    return param >= 0 && param < params ? names[param] : "";
}

std::string paramText (int param, float value01)
{
    char text[32];
    const int p = std::clamp (param, 0, params - 1);
    const int raw = rawFrom (value01, rawMax[(std::size_t) p]);
    const float x = norm (raw, rawMax[(std::size_t) p]);
    switch (p)
    {
        case 0: std::snprintf (text, sizeof (text), "%.3f Hz", (double) sweepHz (x, false)); return text; // small range
        case 3: return x > 0.5f ? "large" : "small";
        case 4: std::snprintf (text, sizeof (text), "%.0f°", (double) x * 180.0); return text;
        case 5: std::snprintf (text, sizeof (text), "%d", std::clamp (raw + 1, 3, maxStages)); return text;
        case 7: std::snprintf (text, sizeof (text), "%d%% wet", 100 - (int) std::nearbyint ((double) (x * 100.0f))); return text;
        case 8:
        {
            const double g = (double) x * 2.0;
            if (g <= 0.0)
                return "-inf dB";
            std::snprintf (text, sizeof (text), "%+.1f dB", 20.0 * std::log10 (g));
            return text;
        }
        default: std::snprintf (text, sizeof (text), "%d%%", (int) std::lround (100.0 * x)); return text;
    }
}

struct Engine::Impl
{
    State raw = defaults;
    std::array<float, params> v {};
    float rate = 0.0f;    // LFO rate (Hz)
    float scale = 0.0f;   // phase units per Hz and sample: (4096 / fs) · 4096
    int stages = 8;
    // all-pass states: each stage's last output and the last input of the first stage (left, right)
    std::array<float, maxStages> outL {}, inL {}, outR {}, inR {};
    float lastL = 0.0f, lastR = 0.0f;
    std::int32_t phaseL = 0, phaseR = 0;

    std::int32_t stereoOffset() const noexcept { return roundEven ((double) v[4] * 8388608.0); }

    void setParam (int i, int value) noexcept
    {
        raw[(std::size_t) i] = value;
        v[(std::size_t) i] = (float) std::clamp ((double) value * (1.0 / (double) rawMax[(std::size_t) i]), 0.0, 1.0);
        if (i == 0)
            rate = sweepHz (v[0], (double) v[3] > 0.5);
        else if (i == 3)
            setParam (0, raw[0]);
        else if (i == 5)
            stages = std::clamp (value + 1, 3, maxStages);
    }

    float lfo (std::int32_t phase) const noexcept
    {
        const auto& t = tables();
        const auto u = (std::uint32_t) phase;
        const auto k = (std::size_t) ((u >> 12) & 4095u);
        return t.sine[k] + t.frac[u & 4095u] * t.slope[k];
    }

    // a cascade of first-order all-passes y = c·(x − y₁) + x₁
    void chain (std::array<float, maxStages>& out, std::array<float, maxStages>& in0, float x, float c) const noexcept
    {
        float prev = out[0];
        out[0] = c * (x - out[0]) + in0[0];
        in0[0] = x;
        for (int k = 1; k < stages; ++k)
        {
            const float old = out[(std::size_t) k];
            out[(std::size_t) k] = c * (out[(std::size_t) k - 1] - old) + prev;
            prev = old;
        }
    }
};

Engine::Engine() : d (std::make_unique<Impl>())
{
    setRate (44100);
    load (defaults);
    reset();
}

Engine::~Engine() = default;

void Engine::setRate (int sampleRate) noexcept
{
    d->scale = (float) ((4096.0 / (double) (float) std::max (1, sampleRate)) * 4096.0);
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

void Engine::reset() noexcept
{
    d->outL.fill (0.0f);
    d->inL.fill (0.0f);
    d->outR.fill (0.0f);
    d->inR.fill (0.0f);
    d->lastL = d->lastR = 0.0f;
    d->phaseL = 0;
    d->phaseR = wrapAdd (d->phaseL, d->stereoOffset());
}

int Engine::param (int index) const noexcept
{
    return index >= 0 && index < params ? d->raw[(std::size_t) index] : 0;
}

void Engine::render (const float* in, float* out, int n) noexcept
{
    auto& s = *d;
    const float fb = (float) ((double) s.v[6] * 0.99);
    const float gain = (float) ((double) s.v[8] * 2.0 * std::sqrt ((double) (1.0f - s.v[6] * s.v[6])));
    const float minDepth = s.v[1], range = s.v[2] - s.v[1], dryWet = s.v[7];
    const auto inc = (std::int32_t) roundEven ((double) (s.rate * s.scale));
    s.phaseR = wrapAdd (s.phaseL, s.stereoOffset());

    for (int i = 0; i < n; ++i)
    {
        const float l = in[2 * i], r = in[2 * i + 1];
        const float cl = -minDepth - s.lfo (s.phaseL) * range;
        s.phaseL = wrapAdd (s.phaseL, inc);
        const float cr = -minDepth - s.lfo (s.phaseR) * range;
        s.phaseR = wrapAdd (s.phaseR, inc);
        s.chain (s.outL, s.inL, l + s.lastL * fb, cl);
        s.chain (s.outR, s.inR, r + s.lastR * fb, cr);
        s.lastL = s.outL[(std::size_t) s.stages - 1];
        s.lastR = s.outR[(std::size_t) s.stages - 1];
        out[2 * i] = (dryWet * (l - s.lastL) + s.lastL) * gain;
        out[2 * i + 1] = (dryWet * (r - s.lastR) + s.lastR) * gain;
    }
}

namespace
{
class PhaserEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override { engine.setRate ((int) std::lround (sampleRate)); }
    void reset() noexcept override { engine.reset(); }
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        const auto s = fromParams (params.data());
        for (int i = 0; i < phaserfx::params; ++i)
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

std::unique_ptr<Effect> make() { return std::make_unique<PhaserEffect>(); }
} // namespace lynx::phaserfx
