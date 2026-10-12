#include "core/Effects.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lynx
{
namespace
{
constexpr double pi = 3.141592653589793;
constexpr double maxSampleRate = 192000.0;

float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }
float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1e-9f)); }

// 0..1 -> faixa logarítmica [lo, hi]
float logRange (float v, float lo, float hi) noexcept { return lo * std::pow (hi / lo, std::clamp (v, 0.0f, 1.0f)); }

// ---------------------------------------------------------------------------
// Mapeamento dos parâmetros (o mesmo usado pelo DSP e pelo texto da interface)

float eqGainDb (float v) { return (v - 0.5f) * 36.0f; } // -18..+18 dB
float eqMidHz (float v) { return logRange (v, 200.0f, 5000.0f); }
float compThresholdDb (float v) { return -60.0f + v * 60.0f; }
float compRatio (float v) { return 1.0f + v * 19.0f; }
float compAttackMs (float v) { return logRange (v, 0.1f, 100.0f); }
float compReleaseMs (float v) { return logRange (v, 10.0f, 1000.0f); }
float compMakeupDb (float v) { return v * 24.0f; }

// Divisões do Delay sincronizado com o BPM, em tempos (semínimas).
constexpr std::array<double, 8> delayDivisions { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
constexpr std::array<const char*, 8> delayNames { "1/16", "1/8", "1/8 pont.", "1/4", "1/4 pont.", "1/2", "1/2 pont.", "1 compasso" };

int delayIndex (float v) { return std::clamp ((int) (v * (float) delayDivisions.size()), 0, (int) delayDivisions.size() - 1); }

// ---------------------------------------------------------------------------
// Biquad (fórmulas clássicas do "Audio EQ Cookbook" de R. Bristow-Johnson)

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }

    float process (float x, int ch) noexcept
    {
        const auto y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    void peaking (double sr, double hz, double q, double db) noexcept
    {
        const auto a = std::pow (10.0, db / 40.0);
        const auto w = 2.0 * pi * hz / sr;
        const auto alpha = std::sin (w) / (2.0 * q);
        set (1 + alpha * a, -2 * std::cos (w), 1 - alpha * a, 1 + alpha / a, -2 * std::cos (w), 1 - alpha / a);
    }

    void shelf (double sr, double hz, double db, bool high) noexcept
    {
        const auto a = std::pow (10.0, db / 40.0);
        const auto w = 2.0 * pi * hz / sr;
        const auto cw = std::cos (w);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0); // inclinação S = 1
        const auto sq = 2.0 * std::sqrt (a) * alpha;

        if (high)
            set (a * ((a + 1) + (a - 1) * cw + sq), -2 * a * ((a - 1) + (a + 1) * cw), a * ((a + 1) + (a - 1) * cw - sq),
                 (a + 1) - (a - 1) * cw + sq, 2 * ((a - 1) - (a + 1) * cw), (a + 1) - (a - 1) * cw - sq);
        else
            set (a * ((a + 1) - (a - 1) * cw + sq), 2 * a * ((a - 1) - (a + 1) * cw), a * ((a + 1) - (a - 1) * cw - sq),
                 (a + 1) + (a - 1) * cw + sq, -2 * ((a - 1) + (a + 1) * cw), (a + 1) + (a - 1) * cw - sq);
    }
};

// ---------------------------------------------------------------------------

class Eq final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        lastParams.fill (-1.0f); // força recalcular
        reset();
    }

    void reset() noexcept override
    {
        for (auto& b : bands)
            b.reset();
    }

    void process (float* l, float* r, int n, const EffectSlot& slot, double) noexcept override
    {
        if (slot.params != lastParams)
        {
            lastParams = slot.params;
            bands[0].shelf (sampleRate, 120.0, eqGainDb (slot.params[0]), false);
            bands[1].peaking (sampleRate, eqMidHz (slot.params[2]), 0.8, eqGainDb (slot.params[1]));
            bands[2].shelf (sampleRate, 8000.0, eqGainDb (slot.params[3]), true);
            output = dbToGain (eqGainDb (slot.params[4]));
        }

        for (int i = 0; i < n; ++i)
        {
            auto a = l[i], b = r[i];

            for (auto& band : bands)
            {
                a = band.process (a, 0);
                b = band.process (b, 1);
            }

            l[i] = a * output;
            r[i] = b * output;
        }
    }

private:
    double sampleRate = 44100.0;
    std::array<Biquad, 3> bands;
    std::array<float, maxEffectParams> lastParams {};
    float output = 1.0f;
};

// ---------------------------------------------------------------------------

class Compressor final : public Effect
{
public:
    void prepare (double sr) noexcept override { sampleRate = sr; reset(); }
    void reset() noexcept override { envelopeDb = 0.0f; }

    void process (float* l, float* r, int n, const EffectSlot& slot, double) noexcept override
    {
        const auto threshold = compThresholdDb (slot.params[0]);
        const auto ratio = compRatio (slot.params[1]);
        const auto attack = (float) std::exp (-1.0 / (compAttackMs (slot.params[2]) * 0.001 * sampleRate));
        const auto release = (float) std::exp (-1.0 / (compReleaseMs (slot.params[3]) * 0.001 * sampleRate));
        const auto makeup = dbToGain (compMakeupDb (slot.params[4]));

        for (int i = 0; i < n; ++i)
        {
            // detector de pico estéreo ligado, curva "hard knee"
            const auto level = gainToDb (std::max (std::abs (l[i]), std::abs (r[i])));
            const auto over = level - threshold;
            const auto reductionDb = over > 0.0f ? over * (1.0f - 1.0f / ratio) : 0.0f;

            // o envelope sobe com o attack e desce com o release
            const auto coeff = reductionDb > envelopeDb ? attack : release;
            envelopeDb = reductionDb + coeff * (envelopeDb - reductionDb);

            const auto gain = dbToGain (-envelopeDb) * makeup;
            l[i] *= gain;
            r[i] *= gain;
        }
    }

private:
    double sampleRate = 44100.0;
    float envelopeDb = 0.0f;
};

// ---------------------------------------------------------------------------

class Delay final : public Effect
{
public:
    static constexpr double maxSeconds = 3.0;

    Delay()
    {
        const auto size = (std::size_t) (maxSeconds * maxSampleRate) + 4;
        buffer[0].assign (size, 0.0f);
        buffer[1].assign (size, 0.0f);
    }

    void prepare (double sr) noexcept override { sampleRate = sr; reset(); }

    void reset() noexcept override
    {
        std::fill (buffer[0].begin(), buffer[0].end(), 0.0f);
        std::fill (buffer[1].begin(), buffer[1].end(), 0.0f);
        damp[0] = damp[1] = 0.0f;
        write = 0;
    }

    void process (float* l, float* r, int n, const EffectSlot& slot, double bpm) noexcept override
    {
        const auto beats = delayDivisions[(std::size_t) delayIndex (slot.params[0])];
        const auto seconds = std::min (maxSeconds - 0.01, beats * 60.0 / std::max (bpm, 20.0));
        const auto delaySamples = std::max<std::size_t> (1, (std::size_t) (seconds * sampleRate));
        const auto feedback = slot.params[1] * 0.95f;
        const auto mix = slot.params[2];
        const auto pingPong = slot.params[3] > 0.5f;
        const auto dampCoeff = 1.0f - slot.params[4] * 0.9f; // 1 = sem filtro no retorno
        const auto size = buffer[0].size();

        for (int i = 0; i < n; ++i)
        {
            const auto readPos = (write + size - delaySamples) % size;
            auto echoL = buffer[0][readPos];
            auto echoR = buffer[1][readPos];

            // passa-baixas de 1 polo no retorno: repetições ficam mais escuras
            damp[0] += dampCoeff * (echoL - damp[0]);
            damp[1] += dampCoeff * (echoR - damp[1]);
            echoL = damp[0];
            echoR = damp[1];

            const auto inL = l[i], inR = r[i];

            if (pingPong) // entrada mono no lado esquerdo, retornos se alternam
            {
                buffer[0][write] = 0.5f * (inL + inR) + echoR * feedback;
                buffer[1][write] = echoL * feedback;
            }
            else
            {
                buffer[0][write] = inL + echoL * feedback;
                buffer[1][write] = inR + echoR * feedback;
            }

            l[i] = inL + mix * echoL;
            r[i] = inR + mix * echoR;
            write = (write + 1) % size;
        }
    }

private:
    double sampleRate = 44100.0;
    std::array<std::vector<float>, 2> buffer;
    std::size_t write = 0;
    float damp[2] {};
};

// ---------------------------------------------------------------------------
// Reverb no estilo Schroeder/Moorer (a estrutura do "Freeverb", domínio público):
// 8 filtros pente com passa-baixas no retorno + 4 passa-tudo por lado.

class Reverb final : public Effect
{
public:
    Reverb()
    {
        const auto scale = maxSampleRate / 44100.0;

        for (int ch = 0; ch < 2; ++ch)
        {
            for (std::size_t i = 0; i < combTunings.size(); ++i)
                combs[ch][i].buffer.assign ((std::size_t) ((combTunings[i] + stereoSpread) * scale) + 2, 0.0f);

            for (std::size_t i = 0; i < allpassTunings.size(); ++i)
                allpasses[ch][i].buffer.assign ((std::size_t) ((allpassTunings[i] + stereoSpread) * scale) + 2, 0.0f);
        }
    }

    void prepare (double sr) noexcept override
    {
        const auto scale = sr / 44100.0;

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto spread = ch == 1 ? stereoSpread : 0;

            for (std::size_t i = 0; i < combTunings.size(); ++i)
                combs[ch][i].length = std::min (combs[ch][i].buffer.size(), (std::size_t) ((combTunings[i] + spread) * scale));

            for (std::size_t i = 0; i < allpassTunings.size(); ++i)
                allpasses[ch][i].length = std::min (allpasses[ch][i].buffer.size(), (std::size_t) ((allpassTunings[i] + spread) * scale));
        }

        reset();
    }

    void reset() noexcept override
    {
        for (auto& side : combs)
            for (auto& c : side)
            {
                std::fill (c.buffer.begin(), c.buffer.end(), 0.0f);
                c.index = 0;
                c.store = 0.0f;
            }

        for (auto& side : allpasses)
            for (auto& a : side)
            {
                std::fill (a.buffer.begin(), a.buffer.end(), 0.0f);
                a.index = 0;
            }
    }

    void process (float* l, float* r, int n, const EffectSlot& slot, double) noexcept override
    {
        const auto feedback = 0.7f + slot.params[0] * 0.28f; // tamanho da sala
        const auto damping = slot.params[1] * 0.4f;
        const auto width = slot.params[2];
        const auto mix = slot.params[3];
        const auto wet1 = width / 2.0f + 0.5f;
        const auto wet2 = (1.0f - width) / 2.0f;
        constexpr float inputGain = 0.015f;

        for (int i = 0; i < n; ++i)
        {
            const auto input = (l[i] + r[i]) * inputGain;
            float out[2] = { 0.0f, 0.0f };

            for (int ch = 0; ch < 2; ++ch)
            {
                for (auto& c : combs[ch])
                    out[ch] += c.process (input, feedback, damping);

                for (auto& a : allpasses[ch])
                    out[ch] = a.process (out[ch]);
            }

            l[i] += mix * (out[0] * wet1 + out[1] * wet2);
            r[i] += mix * (out[1] * wet1 + out[0] * wet2);
        }
    }

private:
    struct Comb
    {
        std::vector<float> buffer;
        std::size_t length = 1, index = 0;
        float store = 0.0f;

        float process (float input, float feedback, float damping) noexcept
        {
            const auto output = buffer[index];
            store = output * (1.0f - damping) + store * damping;
            buffer[index] = input + store * feedback;
            index = (index + 1) % length;
            return output;
        }
    };

    struct Allpass
    {
        std::vector<float> buffer;
        std::size_t length = 1, index = 0;

        float process (float input) noexcept
        {
            const auto delayed = buffer[index];
            const auto output = delayed - input;
            buffer[index] = input + delayed * 0.5f;
            index = (index + 1) % length;
            return output;
        }
    };

    static constexpr std::array<int, 8> combTunings { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static constexpr std::array<int, 4> allpassTunings { 556, 441, 341, 225 };
    static constexpr int stereoSpread = 23;

    std::array<std::array<Comb, 8>, 2> combs;
    std::array<std::array<Allpass, 4>, 2> allpasses;
};
} // namespace

const EffectInfo& effectInfo (EffectKind kind)
{
    static const EffectInfo eq { "EQ", { { "Graves", 0.5f }, { "Médios", 0.5f }, { "Freq. médios", 0.5f }, { "Agudos", 0.5f }, { "Saída", 0.5f } } };
    static const EffectInfo comp { "Compressor", { { "Threshold", 0.7f }, { "Ratio", 3.0f / 19.0f }, { "Attack", 0.67f }, { "Release", 0.54f }, { "Makeup", 0.0f } } };
    static const EffectInfo delay { "Delay", { { "Tempo", 0.3f }, { "Feedback", 0.42f }, { "Mix", 0.3f }, { "Ping-pong", 0.0f }, { "Abafar", 0.3f } } };
    static const EffectInfo reverb { "Reverb", { { "Sala", 0.6f }, { "Abafar", 0.5f }, { "Largura", 1.0f }, { "Mix", 0.25f } } };
    static const EffectInfo plugin { "Plugin", {} };

    switch (kind)
    {
        case EffectKind::eq: return eq;
        case EffectKind::compressor: return comp;
        case EffectKind::delay: return delay;
        case EffectKind::reverb: return reverb;
        case EffectKind::plugin: return plugin;
    }
    return eq;
}

std::string formatEffectParam (EffectKind kind, int param, float v, double bpm)
{
    char text[48];

    switch (kind)
    {
        case EffectKind::eq:
            if (param == 2)
                std::snprintf (text, sizeof (text), "%.0f Hz", eqMidHz (v));
            else
                std::snprintf (text, sizeof (text), "%+.1f dB", eqGainDb (v));
            return text;

        case EffectKind::compressor:
            switch (param)
            {
                case 0: std::snprintf (text, sizeof (text), "%.1f dB", compThresholdDb (v)); break;
                case 1: std::snprintf (text, sizeof (text), "%.1f:1", compRatio (v)); break;
                case 2: std::snprintf (text, sizeof (text), "%.1f ms", compAttackMs (v)); break;
                case 3: std::snprintf (text, sizeof (text), "%.0f ms", compReleaseMs (v)); break;
                default: std::snprintf (text, sizeof (text), "+%.1f dB", compMakeupDb (v)); break;
            }
            return text;

        case EffectKind::delay:
            if (param == 0)
            {
                const auto index = delayIndex (v);
                std::snprintf (text, sizeof (text), "%s (%.0f ms)", delayNames[(std::size_t) index],
                               delayDivisions[(std::size_t) index] * 60000.0 / std::max (bpm, 20.0));
                return text;
            }
            if (param == 3)
                return v > 0.5f ? "ligado" : "desligado";
            std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f);
            return text;

        case EffectKind::reverb:
            std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f);
            return text;

        case EffectKind::plugin:
            break;
    }

    return {};
}

EffectSlot makeEffectSlot (EffectKind kind)
{
    EffectSlot slot;
    slot.kind = kind;
    const auto& info = effectInfo (kind);

    for (std::size_t i = 0; i < info.params.size() && i < slot.params.size(); ++i)
        slot.params[i] = info.params[i].defaultValue;

    return slot;
}

std::unique_ptr<Effect> makeEffect (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::eq: return std::make_unique<Eq>();
        case EffectKind::compressor: return std::make_unique<Compressor>();
        case EffectKind::delay: return std::make_unique<Delay>();
        case EffectKind::reverb: return std::make_unique<Reverb>();
        case EffectKind::plugin: break; // criado pelo app (RackState::Factories)
    }
    return std::make_unique<Eq>();
}
} // namespace lynx
