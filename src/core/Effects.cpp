#include "core/Effects.h"

#include "core/Biquad.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace lynx
{
namespace
{
constexpr double pi = 3.141592653589793;
constexpr double maxSampleRate = 192000.0;

float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

// tanh aproximada (Padé), com saturação em ±1: bem mais barata que std::tanh por sample
inline float fastTanh (float x) noexcept
{
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;

    const auto x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
// seno de uma fase em voltas (0..1) por parábola refinada (erro < 0,1%), para LFOs
inline float sinTurns (double phase) noexcept
{
    const auto x = (float) (phase < 0.5 ? phase : phase - 1.0) * 2.0f;
    const auto y = 4.0f * x * (1.0f - std::abs (x));
    return 0.225f * (y * std::abs (y) - y) + y;
}

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

float reverbLowCutHz (float v) { return logRange (v, 20.0f, 3000.0f); }    // 0 = desligado
float reverbHighCutHz (float v) { return logRange (v, 500.0f, 22000.0f); } // 1 = desligado

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
        const auto thresholdGain = dbToGain (threshold);

        for (int i = 0; i < n; ++i)
        {
            // detector de pico estéreo ligado, curva "hard knee"; abaixo do threshold não precisa do log
            const auto peak = std::max (std::abs (l[i]), std::abs (r[i]));
            const auto reductionDb = peak > thresholdGain ? (gainToDb (peak) - threshold) * (1.0f - 1.0f / ratio) : 0.0f;

            // o envelope sobe com o attack e desce com o release
            const auto coeff = reductionDb > envelopeDb ? attack : release;
            envelopeDb = reductionDb + coeff * (envelopeDb - reductionDb);

            if (envelopeDb < 1.0e-4f)
                envelopeDb = 0.0f; // sem redução audível: dispensa o pow

            const auto gain = envelopeDb > 0.0f ? dbToGain (-envelopeDb) * makeup : makeup;
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

        predelayBuffer.assign ((std::size_t) (maxPredelaySeconds * maxSampleRate) + 4, 0.0f);
    }

    static constexpr double maxPredelaySeconds = 1.0;

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        lastLowCut = lastHighCut = -1.0f;
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

        std::fill (predelayBuffer.begin(), predelayBuffer.end(), 0.0f);
        predelayWrite = 0;
        lowCut.reset();
        highCut.reset();
    }

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        const auto feedback = 0.7f + params[0] * 0.28f; // tamanho da sala
        const auto damping = params[1] * 0.4f;
        const auto width = params[2];
        const auto mix = params[3];
        const auto dry = params[4]; // 0 = só o reverb (reverb num send)
        const auto wet1 = width / 2.0f + 0.5f;
        const auto wet2 = (1.0f - width) / 2.0f;
        constexpr float inputGain = 0.015f;

        // na entrada do molhado: corte de graves/agudos (nas pontas = desligado) e predelay
        const auto lowCutHz = params[5], highCutHz = params[6];
        const auto useLowCut = lowCutHz > 0.001f, useHighCut = highCutHz < 0.999f;

        if (lowCutHz != lastLowCut || highCutHz != lastHighCut)
        {
            lastLowCut = lowCutHz;
            lastHighCut = highCutHz;
            lowCut.filter (sampleRate, 1, reverbLowCutHz (lowCutHz), 0.7071);
            highCut.filter (sampleRate, 0, reverbHighCutHz (highCutHz), 0.7071);
        }

        const auto predelay = std::min ((int) (params[7] * maxPredelaySeconds * sampleRate), (int) predelayBuffer.size() - 1);
        const auto predelayLength = (int) predelayBuffer.size();

        // filtro a filtro sobre um trecho (cada comb/allpass inteiro no cache), na mesma ordem
        // de soma do processamento amostra a amostra
        constexpr int span = 256;
        float input[span], out[2][span];

        for (int start = 0; start < n; start += span)
        {
            const auto m = std::min (span, n - start);
            auto* bl = l + start;
            auto* br = r + start;

            for (int i = 0; i < m; ++i)
            {
                auto x = (bl[i] + br[i]) * inputGain;

                if (useLowCut)
                    x = lowCut.process (x, 0);
                if (useHighCut)
                    x = highCut.process (x, 0);

                if (predelay > 0)
                {
                    predelayBuffer[(std::size_t) predelayWrite] = x;
                    auto read = predelayWrite - predelay;
                    if (read < 0)
                        read += predelayLength;
                    x = predelayBuffer[(std::size_t) read];

                    if (++predelayWrite == predelayLength)
                        predelayWrite = 0;
                }

                input[i] = x;
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                std::fill (out[ch], out[ch] + m, 0.0f);

                // 4 combs por laço: as cadeias de realimentação são independentes e a CPU as
                // intercala (um comb sozinho fica preso na latência do filtro de amortecimento)
                for (std::size_t c = 0; c + 4 <= combs[ch].size(); c += 4)
                    Comb::process4 (&combs[ch][c], input, out[ch], m, feedback, damping);

                for (auto& a : allpasses[ch])
                    a.process (out[ch], m);
            }

            for (int i = 0; i < m; ++i)
            {
                bl[i] = bl[i] * dry + mix * (out[0][i] * wet1 + out[1][i] * wet2);
                br[i] = br[i] * dry + mix * (out[1][i] * wet1 + out[0][i] * wet2);
            }
        }
    }

private:
    struct Comb
    {
        std::vector<float> buffer;
        std::size_t length = 1, index = 0;
        float store = 0.0f;

        // quatro combs de uma vez; soma em `out` na mesma ordem que um por um
        static void process4 (Comb* c, const float* input, float* out, int n, float feedback, float damping) noexcept
        {
            float* buf[4] = { c[0].buffer.data(), c[1].buffer.data(), c[2].buffer.data(), c[3].buffer.data() };
            std::size_t idx[4] = { c[0].index, c[1].index, c[2].index, c[3].index };
            const std::size_t len[4] = { c[0].length, c[1].length, c[2].length, c[3].length };
            float st[4] = { c[0].store, c[1].store, c[2].store, c[3].store };
            const auto keep = 1.0f - damping;

            for (int i = 0; i < n; ++i)
            {
                float o[4];

                for (int k = 0; k < 4; ++k)
                {
                    o[k] = buf[k][idx[k]];
                    st[k] = o[k] * keep + st[k] * damping;
                    buf[k][idx[k]] = input[i] + st[k] * feedback;

                    if (++idx[k] == len[k])
                        idx[k] = 0;
                }

                out[i] = out[i] + o[0] + o[1] + o[2] + o[3];
            }

            for (int k = 0; k < 4; ++k)
            {
                c[k].index = idx[k];
                c[k].store = st[k];
            }
        }

    };

    struct Allpass
    {
        std::vector<float> buffer;
        std::size_t length = 1, index = 0;

        void process (float* io, int n) noexcept
        {
            auto* buf = buffer.data();
            auto idx = index;
            const auto len = length;

            for (int i = 0; i < n; ++i)
            {
                const auto input = io[i];
                const auto delayed = buf[idx];
                io[i] = delayed - input;
                buf[idx] = input + delayed * 0.5f;

                if (++idx == len)
                    idx = 0;
            }

            index = idx;
        }
    };

    static constexpr std::array<int, 8> combTunings { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static constexpr std::array<int, 4> allpassTunings { 556, 441, 341, 225 };
    static constexpr int stereoSpread = 23;

    std::array<std::array<Comb, 8>, 2> combs;
    std::array<std::array<Allpass, 4>, 2> allpasses;
    double sampleRate = 44100.0;
    Biquad lowCut, highCut;
    float lastLowCut = -1.0f, lastHighCut = -1.0f;
    std::vector<float> predelayBuffer;
    int predelayWrite = 0;
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
        maxValues.assign (size, 0.0f);
        maxTimes.assign (size, 0);
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
        position = 0;
        gain = 1.0f;
        clock = 0;
        // a janela começa cheia de zeros: um candidato 0 cobre todos eles
        head = 0;
        count = 1;
        maxValues[0] = 0.0f;
        maxTimes[0] = -1;
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
            const auto peak = std::max (std::abs (a), std::abs (b));

            // maior pico entre os `lookahead` últimos (máximo deslizante com fila monotônica)
            const auto capacity = (int) maxValues.size();

            while (count > 0 && maxValues[(std::size_t) ((head + count - 1) % capacity)] <= peak)
                --count;

            const auto slot = (std::size_t) ((head + count) % capacity);
            maxValues[slot] = peak;
            maxTimes[slot] = clock;
            ++count;

            if (maxTimes[(std::size_t) head] <= clock - lookahead)
            {
                head = (head + 1) % capacity;
                --count;
            }

            ++clock;
            const auto ahead = maxValues[(std::size_t) head];

            const auto target = ahead > ceiling ? ceiling / ahead : 1.0f;
            gain += (target - gain) * (target < gain ? attack : release);

            const auto outL = delay[0][(std::size_t) position] * gain;
            const auto outR = delay[1][(std::size_t) position] * gain;
            delay[0][(std::size_t) position] = a;
            delay[1][(std::size_t) position] = b;

            if (++position == lookahead)
                position = 0;

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
    std::vector<float> maxValues;      // fila monotônica (decrescente) do máximo deslizante
    std::vector<std::int64_t> maxTimes;
    int head = 0, count = 0;
    std::int64_t clock = 0;
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
        const auto compensate = 1.0f / fastTanh (std::max (drive, 1.0f));

        for (int i = 0; i < n; ++i)
        {
            const auto wl = tone.process (fastTanh (l[i] * drive) * compensate, 0);
            const auto wr = tone.process (fastTanh (r[i] * drive) * compensate, 1);
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
            const auto s = sinTurns (phase); // L e R em fases opostas: o mesmo seno com sinal trocado

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto lfo = ch == 0 ? s : -s;
                const auto delaySamples = base + depth * 0.5 * (1.0 + lfo);
                auto readPos = (double) write - delaySamples;
                while (readPos < 0.0)
                    readPos += length;

                const auto i0 = (int) readPos;
                const auto frac = (float) (readPos - i0);
                const auto& buf = buffer[(std::size_t) ch];
                const auto next = i0 + 1 == length ? 0 : i0 + 1;
                const auto delayed = buf[(std::size_t) i0] + frac * (buf[(std::size_t) next] - buf[(std::size_t) i0]);
                buffer[(std::size_t) ch][(std::size_t) write] = *io[ch] + delayed * feedback;
                *io[ch] += (delayed - *io[ch]) * mix * 0.5f;
            }

            if (++write == length)
                write = 0;

            phase += step;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    double sampleRate = 44100.0;
    std::array<std::vector<float>, 2> buffer;
    int length = 1, write = 0;
    double phase = 0.0;
};

// ---------------------------------------------------------------------------
// EQ paramétrico de 7 bandas: cada banda é um biquad do cookbook (ou até 4 em série nos cortes,
// com os Q de um Butterworth da ordem pedida).

constexpr std::array<std::array<double, 4>, 4> butterworthQ { { { 0.70710678, 0, 0, 0 },
                                                                { 0.54119610, 1.30656296, 0, 0 },
                                                                { 0.51763809, 0.70710678, 1.93185165, 0 },
                                                                { 0.50979558, 0.60134489, 0.89997622, 2.56291545 } } };

// Coeficientes das etapas de uma banda; retorna quantas etapas (0 = banda desligada/transparente).
int bandStages (const EffectParams& p, int band, double sr, std::array<Biquad, 4>& out) noexcept
{
    const auto t = peq::type (p[(std::size_t) peq::param (band, 0)]);
    const auto hz = std::min (peq::hz (p[(std::size_t) peq::param (band, 1)]), sr * 0.45);
    const auto db = peq::gainDb (p[(std::size_t) peq::param (band, 2)]);
    const auto q = peq::q (p[(std::size_t) peq::param (band, 3)]);

    switch (t)
    {
        case peq::BandType::off: return 0;
        case peq::BandType::peak:
            if (std::abs (db) < 0.01)
                return 0;
            out[0].peaking (sr, hz, q, db);
            return 1;
        case peq::BandType::lowShelf:
        case peq::BandType::highShelf:
            if (std::abs (db) < 0.01)
                return 0;
            out[0].shelf (sr, hz, db, t == peq::BandType::highShelf, std::clamp (q, 0.1, 4.0)); // Q = inclinação S
            return 1;
        case peq::BandType::lowCut:
        case peq::BandType::highCut:
        {
            // em cascata: os Q de um Butterworth da ordem, escalados pelo Q da banda (0,707 = Butterworth)
            const auto stages = peq::slopeStages (p[(std::size_t) (peq::slopeParam + band)]);
            const auto& qs = butterworthQ[(std::size_t) (stages - 1)];
            for (int s = 0; s < stages; ++s)
                out[(std::size_t) s].filter (sr, t == peq::BandType::lowCut ? 1 : 0, hz, stages == 1 ? q : qs[(std::size_t) s] * q / 0.70710678);
            return stages;
        }
        case peq::BandType::bandPass:
            out[0].filter (sr, 2, hz, q);
            return 1;
        case peq::BandType::notch:
        {
            const auto w = 2.0 * pi * hz / sr;
            const auto alpha = std::sin (w) / (2.0 * q);
            out[0].set (1, -2 * std::cos (w), 1, 1 + alpha, -2 * std::cos (w), 1 - alpha);
            return 1;
        }
    }

    return 0;
}

class ParametricEq final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        lastParams.fill (-1.0f);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& band : stages)
            for (auto& s : band)
                s.reset();
    }

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        if (params != lastParams)
        {
            lastParams = params;
            active = 0;

            for (int b = 0; b < peq::bands; ++b)
            {
                std::array<Biquad, 4> fresh;
                const auto count = bandStages (params, b, sampleRate, fresh);

                // mantém o estado dos filtros que continuam (sem estalo ao mexer num knob)
                for (int s = 0; s < count; ++s)
                {
                    auto& stage = stages[(std::size_t) b][(std::size_t) s];
                    stage.b0 = fresh[(std::size_t) s].b0; stage.b1 = fresh[(std::size_t) s].b1; stage.b2 = fresh[(std::size_t) s].b2;
                    stage.a1 = fresh[(std::size_t) s].a1; stage.a2 = fresh[(std::size_t) s].a2;
                }

                for (int s = count; s < 4; ++s)
                    stages[(std::size_t) b][(std::size_t) s].reset();

                for (int s = 0; s < count; ++s)
                    order[(std::size_t) active++] = &stages[(std::size_t) b][(std::size_t) s];
            }

            output = (float) std::pow (10.0, peq::gainDb (params[peq::outputParam]) / 20.0);
        }

        for (int k = 0; k < active; ++k)
            order[(std::size_t) k]->processBlock (l, r, n);

        if (output != 1.0f)
            for (int i = 0; i < n; ++i)
            {
                l[i] *= output;
                r[i] *= output;
            }
    }

private:
    double sampleRate = 44100.0;
    std::array<std::array<Biquad, 4>, peq::bands> stages;
    std::array<Biquad*, peq::bands * 4> order {};
    int active = 0;
    float output = 1.0f;
    EffectParams lastParams {};
};

// ---------------------------------------------------------------------------
// Utilitário: matriz estéreo (L->L, R->L, L->R, R->R), balanço e ganho.

float utilityGainDb (float v) { return (v - 0.5f) * 48.0f; }   // ±24 dB
float utilityMatrix (float v) { return v * 4.0f - 2.0f; }      // −2..2 (0,75 = 1,0)

// Gate de volume: 32 passos por compasso de 4 tempos (o "volume" do Gross Beat). Ganho por passo,
// mistura com o sinal original e suavização de ataque/release.
namespace gate
{
constexpr int steps = 32;
constexpr int mixParam = 32, attackParam = 33, releaseParam = 34;
inline double attackMs (float v) { return std::pow (501.0, (double) v) - 1.0; }
inline double releaseMs (float v) { return 2.0 * (std::pow (501.0, (double) v) - 1.0); }
} // namespace gate

class VolumeGate final : public Effect
{
public:
    void prepare (double sr) noexcept override { sampleRate = sr; }
    void reset() noexcept override { gain = 1.0f; }
    void setSongPosition (double ticks) noexcept override { tick = ticks; }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        const auto mix = std::clamp (p[gate::mixParam], 0.0f, 1.0f);
        const auto up = (float) (1.0 - std::exp (-1.0 / (std::max (0.05, gate::attackMs (p[gate::attackParam])) * 0.001 * sampleRate)));
        const auto down = (float) (1.0 - std::exp (-1.0 / (std::max (0.05, gate::releaseMs (p[gate::releaseParam])) * 0.001 * sampleRate)));
        const auto ticksPerSample = std::max (1.0, bpm) / 60.0 * ppq / sampleRate;
        const auto bar = 4.0 * ppq;
        auto position = std::fmod (std::max (0.0, tick), bar);

        for (int i = 0; i < n; ++i)
        {
            const auto step = std::clamp ((int) (position / bar * gate::steps), 0, gate::steps - 1);
            const auto target = 1.0f - mix + mix * std::clamp (p[(std::size_t) step], 0.0f, 1.0f);
            gain += (target - gain) * (target > gain ? up : down);
            l[i] *= gain;
            r[i] *= gain;
            position += ticksPerSample;
            if (position >= bar)
                position -= bar;
        }
        tick += ticksPerSample * n;
    }

private:
    double sampleRate = 44100.0, tick = 0.0;
    float gain = 1.0f;
};

// Peak Controller: o som passa direto; um seguidor de pico (ataque imediato, queda pelo "decay")
// vira as saídas de controle: 0 = base + quantidade·pico (com tensão), 1 = base, 2 = igual à 0.
class PeakController final : public Effect
{
public:
    void prepare (double sr) noexcept override { sampleRate = sr; }
    void reset() noexcept override { peak = 0.0f; }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        // decay: velocidade 5..128 do FL; ~0,3 s até −60 dB no padrão [HIP]
        const auto speed = 5.0 + 123.0 * std::clamp ((double) p[3], 0.0, 1.0);
        const auto fall = (float) std::exp (std::log (0.001) / (std::max (0.01, 20.0 / speed) * sampleRate));
        for (int i = 0; i < n; ++i)
        {
            const auto x = std::max (std::abs (l[i]), std::abs (r[i]));
            peak = x > peak ? x : peak * fall;
        }
        const auto tension = (double) (p[2] * 2.0f - 1.0f);
        const auto shaped = std::pow ((double) std::clamp (peak, 0.0f, 1.0f), std::exp2 (-2.0 * tension));
        base = p[0];
        value = std::clamp (base + (p[1] * 2.0f - 1.0f) * (float) shaped, 0.0f, 1.0f);
    }

    float controlOutput (int output) const noexcept override { return output == 1 ? base : value; }

private:
    double sampleRate = 44100.0;
    float peak = 0.0f, value = 0.0f, base = 0.0f;
};

class Utility final : public Effect
{
public:
    void prepare (double) noexcept override {}
    void reset() noexcept override {}

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        const auto gain = std::pow (10.0f, utilityGainDb (p[0]) / 20.0f);
        const auto pan = std::clamp (p[1] * 2.0f - 1.0f, -1.0f, 1.0f);
        const auto gl = gain * (pan > 0.0f ? 1.0f - pan : 1.0f); // balanço: abaixa o lado oposto
        const auto gr = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
        const auto ll = utilityMatrix (p[2]), rl = utilityMatrix (p[3]), lr = utilityMatrix (p[4]), rr = utilityMatrix (p[5]);

        for (int i = 0; i < n; ++i)
        {
            const auto a = l[i], b = r[i];
            l[i] = (ll * a + rl * b) * gl;
            r[i] = (lr * a + rr * b) * gr;
        }
    }
};
} // namespace

namespace peq
{
BandType type (float v) noexcept { return (BandType) std::clamp ((int) std::lround (v * (bandTypes - 1)), 0, bandTypes - 1); }
float typeValue (BandType t) noexcept { return (float) (int) t / (float) (bandTypes - 1); }
double hz (float v) noexcept { return 20.0 * std::pow (1000.0, std::clamp ((double) v, 0.0, 1.0)); }
float hzValue (double f) noexcept { return (float) std::clamp (std::log (std::max (f, 1.0) / 20.0) / std::log (1000.0), 0.0, 1.0); }
double gainDb (float v) noexcept { return (std::clamp ((double) v, 0.0, 1.0) - 0.5) * 36.0; }
float gainValue (double db) noexcept { return (float) std::clamp (db / 36.0 + 0.5, 0.0, 1.0); }
double q (float v) noexcept { return 0.1 * std::pow (180.0, std::clamp ((double) v, 0.0, 1.0)); }
float qValue (double value) noexcept { return (float) std::clamp (std::log (std::max (value, 0.01) / 0.1) / std::log (180.0), 0.0, 1.0); }
int slopeStages (float v) noexcept { return 1 + (int) std::lround (std::clamp (v, 0.0f, 1.0f) * 3.0f); }
float slopeValue (int stages) noexcept { return (float) (std::clamp (stages, 1, 4) - 1) / 3.0f; }

const char* typeName (BandType t) noexcept
{
    static constexpr const char* names[] = { "Desligada", "Pico", "Prateleira grave", "Prateleira aguda", "Corta graves",
                                             "Corta agudos", "Passa-banda", "Rejeita-faixa" };
    return names[(int) t];
}

double responseDb (const EffectParams& params, double sr, double f)
{
    // |H(e^jw)| de cada etapa, somado em dB
    const auto w = 2.0 * pi * std::clamp (f, 1.0, sr * 0.499) / sr;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    auto total = gainDb (params[outputParam]);

    for (int b = 0; b < bands; ++b)
    {
        std::array<Biquad, 4> st;
        const auto count = bandStages (params, b, sr, st);

        for (int s = 0; s < count; ++s)
        {
            const auto& c = st[(std::size_t) s];
            const auto h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
            total += 20.0 * std::log10 (std::max (std::abs (h), 1.0e-9));
        }
    }

    return total;
}
} // namespace peq

const EffectInfo& effectInfo (EffectKind kind)
{
    static const EffectInfo eq { "EQ", { { "Graves", 0.5f }, { "Médios", 0.5f }, { "Freq. médios", 0.5f }, { "Agudos", 0.5f }, { "Saída", 0.5f } } };
    static const EffectInfo comp { "Compressor", { { "Threshold", 0.7f }, { "Ratio", 3.0f / 19.0f }, { "Attack", 0.67f }, { "Release", 0.54f }, { "Makeup", 0.0f } } };
    static const EffectInfo delay { "Delay", { { "Tempo", 0.3f }, { "Feedback", 0.42f }, { "Mix", 0.3f }, { "Ping-pong", 0.0f }, { "Abafar", 0.3f } } };
    static const EffectInfo reverb { "Reverb", { { "Sala", 0.6f }, { "Abafar", 0.5f }, { "Largura", 1.0f }, { "Mix", 0.25f }, { "Seco", 1.0f },
                                                 { "Corta graves", 0.0f }, { "Corta agudos", 1.0f }, { "Predelay", 0.0f } } };
    static const EffectInfo plugin { "Plugin", {} };
    static const EffectInfo limiter { "Limiter", { { "Teto", 0.975f }, { "Ganho", 0.0f }, { "Release", 0.5f } } };
    static const EffectInfo filter { "Filtro", { { "Tipo", 0.0f }, { "Corte", 0.75f }, { "Resson.", 0.2f }, { "Mix", 1.0f } } };
    static const EffectInfo distortion { "Distorção", { { "Drive", 0.4f }, { "Tom", 0.7f }, { "Mix", 1.0f }, { "Saída", 0.5f } } };
    static const EffectInfo chorus { "Chorus", { { "Velocidade", 0.45f }, { "Profund.", 0.4f }, { "Mix", 0.5f }, { "Feedback", 0.0f } } };
    static const EffectInfo utility { "Utilitário", { { "Ganho", 0.5f }, { "Balanço", 0.5f }, { "E -> E", 0.75f }, { "D -> E", 0.5f },
                                                      { "E -> D", 0.5f }, { "D -> D", 0.75f } } };
    static const EffectInfo volumeGate = [] {
        static const char* names[] = { "Passo 1", "Passo 2", "Passo 3", "Passo 4", "Passo 5", "Passo 6", "Passo 7", "Passo 8",
                                       "Passo 9", "Passo 10", "Passo 11", "Passo 12", "Passo 13", "Passo 14", "Passo 15", "Passo 16",
                                       "Passo 17", "Passo 18", "Passo 19", "Passo 20", "Passo 21", "Passo 22", "Passo 23", "Passo 24",
                                       "Passo 25", "Passo 26", "Passo 27", "Passo 28", "Passo 29", "Passo 30", "Passo 31", "Passo 32" };
        EffectInfo info { "Gate de volume", {} };
        for (int i = 0; i < gate::steps; ++i)
            info.params.push_back ({ names[i], (i % 8) < 4 ? 1.0f : 0.0f }); // padrão: meio tempo ligado, meio desligado
        info.params.push_back ({ "Mix", 1.0f });
        info.params.push_back ({ "Ataque", 0.1765f });  // ~2 ms
        info.params.push_back ({ "Release", 0.5f });    // ~43 ms
        return info;
    }();
    static const EffectInfo peakController { "Peak Controller", { { "Base", 0.0f }, { "Quantidade", 0.85f }, { "Tensão", 0.5f }, { "Decaimento", 0.48f } } };
    static const EffectInfo parametric = [] {
        // bandas como num EQ de mixagem: prateleira grave, 5 picos e prateleira aguda, tudo em 0 dB
        constexpr peq::BandType types[] = { peq::BandType::lowShelf, peq::BandType::peak, peq::BandType::peak, peq::BandType::peak,
                                            peq::BandType::peak, peq::BandType::peak, peq::BandType::highShelf };
        constexpr double freqs[] = { 80.0, 200.0, 500.0, 1200.0, 3000.0, 6000.0, 12000.0 };
        static const char* names[peq::bands][4] = {
            { "B1 tipo", "B1 freq.", "B1 ganho", "B1 Q" }, { "B2 tipo", "B2 freq.", "B2 ganho", "B2 Q" },
            { "B3 tipo", "B3 freq.", "B3 ganho", "B3 Q" }, { "B4 tipo", "B4 freq.", "B4 ganho", "B4 Q" },
            { "B5 tipo", "B5 freq.", "B5 ganho", "B5 Q" }, { "B6 tipo", "B6 freq.", "B6 ganho", "B6 Q" },
            { "B7 tipo", "B7 freq.", "B7 ganho", "B7 Q" } };
        static const char* slopes[peq::bands] = { "B1 inclin.", "B2 inclin.", "B3 inclin.", "B4 inclin.", "B5 inclin.", "B6 inclin.", "B7 inclin." };
        EffectInfo info { "EQ paramétrico", {} };

        for (int b = 0; b < peq::bands; ++b)
        {
            info.params.push_back ({ names[b][0], peq::typeValue (types[b]) });
            info.params.push_back ({ names[b][1], peq::hzValue (freqs[b]) });
            info.params.push_back ({ names[b][2], 0.5f });
            // Q 1,0 nas prateleiras = inclinação S 1; nos cortes, 0,707 = Butterworth
            info.params.push_back ({ names[b][3], peq::qValue (types[b] == peq::BandType::lowCut || types[b] == peq::BandType::highCut ? 0.70710678 : 1.0) });
        }

        info.params.push_back ({ "Saída", 0.5f });

        for (int b = 0; b < peq::bands; ++b)
            info.params.push_back ({ slopes[b], 0.0f });

        return info;
    }();

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
        case EffectKind::parametricEq: return parametric;
        case EffectKind::utility: return utility;
        case EffectKind::volumeGate: return volumeGate;
        case EffectKind::peakController: return peakController;
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
            if (param == 5)
                return v <= 0.001f ? std::string ("desligado") : (std::snprintf (text, sizeof (text), "%.0f Hz", reverbLowCutHz (v)), std::string (text));
            if (param == 6)
                return v >= 0.999f ? std::string ("desligado") : (std::snprintf (text, sizeof (text), "%.1f kHz", reverbHighCutHz (v) / 1000.0f), std::string (text));
            if (param == 7)
                std::snprintf (text, sizeof (text), "%.0f ms", v * 1000.0f);
            else
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

        case EffectKind::parametricEq:
        {
            if (param == peq::outputParam)
                std::snprintf (text, sizeof (text), "%+.1f dB", peq::gainDb (v));
            else if (param > peq::outputParam)
                std::snprintf (text, sizeof (text), "%d dB/oit", 12 * peq::slopeStages (v));
            else
                switch (param % 4)
                {
                    case 0: return peq::typeName (peq::type (v));
                    case 1:
                    {
                        const auto f = peq::hz (v);
                        if (f >= 1000.0)
                            std::snprintf (text, sizeof (text), "%.2f kHz", f / 1000.0);
                        else
                            std::snprintf (text, sizeof (text), "%.0f Hz", f);
                        break;
                    }
                    case 2: std::snprintf (text, sizeof (text), "%+.1f dB", peq::gainDb (v)); break;
                    default: std::snprintf (text, sizeof (text), "Q %.2f", peq::q (v)); break;
                }
            return text;
        }

        case EffectKind::utility:
            if (param == 0)
                std::snprintf (text, sizeof (text), "%+.1f dB", utilityGainDb (v));
            else if (param == 1)
            {
                const auto pan = v * 2.0f - 1.0f;
                if (std::abs (pan) < 0.005f)
                    return "centro";
                std::snprintf (text, sizeof (text), "%.0f%% %s", std::abs (pan) * 100.0f, pan < 0.0f ? "E" : "D");
            }
            else
                std::snprintf (text, sizeof (text), "%.2f", utilityMatrix (v));
            return text;

        case EffectKind::volumeGate:
            if (param < gate::steps || param == gate::mixParam)
                std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f);
            else
                std::snprintf (text, sizeof (text), "%.1f ms", param == gate::attackParam ? gate::attackMs (v) : gate::releaseMs (v));
            return text;

        case EffectKind::peakController:
            if (param == 1)
                std::snprintf (text, sizeof (text), "%+.0f%%", (v * 2.0f - 1.0f) * 100.0f);
            else if (param == 3)
                std::snprintf (text, sizeof (text), "%.2f s", 20.0 / (5.0 + 123.0 * v));
            else
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
        case EffectKind::limiter: return std::make_unique<Limiter>();
        case EffectKind::filter: return std::make_unique<Filter>();
        case EffectKind::distortion: return std::make_unique<Distortion>();
        case EffectKind::chorus: return std::make_unique<Chorus>();
        case EffectKind::parametricEq: return std::make_unique<ParametricEq>();
        case EffectKind::utility: return std::make_unique<Utility>();
        case EffectKind::volumeGate: return std::make_unique<VolumeGate>();
        case EffectKind::peakController: return std::make_unique<PeakController>();
        case EffectKind::plugin: break; // criado pelo app (RackState::Factories)
    }
    return std::make_unique<Eq>();
}
} // namespace lynx
