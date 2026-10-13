#include "core/RackRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lynx
{
namespace
{
// Instrumento mudo: canal de plugin que não pôde ser carregado.
class SilentInstrument final : public Instrument
{
public:
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
};

// Efeito que não faz nada: slot de plugin ausente.
class BypassEffect final : public Effect
{
public:
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float*, float*, int, const EffectSlot&, double) noexcept override {}
};
} // namespace

std::unique_ptr<const RackState> RackState::build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const SampleProvider& samples)
{
    Factories factories;
    factories.samples = samples;
    return build (std::move (song), previous, sampleRate, factories);
}

std::unique_ptr<const RackState> RackState::build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const Factories& factories)
{
    const auto& samples = factories.samples;

    auto state = std::make_unique<RackState>();
    state->instruments.reserve (song.channels.size());

    for (std::size_t i = 0; i < song.channels.size(); ++i)
    {
        const auto& channel = song.channels[i];
        const auto kind = channel.instrument;

        if (previous != nullptr && i < previous->song.channels.size() && i < previous->instruments.size()
            && previous->song.channels[i].instrument == kind
            && ((kind != InstrumentKind::sampler && kind != InstrumentKind::audioClip)
                || previous->song.channels[i].samplePath == channel.samplePath)
            && (kind != InstrumentKind::plugin
                || (previous->song.channels[i].plugin.id == channel.plugin.id && previous->sampleRate == sampleRate)))
        {
            state->instruments.push_back (previous->instruments[i]);
            continue;
        }

        if (kind == InstrumentKind::plugin)
        {
            std::shared_ptr<Instrument> instrument;

            if (factories.pluginInstruments && ! channel.plugin.id.empty())
                instrument = factories.pluginInstruments (channel.plugin, sampleRate);

            state->instruments.push_back (instrument != nullptr ? instrument : std::make_shared<SilentInstrument>());
            continue;
        }

        std::shared_ptr<const SampleData> sample;

        if ((kind == InstrumentKind::sampler || kind == InstrumentKind::audioClip) && samples && ! channel.samplePath.empty())
            sample = samples (channel.samplePath);

        std::shared_ptr<Instrument> instrument = makeInstrument (kind, std::move (sample));
        instrument->prepare (sampleRate);
        state->instruments.push_back (std::move (instrument));
    }

    state->effects.resize (song.mixer.size());

    for (std::size_t t = 0; t < song.mixer.size(); ++t)
    {
        const auto& slots = song.mixer[t].effects;

        for (std::size_t i = 0; i < slots.size() && i < (std::size_t) maxEffectSlots; ++i)
        {
            const auto kind = slots[i].kind;

            if (previous != nullptr && t < previous->song.mixer.size() && t < previous->effects.size()
                && i < previous->song.mixer[t].effects.size() && i < previous->effects[t].size()
                && previous->song.mixer[t].effects[i].kind == kind
                && (kind != EffectKind::plugin
                    || (previous->song.mixer[t].effects[i].plugin.id == slots[i].plugin.id && previous->sampleRate == sampleRate)))
            {
                state->effects[t].push_back (previous->effects[t][i]);
                continue;
            }

            std::shared_ptr<Effect> effect;

            if (kind == EffectKind::plugin)
            {
                if (factories.pluginEffects && ! slots[i].plugin.id.empty())
                    effect = factories.pluginEffects (slots[i].plugin, sampleRate);

                if (effect == nullptr)
                    effect = std::make_shared<BypassEffect>();
            }
            else
            {
                effect = makeEffect (kind);
                effect->prepare (sampleRate);
            }

            state->effects[t].push_back (std::move (effect));
        }
    }

    state->song = std::move (song);
    state->sampleRate = sampleRate;
    return state;
}

namespace
{
// Pan de potência constante: no centro os dois lados ficam em 1 (0 dB).
void panGains (float pan, float gain, float& left, float& right) noexcept
{
    const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
    left = gain * std::cos (angle) * 1.41421356f;
    right = gain * std::sin (angle) * 1.41421356f;
}

float peakOf (const float* data, int n) noexcept
{
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::abs (data[i]));

    return peak;
}
} // namespace

void RackRenderer::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    blockCapacity = std::max (64, maxBlockSize);
    mixBuffers.assign ((std::size_t) maxMixerTracks * 2 * (std::size_t) blockCapacity, 0.0f);

    if (lastState != nullptr)
    {
        for (auto& instrument : lastState->instruments)
            instrument->prepare (sr);

        for (auto& track : lastState->effects)
            for (auto& effect : track)
                effect->prepare (sr);
    }
}

float* RackRenderer::trackBuffer (int track, int channel) noexcept
{
    return mixBuffers.data() + ((std::size_t) track * 2 + (std::size_t) channel) * (std::size_t) blockCapacity;
}

void RackRenderer::renderSegment (const RackState& state, int offset, int n) noexcept
{
    const auto& channels = state.song.channels;
    const auto tracks = std::max<int> (1, (int) state.song.mixer.size());

    for (std::size_t i = 0; i < state.instruments.size() && i < channels.size(); ++i)
    {
        const auto& c = channels[i];
        const auto track = std::clamp (c.mixerTrack, 0, tracks - 1);
        float gl = 0.0f, gr = 0.0f;
        panGains (c.pan, c.volume, gl, gr);
        state.instruments[i]->render (trackBuffer (track, 0) + offset, trackBuffer (track, 1) + offset, n, gl, gr);
    }
}

void RackRenderer::runEffects (const RackState& state, int track, int n) noexcept
{
    if (track >= (int) state.effects.size())
        return;

    const auto& slots = state.song.mixer[(std::size_t) track].effects;
    const auto& effects = state.effects[(std::size_t) track];

    for (std::size_t i = 0; i < effects.size() && i < slots.size(); ++i)
        if (slots[i].enabled)
            effects[i]->process (trackBuffer (track, 0), trackBuffer (track, 1), n, slots[i], state.song.bpm);
}

void RackRenderer::process (const RackState* state,
                            bool playing,
                            double startTick,
                            int numSamples,
                            float* left,
                            float* right,
                            LiveEventQueue* live,
                            MeterBank* meters,
                            LiveEventQueue* moreLive) noexcept
{
    std::memset (left, 0, sizeof (float) * (std::size_t) numSamples);
    std::memset (right, 0, sizeof (float) * (std::size_t) numSamples);
    events.clear();

    if (state == nullptr || blockCapacity == 0)
        return;

    // Um snapshot montado antes de o dispositivo abrir pode ter outra taxa. prepare()
    // dos instrumentos não aloca, então pode rodar aqui.
    if (state != lastState && state->sampleRate != sampleRate)
    {
        for (auto& instrument : state->instruments)
            instrument->prepare (sampleRate);

        for (auto& track : state->effects)
            for (auto& effect : track)
                effect->prepare (sampleRate);
    }

    lastState = state; // para prepare() saber quais instrumentos reconfigurar

    // Parar, ou pular para outra posição (seek/rewind), solta as notas presas:
    // os "off" delas não vão mais acontecer.
    const auto jumped = playing && wasPlaying && std::abs (startTick - expectedTick) > 1.0;

    if ((wasPlaying && ! playing) || jumped)
        for (auto& instrument : state->instruments)
            instrument->allNotesOff();

    // começou a tocar ou pulou: clips de áudio em andamento precisam começar no meio
    resumeAudio = playing && (! wasPlaying || jumped);
    wasPlaying = playing;
    const auto tps = ticksPerSample (state->song.bpm, sampleRate);
    expectedTick = startTick + numSamples * tps;

    for (auto* queue : { live, moreLive })
    {
        if (queue == nullptr)
            continue;

        NoteEvent e;

        while (queue->pop (e))
        {
            if (e.channel < 0 || e.channel >= (int) state->instruments.size())
                continue;

            auto& instrument = *state->instruments[(std::size_t) e.channel];

            if (e.type == NoteEvent::Type::on)
                instrument.noteOn (e.key, e.velocity);
            else
                instrument.noteOff (e.key);
        }
    }

    // Blocos maiores que os buffers do mixer são feitos em pedaços.
    for (int done = 0; done < numSamples; done += blockCapacity)
    {
        const auto n = std::min (blockCapacity, numSamples - done);
        processChunk (*state, playing, startTick + done * tps, n, left + done, right + done, meters);
        resumeAudio = false;
    }
}

void RackRenderer::processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                                 float* left, float* right, MeterBank* meters) noexcept
{
    const auto& song = state.song;
    const auto tracks = std::max<int> (1, (int) song.mixer.size());

    for (int t = 0; t < tracks; ++t)
    {
        std::memset (trackBuffer (t, 0), 0, sizeof (float) * (std::size_t) numSamples);
        std::memset (trackBuffer (t, 1), 0, sizeof (float) * (std::size_t) numSamples);
    }

    events.clear();

    if (playing && song.mode == PlayMode::song)
    {
        scheduleSong (song, startTick, numSamples, ticksPerSample (song.bpm, sampleRate), events, resumeAudio);
    }
    else if (playing && ! song.patterns.empty())
    {
        const auto patternIndex = std::clamp (song.currentPattern, 0, (int) song.patterns.size() - 1);
        const auto& pattern = song.patterns[(std::size_t) patternIndex];
        schedulePatternLoop (pattern, pattern.effectiveLength (song.beatsPerBar), startTick, numSamples,
                             ticksPerSample (song.bpm, sampleRate), events);
    }

    // 1) instrumentos -> trilhas do mixer, em segmentos entre eventos para cada nota
    //    começar no sample exato
    int position = 0;

    for (const auto& e : events)
    {
        if (e.sampleOffset > position)
        {
            renderSegment (state, position, e.sampleOffset - position);
            position = e.sampleOffset;
        }

        if (e.channel < 0 || e.channel >= (int) state.instruments.size())
            continue;

        auto& instrument = *state.instruments[(std::size_t) e.channel];

        if (e.type == NoteEvent::Type::on)
        {
            if (! song.channels[(std::size_t) e.channel].muted)
                instrument.noteOnAt (e.key, e.velocity, e.startSeconds);
        }
        else
        {
            instrument.noteOff (e.key);
        }
    }

    if (position < numSamples)
        renderSegment (state, position, numSamples - position);

    // 2) inserts -> Master (com mute/solo)
    const auto anySolo = std::any_of (song.mixer.begin() + std::min<std::ptrdiff_t> (1, (std::ptrdiff_t) song.mixer.size()),
                                      song.mixer.end(), [] (const MixerTrack& t) { return t.solo; });

    float* masterL = trackBuffer (0, 0);
    float* masterR = trackBuffer (0, 1);

    for (int t = 1; t < tracks; ++t)
    {
        const auto& track = song.mixer[(std::size_t) t];

        if (track.muted || (anySolo && ! track.solo))
            continue;

        runEffects (state, t, numSamples);

        float gl = 0.0f, gr = 0.0f;
        panGains (track.pan, track.volume / unityFader, gl, gr);
        const auto* l = trackBuffer (t, 0);
        const auto* r = trackBuffer (t, 1);

        for (int i = 0; i < numSamples; ++i)
        {
            masterL[i] += l[i] * gl;
            masterR[i] += r[i] * gr;
        }

        if (meters != nullptr)
            meters->accumulate (t, peakOf (l, numSamples) * gl, peakOf (r, numSamples) * gr);
    }

    // 3) efeitos do Master, depois Master -> saída. Sem mixer configurado, o Master fica em 0 dB.
    runEffects (state, 0, numSamples);

    float gl = 1.0f, gr = 1.0f;

    if (! song.mixer.empty())
    {
        const auto& master = song.mixer[0];
        panGains (master.pan, master.muted ? 0.0f : master.volume / unityFader, gl, gr);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = masterL[i] * gl;
        right[i] = masterR[i] * gr;
    }

    if (meters != nullptr)
        meters->accumulate (0, peakOf (left, numSamples), peakOf (right, numSamples));
}
} // namespace lynx
