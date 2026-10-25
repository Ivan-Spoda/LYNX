#include "core/EmphasizerFx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/DelphiMath.h"
#include "core/Denormals.h"
#include "core/IldspMath.h"

// Clean-room rebuild of FL's Emphasizer (docs/analise/REA_rodada154.md). Float32 throughout like the library; the
// coefficient set-up uses double exp then rounds to float, the gains use the library's expf.

#pragma clang fp contract(off)

namespace lynx::emphasizer
{
namespace
{
std::int32_t i32At (const std::vector<std::uint8_t>& d, std::size_t o)
{
    std::int32_t v = 0;
    std::memcpy (&v, d.data() + o, 4);
    return v;
}

constexpr float ceiling = 0.9885531f;       // the limiter's threshold (≈ −0.1 dB)
constexpr double twoPi = 6.283185307179586; // the library's −2π factor (time constant → coefficient)
} // namespace

std::optional<State> parse (const std::vector<std::uint8_t>& d)
{
    if (d.size() < 4)
        return std::nullopt;
    const auto version = i32At (d, 0);
    if (version >= 3)
        return std::nullopt;
    const std::size_t controls = version < 2 ? 5 : 6;
    if (d.size() < 4 + 4 * controls)
        return std::nullopt;
    State s;
    for (std::size_t k = 0; k < controls; ++k)
        s.raw[k] = i32At (d, 4 + 4 * k);
    // version 1 has no output control: the plug-in keeps the control's default (0 dB)
    if (d.size() > 4 + 4 * controls)
        s.loudnessMeter = d[4 + 4 * controls] != 0;
    return s;
}

void toParams (const State& s, float* params) noexcept
{
    for (int k = 0; k < numParams; ++k)
        params[k] = (float) std::clamp ((double) s.raw[(std::size_t) k] / rawMax[(std::size_t) k], 0.0, 1.0);
}

float engineValue (int k, int raw) noexcept
{
    // the front end's control value: (raw − min) · (1 / range), clipped to 0…1, as float
    const auto x = (float) std::clamp ((double) raw * (1.0 / rawMax[(std::size_t) std::clamp (k, 0, numParams - 1)]), 0.0, 1.0);
    switch (k)
    {
        case 0: // 0 + (24 − 0) · x^(1/1), through the run-time's Exp(Ln(x) / 1)
        {
            const double p = x > 0.0f ? dmath::exp (dmath::ln ((double) x) / 1.0) : 0.0;
            return (float) (0.0 + (24.0 - 0.0) * p);
        }
        case 1: return x;
        case 5:
        {
            float v;
            if (x == 1.0f)
                v = 1.0f;
            else
                v = (float) ((dmath::exp ((double) (x * 2.39789534f)) - 1.0) * (double) 0.1f);
            if (! (v > 0.0f))
                return -INFINITY;
            return (float) (dmath::ln ((double) v) * 0.4342944819032518 * 20.0);
        }
        default: return (float) raw; // the switches go through as their raw integer
    }
}

void Engine::Delay::run (const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        buf[write] = in[i];
        read = (std::uint16_t) (write - length);
        out[i] = buf[read];
        ++write;
    }
}

void Engine::init (float rate)
{
    sr = rate;
    invSr = 1.0f / rate;
    inDb = 0.0f;
    amount2 = 0.0f;
    attackT = 0.0f;
    releaseT = 0.0f;
    inTarget = inGain = 1.0f;
    detScale = 1.0f;
    std::fill (dryBufL.begin(), dryBufL.end(), 0.0f);
    std::fill (dryBufR.begin(), dryBufR.end(), 0.0f);
    std::fill (wetL.begin(), wetL.end(), 0.0f);
    std::fill (wetR.begin(), wetR.end(), 0.0f);
    std::fill (gain.begin(), gain.end(), 0.0f);
    lastGain = refGain = 0.0f;
    softClip = levelMatch = bypass = mix = 0.0f;
    smooth = (float) std::exp ((double) invSr * -125.66370614359172);
    const auto length = (std::uint16_t) (std::uint64_t) std::max (0.0f, rate * 0.002f);
    for (auto* d : { &dryL, &dryR, &wetDelayL, &wetDelayR })
    {
        d->clear();
        d->length = length;
    }
    // the peak hold / envelope keeps its times; its coefficients follow the new rate
    holdLen = std::max (0.025f, 0.0f) * 0.125f * rate;
    if (envAttackT > 0.0f && envReleaseT > 0.0f)
    {
        envAttack = (float) std::exp ((double) invSr * -twoPi / (double) envAttackT);
        envRelease = (float) std::exp ((double) invSr * -twoPi / (double) envReleaseT);
    }
}

void Engine::setParam (int k, float v) noexcept
{
    switch (k)
    {
        case 0:
            inDb = v;
            inTarget = ildsp::expf_ (v * 0.115129255f);
            break;
        case 1:
        {
            const float a = v * v;
            amount2 = a;
            const float rel = (1.0f - a) * 3.0f;
            const float att = a * 4.0f + 0.002f;
            detScale = 1.0f - a * 0.5f;
            attackT = att;
            releaseT = (float) ((double) rel + 0.1);
            envAttackT = att;
            envAttack = (float) std::exp (((double) invSr * -twoPi) / (double) att);
            holdLen = sr * 0.003125f;
            envReleaseT = releaseT;
            envRelease = (float) std::exp (((double) invSr * -twoPi) / (double) releaseT);
            const double c = (double) invSr * -twoPi;
            relFast = ildsp::expf_ ((float) (c / (double) releaseT));
            relSlow = ildsp::expf_ ((float) (c / (double) (releaseT * 0.25f)));
            break;
        }
        case 2: softClip = v; break;
        case 3: levelMatch = v; break;
        case 4: bypass = v; break;
        case 5:
            outDb = v;
            outTarget = ildsp::expf_ (v * 0.115129255f);
            break;
        default: break;
    }
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
{
    ScopedFlushDenormals ftz;
    const float scale = detScale, invScale = 1.0f / scale;
    const float c = smooth;
    const float rel[2] = { relFast, relSlow };
    const bool matching = levelMatch != 0.0f, clipSoft = softClip != 0.0f;
    const float wetTarget = 1.0f - (bypass != 0.0f ? 1.0f : 0.0f);

    dryL.run (inL, dryBufL.data(), n);
    dryR.run (inR, dryBufR.data(), n);

    float g = inGain;
    for (int i = 0; i < n; ++i)
    {
        g = (g - inTarget) * c + inTarget;
        wetL[(std::size_t) i] = g * inL[i];
        wetR[(std::size_t) i] = g * inR[i];
    }
    for (int i = 0; i < n; ++i)
    {
        const float l = std::fabs (wetL[(std::size_t) i]), r = std::fabs (wetR[(std::size_t) i]);
        gain[(std::size_t) i] = scale * (r <= l ? l : r);
    }

    float prev = lastGain, ref = refGain, gi = prev;
    for (int i = 0; i < n; ++i)
    {
        // 8 cascaded holds of holdLen samples (a running maximum)
        float x = gain[(std::size_t) i];
        for (int s = 0; s < 8; ++s)
        {
            if (holdV[(std::size_t) s] <= x || holdLen <= (float) holdN[(std::size_t) s])
                holdN[(std::size_t) s] = 0;
            else
            {
                ++holdN[(std::size_t) s];
                x = holdV[(std::size_t) s];
            }
            holdV[(std::size_t) s] = x;
        }
        const float h = x;
        lastHold = h;
        env = (env - h) * (env <= h && h != env ? envAttack : envRelease) + h;
        gi = ceiling / (env <= ceiling ? 0.9885531f : env);
        if (gi <= prev)
            ref = gi;
        float q = (invScale * h * 0.5f) / ref;
        if (q <= 1.0f)
            q = 1.0f;
        envRelease = rel[gi * gi < ref / q ? 1 : 0];
        gain[(std::size_t) i] = gi;
        prev = gi;
    }
    lastGain = n > 0 ? gi : lastGain;
    refGain = ref;

    wetDelayL.run (wetL.data(), wetL.data(), n);
    wetDelayR.run (wetR.data(), wetR.data(), n);
    for (int i = 0; i < n; ++i)
    {
        outL[i] = gain[(std::size_t) i] * wetL[(std::size_t) i];
        outR[i] = gain[(std::size_t) i] * wetR[(std::size_t) i];
    }

    auto clip = [&] (float* b) {
        if (! clipSoft)
        {
            for (int i = 0; i < n; ++i)
            {
                float v = b[i] < 1.0f ? b[i] : 1.0f;
                if (v <= -1.0f)
                    v = -1.0f;
                b[i] = v;
            }
            return;
        }
        constexpr float a = 0.84139514f, k = 0.15860486f, s = 6.30497694f;
        for (int i = 0; i < n; ++i)
        {
            float v = b[i];
            if (-a <= v)
            {
                if (a < v)
                {
                    const float t = (v - a) * s;
                    v = (t / std::sqrt (t * t + 1.0f)) * k + a;
                }
            }
            else
            {
                const float t = (v + a) * s;
                v = (t / std::sqrt (t * t + 1.0f)) * k - a;
            }
            b[i] = v;
        }
    };
    clip (outL);
    clip (outR);

    float go = outGain;
    for (int i = 0; i < n; ++i)
    {
        go = (go - outTarget) * c + outTarget;
        outL[i] = go * outL[i];
        outR[i] = go * outR[i];
    }
    outGain = go;

    if (matching)
    {
        const float m = 1.0f / g;
        for (int i = 0; i < n; ++i)
        {
            outL[i] = m * outL[i];
            outR[i] = m * outR[i];
        }
    }

    float w = mix;
    for (int i = 0; i < n; ++i)
    {
        w = (w - wetTarget) * c + wetTarget;
        const float dl = dryBufL[(std::size_t) i], dr = dryBufR[(std::size_t) i];
        outL[i] = ((outL[i] - dl) * w + dl) * 0.9999f;
        outR[i] = ((outR[i] - dr) * w + dr) * 0.9999f;
    }
    mix = w;
    inGain = g;
}

std::string paramText (int param, float v01)
{
    const int raw = (int) std::lround (std::clamp (v01, 0.0f, 1.0f) * rawMax[(std::size_t) std::clamp (param, 0, numParams - 1)]);
    char b[32];
    switch (param)
    {
        case 0: std::snprintf (b, sizeof b, "%+.1f dB", (double) engineValue (0, raw)); return b;
        case 1: std::snprintf (b, sizeof b, "%.0f%%", raw / 100.0); return b;
        case 2: return raw != 0 ? "Soft" : "Hard";
        case 3: case 4: return raw != 0 ? "On" : "Off";
        case 5:
        {
            const auto db = engineValue (5, raw);
            if (! std::isfinite (db))
                return "-inf dB";
            std::snprintf (b, sizeof b, "%.1f dB", (double) db);
            return b;
        }
        default: return {};
    }
}

namespace
{
class EmphasizerEffect final : public Effect
{
public:
    void prepare (double sampleRate) noexcept override
    {
        rate = (float) sampleRate;
        ready = false;
    }
    void reset() noexcept override {} // FL's flush only resets the plug-in's meters
    int latencySamples() const noexcept override { return (int) std::lround (rate * 0.002f); } // reported to FL's PDC
    void process (float* left, float* right, int n, const EffectParams& params, double) noexcept override
    {
        std::array<int, numParams> raw {};
        for (int k = 0; k < numParams; ++k)
            raw[(std::size_t) k] = (int) std::lround (std::clamp (params[(std::size_t) k], 0.0f, 1.0f) * rawMax[(std::size_t) k]);
        if (! ready)
        {
            // FL: the sample rate first (the engine starts), then the state's controls in index order
            engine.init (rate);
            for (int k = 0; k < numParams; ++k)
                engine.setParam (k, engineValue (k, raw[(std::size_t) k]));
            current = raw;
            ready = true;
            // FL's mixer runs the plug-in on silence before a render starts (measured against FL's renders on the
            // reference PC: 1536 samples, plus 768 for a plug-in that reports latency, as this one does), so its gain
            // ramps and bypass cross-fade have nearly settled when the song begins
            std::array<float, 512> zl {}, zr {};
            for (int done = 0; done < warmUpSamples; done += 512)
            {
                zl.fill (0.0f);
                zr.fill (0.0f);
                engine.process (zl.data(), zr.data(), zl.data(), zr.data(), std::min (512, warmUpSamples - done));
            }
        }
        else
            for (int k = 0; k < numParams; ++k)
                if (raw[(std::size_t) k] != current[(std::size_t) k])
                {
                    engine.setParam (k, engineValue (k, raw[(std::size_t) k]));
                    current[(std::size_t) k] = raw[(std::size_t) k];
                }
        for (int done = 0; done < n;)
        {
            const int m = std::min (512, n - done);
            engine.process (left + done, right + done, left + done, right + done, m);
            done += m;
        }
    }

private:
    static constexpr int warmUpSamples = 1536 + 768;
    Engine engine;
    std::array<int, numParams> current {};
    float rate = 44100.0f;
    bool ready = false;
};
} // namespace

std::unique_ptr<Effect> make() { return std::make_unique<EmphasizerEffect>(); }
} // namespace lynx::emphasizer
