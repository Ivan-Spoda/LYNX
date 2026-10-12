#include "core/Instruments.h"

#include <cmath>

namespace lynx
{
namespace
{
constexpr double twoPi = 6.283185307179586;

// Fator por sample para um decaimento exponencial que cai 60 dB em `seconds`.
float decayCoefficient (double seconds, double sampleRate) noexcept
{
    return (float) std::exp (std::log (0.001) / (seconds * sampleRate));
}

double midiToHz (int key) noexcept
{
    return 440.0 * std::pow (2.0, (key - 69) / 12.0);
}

// ---------------------------------------------------------------------------
// Bateria: cada disparo ocupa uma voz; poucas vozes permitem sobreposição curta.

struct DrumVoice
{
    bool active = false;
    float level = 0.0f;     // envelope de amplitude
    float velocity = 0.0f;
    double phase = 0.0;
    double pitchEnv = 0.0;  // kick/snare: envelope de pitch
    float filterState = 0.0f;
    float lastNoise = 0.0f;
    float lastHighPass = 0.0f;
    int age = 0;            // samples desde o disparo
};

class DrumInstrument final : public Instrument
{
public:
    explicit DrumInstrument (InstrumentKind k) : kind (k) {}

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        const double decaySeconds = kind == InstrumentKind::kick    ? 0.45
                                    : kind == InstrumentKind::snare ? 0.22
                                    : kind == InstrumentKind::clap  ? 0.30
                                                                    : 0.06; // hat
        decay = decayCoefficient (decaySeconds, sr);
        pitchDecay = decayCoefficient (kind == InstrumentKind::kick ? 0.08 : 0.03, sr);
        allNotesOff();
    }

    void noteOn (int, float velocity) noexcept override
    {
        // Rouba a voz mais antiga se todas estiverem ocupadas.
        DrumVoice* target = &voices[0];

        for (auto& v : voices)
        {
            if (! v.active)
            {
                target = &v;
                break;
            }

            if (v.age > target->age)
                target = &v;
        }

        *target = {};
        target->active = true;
        target->level = 1.0f;
        target->velocity = velocity;
        target->pitchEnv = 1.0;
    }

    void noteOff (int) noexcept override {} // bateria toca até o fim do decaimento

    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            v.active = false;
    }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            for (int i = 0; i < n; ++i)
            {
                const auto s = nextSample (v) * v.velocity;
                left[i] += s * gl;
                right[i] += s * gr;
            }

            if (v.level < 0.0005f)
                v.active = false;
        }
    }

private:
    float nextSample (DrumVoice& v) noexcept
    {
        float out = 0.0f;

        switch (kind)
        {
            case InstrumentKind::kick:
            {
                const auto hz = 45.0 + 110.0 * v.pitchEnv; // varredura 155 → 45 Hz
                v.phase += twoPi * hz / sampleRate;
                out = (float) std::sin (v.phase) * v.level;
                break;
            }

            case InstrumentKind::snare:
            {
                const auto hz = 180.0 + 60.0 * v.pitchEnv;
                v.phase += twoPi * hz / sampleRate;
                const auto tone = (float) std::sin (v.phase) * (float) v.pitchEnv;
                const auto noise = highPass (v, noiseGen.next());
                out = (0.5f * tone + 0.7f * noise) * v.level;
                break;
            }

            case InstrumentKind::hat:
                out = 0.8f * highPass (v, noiseGen.next()) * v.level;
                break;

            case InstrumentKind::clap:
            {
                // Três estalos curtos seguidos de uma cauda: imita várias mãos.
                const auto ms = v.age * 1000.0 / sampleRate;
                const auto burst = ms < 30.0 ? (float) std::exp (-std::fmod (ms, 10.0) / 2.0) : 1.0f;
                out = 0.8f * bandPass (v, noiseGen.next()) * v.level * burst;
                break;
            }

            case InstrumentKind::synth:
            case InstrumentKind::sampler:
                break;
        }

        v.level *= decay;
        v.pitchEnv *= pitchDecay;
        ++v.age;
        return out;
    }

    // Passa-altas de 1ª ordem simples (diferença), suficiente para ruído de prato.
    static float highPass (DrumVoice& v, float x) noexcept
    {
        const auto y = 0.85f * (v.filterState + x - v.lastNoise);
        v.lastNoise = x;
        v.filterState = y;
        return y;
    }

    // Passa-banda barato: passa-altas seguido de média de dois pontos.
    static float bandPass (DrumVoice& v, float x) noexcept
    {
        const auto hp = highPass (v, x);
        const auto y = 0.5f * (hp + v.lastHighPass);
        v.lastHighPass = hp;
        return y;
    }

    InstrumentKind kind;
    double sampleRate = 44100.0;
    float decay = 0.999f;
    double pitchDecay = 0.999;
    std::array<DrumVoice, 4> voices {};
    Noise noiseGen;
};

// ---------------------------------------------------------------------------
// Synth polifônico simples: dente-de-serra com passa-baixas e envelope AR.

struct SynthVoice
{
    bool active = false;
    bool releasing = false;
    int key = 0;
    float velocity = 0.0f;
    float level = 0.0f;
    double phase = 0.0;
    double phaseDelta = 0.0;
    float filter = 0.0f;
    int age = 0;
};

class SynthInstrument final : public Instrument
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        attackStep = (float) (1.0 / (0.005 * sr));
        release = decayCoefficient (0.25, sr);
        filterCoeff = (float) (1.0 - std::exp (-twoPi * 2500.0 / sr));
        allNotesOff();
    }

    void noteOn (int key, float velocity) noexcept override
    {
        SynthVoice* target = &voices[0];

        for (auto& v : voices)
        {
            if (! v.active)
            {
                target = &v;
                break;
            }

            if (v.age > target->age)
                target = &v;
        }

        *target = {};
        target->active = true;
        target->key = key;
        target->velocity = velocity;
        target->phaseDelta = midiToHz (key) / sampleRate;
    }

    void noteOff (int key) noexcept override
    {
        for (auto& v : voices)
            if (v.active && ! v.releasing && v.key == key)
                v.releasing = true;
    }

    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            v.active = false;
    }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            for (int i = 0; i < n; ++i)
            {
                if (v.releasing)
                    v.level *= release;
                else if (v.level < 1.0f)
                    v.level = std::fmin (1.0f, v.level + attackStep);

                v.phase += v.phaseDelta;
                v.phase -= std::floor (v.phase);
                const auto saw = (float) (2.0 * v.phase - 1.0);
                v.filter += filterCoeff * (saw - v.filter);

                const auto s = 0.3f * v.filter * v.level * v.velocity;
                left[i] += s * gl;
                right[i] += s * gr;
                ++v.age;
            }

            if (v.releasing && v.level < 0.0005f)
                v.active = false;
        }
    }

private:
    double sampleRate = 44100.0;
    float attackStep = 0.01f;
    float release = 0.999f;
    float filterCoeff = 0.3f;
    std::array<SynthVoice, 16> voices {};
};
// ---------------------------------------------------------------------------
// Sampler: toca o sample inteiro a cada nota (one-shot, como um step do FL),
// transposto pela tecla (60 = altura original), com interpolação linear.

struct SamplerVoice
{
    bool active = false;
    double position = 0.0;
    double rate = 1.0;
    float velocity = 0.0f;
    float fade = 1.0f;     // < 1 quando a voz foi roubada (declick)
    bool stolen = false;
    int age = 0;
};

class SamplerInstrument final : public Instrument
{
public:
    explicit SamplerInstrument (std::shared_ptr<const SampleData> s) : sample (std::move (s)) {}

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        fadeStep = (float) (1.0 / (0.003 * sr)); // 3 ms
        allNotesOff();
    }

    void noteOn (int key, float velocity) noexcept override
    {
        if (sample == nullptr || sample->frames() < 2)
            return;

        SamplerVoice* target = nullptr;

        for (auto& v : voices)
            if (! v.active)
            {
                target = &v;
                break;
            }

        if (target == nullptr)
        {
            // Sem voz livre: rouba a mais antiga.
            target = &voices[0];
            for (auto& v : voices)
                if (v.age > target->age)
                    target = &v;
        }

        *target = {};
        target->active = true;
        target->velocity = velocity;
        target->rate = sample->sampleRate / sampleRate * std::pow (2.0, (key - 60) / 12.0);
    }

    void noteOff (int) noexcept override {}

    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            if (v.active)
                v.stolen = true; // some com fade em vez de cortar seco
    }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        if (sample == nullptr)
            return;

        const auto& l = sample->left;
        const auto& r = sample->right.empty() ? sample->left : sample->right;
        const auto last = (double) (sample->frames() - 1);

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            for (int i = 0; i < n; ++i)
            {
                if (v.position >= last || (v.stolen && v.fade <= 0.0f))
                {
                    v.active = false;
                    break;
                }

                const auto index = (std::size_t) v.position;
                const auto frac = (float) (v.position - (double) index);
                const auto sl = l[index] + frac * (l[index + 1] - l[index]);
                const auto sr = r[index] + frac * (r[index + 1] - r[index]);
                const auto gain = v.velocity * v.fade;

                left[i] += sl * gain * gl;
                right[i] += sr * gain * gr;

                v.position += v.rate;
                ++v.age;

                if (v.stolen)
                    v.fade -= fadeStep;
            }
        }
    }

private:
    std::shared_ptr<const SampleData> sample;
    double sampleRate = 44100.0;
    float fadeStep = 0.01f;
    std::array<SamplerVoice, 16> voices {};
};
} // namespace

std::unique_ptr<Instrument> makeInstrument (InstrumentKind kind, std::shared_ptr<const SampleData> sample)
{
    if (kind == InstrumentKind::sampler)
        return std::make_unique<SamplerInstrument> (std::move (sample));

    if (kind == InstrumentKind::synth)
        return std::make_unique<SynthInstrument>();

    return std::make_unique<DrumInstrument> (kind);
}
} // namespace lynx
