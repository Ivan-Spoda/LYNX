#include "core/Effects.h"

#include "core/Effector.h"

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
        const auto sidechain = useSidechain;
        useSidechain = false;

        for (int i = 0; i < n; ++i)
        {
            // detector de pico estéreo ligado, curva "hard knee"; abaixo do threshold não precisa do log.
            // Com sidechain, o detector ouve a outra trilha (o ducking do bumbo)
            const auto peak = sidechain ? (sideL != nullptr ? std::max (std::abs (sideL[i]), std::abs (sideR[i])) : 0.0f)
                                        : std::max (std::abs (l[i]), std::abs (r[i]));
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

    void setSidechain (const float* left, const float* right) noexcept override
    {
        sideL = left;
        sideR = right;
        useSidechain = true;
    }

private:
    double sampleRate = 44100.0;
    float envelopeDb = 0.0f;
    const float* sideL = nullptr;
    const float* sideR = nullptr;
    bool useSidechain = false;
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

// Pitch shift em tempo real: duas cabeças de leitura numa linha de atraso cujo atraso varia em dente
// de serra (a "fita giratória"); quando uma cabeça dá a volta, a outra está no meio da janela e o
// crossfade em seno/cosseno esconde o salto. Parâmetros: semitons (±24), cents (±100), mix, janela.
namespace pitch
{
inline double semitones (float v) { return std::round ((std::clamp ((double) v, 0.0, 1.0) - 0.5) * 48.0); }
inline double cents (float v) { return (std::clamp ((double) v, 0.0, 1.0) - 0.5) * 200.0; }
inline double windowMs (float v) { return 20.0 + 100.0 * std::clamp ((double) v, 0.0, 1.0); }
} // namespace pitch

class PitchShift final : public Effect
{
public:
    static constexpr double maxSeconds = 0.13;

    PitchShift()
    {
        const auto size = (std::size_t) (maxSeconds * maxSampleRate) + 8;
        buffer[0].assign (size, 0.0f);
        buffer[1].assign (size, 0.0f);
    }

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        length = (int) std::min<std::size_t> (buffer[0].size(), (std::size_t) (maxSeconds * sr) + 8);
        reset();
    }

    void reset() noexcept override
    {
        std::fill (buffer[0].begin(), buffer[0].end(), 0.0f);
        std::fill (buffer[1].begin(), buffer[1].end(), 0.0f);
        write = 0;
        phase = 0.0;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        const auto ratio = std::pow (2.0, (pitch::semitones (p[0]) + pitch::cents (p[1]) / 100.0) / 12.0);
        const auto mix = std::clamp (p[2], 0.0f, 1.0f);
        const auto window = std::min (pitch::windowMs (p[3]) * 0.001 * sampleRate, (double) length - 4.0);
        const auto step = (1.0 - ratio) / window; // o atraso cresce (desce o tom) ou encolhe (sobe)

        for (int i = 0; i < n; ++i)
        {
            float* io[2] = { l + i, r + i };
            buffer[0][(std::size_t) write] = *io[0];
            buffer[1][(std::size_t) write] = *io[1];

            const auto phaseB = phase + 0.5 >= 1.0 ? phase - 0.5 : phase + 0.5;
            const auto gainA = (float) std::sin (3.141592653589793 * phase);
            const auto gainB = (float) std::sin (3.141592653589793 * phaseB);

            for (int c = 0; c < 2; ++c)
            {
                const auto shifted = gainA * read (c, 1.0 + phase * window) + gainB * read (c, 1.0 + phaseB * window);
                *io[c] += (shifted - *io[c]) * mix;
            }

            phase += step;
            phase -= std::floor (phase);
            if (++write >= length)
                write = 0;
        }
    }

private:
    float read (int c, double delay) const noexcept
    {
        auto position = (double) write - delay;
        while (position < 0.0)
            position += length;
        const auto i0 = (int) position;
        const auto frac = (float) (position - i0);
        const auto i1 = i0 + 1 >= length ? 0 : i0 + 1;
        const auto& b = buffer[(std::size_t) c];
        return b[(std::size_t) i0] + (b[(std::size_t) i1] - b[(std::size_t) i0]) * frac;
    }

    std::array<std::vector<float>, 2> buffer;
    double sampleRate = 44100.0, phase = 0.0;
    int length = 1, write = 0;
};

// Frequency shifter: o sinal analítico (par em quadratura de duas cadeias de allpass de 2ª ordem
// em z², um "transformador de Hilbert" de ~90° na faixa útil) multiplicado por um oscilador complexo.
// Parâmetros: deslocamento (±2 kHz, curva quadrática em volta do centro), mix, abertura estéreo
// (o lado direito desloca ao contrário).
namespace fshift
{
inline double hz (float v)
{
    const auto x = (std::clamp ((double) v, 0.0, 1.0) - 0.5) * 2.0;
    return 2000.0 * x * std::abs (x);
}
} // namespace fshift

// Lo-fi: segura cada amostra por 128·t³ + 1 amostras e quantiza em 65536/(2048·b² + 1) níveis
// (as leis do Lo-fi do Effector do FL, REA_rodada18 §3.2); mistura dry/wet linear.
namespace lofi
{
int holdSamples (float v) { return (int) (128.0f * v * v * v) + 1; }
double levels (float v) { return 65536.0 / (2048.0 * v * v + 1.0); }
} // namespace lofi

// Crossover (banda do Frequency Splitter do FL, REA_rodada17 §1.2): passa-altas e/ou passa-baixas com a
// estrutura de cada inclinação — 6 dB: 1ª ordem; 12: 1 biquad Q 0,5 (HP invertido); 18: Butterworth 3
// (no FL é uma estrutura com passa-tudo não detalhada: aproximação); 24: 2× Q 0,707; 36: Q 0,5412/1,3066/
// 0,7070 (HP invertido); 48: Butterworth 4 ao quadrado; 72: Butterworth 6 ao quadrado; 96: Butterworth 8 ao
// quadrado. Parâmetros: 0 liga o HP, 1 corte do HP, 2 inclinação do HP, 3 liga o LP, 4 corte do LP, 5 inclinação.
namespace xover
{
double hz (float v) { return 30.0 * std::pow (2.0, 9.0589 * std::clamp (v, 0.0f, 1.0f)); }
float hzValue (double hz) { return (float) std::clamp (std::log2 (hz / 30.0) / 9.0589, 0.0, 1.0); }
int combo (float v) { return std::clamp ((int) std::lround (v * 7.0f), 0, 7); }
constexpr int dbPerOctave[8] { 6, 12, 18, 24, 36, 48, 72, 96 };

struct Design
{
    std::vector<double> qs; // biquads de 2ª ordem
    bool firstOrder = false;
    bool invertHighPass = false;
};

Design design (int c)
{
    switch (c)
    {
        case 0: return { {}, true, false };
        case 1: return { { 0.5 }, false, true };
        case 2: return { { 1.0 }, true, false };
        case 3: return { { 0.7071, 0.7071 }, false, false };
        case 4: return { { 0.5412, 1.3066, 0.7070 }, false, true };
        case 5: return { { 0.5412, 1.3066, 0.5412, 1.3066 }, false, false };
        case 6: return { { 0.5176, 0.7071, 1.9319, 0.5176, 0.7071, 1.9319 }, false, false };
        default: return { { 0.5098, 0.6013, 0.9000, 2.5629, 0.5098, 0.6013, 0.9000, 2.5629 }, false, false };
    }
}
} // namespace xover

class Crossover final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        last.fill (-1.0f);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& side : sides)
        {
            for (auto& b : side.biquads)
                b.reset();
            side.firstState[0] = side.firstState[1] = 0.0;
            side.firstIn[0] = side.firstIn[1] = 0.0;
        }
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        if (p[0] != last[0] || p[1] != last[1] || p[2] != last[2] || p[3] != last[3] || p[4] != last[4] || p[5] != last[5])
        {
            for (int i = 0; i < 6; ++i)
                last[(std::size_t) i] = p[(std::size_t) i];
            configure (sides[0], p[0] > 0.5f, true, xover::hz (p[1]), xover::combo (p[2]));
            configure (sides[1], p[3] > 0.5f, false, xover::hz (p[4]), xover::combo (p[5]));
        }

        for (auto& side : sides)
            if (side.active)
                for (int i = 0; i < n; ++i)
                {
                    l[i] = (float) side.process (l[i], 0);
                    r[i] = (float) side.process (r[i], 1);
                }
    }

private:
    struct Side
    {
        bool active = false, highPass = false, firstOrder = false;
        double sign = 1.0;
        std::array<Biquad, 8> biquads;
        int count = 0;
        double a = 0.0; // 1ª ordem (transformação bilinear)
        double firstState[2] {}, firstIn[2] {};

        double process (double x, int ch) noexcept
        {
            if (firstOrder)
            {
                // passa-baixas de 1ª ordem: y = a·(x + x[n−1]) + (1 − 2a)·y[n−1]; o passa-altas é o complemento
                const auto lp = a * (x + firstIn[ch]) + (1.0 - 2.0 * a) * firstState[ch];
                firstIn[ch] = x;
                firstState[ch] = lp;
                x = highPass ? x - lp : lp;
            }
            for (int i = 0; i < count; ++i)
                x = biquads[(std::size_t) i].process (x, ch);
            return sign * x;
        }
    };

    void configure (Side& side, bool on, bool highPass, double hz, int combo)
    {
        side.active = on;
        if (! on)
            return;
        const auto d = xover::design (combo);
        side.highPass = highPass;
        side.firstOrder = d.firstOrder;
        side.sign = highPass && d.invertHighPass ? -1.0 : 1.0;
        side.count = (int) d.qs.size();
        const auto f = std::clamp (hz, 10.0, 0.49 * sampleRate);
        const auto k = std::tan (3.141592653589793 * f / sampleRate);
        side.a = k / (1.0 + k);
        for (int i = 0; i < side.count; ++i)
            side.biquads[(std::size_t) i].filter (sampleRate, highPass ? 1 : 0, f, d.qs[(std::size_t) i]);
    }

    double sampleRate = 44100.0;
    std::array<Side, 2> sides {};
    std::array<float, 6> last {};
};

// Envelope rítmico (REA_rodada20 §1.2/§1.3): período em tempos, até 12 pontos (posição 0..1 no período, nível,
// tensão); o valor entre dois pontos segue a curva do ponto de chegada. Parâmetros: 0 período/64 tempos,
// 1 nº de pontos/12, depois 3 por ponto (posição, nível, (tensão + 1)/2).
namespace rhythm
{
constexpr int maxPoints = 12;
double curve (double x, double t) noexcept
{
    if (std::abs (t) < 1e-6)
        return x;
    const auto q = std::pow (1000001.0, std::abs (t)) - 1.0;
    return t > 0.0 ? 1.0 - (std::pow (1.0 + q, 1.0 - x) - 1.0) / q : (std::pow (1.0 + q, x) - 1.0) / q;
}

double valueAt (const EffectParams& p, double u) noexcept
{
    const auto count = std::clamp ((int) std::lround (p[1] * maxPoints), 0, maxPoints);
    if (count == 0)
        return 1.0;
    auto prevX = 0.0, prevY = (double) p[3];
    for (int k = 0; k < count; ++k)
    {
        const auto x = (double) p[(std::size_t) (2 + 3 * k)], y = (double) p[(std::size_t) (3 + 3 * k)];
        if (u < x)
        {
            const auto f = x > prevX ? (u - prevX) / (x - prevX) : 1.0;
            return prevY + (y - prevY) * curve (f, 2.0 * p[(std::size_t) (4 + 3 * k)] - 1.0);
        }
        prevX = x;
        prevY = y;
    }
    return prevY;
}
} // namespace rhythm

class RhythmGain final : public Effect
{
public:
    void prepare (double sr) noexcept override { sampleRate = sr; }
    void reset() noexcept override {}
    void setSongPosition (double ticks) noexcept override { beats = ticks / 960.0; }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        const auto period = std::max (1e-3, (double) p[0] * 64.0);
        const auto step = std::max (bpm, 1.0) / 60.0 / sampleRate;
        if (p[38] > 0.5f) // só controle: o valor vira fonte de automação ao vivo, o áudio passa intacto
        {
            const auto t = beats + step * n;
            current = (float) rhythm::valueAt (p, (t - std::floor (t / period) * period) / period);
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const auto t = beats + step * i;
            const auto g = (float) rhythm::valueAt (p, (t - std::floor (t / period) * period) / period);
            l[i] *= g;
            r[i] *= g;
            current = g;
        }
    }

    float controlOutput (int) const noexcept override { return current; }

private:
    double sampleRate = 44100.0, beats = 0.0;
    float current = 1.0f;
};

// Auto-pan (REA_rodada20 §1.5): triângulo (+1 em φ = 0, −1 em 0,5) com fase 0,75 + deslocamento + tempos/período.
// Parâmetros: 0 período/64 tempos, 1 profundidade ((a + 1)/2, a = deslocamento máximo do pan), 2 deslocamento de
// fase (ciclos), 3 pan fixo ((p + 1)/2). Lei do pan: o balanço do utilitário (a do FL não foi decodificada).
class AutoPan final : public Effect
{
public:
    void prepare (double sr) noexcept override { sampleRate = sr; }
    void reset() noexcept override {}
    void setSongPosition (double ticks) noexcept override { beats = ticks / 960.0; }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        const auto period = std::max (1e-3, (double) p[0] * 64.0);
        const auto depth = 2.0 * p[1] - 1.0, fixed = 2.0 * p[3] - 1.0;
        const auto step = std::max (bpm, 1.0) / 60.0 / sampleRate / period;
        auto phase = 0.75 + p[2] + beats / period;
        {
            const auto f = phase + step * n - std::floor (phase + step * n);
            current = (float) ((f < 0.5 ? 1.0 - 4.0 * f : -3.0 + 4.0 * f) + 1.0) * 0.5f; // onda em 0..1
        }
        if (p[4] > 0.5f) // só controle (fonte de automação ao vivo)
            return;
        for (int i = 0; i < n; ++i, phase += step)
        {
            const auto f = phase - std::floor (phase);
            const auto w = f < 0.5 ? 1.0 - 4.0 * f : -3.0 + 4.0 * f;
            const auto pan = std::clamp (fixed + depth * w, -1.0, 1.0);
            l[i] *= (float) (pan > 0.0 ? 1.0 - pan : 1.0);
            r[i] *= (float) (pan < 0.0 ? 1.0 + pan : 1.0);
        }
    }

    float controlOutput (int) const noexcept override { return current; }

private:
    double sampleRate = 44100.0, beats = 0.0;
    float current = 0.5f;
};

// Vocoder (Fruity Vocoder, REA_rodada20 §4.1). Parâmetros (0..1): 0 bandas (4 << round(5v)), 1 ordem
// (1 + round(2v)), 2 listen (0 modulador, 1 portadora, 2 mistura), 3 troca L/R, 4/5 FreqMin/Max (v·65536),
// 6 FreqScale (v·128), 7 inverter, 8 Formant ((v·2400 − 1200) centésimos), 9 largura (5 + 195v %),
// 10/11 ataque/release (10 + 990v ms), 12 hold, 13/14/15 níveis do modulador/portadora/wet (v·160).
namespace vocoder
{
int bands (float v) { return 4 << std::clamp ((int) std::lround (v * 5.0f), 0, 5); }
int order (float v) { return 1 + std::clamp ((int) std::lround (v * 2.0f), 0, 2); }
double freq (float v) { return 10.0 + 159.9 * (std::pow (101.0, std::clamp (v, 0.0f, 1.0f)) - 1.0); }
double curve11 (double x) { return (std::pow (11.0, x) - 1.0) / 10.0; }
double ms (float v) { return 10.0 + 990.0 * std::clamp (v, 0.0f, 1.0f); }
} // namespace vocoder

class Vocoder final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        last.fill (-1.0f);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& b : bands)
        {
            for (auto& f : b.analysis)
                f.reset();
            for (auto& f : b.synthesis)
                f.reset();
            b.env = 0.0;
        }
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        bool changed = false;
        for (std::size_t i = 0; i < 16; ++i)
            if (p[i] != last[i])
            {
                last[i] = p[i];
                changed = true;
            }
        if (changed)
            configure (p);

        const auto listen = std::clamp ((int) std::lround (p[2] * 2.0f), 0, 2);
        const auto swap = p[3] > 0.5f, hold = p[12] > 0.5f;
        const auto gMod = listen == 0 ? 1.0 : listen == 1 ? 0.0 : vocoder::curve11 (p[13] * 160.0 / 128.0);
        const auto gCar = listen == 1 ? 1.0 : listen == 0 ? 0.0 : vocoder::curve11 (p[14] * 160.0 / 128.0);
        const auto wet = listen == 2 ? vocoder::curve11 (p[15] * 160.0 / 128.0) : 0.0;
        const auto gain = wet * 1500.0 / bandwidthPercent;

        for (int i = 0; i < n; ++i)
        {
            const double mod = swap ? r[i] : l[i], car = swap ? l[i] : r[i];
            double sum = 0.0;
            if (gain > 0.0)
                for (int k = 0; k < activeBands; ++k)
                {
                    auto& b = bands[(std::size_t) k];
                    if (! b.active)
                        continue;
                    if (! hold)
                    {
                        auto y = mod;
                        for (int o = 0; o < filterOrder; ++o)
                            y = b.analysis[(std::size_t) o].process ((float) y, 0);
                        const auto x = std::abs (y) * gain;
                        b.env = x + (b.env - x) * (x > b.env ? attackCoeff : releaseCoeff);
                        if (b.env < 1e-12)
                            b.env = 0.0;
                    }
                    auto c = car;
                    for (int o = 0; o < filterOrder; ++o)
                        c = b.synthesis[(std::size_t) o].process ((float) c, 0);
                    sum += c * b.env;
                }
            const auto out = (float) (sum + mod * gMod + car * gCar);
            l[i] = r[i] = out; // saída mono nos dois lados
        }
    }

private:
    struct Band
    {
        std::array<Biquad, 3> analysis, synthesis;
        double env = 0.0;
        bool active = false;
    };

    static void bandPass (Biquad& f, double hz, double octaves, double sr)
    {
        const auto w = 2.0 * 3.141592653589793 * hz / sr;
        const auto sw = std::sin (w);
        const auto alpha = sw * std::sinh (std::log (2.0) / 2.0 * octaves * w / sw);
        f.set (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * std::cos (w), 1.0 - alpha);
    }

    void configure (const EffectParams& p)
    {
        activeBands = vocoder::bands (p[0]);
        filterOrder = vocoder::order (p[1]);
        auto fmin = vocoder::freq (p[4]), fmax = vocoder::freq (p[5]);
        if (fmin >= fmax)
            fmin = fmax - 1.0;
        const auto scale = (double) ((int) (std::pow (10000.0, std::clamp (p[6], 0.0f, 1.0f)) - 1.0) + 1);
        const auto invert = p[7] > 0.5f;
        const auto formant = p[8] * 2400.0 - 1200.0;
        bandwidthPercent = 5.0 + 195.0 * std::clamp (p[9], 0.0f, 1.0f);
        attackCoeff = std::pow (0.01, 1.0 / (vocoder::ms (p[10]) * 0.001 * sampleRate));
        releaseCoeff = std::pow (0.01, 1.0 / (vocoder::ms (p[11]) * 0.001 * sampleRate));

        std::array<double, 129> edges {};
        for (int i = 0; i <= activeBands; ++i)
        {
            const auto f = fmin + (fmax - fmin) * (std::pow (scale + 1.0, (double) i / activeBands) - 1.0) / scale;
            edges[(std::size_t) i] = 1200.0 * std::log2 (std::max (1.0, f) / 440.0) + 5700.0;
        }
        auto hzOf = [] (double cents) { return 440.0 * std::pow (2.0, (cents - 5700.0) / 1200.0); };
        const auto top = 20000.0 * sampleRate / 44100.0;
        for (int k = 0; k < activeBands; ++k)
        {
            auto& b = bands[(std::size_t) k];
            const auto lo = edges[(std::size_t) k], hi = edges[(std::size_t) k + 1];
            const auto s = invert ? activeBands - 1 - k : k; // banco da portadora (espelhado com Invert)
            const auto slo = edges[(std::size_t) s] + formant, shi = edges[(std::size_t) s + 1] + formant;
            const auto fa = hzOf ((lo + hi) / 2.0), fs = hzOf ((slo + shi) / 2.0);
            b.active = fa >= 10.0 && fa < top && fs >= 10.0 && fs < top && fa < 0.49 * sampleRate && fs < 0.49 * sampleRate;
            if (! b.active)
                continue;
            const auto octavesA = bandwidthPercent / 100.0 * filterOrder * (hi - lo) / 1200.0;
            const auto octavesS = bandwidthPercent / 100.0 * filterOrder * (shi - slo) / 1200.0;
            for (int o = 0; o < filterOrder; ++o)
            {
                bandPass (b.analysis[(std::size_t) o], fa, octavesA, sampleRate);
                bandPass (b.synthesis[(std::size_t) o], fs, octavesS, sampleRate);
            }
        }
    }

    double sampleRate = 44100.0;
    std::array<float, 16> last {};
    std::array<Band, 128> bands {};
    int activeBands = 16, filterOrder = 2;
    double bandwidthPercent = 50.0, attackCoeff = 0.99, releaseCoeff = 0.999;
};

// Correção de afinação (Pitcher do FL; leis em REA_rodada15 §1 e rodada19 §6). Parâmetros: 0 Correction Speed,
// 1 Fine Tune, 2 Bypass All, 3 Note A Freq, 4 faixa (índice/5: f mín. 220/170/110/80/55/25 Hz), 5…16 máscara de
// notas C…B (1 = permitida), 17…28 Key Bypass C…B. O detector (YIN) e o deslocador (duas cabeças) são do LYNX:
// os do FL não foram decodificados.
namespace pcorr
{
constexpr double minHz[6] { 220.0, 170.0, 110.0, 80.0, 55.0, 25.0 };
int range (float v) { return std::clamp ((int) std::lround (v * 5.0f), 0, 5); }
double noteA (float v) { return 55.0 * std::pow (2.0, (2.0 * v - 1.0) / 12.0); } // ×8 = Lá 440 em v = 0,5
} // namespace pcorr

class PitchCorrect final : public Effect
{
public:
    static constexpr int decimation = 4, history = 4096, hop = 256;
    static constexpr double windowSeconds = 0.04;

    PitchCorrect()
    {
        ring.assign ((std::size_t) history, 0.0f);
        diff.assign ((std::size_t) history, 0.0f);
        const auto size = (std::size_t) (0.13 * maxSampleRate) + 8;
        buffer[0].assign (size, 0.0f);
        buffer[1].assign (size, 0.0f);
    }

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        length = (int) std::min<std::size_t> (buffer[0].size(), (std::size_t) (0.13 * sr) + 8);
        reset();
    }

    void reset() noexcept override
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (buffer[0].begin(), buffer[0].end(), 0.0f);
        std::fill (buffer[1].begin(), buffer[1].end(), 0.0f);
        ringWrite = 0;
        decimCount = 0;
        decimSum = 0.0f;
        hopCount = 0;
        detectedHz = 0.0;
        correction = 0.0;
        write = 0;
        phase = 0.0;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        const auto u = 1.0 - (double) std::clamp (p[0], 0.0f, 1.0f);
        const auto coeff = u < 0.01 ? 1.0 : std::min (1.0, 8.82 / (sampleRate * (3.0 * u * u * u + 0.001)));
        const auto fine = 2.0 * p[1] - 1.0;
        const auto window = std::min (windowSeconds * sampleRate, (double) length - 4.0);

        for (int i = 0; i < n; ++i)
        {
            // detector: mono decimado por 4, YIN a cada `hop` amostras
            decimSum += 0.5f * (l[i] + r[i]);
            if (++decimCount == decimation)
            {
                ring[(std::size_t) ringWrite] = decimSum / decimation;
                ringWrite = (ringWrite + 1) % history;
                decimCount = 0;
                decimSum = 0.0f;
            }
            if (++hopCount >= hop)
            {
                hopCount = 0;
                detectedHz = detect (pcorr::minHz[pcorr::range (p[4])]);
                target = targetFor (p);
            }

            // correção suavizada pelo polo da Correction Speed; sem altura detectada volta a zero
            correction += coeff * ((detectedHz > 0.0 ? target : 0.0) - correction);
            const auto ratio = std::pow (2.0, (correction + fine) / 12.0);
            const auto step = (1.0 - ratio) / window;

            float* io[2] = { l + i, r + i };
            buffer[0][(std::size_t) write] = *io[0];
            buffer[1][(std::size_t) write] = *io[1];
            const auto phaseB = phase + 0.5 >= 1.0 ? phase - 0.5 : phase + 0.5;
            const auto gainA = (float) std::sin (3.141592653589793 * phase);
            const auto gainB = (float) std::sin (3.141592653589793 * phaseB);
            for (int c = 0; c < 2; ++c)
                *io[c] = gainA * read (c, 1.0 + phase * window) + gainB * read (c, 1.0 + phaseB * window);
            phase += step;
            phase -= std::floor (phase);
            if (++write >= length)
                write = 0;
        }
    }

private:
    // YIN (de Cheveigné & Kawahara) no sinal decimado: período com a diferença normalizada abaixo de 0,15
    double detect (double fmin) noexcept
    {
        const auto fs = sampleRate / decimation;
        const auto maxLag = std::min (history / 2 - 2, (int) std::ceil (fs / fmin));
        const auto minLag = std::max (2, (int) (fs / 1200.0));
        const auto w = std::min (history - maxLag - 1, maxLag * 2);
        if (maxLag <= minLag || w <= 0)
            return 0.0;
        auto at = [this] (int back) { return ring[(std::size_t) ((ringWrite - 1 - back + 2 * history) % history)]; };
        double energy = 0.0;
        for (int j = 0; j < w; ++j)
            energy += (double) at (j) * at (j);
        if (energy / w < 1e-6)
            return 0.0; // silêncio
        double running = 0.0;
        diff[0] = 1.0f;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < w; ++j)
            {
                const auto x = (double) at (j) - at (j + tau);
                d += x * x;
            }
            running += d;
            diff[(std::size_t) tau] = (float) (running > 0.0 ? d * tau / running : 1.0);
        }
        for (int tau = minLag; tau < maxLag; ++tau)
            if (diff[(std::size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxLag && diff[(std::size_t) tau + 1] < diff[(std::size_t) tau])
                    ++tau;
                // interpolação parabólica do mínimo
                const double a = diff[(std::size_t) tau - 1], b = diff[(std::size_t) tau], c = diff[(std::size_t) tau + 1];
                const auto den = a + c - 2.0 * b;
                const auto t = tau + (std::abs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
                return fs / t;
            }
        return 0.0;
    }

    // nota permitida mais próxima: n = 9 + 12·log2(f/A); Key Bypass na nota escolhida = sem correção
    double targetFor (const EffectParams& p) const noexcept
    {
        if (detectedHz <= 0.0 || p[2] > 0.5f)
            return 0.0;
        const auto noteNumber = 9.0 + 12.0 * std::log2 (detectedHz / pcorr::noteA (p[3]));
        auto best = 0.0, bestDistance = 1e9;
        for (int k = (int) std::floor (noteNumber) - 12; k <= (int) std::ceil (noteNumber) + 12; ++k)
            if (p[(std::size_t) (5 + ((k % 12) + 12) % 12)] > 0.5f && std::abs (k - noteNumber) < bestDistance)
            {
                bestDistance = std::abs (k - noteNumber);
                best = k;
            }
        if (bestDistance > 100.0 || p[(std::size_t) (17 + (((int) best % 12) + 12) % 12)] > 0.5f)
            return 0.0;
        return best - noteNumber;
    }

    float read (int c, double delay) const noexcept
    {
        auto position = (double) write - delay;
        while (position < 0.0)
            position += length;
        const auto i0 = (int) position;
        const auto frac = (float) (position - i0);
        const auto i1 = i0 + 1 >= length ? 0 : i0 + 1;
        const auto& b = buffer[(std::size_t) c];
        return b[(std::size_t) i0] + (b[(std::size_t) i1] - b[(std::size_t) i0]) * frac;
    }

    double sampleRate = 44100.0;
    std::vector<float> ring, diff;
    int ringWrite = 0, decimCount = 0, hopCount = 0;
    float decimSum = 0.0f;
    double detectedHz = 0.0, target = 0.0, correction = 0.0;
    std::array<std::vector<float>, 2> buffer;
    int length = 1, write = 0;
    double phase = 0.0;
};

class LoFi final : public Effect
{
public:
    void prepare (double) noexcept override { reset(); }
    void reset() noexcept override
    {
        held[0] = held[1] = 0.0f;
        countdown = 0;
    }

    void process (float* l, float* r, int n, const EffectParams& params, double) noexcept override
    {
        const auto hold = lofi::holdSamples (params[0]);
        const auto step = (float) (2.0 / lofi::levels (params[1]));
        const auto mix = params[2];
        auto quantize = [step] (float x) { return step * std::floor (x / step + 0.5f); };

        for (int i = 0; i < n; ++i)
        {
            if (--countdown <= 0)
            {
                countdown = hold;
                held[0] = quantize (l[i]);
                held[1] = quantize (r[i]);
            }
            l[i] += mix * (held[0] - l[i]);
            r[i] += mix * (held[1] - r[i]);
        }
    }

private:
    float held[2] {};
    int countdown = 0;
};

class FreqShift final : public Effect
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& c : chains)
            c = {};
        phase = 0.0;
    }

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        const auto shift = fshift::hz (p[0]);
        const auto mix = std::clamp (p[1], 0.0f, 1.0f);
        const auto spread = std::clamp (p[2], 0.0f, 1.0f);
        const auto step = shift / sampleRate;

        for (int i = 0; i < n; ++i)
        {
            const auto angle = 2.0 * 3.141592653589793 * phase;
            const auto cosA = (float) std::cos (angle), sinA = (float) std::sin (angle);
            float* io[2] = { l + i, r + i };

            for (int c = 0; c < 2; ++c)
            {
                float re = 0.0f, im = 0.0f;
                chains[(std::size_t) c].analytic (*io[c], re, im);
                // lado direito: entre o mesmo sentido (0) e o oposto (1)
                const auto sign = c == 0 ? 1.0f : 1.0f - 2.0f * spread;
                const auto shifted = re * cosA + sign * im * sinA;
                *io[c] += (shifted - *io[c]) * mix;
            }

            phase += step;
            phase -= std::floor (phase);
        }
    }

private:
    // 4 allpass de 2ª ordem em z² por ramo; o ramo real tem 1 amostra de atraso a mais
    struct Chain
    {
        static constexpr float a[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static constexpr float b[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        std::array<float, 4> ax1 {}, ax2 {}, ay1 {}, ay2 {}, bx1 {}, bx2 {}, by1 {}, by2 {};
        float delayed = 0.0f;

        static float stage (float k, float x, float& x1, float& x2, float& y1, float& y2) noexcept
        {
            const auto y = k * k * (x + y2) - x2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }

        void analytic (float x, float& re, float& im) noexcept
        {
            auto u = x, v = x;
            for (std::size_t k = 0; k < 4; ++k)
            {
                u = stage (a[k], u, ax1[k], ax2[k], ay1[k], ay2[k]);
                v = stage (b[k], v, bx1[k], bx2[k], by1[k], by2[k]);
            }
            re = delayed;
            im = v;
            delayed = u;
        }
    };

    std::array<Chain, 2> chains {};
    double sampleRate = 44100.0, phase = 0.0;
};

// Contêiner de ramos paralelos (Patcher do FL): cada ramo processa uma cópia da entrada pela sua
// cadeia de efeitos internos e a saída é a soma dos ramos com os ganhos.
class ParallelEffect final : public Effect
{
public:
    static constexpr int capacity = 4096; // amostras por pedaço (blocos maiores são divididos)

    explicit ParallelEffect (const EffectSlot& slot)
    {
        for (const auto& b : slot.branches)
        {
            Branch branch;
            branch.gain = b.gain;
            for (const auto& child : b.chain)
                if (child.kind == EffectKind::parallel) // contêineres aninhados (grafos série-paralelo do Patcher)
                    branch.chain.push_back ({ makeParallelEffect (child), child });
                else if (child.kind != EffectKind::plugin)
                    branch.chain.push_back ({ makeEffect (child.kind), child });
            branches.push_back (std::move (branch));
        }
        for (auto& buffer : scratch)
            buffer.assign ((std::size_t) capacity, 0.0f);
    }

    void prepare (double sr) noexcept override
    {
        for (auto& b : branches)
            for (auto& c : b.chain)
                c.effect->prepare (sr);
    }

    void reset() noexcept override
    {
        for (auto& b : branches)
            for (auto& c : b.chain)
                c.effect->reset();
    }

    void setSongPosition (double ticks) noexcept override
    {
        for (auto& b : branches)
            for (auto& c : b.chain)
                c.effect->setSongPosition (ticks);
    }

    void process (float* l, float* r, int n, const EffectParams&, double bpm) noexcept override
    {
        for (int done = 0; done < n; done += capacity)
            processPiece (l + done, r + done, std::min (capacity, n - done), bpm);
    }

private:
    struct Child
    {
        std::unique_ptr<Effect> effect;
        EffectSlot slot;
    };
    struct Branch
    {
        std::vector<Child> chain;
        float gain = 1.0f;
    };

    void processPiece (float* l, float* r, int n, double bpm) noexcept
    {
        auto* inL = scratch[0].data();
        auto* inR = scratch[1].data();
        auto* sumL = scratch[2].data();
        auto* sumR = scratch[3].data();
        auto* wL = scratch[4].data();
        auto* wR = scratch[5].data();
        auto* dryL = scratch[6].data();
        auto* dryR = scratch[7].data();
        std::copy (l, l + n, inL);
        std::copy (r, r + n, inR);
        std::fill (sumL, sumL + n, 0.0f);
        std::fill (sumR, sumR + n, 0.0f);

        for (auto& b : branches)
        {
            std::copy (inL, inL + n, wL);
            std::copy (inR, inR + n, wR);
            for (auto& c : b.chain)
            {
                if (! c.slot.enabled || c.slot.mix <= 0.0f)
                    continue;
                const auto partial = c.slot.mix < 0.999f;
                if (partial)
                {
                    std::copy (wL, wL + n, dryL);
                    std::copy (wR, wR + n, dryR);
                }
                c.effect->process (wL, wR, n, c.slot.params, bpm);
                if (partial)
                    for (int i = 0; i < n; ++i)
                    {
                        wL[i] = dryL[i] + (wL[i] - dryL[i]) * c.slot.mix;
                        wR[i] = dryR[i] + (wR[i] - dryR[i]) * c.slot.mix;
                    }
            }
            for (int i = 0; i < n; ++i)
            {
                sumL[i] += wL[i] * b.gain;
                sumR[i] += wR[i] * b.gain;
            }
        }

        std::copy (sumL, sumL + n, l);
        std::copy (sumR, sumR + n, r);
    }

    std::vector<Branch> branches;
    std::array<std::vector<float>, 8> scratch;
};

// Peak Controller (REA_rodada17 §5): o som passa direto. Detector com ataque instantâneo (pico do bloco)
// e queda LINEAR em tempo musical: (10001^(D/128) − 1)·0,0192 por semínima. Saída Peak =
// base + a·forma(p) com a = ±0,4·(6^(|A|/256) − 1) e a tensão do FL; saída 0 = Peak + LFO, 1 = Peak,
// 2 = LFO (o LFO dele ainda não é gerado: vale a base do LFO). Parâmetros = valores crus do FL normalizados.
class PeakController final : public Effect
{
public:
    void prepare (double sr) noexcept override { sampleRate = sr; }
    void reset() noexcept override { detector = 0.0; }

    void process (float* l, float* r, int n, const EffectParams& p, double bpm) noexcept override
    {
        const auto amountRaw = (std::clamp ((double) p[1], 0.0, 1.0) * 2.0 - 1.0) * 256.0;
        const auto tensionRaw = (std::clamp ((double) p[2], 0.0, 1.0) * 2.0 - 1.0) * 128.0;
        const auto decayRaw = 5.0 + 123.0 * std::clamp ((double) p[3], 0.0, 1.0);
        const auto perBeat = (std::pow (10001.0, decayRaw / 128.0) - 1.0) * 0.0192;

        float blockPeak = 0.0f;
        for (int i = 0; i < n; ++i)
            blockPeak = std::max ({ blockPeak, std::abs (l[i]), std::abs (r[i]) });
        detector = std::max (0.0, detector - perBeat * std::max (1.0, bpm) / 60.0 * n / sampleRate);
        detector = std::max (detector, (double) blockPeak);

        const auto amount = (amountRaw < 0.0 ? -1.0 : 1.0) * (std::pow (6.0, std::abs (amountRaw) / 256.0) - 1.0) * 0.4;
        double shaped = detector;
        if (std::abs (tensionRaw) > 1e-9)
        {
            const auto k = (std::pow (1001.0, std::abs (tensionRaw) / 128.0) - 1.0) * 0.1;
            const auto y = tensionRaw > 0.0 ? k - (std::pow (k + 1.0, 1.0 - detector) - 1.0) : std::pow (k + 1.0, detector) - 1.0;
            shaped = y / k;
        }
        peakOut = (float) std::clamp (p[0] + amount * shaped, 0.0, 1.0);
        lfoOut = std::clamp (p[4], 0.0f, 1.0f);
    }

    float controlOutput (int output) const noexcept override
    {
        return output == 1 ? peakOut : output == 2 ? lfoOut : std::clamp (peakOut + lfoOut, 0.0f, 1.0f);
    }

private:
    double sampleRate = 44100.0, detector = 0.0;
    float peakOut = 0.0f, lfoOut = 0.0f;
};

class Utility final : public Effect
{
public:
    void prepare (double) noexcept override {}
    void reset() noexcept override {}

    void process (float* l, float* r, int n, const EffectParams& p, double) noexcept override
    {
        if (p[0] != lastGainParam) // pow só quando o parâmetro muda (os Patchers têm dezenas de utilitários)
        {
            lastGainParam = p[0];
            cachedGain = p[0] <= 0.0f ? 0.0f : std::pow (10.0f, utilityGainDb (p[0]) / 20.0f); // no fundo = mudo
        }
        const auto gain = cachedGain;
        const auto pan = std::clamp (p[1] * 2.0f - 1.0f, -1.0f, 1.0f);
        const auto gl = gain * (pan > 0.0f ? 1.0f - pan : 1.0f); // balanço: abaixa o lado oposto
        const auto gr = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
        const auto ll = utilityMatrix (p[2]), rl = utilityMatrix (p[3]), lr = utilityMatrix (p[4]), rr = utilityMatrix (p[5]);

        // atalhos: identidade (comum nos PanOMatic/Stereo Shaper dos Patchers) e mudo
        if (gl == 0.0f && gr == 0.0f)
        {
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);
            return;
        }
        if (rl == 0.0f && lr == 0.0f)
        {
            const auto fl = ll * gl, fr = rr * gr;
            if (fl == 1.0f && fr == 1.0f)
                return;
            for (int i = 0; i < n; ++i)
            {
                l[i] *= fl;
                r[i] *= fr;
            }
            return;
        }

        for (int i = 0; i < n; ++i)
        {
            const auto a = l[i], b = r[i];
            l[i] = (ll * a + rl * b) * gl;
            r[i] = (lr * a + rr * b) * gr;
        }
    }

private:
    float lastGainParam = -1.0f, cachedGain = 1.0f;
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
    static const EffectInfo pitchShift { "Pitch shift", { { "Semitons", 0.5f }, { "Cents", 0.5f }, { "Mix", 1.0f }, { "Janela", 0.4f } } };
    static const EffectInfo freqShift { "Frequency shifter", { { "Deslocamento", 0.5f }, { "Mix", 1.0f }, { "Estéreo", 0.0f } } };
    static const EffectInfo parallelInfo { "Patcher (paralelo)", {} };
    static const EffectInfo crossoverInfo { "Crossover", { { "Passa-altas", 0.0f }, { "Corte PA", 0.3f }, { "Incl. PA", 3.0f / 7.0f },
                                                           { "Passa-baixas", 0.0f }, { "Corte PB", 0.7f }, { "Incl. PB", 3.0f / 7.0f } } };
    static const EffectInfo rhythmInfo = [] {
        EffectInfo info { "Envelope rítmico", { { "Período", 1.0f / 64.0f }, { "Pontos", 0.0f } } };
        static const char* names[] = { "P1 pos", "P1 nível", "P1 tensão", "P2 pos", "P2 nível", "P2 tensão", "P3 pos", "P3 nível", "P3 tensão",
                                       "P4 pos", "P4 nível", "P4 tensão", "P5 pos", "P5 nível", "P5 tensão", "P6 pos", "P6 nível", "P6 tensão",
                                       "P7 pos", "P7 nível", "P7 tensão", "P8 pos", "P8 nível", "P8 tensão", "P9 pos", "P9 nível", "P9 tensão",
                                       "P10 pos", "P10 nível", "P10 tensão", "P11 pos", "P11 nível", "P11 tensão", "P12 pos", "P12 nível", "P12 tensão" };
        for (int i = 0; i < 36; ++i)
            info.params.push_back ({ names[i], i % 3 == 2 ? 0.5f : 1.0f });
        info.params.push_back ({ "Só controle", 0.0f });
        return info;
    }();
    static const EffectInfo autoPanInfo { "Auto-pan", { { "Período", 1.0f / 64.0f }, { "Profund.", 0.75f }, { "Fase", 0.0f }, { "Pan", 0.5f }, { "Só controle", 0.0f } } };
    static const EffectInfo vocoderInfo { "Vocoder", { { "Bandas", 0.4f }, { "Ordem", 0.5f }, { "Ouvir", 1.0f }, { "Trocar L/R", 0.0f },
                                                       { "Freq. mín.", 0.0f }, { "Freq. máx.", 1.0f }, { "Escala", 0.5f }, { "Inverter", 0.0f },
                                                       { "Formante", 0.5f }, { "Largura", 45.0f / 195.0f }, { "Ataque", 10.0f / 990.0f },
                                                       { "Release", 90.0f / 990.0f }, { "Hold", 0.0f }, { "Modulador", 0.0f },
                                                       { "Portadora", 0.0f }, { "Wet", 0.8f } } };
    static const EffectInfo pitchCorrectInfo = [] {
        EffectInfo info { "Correção de afinação", { { "Velocidade", 1.0f }, { "Fino", 0.5f }, { "Desligar tudo", 0.0f }, { "Lá", 0.5f },
                                                     { "Faixa", 0.4f } } };
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (const auto* name : names)
            info.params.push_back ({ name, 1.0f });
        static const char* bypass[] = { "C off", "C# off", "D off", "D# off", "E off", "F off", "F# off", "G off", "G# off", "A off", "A# off", "B off" };
        for (const auto* name : bypass)
            info.params.push_back ({ name, 0.0f });
        return info;
    }();
    static const EffectInfo lofiInfo { "Lo-fi", { { "Taxa", 0.0f }, { "Bits", 0.0f }, { "Mix", 1.0f } } };
    static const EffectInfo effectorInfo { "Effector", { { "Tipo", 0.0f }, { "Bypass", 0.0f }, { "Wet", 1.0f }, { "X", 0.5f }, { "Y", 0.5f },
                                                         { "Mod rate", 0.5f }, { "Tempo red.", 0.0f }, { "Mod X", 0.5f }, { "Mod Y", 0.5f },
                                                         { "Forma LFO", 0.0f }, { "Entrada", 1.0f }, { "Saída", 0.5f } } };
    static const EffectInfo peakController { "Peak Controller", { { "Base", 0.0f }, { "Quantidade", 0.8496f }, { "Tensão", 0.5f },
                                                                  { "Queda", 0.4959f }, { "Base do LFO", 0.0f } } };
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
        case EffectKind::pitchShift: return pitchShift;
        case EffectKind::freqShift: return freqShift;
        case EffectKind::parallel: return parallelInfo;
        case EffectKind::lofi: return lofiInfo;
        case EffectKind::effector: return effectorInfo;
        case EffectKind::crossover: return crossoverInfo;
        case EffectKind::rhythmGain: return rhythmInfo;
        case EffectKind::autoPan: return autoPanInfo;
        case EffectKind::vocoder: return vocoderInfo;
        case EffectKind::pitchCorrect: return pitchCorrectInfo;
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
            {
                if (v <= 0.0f)
                    return "mudo";
                std::snprintf (text, sizeof (text), "%+.1f dB", utilityGainDb (v));
            }
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

        case EffectKind::pitchShift:
            switch (param)
            {
                case 0: std::snprintf (text, sizeof (text), "%+.0f st", pitch::semitones (v)); break;
                case 1: std::snprintf (text, sizeof (text), "%+.0f ct", pitch::cents (v)); break;
                case 3: std::snprintf (text, sizeof (text), "%.0f ms", pitch::windowMs (v)); break;
                default: std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f); break;
            }
            return text;

        case EffectKind::freqShift:
            if (param == 0)
                std::snprintf (text, sizeof (text), "%+.1f Hz", fshift::hz (v));
            else
                std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f);
            return text;

        case EffectKind::peakController:
            if (param == 1)
            {
                const auto raw = (v * 2.0 - 1.0) * 256.0;
                std::snprintf (text, sizeof (text), "%+.0f%%", (raw < 0.0 ? -1.0 : 1.0) * (std::pow (6.0, std::abs (raw) / 256.0) - 1.0) * 40.0);
            }
            else if (param == 3)
                std::snprintf (text, sizeof (text), "%.2f / tempo", (std::pow (10001.0, (5.0 + 123.0 * v) / 128.0) - 1.0) * 0.0192);
            else if (param == 2)
                std::snprintf (text, sizeof (text), "%+.0f", (v * 2.0f - 1.0f) * 128.0f);
            else
                std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f);
            return text;

        case EffectKind::lofi:
            if (param == 0)
                std::snprintf (text, sizeof (text), "%.0f Hz", 44100.0 / lofi::holdSamples (v));
            else if (param == 1)
                std::snprintf (text, sizeof (text), "%.1f bits", std::log2 (lofi::levels (v)));
            else
                std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f);
            return text;

        case EffectKind::effector:
            return effector::formatParam (param, v, bpm);

        case EffectKind::pitchCorrect:
            switch (param)
            {
                case 0:
                {
                    const auto u = 1.0 - v;
                    if (u < 0.01)
                        return "instantânea";
                    std::snprintf (text, sizeof (text), "%.1f ms", (3.0 * u * u * u + 0.001) / 8.82 * 1000.0);
                    break;
                }
                case 1: std::snprintf (text, sizeof (text), "%+.0f ct", (2.0f * v - 1.0f) * 100.0f); break;
                case 3: std::snprintf (text, sizeof (text), "%.1f Hz", pcorr::noteA (v) * 8.0); break;
                case 4: std::snprintf (text, sizeof (text), "%.0f Hz mín.", pcorr::minHz[pcorr::range (v)]); break;
                default: return v > 0.5f ? (param >= 17 ? "fora" : "sim") : (param >= 17 ? "-" : (param == 2 ? "não" : "não"));
            }
            return text;

        case EffectKind::vocoder:
            switch (param)
            {
                case 0: std::snprintf (text, sizeof (text), "%d", vocoder::bands (v)); break;
                case 1: std::snprintf (text, sizeof (text), "%d", vocoder::order (v)); break;
                case 2: return std::array<const char*, 3> { "modulador", "portadora", "mistura" }[(std::size_t) std::clamp ((int) std::lround (v * 2.0f), 0, 2)];
                case 3: case 7: case 12: return v > 0.5f ? "ligado" : "desligado";
                case 4: case 5: std::snprintf (text, sizeof (text), "%.0f Hz", vocoder::freq (v)); break;
                case 8: std::snprintf (text, sizeof (text), "%+.0f ct", v * 2400.0f - 1200.0f); break;
                case 9: std::snprintf (text, sizeof (text), "%.0f%%", 5.0f + 195.0f * v); break;
                case 10: case 11: std::snprintf (text, sizeof (text), "%.0f ms", vocoder::ms (v)); break;
                case 13: case 14: case 15: std::snprintf (text, sizeof (text), "%.2f", vocoder::curve11 (v * 160.0 / 128.0)); break;
                default: std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f); break;
            }
            return text;

        case EffectKind::rhythmGain:
        case EffectKind::autoPan:
            if (param == 0)
                std::snprintf (text, sizeof (text), "%.3g tempos", v * 64.0f);
            else if (kind == EffectKind::rhythmGain && param == 1)
                std::snprintf (text, sizeof (text), "%d", (int) std::lround (v * rhythm::maxPoints));
            else if (kind == EffectKind::autoPan && (param == 1 || param == 3))
                std::snprintf (text, sizeof (text), "%+.0f%%", (2.0f * v - 1.0f) * 100.0f);
            else
                std::snprintf (text, sizeof (text), "%.0f%%", v * 100.0f);
            return text;

        case EffectKind::crossover:
            if (param == 0 || param == 3)
                return v > 0.5f ? "ligado" : "desligado";
            if (param == 1 || param == 4)
                std::snprintf (text, sizeof (text), "%.0f Hz", xover::hz (v));
            else
                std::snprintf (text, sizeof (text), "%d dB/oit", xover::dbPerOctave[xover::combo (v)]);
            return text;

        case EffectKind::plugin:
        case EffectKind::parallel:
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

std::unique_ptr<Effect> makeParallelEffect (const EffectSlot& slot)
{
    return std::make_unique<ParallelEffect> (slot);
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
        case EffectKind::pitchShift: return std::make_unique<PitchShift>();
        case EffectKind::freqShift: return std::make_unique<FreqShift>();
        case EffectKind::parallel: return std::make_unique<ParallelEffect> (EffectSlot {}); // vazio: use makeParallelEffect
        case EffectKind::lofi: return std::make_unique<LoFi>();
        case EffectKind::effector: return effector::make();
        case EffectKind::crossover: return std::make_unique<Crossover>();
        case EffectKind::rhythmGain: return std::make_unique<RhythmGain>();
        case EffectKind::autoPan: return std::make_unique<AutoPan>();
        case EffectKind::vocoder: return std::make_unique<Vocoder>();
        case EffectKind::pitchCorrect: return std::make_unique<PitchCorrect>();
        case EffectKind::plugin: break; // criado pelo app (RackState::Factories)
    }
    return std::make_unique<Eq>();
}
} // namespace lynx
