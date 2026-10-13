#include "core/Instruments.h"
#include "core/FlSynths.h"

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
            case InstrumentKind::controller:
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

// ---------------------------------------------------------------------------
// Envelopes de modulação (corte e afinação), por voz: 0 ataque, 1 hold, 2 decaimento,
// 3 sustentação, 4 release. Segmentos retos (tensão 0, o padrão do FL; REA_rodada3 §1.5).

struct ModState
{
    float level = 0.0f;
    float releaseStep = 0.0f; // fixado ao soltar a nota: do nível atual até 0
    int stage = 0, held = 0;
};

struct ModCoefficients
{
    bool active = false;
    float amount = 0.0f, attackStep = 1.0f, decayStep = 1.0f, releasePerLevel = 1.0f, sustain = 0.0f;
    int holdSamples = 0;

    static ModCoefficients make (const ModEnvelope& e, double sampleRate) noexcept
    {
        ModCoefficients c;
        c.active = e.amount != 0.0f;
        c.amount = e.amount;
        c.attackStep = (float) (1.0 / (std::max (0.0005, (double) e.attack) * sampleRate));
        c.holdSamples = (int) (std::max (0.0f, e.hold) * sampleRate);
        c.sustain = std::clamp (e.sustain, 0.0f, 1.0f);
        c.decayStep = (float) ((1.0 - c.sustain) / (std::max (0.0005, (double) e.decay) * sampleRate)); // de 1 ao sustain
        c.releasePerLevel = (float) (1.0 / (std::max (0.002, (double) e.release) * sampleRate));
        return c;
    }

    void advance (ModState& m) const noexcept
    {
        switch (m.stage)
        {
            case 0:
                m.level += attackStep;
                if (m.level >= 1.0f) { m.level = 1.0f; m.stage = 1; }
                break;
            case 1:
                if (++m.held >= holdSamples) m.stage = 2;
                break;
            case 2:
                m.level -= decayStep;
                if (m.level <= sustain) { m.level = sustain; m.stage = 3; }
                break;
            case 3: break;
            default: m.level = std::max (0.0f, m.level - m.releaseStep); break;
        }
    }

    void release (ModState& m) const noexcept
    {
        m.stage = 4;
        m.releaseStep = m.level * releasePerLevel;
    }
};

// LFO do canal, avançado no tick de controle.
struct LfoState
{
    double phase = 0.0;
    float fade = 0.0f;
    double delayLeft = 0.0; // samples
};

struct LfoCoefficients
{
    bool active = false;
    float amount = 0.0f;
    double increment = 0.0; // ciclos por sample
    double delaySamples = 0.0;
    float fadeStep = 1.0f;   // por sample
    int shape = 0;
    bool global = false;
    double cyclesPerTick = 0.0; // global: fase = posição da música (ticks) × isto

    static LfoCoefficients make (const ChannelLfo& l, double sampleRate, double bpm) noexcept
    {
        LfoCoefficients c;
        c.active = l.amount != 0.0f;
        c.amount = l.amount;
        const auto unit = l.sync ? 60.0 / std::max (1.0, bpm) : 1.0; // tempos -> segundos
        c.increment = 1.0 / (std::max (0.005, (double) l.period * unit) * sampleRate);
        c.delaySamples = std::max (0.0, (double) l.delay * unit * sampleRate);
        c.fadeStep = l.attack > 0.0f ? (float) (1.0 / ((double) l.attack * unit * sampleRate)) : 1.0f;
        c.shape = std::clamp (l.shape, 0, 2);
        c.global = l.global;
        // período em tempos (sincronizado) ou em segundos convertidos pelo BPM
        const auto periodBeats = l.sync ? std::max (0.005, (double) l.period) : std::max (0.005, (double) l.period) * std::max (1.0, bpm) / 60.0;
        c.cyclesPerTick = 1.0 / (periodBeats * ppq);
        return c;
    }

    // `songTicks`: posição da música (LFO global começa na fase dela; delay e fade-in continuam por nota)
    void start (LfoState& s, double songTicks = 0.0) const noexcept
    {
        s = { 0.0, fadeStep >= 1.0f ? 1.0f : 0.0f, delaySamples };
        if (global)
        {
            s.phase = songTicks * cyclesPerTick;
            s.phase -= std::floor (s.phase);
        }
    }

    // avança `n` samples e devolve a saída (±1, já com o fade-in)
    float tick (LfoState& s, int n) const noexcept
    {
        if (s.delayLeft > 0.0)
        {
            s.delayLeft -= n;
            return 0.0f;
        }

        s.phase += increment * n;
        s.phase -= std::floor (s.phase);
        s.fade = std::min (1.0f, s.fade + fadeStep * (float) n);
        const auto p = s.phase;
        float value = 0.0f;

        switch (shape)
        {
            case 1: value = (float) (p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0); break;
            case 2: value = p < 0.5 ? 1.0f : -1.0f; break;
            default: value = (float) std::sin (6.283185307179586 * p); break;
        }

        return value * s.fade;
    }
};

// Coeficientes do passa-baixas SVF "TPT" (estável em qualquer corte), compartilhado pelo synth e o sampler.
// O mesmo filtro dá as quatro respostas (mode: 0 passa-baixas, 1 passa-banda, 2 passa-altas,
// 3 rejeita-faixa), como os tipos LP/BP/HP/BS do filtro do canal do FL.
struct SvfCoefficients
{
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 2.0f;
    int mode = 0;

    void set (double cutoffHz, float resonance, double sampleRate, int filterMode = 0) noexcept
    {
        const auto fc = std::clamp (cutoffHz, 20.0, sampleRate * 0.45);
        const auto g = (float) std::tan (3.14159265358979 * fc / sampleRate);
        k = 2.0f - 1.9f * std::clamp (resonance, 0.0f, 1.0f); // 2 = sem ressonância
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        mode = std::clamp (filterMode, 0, 3);
    }

    float process (float in, float& low, float& band) const noexcept
    {
        const auto v3 = in - low;
        const auto v1 = a1 * band + a2 * v3;
        const auto v2 = low + a2 * band + a3 * v3;
        band = 2.0f * v1 - band;
        low = 2.0f * v2 - low;

        switch (mode)
        {
            case 1: return v1;
            case 2: return in - k * v1 - v2;
            case 3: return in - k * v1;
            default: return v2;
        }
    }
};

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
    float lowR = 0.0f, bandR = 0.0f; // filtro do lado direito (só com pan por oscilador)

    std::array<ModState, 2> mod {}; // filtro, afinação
    std::array<LfoState, lfoTargets> lfo {};
    float lfoGain = 1.0f, lfoGainStep = 0.0f; // LFO de volume, em rampa entre ticks
    float basePan = 0.0f;                     // pan da nota (o LFO de pan soma a ele)
    SvfCoefficients filter; // desta voz (só com envelope de corte)
    double pitchMul = 1.0;
    float fmLevel = 1.0f;   // envelope do índice de FM (1 -> fmSustain)
    int controlCountdown = 0;
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
        lastPhaseDelta = 0.0;
        updateCoefficients();
        allNotesOff();
    }

    void configure (const ChannelDef& channel, double songBpm) noexcept override
    {
        bpm.store (std::max (1.0, songBpm)); // antes: o LFO sincronizado usa ao ler os ajustes
        mailbox.write (channel.synth);
    }

    void startModulation (SynthVoice& v, float pan) noexcept
    {
        v.basePan = pan;
        v.lfoGain = 1.0f;
        v.lfoGainStep = 0.0f;
        v.controlCountdown = 0;

        for (std::size_t i = 0; i < lfoTargets; ++i)
            if (lfos[i].active)
                lfos[i].start (v.lfo[i], songTicks);
    }

    void setSongPosition (double ticks) noexcept override { songTicks = ticks; }

    // portamento: a voz parte de `from` e desliza até `to` no tempo de porta (porta do canal,
    // invertido pela nota)
    void glide (SynthVoice& v, double from, double to) noexcept
    {
        const auto steps = (int) (settings.portaBeats * 60.0 / bpm.load() * sampleRate);
        const auto on = settings.porta != nextPorta;

        if (! on || settings.portaBeats <= 0.0f || from <= 0.0 || steps < 1)
        {
            v.phaseDelta = to;
            v.glideLeft = 0;
            return;
        }

        v.phaseDelta = from;
        v.glideRatio = std::pow (to / from, 1.0 / steps); // reta em semitons, como o FL
        v.glideLeft = steps;
    }

    void noteOn (int key, float velocity) noexcept override
    {
        if (mailbox.read (settings))
            updateCoefficients();

        if (settings.mono)
        {
            // canal mono (polifonia 1 no FL): a voz que soa troca de nota sem zerar fase nem filtro;
            // os envelopes recomeçam do nível atual
            SynthVoice* sounding = nullptr;

            for (auto& v : voices)
                if (v.active && (sounding == nullptr || v.age < sounding->age))
                    sounding = &v;

            if (sounding != nullptr)
            {
                for (auto& v : voices)
                    if (&v != sounding)
                        v.active = false;

                auto& v = *sounding;
                v.key = key;
                v.velocity = velocity;
                v.releasing = false;
                v.stage = 0;
                v.age = 0;
                const auto target = midiToHz (key) / sampleRate * std::pow (2.0, nextCents / 1200.0);
                glide (v, v.phaseDelta, target);
                lastPhaseDelta = target;
                for (auto& m : v.mod)
                    m = { m.level, 0.0f, 0, 0 };
                v.fmLevel = 1.0f;
                voicePan (nextPan, v.panL, v.panR);
                startModulation (v, nextPan);
                nextPan = nextCents = 0.0f;
        nextPorta = false;
                return;
            }
        }

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
        const auto delta = midiToHz (key) / sampleRate * std::pow (2.0, nextCents / 1200.0);
        glide (*target, lastPhaseDelta, delta); // com porta, parte da altura da nota anterior
        lastPhaseDelta = delta;
        voicePan (nextPan, target->panL, target->panR);
        startModulation (*target, nextPan);
        nextPan = nextCents = 0.0f;
        nextPorta = false;
    }

    void setNextNoteExpression (float pan, float cents, bool porta) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
        nextPorta = porta;
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
                for (std::size_t e = 0; e < 2; ++e)
                    modEnv[e].release (v.mod[e]);
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

                if (modulated)
                {
                    for (std::size_t e = 0; e < 2; ++e)
                        if (modEnv[e].active)
                            modEnv[e].advance (v.mod[e]);

                    // a cada 16 samples (o "tick" de controle): afinação, filtro, volume e pan
                    if (--v.controlCountdown <= 0)
                    {
                        v.controlCountdown = 16;
                        auto semitones = modEnv[1].active ? modEnv[1].amount * v.mod[1].level : 0.0f;
                        auto octaves = modEnv[0].active ? modEnv[0].amount * v.mod[0].level : 0.0f;

                        if (lfos[lfoPitch].active)
                            semitones += lfos[lfoPitch].amount * lfos[lfoPitch].tick (v.lfo[lfoPitch], 16);
                        if (lfos[lfoCutoff].active)
                            octaves += lfos[lfoCutoff].amount * lfos[lfoCutoff].tick (v.lfo[lfoCutoff], 16);
                        if (lfos[lfoVolume].active)
                        {
                            const auto target = std::max (0.0f, 1.0f + lfos[lfoVolume].amount * lfos[lfoVolume].tick (v.lfo[lfoVolume], 16));
                            v.lfoGainStep = (target - v.lfoGain) / 16.0f;
                        }
                        if (lfos[lfoPan].active)
                            voicePan (v.basePan + lfos[lfoPan].amount * lfos[lfoPan].tick (v.lfo[lfoPan], 16), v.panL, v.panR);

                        v.pitchMul = semitones != 0.0f ? std::exp2 (semitones / 12.0) : 1.0;

                        if (filterModulated)
                            v.filter.set (settings.cutoff * std::exp2 (octaves), settings.resonance, sampleRate, settings.filterType);
                    }

                    v.lfoGain += v.lfoGainStep;
                }

                float sum = 0.0f, sumR = 0.0f, modulator = 0.0f;
                double fmShift = 0.0;

                // AM/FM: o osc 3 não soma, modula (primeiro, para a FM deslocar a fase de osc 1 + 2)
                if (osc3Modulates)
                {
                    const auto& osc = settings.osc[2];
                    if (osc.level > 0.0f)
                    {
                        const auto delta = std::min (0.49, v.phaseDelta * ratios[2] * v.pitchMul);
                        modulator = osc.level * oscillator (osc.wave, v.phase[2], delta);
                        v.phase[2] += delta;
                        if (v.phase[2] >= 1.0)
                            v.phase[2] -= 1.0;
                    }
                    if (settings.fm > 0.0f)
                    {
                        v.fmLevel = settings.fmSustain + (v.fmLevel - settings.fmSustain) * fmDecayCoeff;
                        fmShift = (double) (settings.fm * v.fmLevel * modulator);
                    }
                }

                for (std::size_t o = 0; o < (osc3Modulates ? 2u : 3u); ++o)
                {
                    const auto& osc = settings.osc[o];

                    if (osc.level <= 0.0f)
                        continue;

                    const auto delta = std::min (0.49, v.phaseDelta * ratios[o] * v.pitchMul);
                    auto phase = v.phase[o];
                    if (fmShift != 0.0)
                    {
                        phase += fmShift;
                        phase -= std::floor (phase);
                    }
                    const auto value = osc.level * oscillator (osc.wave, phase, delta);
                    sum += value * oscPanL[o];
                    sumR += value * oscPanR[o];
                    v.phase[o] += delta;
                    if (v.phase[o] >= 1.0) // delta < 0,5: no máximo uma volta (sem floor, que é chamada de função em CPUs sem SSE4)
                        v.phase[o] -= 1.0;
                }

                if (settings.am) // (o1·g1 + o2·g2)·(1 − g3·o3), REA_rodada6 §3
                {
                    sum *= 1.0f - modulator;
                    sumR *= 1.0f - modulator;
                }

                // filtro passa-baixas ressonante (state variable "TPT": estável em qualquer corte)
                const auto& f = filterModulated ? v.filter : sharedFilter;
                const auto outL = f.process (sum, v.low, v.band);
                const auto outR = stereoOscillators ? f.process (sumR, v.lowR, v.bandR) : outL;

                const auto s = 0.3f * v.level * v.velocity * v.lfoGain;
                left[i] += outL * s * gl * v.panL;
                right[i] += outR * s * gr * v.panR;
                ++v.age;
            }

            if (v.stage == 3 && v.level < 0.0005f)
                v.active = false;
        }
    }

private:
    // seno de uma fase 0..1 por parábola refinada (erro < 0,1%), bem mais barato que std::sin
    static float fastSine (double phase) noexcept
    {
        auto x = (float) (phase < 0.5 ? phase : phase - 1.0) * 2.0f; // -1..1 (meia volta = 1)
        const auto y = 4.0f * x * (1.0f - std::abs (x));            // parábola
        return 0.225f * (y * std::abs (y) - y) + y;                  // refinamento
    }

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
            case Waveform::sine: return fastSine (phase);
            case Waveform::triangle: return (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
            case Waveform::saw: return (float) (2.0 * phase - 1.0) - polyBlep (phase, dt);
            case Waveform::square:
            {
                auto s = phase < 0.5 ? 1.0f : -1.0f;
                s += polyBlep (phase, dt);
                s -= polyBlep (phase < 0.5 ? phase + 0.5 : phase - 0.5, dt);
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
        {
            const auto& osc = settings.osc[o];
            ratios[o] = std::pow (2.0, osc.octave + (osc.semitones * 100.0 + osc.detune) / 1200.0);
            const auto p = std::clamp (osc.pan, -1.0f, 1.0f);
            oscPanL[o] = p > 0.0f ? 1.0f - p : 1.0f;
            oscPanR[o] = p < 0.0f ? 1.0f + p : 1.0f;
        }

        stereoOscillators = std::any_of (settings.osc.begin(), settings.osc.end(), [] (const SynthOscillator& o) { return o.pan != 0.0f; });
        osc3Modulates = settings.am || settings.fm > 0.0f;
        fmDecayCoeff = settings.fmDecay > 0.0f ? decayCoefficient (std::max (0.001, (double) settings.fmDecay), sampleRate) : 1.0f;

        sharedFilter.set (settings.cutoff, settings.resonance, sampleRate, settings.filterType);
        modEnv[0] = ModCoefficients::make (settings.filterEnv, sampleRate);
        modEnv[1] = ModCoefficients::make (settings.pitchEnv, sampleRate);
        auto anyLfo = false;

        for (std::size_t i = 0; i < lfoTargets; ++i)
        {
            lfos[i] = LfoCoefficients::make (settings.lfo[i], sampleRate, bpm.load());
            anyLfo = anyLfo || lfos[i].active;
        }

        filterModulated = modEnv[0].active || lfos[lfoCutoff].active;
        modulated = modEnv[0].active || modEnv[1].active || anyLfo;
    }

    double sampleRate = 44100.0;
    SettingsMailbox<SynthSettings> mailbox;
    SynthSettings settings;
    float attackStep = 0.01f;
    float decayCoeff = 0.999f;
    float releaseCoeff = 0.999f;
    SvfCoefficients sharedFilter; // sem envelope de corte, todas as vozes usam este
    std::array<ModCoefficients, 2> modEnv {};
    std::array<LfoCoefficients, lfoTargets> lfos {};
    bool modulated = false, filterModulated = false;
    std::array<double, 3> ratios { 1.0, 1.0, 1.0 };
    std::atomic<double> bpm { 120.0 };
    double lastPhaseDelta = 0.0; // altura da última nota (de onde o porta parte)
    double songTicks = 0.0;      // posição da música no bloco (LFO global)
    std::array<float, 3> oscPanL { 1.0f, 1.0f, 1.0f }, oscPanR { 1.0f, 1.0f, 1.0f };
    bool stereoOscillators = false;
    bool osc3Modulates = false; // AM ou FM: o osc 3 não soma
    float fmDecayCoeff = 1.0f;
    float nextPan = 0.0f, nextCents = 0.0f;
    bool nextPorta = false;
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
    float lowL = 0.0f, bandL = 0.0f, lowR = 0.0f, bandR = 0.0f; // filtro do canal
    std::array<ModState, 2> mod {}; // envelopes de corte e de afinação
    std::array<LfoState, lfoTargets> lfo {};
    float lfoGain = 1.0f, lfoGainStep = 0.0f;
    float basePan = 0.0f;
    const SampleData* data = nullptr; // o sample desta voz (a zona escolhida, no multi-sample)
    float zoneGain = 1.0f;
    double loopStart = 0.0, loopEnd = 0.0; // loopEnd > loopStart: a voz volta ao início do loop
    bool loopWhileHeld = false;
    SvfCoefficients filter;         // desta voz (só com envelope de corte)
    double pitchMul = 1.0;
    int controlCountdown = 0;
};

class SamplerInstrument final : public Instrument
{
public:
    // clipMode: clip de áudio da Playlist (sem transposição, para no noteOff)
    SamplerInstrument (std::shared_ptr<const SampleData> s, bool clip) : sample (std::move (s)), clipMode (clip) {}

    // multi-sample: chamado antes de o instrumento ir para a thread de áudio
    void setZones (std::vector<LoadedZone> z)
    {
        zones = std::move (z);
        zones.erase (std::remove_if (zones.begin(), zones.end(), [] (const LoadedZone& lz) { return lz.sample == nullptr || lz.sample->frames() < 2; }),
                     zones.end());

        if (sample == nullptr && ! zones.empty())
            sample = zones.front().sample;
    }

    // a zona da tecla/velocity; várias cobrindo a mesma nota se revezam (round-robin, como as
    // camadas em ciclo do FPC); sem nenhuma que cubra, a de nota-raiz mais próxima
    const LoadedZone* zoneFor (int key, float velocity) noexcept
    {
        if (zones.empty())
            return nullptr;

        const auto vel = (int) std::lround (std::clamp (velocity, 0.0f, 1.0f) * 127.0f);
        const LoadedZone* nearest = nullptr;
        const LoadedZone* matches[16] {};
        int matchCount = 0;

        for (const auto& z : zones)
        {
            if (key >= z.zone.lowKey && key <= z.zone.highKey && vel >= z.zone.lowVelocity && vel <= z.zone.highVelocity && matchCount < 16)
                matches[matchCount++] = &z;

            if (nearest == nullptr || std::abs (z.zone.rootKey - key) < std::abs (nearest->zone.rootKey - key))
                nearest = &z;
        }

        if (matchCount == 0)
            return nearest;

        return matches[(roundRobin++) % (unsigned) matchCount];
    }

    void prepare (double sr) noexcept override
    {
        sampleRate = sr;
        fadeStep = (float) (1.0 / (0.003 * sr)); // 3 ms
        updateModulation();
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
        porta.store (s.porta);
        portaBeats.store (std::max (0.0f, s.portaBeats));
        cutoff.store (std::clamp (s.cutoff, 20.0f, 20000.0f));
        resonance.store (std::clamp (s.resonance, 0.0f, 1.0f));
        filterType.store (std::clamp (s.filterType, 0, 3));
        bpm.store (std::max (1.0, songBpm)); // antes do mailbox: o LFO sincronizado lê ao atualizar
        envelopeMailbox.write ({ { s.filterEnv, s.pitchEnv }, s.lfo });
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
    double baseRate (const SampleData& data) const noexcept
    {
        const auto beats = stretchBeats.load();

        if (beats > 0.0f)
            return (double) data.frames() / ((double) beats * 60.0 / bpm.load() * sampleRate);

        return data.sampleRate / sampleRate;
    }

    void noteOn (int key, float velocity) noexcept override { noteOnAt (key, velocity, 0.0); }

    void noteOnAt (int key, float velocity, double startSeconds) noexcept override
    {
        const auto* zone = clipMode ? nullptr : zoneFor (key, velocity);
        const auto* data = zone != nullptr ? zone->sample.get() : sample.get();

        if (data == nullptr || data->frames() < 2)
            return;

        sliceMailbox.read (slices);
        const auto backwards = ! clipMode && reverse.load();
        const auto total = (double) (data->frames() - 1);
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
        const auto base = baseRate (*data);
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
        const auto root = zone != nullptr ? zone->zone.rootKey : 60;
        const auto tune = zone != nullptr ? (double) zone->zone.tune : 0.0;
        const auto semitones = clipMode || stretched ? 0.0 : (sliced ? 0 : key - root) + tune + (double) pitch.load() + nextCents / 100.0;
        const auto attackSeconds = clipMode ? 0.0f : attack.load();

        *target = {};
        target->active = true;
        target->key = key;
        target->velocity = velocity;
        target->data = data;
        target->zoneGain = zone != nullptr ? zone->zone.gain : 1.0f;

        // loop da zona (DirectWave): só tocando para frente e com o loop dentro do trecho
        if (zone != nullptr && zone->zone.loopMode != 0 && ! backwards && zone->zone.loopEnd > zone->zone.loopStart + 16
            && (double) zone->zone.loopEnd <= last && (double) zone->zone.loopStart >= first)
        {
            target->loopStart = (double) zone->zone.loopStart;
            target->loopEnd = (double) zone->zone.loopEnd;
            target->loopWhileHeld = zone->zone.loopMode == 2;
        }
        target->position = startFrame;
        target->first = first;
        target->last = last;
        target->clipElapsed = nextClip[0];
        target->clipRemaining = nextClip[1];
        target->fadeIn = nextClip[2];
        target->fadeOut = nextClip[3];
        nextClip = {};
        target->rate = base * std::pow (2.0, semitones / 12.0) * (backwards ? -1.0 : 1.0);

        // portamento (porta do canal, invertido pela nota): parte da altura da nota anterior
        if (! clipMode && (porta.load() != nextPorta) && lastRate != 0.0 && ! stretched)
        {
            const auto steps = (int) (portaBeats.load() * 60.0 / bpm.load() * sampleRate);
            const auto from = std::abs (lastRate) * (target->rate < 0.0 ? -1.0 : 1.0);

            if (steps >= 1 && from != 0.0)
            {
                target->glideRatio = std::pow (target->rate / from, 1.0 / steps);
                target->glideLeft = steps;
                target->rate = from;
            }
        }

        if (! clipMode)
            lastRate = target->glideLeft > 0 ? target->rate * std::pow (target->glideRatio, target->glideLeft) : target->rate;
        target->env = attackSeconds > 0.0f ? 0.0f : 1.0f;
        target->envStep = attackSeconds > 0.0f ? (float) (1.0 / (attackSeconds * sampleRate)) : 0.0f;
        target->stage = attackSeconds > 0.0f ? 0 : 1;
        voicePan (clipMode ? 0.0f : nextPan, target->panL, target->panR);
        target->basePan = clipMode ? 0.0f : nextPan;

        for (std::size_t i = 0; i < lfoTargets; ++i)
            if (lfos[i].active && ! clipMode)
                lfos[i].start (target->lfo[i], songTicks);
        nextPan = nextCents = 0.0f;
        nextPorta = false;
    }

    void setNextNoteExpression (float pan, float cents, bool porta) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
        nextPorta = porta;
    }

    void setNextClipFades (float elapsed, float remaining, float in, float out) noexcept override
    {
        nextClip = { elapsed, remaining, in, out };
    }

    void slideTo (int key, float cents, float samples) noexcept override
    {
        if (clipMode)
            return;

        const auto semitones = (key - 60) + (double) pitch.load() + cents / 100.0;
        const auto steps = std::max (1, (int) samples);

        for (auto& v : voices)
            if (v.active && ! v.stolen && v.rate != 0.0 && v.data != nullptr)
            {
                const auto target = v.data->sampleRate / sampleRate * std::pow (2.0, semitones / 12.0) * (v.rate < 0.0 ? -1.0 : 1.0);
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

        // os envelopes de corte/afinação soltam sempre
        for (auto& v : voices)
            if (v.active && v.key == key && v.stage < 3)
                for (std::size_t e = 0; e < 2; ++e)
                    modEnv[e].release (v.mod[e]);

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
        if (sample == nullptr && zones.empty())
            return;

        if (envelopeMailbox.read (modulation))
            updateModulation();

        // filtro do canal (SVF TPT, o mesmo do synth); desligado quando aberto, sem ressonância e sem modulação
        const auto fc = cutoff.load(), res = resonance.load();
        const auto type = filterType.load();
        const auto filterModulated = modEnv[0].active || lfos[lfoCutoff].active;
        const auto filtered = fc < 19500.0f || res > 0.0f || type != 0 || filterModulated;
        const auto modulated = modEnv[0].active || modEnv[1].active || anyLfo;

        if (filtered && (fc != filterCutoff || res != filterResonance || type != sharedFilter.mode))
        {
            filterCutoff = fc;
            filterResonance = res;
            sharedFilter.set (fc, res, sampleRate, type);
        }

        for (auto& v : voices)
        {
            if (! v.active || v.data == nullptr)
                continue;

            const auto* l = v.data->leftData();
            const auto* r = v.data->rightData();
            const auto last = (double) (v.data->frames() - 1);

            for (int i = 0; i < n; ++i)
            {
                // loop: volta ao começo dele (o "enquanto pressionada" para de voltar no release)
                if (v.loopEnd > v.loopStart && v.position >= v.loopEnd && (! v.loopWhileHeld || v.stage < 3))
                    v.position -= v.loopEnd - v.loopStart;

                if (v.position >= std::min (last, v.last) || v.position < v.first || (v.stolen && v.fade <= 0.0f) || (v.env <= 0.0f && v.stage >= 1))
                {
                    v.active = false;
                    break;
                }

                const auto index = (std::size_t) v.position;
                const auto frac = (float) (v.position - (double) index);
                auto sl = l[index] + frac * (l[index + 1] - l[index]);
                auto sr = r[index] + frac * (r[index + 1] - r[index]);

                if (modulated)
                {
                    for (std::size_t e = 0; e < 2; ++e)
                        if (modEnv[e].active)
                            modEnv[e].advance (v.mod[e]);

                    if (--v.controlCountdown <= 0) // tick de controle de 16 samples, como no synth
                    {
                        v.controlCountdown = 16;
                        auto semitones = modEnv[1].active ? modEnv[1].amount * v.mod[1].level : 0.0f;
                        auto octaves = modEnv[0].active ? modEnv[0].amount * v.mod[0].level : 0.0f;

                        if (lfos[lfoPitch].active)
                            semitones += lfos[lfoPitch].amount * lfos[lfoPitch].tick (v.lfo[lfoPitch], 16);
                        if (lfos[lfoCutoff].active)
                            octaves += lfos[lfoCutoff].amount * lfos[lfoCutoff].tick (v.lfo[lfoCutoff], 16);
                        if (lfos[lfoVolume].active)
                        {
                            const auto target = std::max (0.0f, 1.0f + lfos[lfoVolume].amount * lfos[lfoVolume].tick (v.lfo[lfoVolume], 16));
                            v.lfoGainStep = (target - v.lfoGain) / 16.0f;
                        }
                        if (lfos[lfoPan].active)
                            voicePan (v.basePan + lfos[lfoPan].amount * lfos[lfoPan].tick (v.lfo[lfoPan], 16), v.panL, v.panR);

                        v.pitchMul = semitones != 0.0f ? std::exp2 (semitones / 12.0) : 1.0;

                        if (filterModulated)
                            v.filter.set (fc * std::exp2 (octaves), res, sampleRate, type);
                    }

                    v.lfoGain += v.lfoGainStep;
                }

                if (filtered)
                {
                    const auto& f = filterModulated ? v.filter : sharedFilter;
                    sl = f.process (sl, v.lowL, v.bandL);
                    sr = f.process (sr, v.lowR, v.bandR);
                }

                auto gain = v.velocity * v.fade * v.env * v.lfoGain * v.zoneGain;

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

                v.position += v.rate * v.pitchMul;
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
    std::atomic<float> cutoff { 20000.0f }, resonance { 0.0f };
    std::atomic<bool> porta { false };
    std::atomic<float> portaBeats { 0.125f };
    std::vector<LoadedZone> zones; // multi-sample (fixo depois de criado)
    double songTicks = 0.0;         // posição da música no bloco (LFO global)

public:
    void setSongPosition (double ticks) noexcept override { songTicks = ticks; }

private:
    unsigned roundRobin = 0;        // vez da próxima camada sobreposta
    double lastRate = 0.0; // velocidade de leitura da última nota (de onde o porta parte)
    std::atomic<int> filterType { 0 };
    float filterCutoff = -1.0f, filterResonance = -1.0f; // valores com que os coeficientes foram feitos
    SvfCoefficients sharedFilter;
    struct Modulation
    {
        std::array<ModEnvelope, 2> envelopes {};
        std::array<ChannelLfo, lfoTargets> lfo {};
    };
    SettingsMailbox<Modulation> envelopeMailbox;
    Modulation modulation;
    std::array<LfoCoefficients, lfoTargets> lfos {};
    bool anyLfo = false;

    void updateModulation() noexcept
    {
        anyLfo = false;

        for (std::size_t e = 0; e < 2; ++e)
            modEnv[e] = ModCoefficients::make (modulation.envelopes[e], sampleRate);

        for (std::size_t i = 0; i < lfoTargets; ++i)
        {
            lfos[i] = LfoCoefficients::make (modulation.lfo[i], sampleRate, bpm.load());
            anyLfo = anyLfo || lfos[i].active;
        }
    }
    std::array<ModCoefficients, 2> modEnv {};
    std::array<float, 4> nextClip {}; // fades do próximo noteOn (só a thread de áudio mexe)

    struct SliceTable
    {
        std::array<float, 128> starts {};
        int count = 0;
    };
    SettingsMailbox<SliceTable> sliceMailbox;
    SliceTable slices; // cópia da thread de áudio
    float nextPan = 0.0f, nextCents = 0.0f;
    bool nextPorta = false;
    std::atomic<bool> reverse { false }, cut { false };
};
} // namespace

std::unique_ptr<Instrument> makeZonedSampler (std::vector<LoadedZone> zones)
{
    auto sampler = std::make_unique<SamplerInstrument> (nullptr, false);
    sampler->setZones (std::move (zones));
    return sampler;
}

namespace
{
// Envelope Controller do FL: sem áudio; cada articulador segue a última nota da sua faixa de teclas e
// a saída fica no último valor quando não há nota.
class ControllerInstrument final : public Instrument
{
public:
    void prepare (double sr) noexcept override { sampleRate = sr; }

    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        Data data {};
        if (auto keyboard = parseKeyboardController (channel.controllerState))
        {
            data.keyboard = true;
            data.keys = *keyboard;
        }
        else if (auto parsed = parseEnvelopeController (channel.controllerState))
        {
            data.articulators = *parsed;
            const auto beat = 60.0 / std::max (1.0, bpm);
            for (auto& a : data.articulators)
                if (a.tempo)
                {
                    for (int i = 0; i < a.count; ++i)
                        a.time[(std::size_t) i] = (float) (a.time[(std::size_t) i] * beat);
                    a.tempo = false;
                }
        }
        mailbox.write (data);
    }

    void noteOn (int key, float) noexcept override
    {
        mailbox.read (data);
        if (data.keyboard) // Keyboard Controller: a saída 0 é o valor da tecla
        {
            heldKey = key;
            if (key >= 0 && key < (int) data.keys.keys.size())
                values[0] = data.keys.keys[(std::size_t) key];
            return;
        }
        for (std::size_t k = 0; k < voices.size(); ++k)
        {
            const auto& a = data.articulators[k];
            if (key >= a.lowKey && key <= a.highKey)
                voices[k] = { true, key, 0.0, -1.0 };
        }
    }

    void noteOff (int key) noexcept override
    {
        if (data.keyboard)
        {
            if (key == heldKey && data.keys.autoRelease)
                values[0] = data.keys.noteOffValue;
            return;
        }
        for (auto& v : voices)
            if (v.active && v.key == key && v.sinceRelease < 0.0)
                v.sinceRelease = 0.0;
    }

    void allNotesOff() noexcept override
    {
        for (auto& v : voices)
            if (v.active && v.sinceRelease < 0.0)
                v.sinceRelease = 0.0;
    }

    void render (float*, float*, int n, float, float) noexcept override
    {
        if (mailbox.read (data) && ! data.keyboard)
            for (std::size_t k = 0; k < values.size(); ++k)
                if (! voices[k].active)
                    values[k] = data.articulators[k].base;
        if (data.keyboard)
            return;
        const auto seconds = n / sampleRate;
        for (std::size_t k = 0; k < voices.size(); ++k)
        {
            auto& v = voices[k];
            if (! v.active)
                continue;
            v.sinceOn += seconds;
            if (v.sinceRelease >= 0.0)
                v.sinceRelease += seconds;
            values[k] = controllerValue (data.articulators[k], v.sinceOn, v.sinceRelease);
        }
    }

    float controlOutput (int output) const noexcept override
    {
        return output >= 0 && output < (int) values.size() ? values[(std::size_t) output] : 0.0f;
    }

private:
    struct Data
    {
        EnvelopeControllerData articulators {};
        bool keyboard = false;
        KeyboardControllerData keys {};
    };
    int heldKey = -1;
    struct Voice
    {
        bool active = false;
        int key = 0;
        double sinceOn = 0.0, sinceRelease = -1.0;
    };

    SettingsMailbox<Data> mailbox;
    Data data {};
    std::array<Voice, 8> voices {};
    std::array<float, 8> values { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    double sampleRate = 44100.0;
};
} // namespace

std::unique_ptr<Instrument> makeInstrument (InstrumentKind kind, std::shared_ptr<const SampleData> sample)
{
    if (kind == InstrumentKind::controller)
        return std::make_unique<ControllerInstrument>();

    if (kind == InstrumentKind::sampler || kind == InstrumentKind::audioClip)
        return std::make_unique<SamplerInstrument> (std::move (sample), kind == InstrumentKind::audioClip);

    if (kind == InstrumentKind::synth)
        return std::make_unique<SynthInstrument>();

    return std::make_unique<DrumInstrument> (kind);
}
} // namespace lynx
