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

float limiterCeilingDb (float v) { return -12.0f + v * 12.0f; }
float limiterGainDb (float v) { return v * 24.0f; }
float limiterReleaseMs (float v) { return logRange (v, 10.0f, 1000.0f); }
int filterType (float v) { return std::clamp ((int) (v * 3.0f), 0, 2); } // passa-baixas, passa-altas, passa-banda
constexpr std::array<const char*, 3> filterNames { "Passa-baixas", "Passa-altas", "Passa-banda" };
float filterHz (float v) { return logRange (v, 20.0f, 20000.0f); }
float filterQ (float v) { return logRange (v, 0.5f, 12.0f); }
float distortionDriveDb (float v) { return v * 36.0f; }
float distortionToneHz (float v) { return logRange (v, 800.0f, 20000.0f); }
float chorusRateHz (float v) { return logRange (v, 0.05f, 5.0f); }
float chorusDepthMs (float v) { return v * 8.0f; }

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

    // passa-baixas/altas/banda (cookbook); o passa-banda tem ganho 0 dB no pico
    void filter (double sr, int type, double hz, double q) noexcept
    {
        const auto w = 2.0 * pi * std::min (hz, sr * 0.45) / sr;
        const auto cw = std::cos (w);
        const auto alpha = std::sin (w) / (2.0 * q);

        if (type == 0)
            set ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
        else if (type == 1)
            set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
        else
            set (alpha, 0, -alpha, 1 + alpha, -2 * cw, 1 - alpha);
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

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        if (params != lastParams)
        {
            lastParams = params;
            bands[0].shelf (sampleRate, 120.0, eqGainDb (params[0]), false);
            bands[1].peaking (sampleRate, eqMidHz (params[2]), 0.8, eqGainDb (params[1]));
            bands[2].shelf (sampleRate, 8000.0, eqGainDb (params[3]), true);
            output = dbToGain (eqGainDb (params[4]));
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

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        const auto threshold = compThresholdDb (params[0]);
        const auto ratio = compRatio (params[1]);
        const auto attack = (float) std::exp (-1.0 / (compAttackMs (params[2]) * 0.001 * sampleRate));
        const auto release = (float) std::exp (-1.0 / (compReleaseMs (params[3]) * 0.001 * sampleRate));
        const auto makeup = dbToGain (compMakeupDb (params[4]));

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

    void process (float* l, float* r, int n, const EffectParams& params, double bpm) noexcept override
    {
        const auto beats = delayDivisions[(std::size_t) delayIndex (params[0])];
        const auto seconds = std::min (maxSeconds - 0.01, beats * 60.0 / std::max (bpm, 20.0));
        const auto delaySamples = std::max<std::size_t> (1, (std::size_t) (seconds * sampleRate));
        const auto feedback = params[1] * 0.95f;
        const auto mix = params[2];
        const auto pingPong = params[3] > 0.5f;
        const auto dampCoeff = 1.0f - params[4] * 0.9f; // 1 = sem filtro no retorno
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

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        const auto feedback = 0.7f + params[0] * 0.28f; // tamanho da sala
        const auto damping = params[1] * 0.4f;
        const auto width = params[2];
        const auto mix = params[3];
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
// ---------------------------------------------------------------------------
// Limiter com lookahead: o ganho já começa a cair antes do pico chegar (atraso de 1,5 ms,
// informado como latência para o PDC alinhar com as outras trilhas).

class Limiter final : public Effect
{
public:
    static constexpr double lookaheadSeconds = 0.0015;

    Limiter()
    {
        const auto size = (std::size_t) (lookaheadSeconds * maxSampleRate) + 4;
        delay[0].assign (size, 0.0f);
        delay[1].assign (size, 0.0f);
        peaks.assign (size, 0.0f);
    }

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        lookahead = std::max (1, (int) std::lround (lookaheadSeconds * sr));
        attack = (float) (1.0 - std::exp (-1.0 / (lookahead * 0.3)));
        reset();
    }

    void reset() noexcept override
    {
        std::fill (delay[0].begin(), delay[0].end(), 0.0f);
        std::fill (delay[1].begin(), delay[1].end(), 0.0f);
        std::fill (peaks.begin(), peaks.end(), 0.0f);
        position = 0;
        gain = 1.0f;
    }

    int latencySamples() const noexcept override { return lookahead; }

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        const auto ceiling = dbToGain (limiterCeilingDb (params[0]));
        const auto input = dbToGain (limiterGainDb (params[1]));
        const auto release = (float) (1.0 - std::exp (-1.0 / (limiterReleaseMs (params[2]) * 0.001 * sampleRate)));

        for (int i = 0; i < n; ++i)
        {
            const auto a = l[i] * input, b = r[i] * input;
            peaks[(std::size_t) position] = std::max (std::abs (a), std::abs (b));

            // maior pico na janela que ainda vai sair (janela curta: busca direta é barata)
            float ahead = 0.0f;
            for (int k = 0; k < lookahead; ++k)
                ahead = std::max (ahead, peaks[(std::size_t) k]);

            const auto target = ahead > ceiling ? ceiling / ahead : 1.0f;
            gain += (target - gain) * (target < gain ? attack : release);

            const auto outL = delay[0][(std::size_t) position] * gain;
            const auto outR = delay[1][(std::size_t) position] * gain;
            delay[0][(std::size_t) position] = a;
            delay[1][(std::size_t) position] = b;
            position = (position + 1) % lookahead;

            // segurança: nunca passa do teto
            l[i] = std::clamp (outL, -ceiling, ceiling);
            r[i] = std::clamp (outR, -ceiling, ceiling);
        }
    }

private:
    double sampleRate = 44100.0;
    int lookahead = 64, position = 0;
    float attack = 0.1f, gain = 1.0f;
    std::array<std::vector<float>, 2> delay;
    std::vector<float> peaks;
};

// ---------------------------------------------------------------------------

class Filter final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        last.fill (-1.0f);
        reset();
    }

    void reset() noexcept override { biquad.reset(); }

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        if (params[0] != last[0] || params[1] != last[1] || params[2] != last[2])
        {
            last = params;
            biquad.filter (sampleRate, filterType (params[0]), filterHz (params[1]), filterQ (params[2]));
        }

        const auto mix = params[3];

        for (int i = 0; i < n; ++i)
        {
            l[i] += (biquad.process (l[i], 0) - l[i]) * mix;
            r[i] += (biquad.process (r[i], 1) - r[i]) * mix;
        }
    }

private:
    double sampleRate = 44100.0;
    Biquad biquad;
    EffectParams last {};
};

// ---------------------------------------------------------------------------

class Distortion final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        lastTone = -1.0f;
        reset();
    }

    void reset() noexcept override { tone.reset(); }

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        if (params[1] != lastTone)
        {
            lastTone = params[1];
            tone.filter (sampleRate, 0, distortionToneHz (params[1]), 0.707);
        }

        const auto drive = dbToGain (distortionDriveDb (params[0]));
        const auto mix = params[2];
        const auto output = dbToGain ((params[3] - 0.5f) * 24.0f);
        // compensa o ganho do drive para o volume não explodir
        const auto compensate = 1.0f / std::tanh (std::max (drive, 1.0f));

        for (int i = 0; i < n; ++i)
        {
            const auto wl = tone.process (std::tanh (l[i] * drive) * compensate, 0);
            const auto wr = tone.process (std::tanh (r[i] * drive) * compensate, 1);
            l[i] = (l[i] + (wl - l[i]) * mix) * output;
            r[i] = (r[i] + (wr - r[i]) * mix) * output;
        }
    }

private:
    double sampleRate = 44100.0;
    Biquad tone;
    float lastTone = -1.0f;
};

// ---------------------------------------------------------------------------
// Chorus: uma linha de atraso modulada por LFO, com fases opostas em L e R.

class Chorus final : public Effect
{
public:
    static constexpr double maxSeconds = 0.05;

    Chorus()
    {
        const auto size = (std::size_t) (maxSeconds * maxSampleRate) + 4;
        buffer[0].assign (size, 0.0f);
        buffer[1].assign (size, 0.0f);
    }

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        length = (int) std::min<std::size_t> (buffer[0].size(), (std::size_t) (maxSeconds * sr) + 4);
        reset();
    }

    void reset() noexcept override
    {
        std::fill (buffer[0].begin(), buffer[0].end(), 0.0f);
        std::fill (buffer[1].begin(), buffer[1].end(), 0.0f);
        write = 0;
        phase = 0.0;
    }

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        const auto rate = chorusRateHz (params[0]);
        const auto depth = chorusDepthMs (params[1]) * 0.001 * sampleRate;
        const auto mix = params[2];
        const auto feedback = params[3] * 0.7f;
        const auto base = 0.012 * sampleRate; // 12 ms
        const auto step = rate / sampleRate;

        for (int i = 0; i < n; ++i)
        {
            float* io[2] = { l + i, r + i };

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto lfo = std::sin (2.0 * pi * (phase + (ch == 0 ? 0.0 : 0.5)));
                const auto delaySamples = base + depth * 0.5 * (1.0 + lfo);
                auto readPos = (double) write - delaySamples;
                while (readPos < 0.0)
                    readPos += length;

                const auto i0 = (int) readPos;
                const auto frac = (float) (readPos - i0);
                const auto& buf = buffer[(std::size_t) ch];
                const auto delayed = buf[(std::size_t) i0] + frac * (buf[(std::size_t) ((i0 + 1) % length)] - buf[(std::size_t) i0]);
                buffer[(std::size_t) ch][(std::size_t) write] = *io[ch] + delayed * feedback;
                *io[ch] += (delayed - *io[ch]) * mix * 0.5f;
            }

            write = (write + 1) % length;
            phase += step;
            phase -= std::floor (phase);
        }
    }

private:
    double sampleRate = 44100.0;
    std::array<std::vector<float>, 2> buffer;
    int length = 1, write = 0;
    double phase = 0.0;
};
} // namespace

const EffectInfo& effectInfo (EffectKind kind)
{
    static const EffectInfo eq { "EQ", { { "Graves", 0.5f }, { "Médios", 0.5f }, { "Freq. médios", 0.5f }, { "Agudos", 0.5f }, { "Saída", 0.5f } } };
    static const EffectInfo comp { "Compressor", { { "Threshold", 0.7f }, { "Ratio", 3.0f / 19.0f }, { "Attack", 0.67f }, { "Release", 0.54f }, { "Makeup", 0.0f } } };
    static const EffectInfo delay { "Delay", { { "Tempo", 0.3f }, { "Feedback", 0.42f }, { "Mix", 0.3f }, { "Ping-pong", 0.0f }, { "Abafar", 0.3f } } };
    static const EffectInfo reverb { "Reverb", { { "Sala", 0.6f }, { "Abafar", 0.5f }, { "Largura", 1.0f }, { "Mix", 0.25f } } };
    static const EffectInfo plugin { "Plugin", {} };
    static const EffectInfo limiter { "Limiter", { { "Teto", 0.975f }, { "Ganho", 0.0f }, { "Release", 0.5f } } };
    static const EffectInfo filter { "Filtro", { { "Tipo", 0.0f }, { "Corte", 0.75f }, { "Resson.", 0.2f }, { "Mix", 1.0f } } };
    static const EffectInfo distortion { "Distorção", { { "Drive", 0.4f }, { "Tom", 0.7f }, { "Mix", 1.0f }, { "Saída", 0.5f } } };
    static const EffectInfo chorus { "Chorus", { { "Velocidade", 0.45f }, { "Profund.", 0.4f }, { "Mix", 0.5f }, { "Feedback", 0.0f } } };

    switch (kind)
    {
        case EffectKind::eq: return eq;
        case EffectKind::compressor: return comp;
        case EffectKind::delay: return delay;
        case EffectKind::reverb: return reverb;
        case EffectKind::plugin: return plugin;
        case EffectKind::limiter: return limiter;
        case EffectKind::filter: return filter;
        case EffectKind::distortion: return distortion;
        case EffectKind::chorus: return chorus;
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

        case EffectKind::limiter:
            switch (param)
            {
                case 0: std::snprintf (text, sizeof (text), "%.1f dB", limiterCeilingDb (v)); break;
                case 1: std::snprintf (text, sizeof (text), "+%.1f dB", limiterGainDb (v)); break;
                default: std::snprintf (text, sizeof (text), "%.0f ms", limiterReleaseMs (v)); break;
            }
            return text;

        case EffectKind::filter:
            switch (param)
            {
                case 0: return filterNames[(std::size_t) filterType (v)];
                case 1: std::snprintf (text, sizeof (text), "%.0f Hz", filterHz (v)); break;
                case 2: std::snprintf (text, sizeof (text), "Q %.2f", filterQ (v)); break;
                default: std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f); break;
            }
            return text;

        case EffectKind::distortion:
            switch (param)
            {
                case 0: std::snprintf (text, sizeof (text), "+%.1f dB", distortionDriveDb (v)); break;
                case 1: std::snprintf (text, sizeof (text), "%.0f Hz", distortionToneHz (v)); break;
                case 2: std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f); break;
                default: std::snprintf (text, sizeof (text), "%+.1f dB", (v - 0.5f) * 24.0f); break;
            }
            return text;

        case EffectKind::chorus:
            switch (param)
            {
                case 0: std::snprintf (text, sizeof (text), "%.2f Hz", chorusRateHz (v)); break;
                case 1: std::snprintf (text, sizeof (text), "%.1f ms", chorusDepthMs (v)); break;
                default: std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f); break;
            }
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
        case EffectKind::limiter: return std::make_unique<Limiter>();
        case EffectKind::filter: return std::make_unique<Filter>();
        case EffectKind::distortion: return std::make_unique<Distortion>();
        case EffectKind::chorus: return std::make_unique<Chorus>();
        case EffectKind::plugin: break; // criado pelo app (RackState::Factories)
    }
    return std::make_unique<Eq>();
}
} // namespace lynx
