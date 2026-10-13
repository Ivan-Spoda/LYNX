#include "core/Instruments.h"

#include <algorithm>
#include <atomic>

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
            case InstrumentKind::audioClip:
            case InstrumentKind::plugin:
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

// Ganhos de pan por voz (potência constante, 1 no centro).
inline void voicePan (float pan, float& left, float& right) noexcept
{
    const auto angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * 3.14159265f;
    left = std::cos (angle) * 1.41421356f;
    right = std::sin (angle) * 1.41421356f;
}

// ---------------------------------------------------------------------------
// Synth polifônico simples: dente-de-serra com passa-baixas e envelope AR.

struct SynthVoice
{
    float panL = 1.0f, panR = 1.0f;
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
        target->phaseDelta = midiToHz (key) / sampleRate * std::pow (2.0, nextCents / 1200.0);
        voicePan (nextPan, target->panL, target->panR);
        nextPan = nextCents = 0.0f;
    }

    void setNextNoteExpression (float pan, float cents) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
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
                left[i] += s * gl * v.panL;
                right[i] += s * gr * v.panR;
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
    float nextPan = 0.0f, nextCents = 0.0f;
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
    int key = 60;
    float env = 1.0f;      // envelope de ataque/release
    float envStep = 0.0f;  // > 0 subindo (ataque), < 0 descendo (release)
    float panL = 1.0f, panR = 1.0f;
};

class SamplerInstrument final : public Instrument
{
public:
    // clipMode: clip de áudio da Playlist (sem transposição, para no noteOff)
    SamplerInstrument (std::shared_ptr<const SampleData> s, bool clip) : sample (std::move (s)), clipMode (clip) {}

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        fadeStep = (float) (1.0 / (0.003 * sr)); // 3 ms
        allNotesOff();
    }

    void configure (const SamplerSettings& s) noexcept override
    {
        pitch.store (std::clamp (s.pitch, -24.0f, 24.0f));
        attack.store (std::clamp (s.attack, 0.0f, 2.0f));
        release.store (std::clamp (s.release, 0.0f, 4.0f));
        reverse.store (s.reverse);
        cut.store (s.cut);
    }

    void noteOn (int key, float velocity) noexcept override { noteOnAt (key, velocity, 0.0); }

    void noteOnAt (int key, float velocity, double startSeconds) noexcept override
    {
        if (sample == nullptr || sample->frames() < 2)
            return;

        const auto backwards = ! clipMode && reverse.load();
        const auto last = (double) (sample->frames() - 1);
        auto startFrame = std::max (0.0, startSeconds * sample->sampleRate);

        if (startFrame >= last)
            return;

        if (backwards)
            startFrame = last - 1.0 - startFrame; // o par [index, index + 1] tem que caber

        if (! clipMode && cut.load())
            for (auto& v : voices)
                if (v.active)
                    v.stolen = true; // "cut itself": a nota anterior some com fade curto

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

        const auto semitones = clipMode ? 0.0 : (key - 60) + (double) pitch.load() + nextCents / 100.0;
        const auto attackSeconds = clipMode ? 0.0f : attack.load();

        *target = {};
        target->active = true;
        target->key = key;
        target->velocity = velocity;
        target->position = startFrame;
        target->rate = sample->sampleRate / sampleRate * std::pow (2.0, semitones / 12.0) * (backwards ? -1.0 : 1.0);
        target->env = attackSeconds > 0.0f ? 0.0f : 1.0f;
        target->envStep = attackSeconds > 0.0f ? (float) (1.0 / (attackSeconds * sampleRate)) : 0.0f;
        voicePan (clipMode ? 0.0f : nextPan, target->panL, target->panR);
        nextPan = nextCents = 0.0f;
    }

    void setNextNoteExpression (float pan, float cents) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    void noteOff (int key) noexcept override
    {
        if (clipMode)
        {
            allNotesOff(); // o clip acabou: some com fade curto
            return;
        }

        // com release, a nota solta decai; sem release o sample toca inteiro (one-shot)
        const auto seconds = release.load();

        if (seconds <= 0.0f)
            return;

        for (auto& v : voices)
            if (v.active && v.key == key && v.envStep >= 0.0f)
                v.envStep = -(float) (1.0 / (seconds * sampleRate));
    }

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

        const auto* l = sample->leftData();
        const auto* r = sample->rightData();
        const auto last = (double) (sample->frames() - 1);

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            for (int i = 0; i < n; ++i)
            {
                if (v.position >= last || v.position < 0.0 || (v.stolen && v.fade <= 0.0f) || (v.env <= 0.0f && v.envStep < 0.0f))
                {
                    v.active = false;
                    break;
                }

                const auto index = (std::size_t) v.position;
                const auto frac = (float) (v.position - (double) index);
                const auto sl = l[index] + frac * (l[index + 1] - l[index]);
                const auto sr = r[index] + frac * (r[index + 1] - r[index]);
                const auto gain = v.velocity * v.fade * v.env;

                left[i] += sl * gain * gl * v.panL;
                right[i] += sr * gain * gr * v.panR;

                v.position += v.rate;
                ++v.age;

                if (v.envStep != 0.0f)
                {
                    v.env += v.envStep;

                    if (v.env >= 1.0f && v.envStep > 0.0f)
                    {
                        v.env = 1.0f;
                        v.envStep = 0.0f;
                    }
                }

                if (v.stolen)
                    v.fade -= fadeStep;
            }
        }
    }

private:
    std::shared_ptr<const SampleData> sample;
    bool clipMode = false;
    double sampleRate = 44100.0;
    float fadeStep = 0.01f;
    std::array<SamplerVoice, 16> voices {};
    std::atomic<float> pitch { 0.0f }, attack { 0.0f }, release { 0.0f };
    float nextPan = 0.0f, nextCents = 0.0f;
    std::atomic<bool> reverse { false }, cut { false };
};
} // namespace

std::unique_ptr<Instrument> makeInstrument (InstrumentKind kind, std::shared_ptr<const SampleData> sample)
{
    if (kind == InstrumentKind::sampler || kind == InstrumentKind::audioClip)
        return std::make_unique<SamplerInstrument> (std::move (sample), kind == InstrumentKind::audioClip);

    if (kind == InstrumentKind::synth)
        return std::make_unique<SynthInstrument>();

    return std::make_unique<DrumInstrument> (kind);
}
} // namespace lynx
