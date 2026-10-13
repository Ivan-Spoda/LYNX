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
    double glideRatio = 1.0;  // multiplica a frequência a cada sample durante um slide
    int glideLeft = 0;
    bool active = false;
    bool releasing = false;
    int key = 0;
    float velocity = 0.0f;
    float level = 0.0f;       // envelope
    int stage = 0;            // 0 ataque, 1 decaimento, 2 sustentação, 3 release
    double phaseDelta = 0.0;  // da fundamental (cada oscilador multiplica)
    std::array<double, 3> phase {};
    float low = 0.0f, band = 0.0f; // estados do filtro
    int age = 0;
};

// Troca de ajustes sem bloquear a thread de áudio: a UI escreve sob um flag curto e o áudio
// só copia quando consegue pegar o flag (se não conseguir, usa os ajustes antigos neste bloco).
template <typename T>
class SettingsMailbox
{
public:
    void write (const T& value) noexcept
    {
        while (busy.test_and_set (std::memory_order_acquire)) {}
        pending = value;
        dirty = true;
        busy.clear (std::memory_order_release);
    }

    bool read (T& out) noexcept
    {
        if (busy.test_and_set (std::memory_order_acquire))
            return false;

        const auto changed = dirty;

        if (changed)
        {
            out = pending;
            dirty = false;
        }

        busy.clear (std::memory_order_release);
        return changed;
    }

private:
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    T pending {};
    bool dirty = false;
};

class SynthInstrument final : public Instrument
{
public:
    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        updateCoefficients();
        allNotesOff();
    }

    void configure (const ChannelDef& channel, double) noexcept override { mailbox.write (channel.synth); }

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

    void slideTo (int key, float cents, float samples) noexcept override
    {
        const auto target = midiToHz (key) / sampleRate * std::pow (2.0, cents / 1200.0);
        const auto steps = std::max (1, (int) samples);

        for (auto& v : voices)
            if (v.active && ! v.releasing && v.phaseDelta > 0.0)
            {
                v.glideRatio = std::pow (target / v.phaseDelta, 1.0 / steps);
                v.glideLeft = steps; // a voz mantém a chave original: o "off" dela é que encerra a nota
            }
    }

    void noteOff (int key) noexcept override
    {
        for (auto& v : voices)
            if (v.active && ! v.releasing && v.key == key)
            {
                v.releasing = true;
                v.stage = 3;
            }
    }

    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            v.active = false;
    }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        if (mailbox.read (settings))
            updateCoefficients();

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            for (int i = 0; i < n; ++i)
            {
                // envelope ADSR
                switch (v.stage)
                {
                    case 0:
                        v.level += attackStep;
                        if (v.level >= 1.0f) { v.level = 1.0f; v.stage = 1; }
                        break;
                    case 1:
                        v.level = settings.sustain + (v.level - settings.sustain) * decayCoeff;
                        if (v.level - settings.sustain < 0.001f) { v.level = settings.sustain; v.stage = 2; }
                        break;
                    case 2: break;
                    default: v.level *= releaseCoeff; break;
                }

                if (v.glideLeft > 0)
                {
                    v.phaseDelta *= v.glideRatio;
                    --v.glideLeft;
                }

                float sum = 0.0f;

                for (std::size_t o = 0; o < 3; ++o)
                {
                    const auto& osc = settings.osc[o];

                    if (osc.level <= 0.0f)
                        continue;

                    const auto delta = std::min (0.49, v.phaseDelta * ratios[o]);
                    sum += osc.level * oscillator (osc.wave, v.phase[o], delta);
                    v.phase[o] += delta;
                    v.phase[o] -= std::floor (v.phase[o]);
                }

                // filtro passa-baixas ressonante (state variable "TPT": estável em qualquer corte)
                const auto v3 = sum - v.low;
                const auto v1 = a1 * v.band + a2 * v3;
                const auto v2 = v.low + a2 * v.band + a3 * v3;
                v.band = 2.0f * v1 - v.band;
                v.low = 2.0f * v2 - v.low;

                const auto s = 0.3f * v2 * v.level * v.velocity;
                left[i] += s * gl * v.panL;
                right[i] += s * gr * v.panR;
                ++v.age;
            }

            if (v.stage == 3 && v.level < 0.0005f)
                v.active = false;
        }
    }

private:
    // correção de aliasing nas descontinuidades (polyBLEP)
    static float polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return (float) (t + t - t * t - 1.0);
        }

        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return (float) (t * t + t + t + 1.0);
        }

        return 0.0f;
    }

    float oscillator (Waveform w, double phase, double dt) noexcept
    {
        switch (w)
        {
            case Waveform::sine: return (float) std::sin (twoPi * phase);
            case Waveform::triangle: return (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
            case Waveform::saw: return (float) (2.0 * phase - 1.0) - polyBlep (phase, dt);
            case Waveform::square:
            {
                auto s = phase < 0.5 ? 1.0f : -1.0f;
                s += polyBlep (phase, dt);
                s -= polyBlep (std::fmod (phase + 0.5, 1.0), dt);
                return s;
            }
            case Waveform::noise: return noise.next();
        }

        return 0.0f;
    }

    void updateCoefficients() noexcept
    {
        attackStep = (float) (1.0 / (std::max (0.0005, (double) settings.attack) * sampleRate));
        decayCoeff = decayCoefficient (std::max (0.001, (double) settings.decay), sampleRate);
        releaseCoeff = decayCoefficient (std::max (0.005, (double) settings.release), sampleRate);

        for (std::size_t o = 0; o < 3; ++o)
            ratios[o] = std::pow (2.0, settings.osc[o].octave + settings.osc[o].detune / 1200.0);

        const auto fc = std::clamp ((double) settings.cutoff, 20.0, sampleRate * 0.45);
        const auto g = (float) std::tan (3.14159265358979 * fc / sampleRate);
        const auto k = 2.0f - 1.9f * std::clamp (settings.resonance, 0.0f, 1.0f); // 2 = sem ressonância
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double sampleRate = 44100.0;
    SettingsMailbox<SynthSettings> mailbox;
    SynthSettings settings;
    float attackStep = 0.01f;
    float decayCoeff = 0.999f;
    float releaseCoeff = 0.999f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f; // coeficientes do filtro
    std::array<double, 3> ratios { 1.0, 1.0, 1.0 };
    float nextPan = 0.0f, nextCents = 0.0f;
    Noise noise;
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
    float env = 1.0f;      // envelope
    float envStep = 0.0f;  // passo do ataque (> 0) ou do release (< 0)
    int stage = 1;         // 0 ataque, 1 decaimento, 2 sustentação, 3 release
    double first = 0.0, last = 0.0; // trecho tocado
    float clipElapsed = 0.0f, clipRemaining = 0.0f, fadeIn = 0.0f, fadeOut = 0.0f; // fades de clip
    float panL = 1.0f, panR = 1.0f;
    double glideRatio = 1.0;
    int glideLeft = 0;
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

    void configure (const ChannelDef& channel, double songBpm) noexcept override
    {
        const auto& s = channel.sampler;
        pitch.store (std::clamp (s.pitch, -24.0f, 24.0f));
        attack.store (std::clamp (s.attack, 0.0f, 2.0f));
        decay.store (std::clamp (s.decay, 0.0f, 10.0f));
        sustain.store (std::clamp (s.sustain, 0.0f, 1.0f));
        release.store (std::clamp (s.release, 0.0f, 4.0f));
        reverse.store (s.reverse);
        cut.store (s.cut);
        // com "manter altura" o sample já chega esticado (RackState): toca em velocidade normal
        stretchBeats.store (s.keepPitch ? 0.0f : std::max (0.0f, s.stretchBeats));
        regionStart.store (std::clamp (s.regionStart, 0.0f, 1.0f));
        regionEnd.store (std::clamp (s.regionEnd, 0.0f, 1.0f));
        SliceTable table;
        table.count = (int) std::min<std::size_t> (s.slices.size(), table.starts.size());
        for (int i = 0; i < table.count; ++i)
            table.starts[(std::size_t) i] = std::clamp (s.slices[(std::size_t) i], 0.0f, 1.0f);
        sliceMailbox.write (table);
        bpm.store (std::max (1.0, songBpm));
    }

    // Leitura em frames do sample por sample de saída (antes da afinação das notas). Com
    // stretch, o sample inteiro dura `stretchBeats` no andamento atual (reamostrando, como o
    // modo "resample" do FL: a altura acompanha).
    double baseRate() const noexcept
    {
        const auto beats = stretchBeats.load();

        if (beats > 0.0f && sample != nullptr)
            return (double) sample->frames() / ((double) beats * 60.0 / bpm.load() * sampleRate);

        return sample->sampleRate / sampleRate;
    }

    void noteOn (int key, float velocity) noexcept override { noteOnAt (key, velocity, 0.0); }

    void noteOnAt (int key, float velocity, double startSeconds) noexcept override
    {
        if (sample == nullptr || sample->frames() < 2)
            return;

        sliceMailbox.read (slices);
        const auto backwards = ! clipMode && reverse.load();
        const auto total = (double) (sample->frames() - 1);
        // trecho do sample (o início vale como "start offset"); clips de áudio usam o arquivo todo
        auto first = clipMode ? 0.0 : std::floor (total * std::min (regionStart.load(), regionEnd.load()));
        auto last = clipMode ? total : std::max (first + 1.0, std::floor (total * regionEnd.load()));
        auto sliced = false;

        // sample fatiado: cada tecla a partir do C5 toca uma fatia, na altura original
        if (! clipMode && slices.count > 0)
        {
            const auto index = key - 60;

            if (index < 0 || index >= slices.count)
                return;

            first = std::floor (total * slices.starts[(std::size_t) index]);
            last = index + 1 < slices.count ? std::floor (total * slices.starts[(std::size_t) index + 1]) : total;
            sliced = true;
        }
        const auto stretched = stretchBeats.load() > 0.0f;
        const auto base = baseRate();
        auto startFrame = first + std::max (0.0, startSeconds * sampleRate * base);

        if (startFrame >= last)
            return;

        if (backwards)
            startFrame = last - 1.0 - (startFrame - first); // o par [index, index + 1] tem que caber

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

        // com stretch ("resample" do FL) a altura da nota não muda a velocidade
        const auto semitones = clipMode || stretched ? 0.0 : (sliced ? 0 : key - 60) + (double) pitch.load() + nextCents / 100.0;
        const auto attackSeconds = clipMode ? 0.0f : attack.load();

        *target = {};
        target->active = true;
        target->key = key;
        target->velocity = velocity;
        target->position = startFrame;
        target->first = first;
        target->last = last;
        target->clipElapsed = nextClip[0];
        target->clipRemaining = nextClip[1];
        target->fadeIn = nextClip[2];
        target->fadeOut = nextClip[3];
        nextClip = {};
        target->rate = base * std::pow (2.0, semitones / 12.0) * (backwards ? -1.0 : 1.0);
        target->env = attackSeconds > 0.0f ? 0.0f : 1.0f;
        target->envStep = attackSeconds > 0.0f ? (float) (1.0 / (attackSeconds * sampleRate)) : 0.0f;
        target->stage = attackSeconds > 0.0f ? 0 : 1;
        voicePan (clipMode ? 0.0f : nextPan, target->panL, target->panR);
        nextPan = nextCents = 0.0f;
    }

    void setNextNoteExpression (float pan, float cents) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    void setNextClipFades (float elapsed, float remaining, float in, float out) noexcept override
    {
        nextClip = { elapsed, remaining, in, out };
    }

    void slideTo (int key, float cents, float samples) noexcept override
    {
        if (clipMode || sample == nullptr)
            return;

        const auto semitones = (key - 60) + (double) pitch.load() + cents / 100.0;
        const auto steps = std::max (1, (int) samples);

        for (auto& v : voices)
            if (v.active && ! v.stolen && v.rate != 0.0)
            {
                const auto target = sample->sampleRate / sampleRate * std::pow (2.0, semitones / 12.0) * (v.rate < 0.0 ? -1.0 : 1.0);
                v.glideRatio = std::pow (target / v.rate, 1.0 / steps);
                v.glideLeft = steps;
            }
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

        // release linear a partir do nível atual (como o FL)
        for (auto& v : voices)
            if (v.active && v.key == key && v.stage < 3)
            {
                v.stage = 3;
                v.envStep = -(float) (std::max (v.env, 0.001f) / (seconds * sampleRate));
            }
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
                if (v.position >= std::min (last, v.last) || v.position < v.first || (v.stolen && v.fade <= 0.0f) || (v.env <= 0.0f && v.stage >= 1))
                {
                    v.active = false;
                    break;
                }

                const auto index = (std::size_t) v.position;
                const auto frac = (float) (v.position - (double) index);
                const auto sl = l[index] + frac * (l[index + 1] - l[index]);
                const auto sr = r[index] + frac * (r[index + 1] - r[index]);
                auto gain = v.velocity * v.fade * v.env;

                if (v.fadeIn > 0.0f || v.fadeOut > 0.0f)
                {
                    if (v.fadeIn > 0.0f && v.clipElapsed < v.fadeIn)
                        gain *= v.clipElapsed / v.fadeIn;
                    if (v.fadeOut > 0.0f && v.clipRemaining < v.fadeOut)
                        gain *= std::max (0.0f, v.clipRemaining) / v.fadeOut;

                    v.clipElapsed += 1.0f;
                    v.clipRemaining -= 1.0f;
                }

                left[i] += sl * gain * gl * v.panL;
                right[i] += sr * gain * gr * v.panR;

                v.position += v.rate;
                ++v.age;

                if (v.glideLeft > 0)
                {
                    v.rate *= v.glideRatio;
                    --v.glideLeft;
                }

                // envelope: 0 ataque, 1 decaimento, 2 sustentação, 3 release
                if (v.stage == 0)
                {
                    v.env += v.envStep;

                    if (v.env >= 1.0f)
                    {
                        v.env = 1.0f;
                        v.stage = 1;
                    }
                }
                else if (v.stage == 1)
                {
                    const auto s = sustain.load (std::memory_order_relaxed);
                    const auto d = decay.load (std::memory_order_relaxed);

                    if (s >= 1.0f)
                        v.stage = 2;
                    else if (d <= 0.0f)
                    {
                        v.env = s;
                        v.stage = 2;
                    }
                    else
                    {
                        // a rampa vai de 1 a 0 em `decay` e para no sustain (como no FL)
                        v.env -= (float) (1.0 / (d * sampleRate));

                        if (v.env <= s)
                        {
                            v.env = s;
                            v.stage = 2;
                        }
                    }
                }
                else if (v.stage == 3)
                    v.env += v.envStep;

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
    std::atomic<float> pitch { 0.0f }, attack { 0.0f }, decay { 0.0f }, sustain { 1.0f }, release { 0.0f }, stretchBeats { 0.0f };
    std::atomic<double> bpm { 120.0 };
    std::atomic<float> regionStart { 0.0f }, regionEnd { 1.0f };
    std::array<float, 4> nextClip {}; // fades do próximo noteOn (só a thread de áudio mexe)

    struct SliceTable
    {
        std::array<float, 128> starts {};
        int count = 0;
    };
    SettingsMailbox<SliceTable> sliceMailbox;
    SliceTable slices; // cópia da thread de áudio
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
