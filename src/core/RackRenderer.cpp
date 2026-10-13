#include "core/RackRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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
    void process (float*, float*, int, const EffectParams&, double) noexcept override {}
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
    state->buildAutomationTables();
    state->buildPdc (previous);
    state->mixerOrder = state->song.mixerOrder();
    const auto audible = state->song.soloAudible();
    state->mixerAudible.assign (audible.begin(), audible.end());
    return state;
}

std::vector<int> RackState::currentLatencies() const
{
    std::vector<int> out;

    for (const auto& i : instruments)
        out.push_back (i->latencySamples());

    for (const auto& track : effects)
        for (const auto& e : track)
            out.push_back (e->latencySamples());

    return out;
}

void RackState::buildPdc (const RackState* previous)
{
    latencies = currentLatencies();

    std::vector<int> channelLatency (latencies.begin(), latencies.begin() + (std::ptrdiff_t) instruments.size());
    std::vector<std::vector<int>> slotLatency (effects.size());
    auto next = latencies.begin() + (std::ptrdiff_t) instruments.size();

    for (std::size_t t = 0; t < effects.size(); ++t)
        for (std::size_t s = 0; s < effects[t].size(); ++s)
            slotLatency[t].push_back (*next++);

    pdc = computePdc (song, channelLatency, slotLatency);

    // reaproveita as linhas de mesmo comprimento: editar a música não pode estalar o áudio
    auto line = [] (int length, const std::shared_ptr<DelayLine>* old) -> std::shared_ptr<DelayLine> {
        if (length <= 0)
            return nullptr;
        if (old != nullptr && *old != nullptr && (*old)->length() == length)
            return *old;
        return std::make_shared<DelayLine> (length);
    };

    channelDelays.resize (pdc.channelDelay.size());

    for (std::size_t c = 0; c < pdc.channelDelay.size(); ++c)
        channelDelays[c] = line (pdc.channelDelay[c],
                                 previous != nullptr && c < previous->channelDelays.size() ? &previous->channelDelays[c] : nullptr);

    sendDelays.resize (pdc.sendDelay.size());

    for (std::size_t t = 0; t < pdc.sendDelay.size(); ++t)
    {
        sendDelays[t].resize (pdc.sendDelay[t].size());

        for (std::size_t k = 0; k < pdc.sendDelay[t].size(); ++k)
        {
            const std::shared_ptr<DelayLine>* old = nullptr;

            if (previous != nullptr && t < previous->sendDelays.size() && k < previous->sendDelays[t].size())
                old = &previous->sendDelays[t][k];

            sendDelays[t][k] = line (pdc.sendDelay[t][k], old);
        }
    }
}

void RackState::buildAutomationTables()
{
    const auto& automations = song.automations;
    const auto count = std::min<int> ((int) automations.size(), maxAutomations);

    automationClips.assign ((std::size_t) count, {});

    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];

        if (c.kind == ClipKind::automation && c.automation >= 0 && c.automation < count && c.length > 0)
            automationClips[(std::size_t) c.automation].push_back (i);
    }

    for (auto& clips : automationClips)
        std::sort (clips.begin(), clips.end(),
                   [this] (int a, int b) { return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start; });

    channelVolumeAuto.assign (song.channels.size(), -1);
    channelPanAuto.assign (song.channels.size(), -1);
    mixerVolumeAuto.assign (song.mixer.size(), -1);
    mixerPanAuto.assign (song.mixer.size(), -1);
    effectParamAuto.assign (song.mixer.size(), {});

    for (std::size_t t = 0; t < song.mixer.size(); ++t)
    {
        std::array<int, maxEffectParams> none;
        none.fill (-1);
        effectParamAuto[t].assign (song.mixer[t].effects.size(), none);
    }

    pluginParams.clear();
    hasAutomation = false;

    // a última automação de um mesmo alvo ganha
    for (int a = 0; a < count; ++a)
    {
        if (automationClips[(std::size_t) a].empty())
            continue;

        const auto& t = automations[(std::size_t) a].target;
        const auto channelOk = t.index >= 0 && t.index < (int) song.channels.size();
        const auto trackOk = t.index >= 0 && t.index < (int) song.mixer.size();
        auto used = true;

        switch (t.kind)
        {
            case AutomationTargetKind::channelVolume: if (channelOk) channelVolumeAuto[(std::size_t) t.index] = a; else used = false; break;
            case AutomationTargetKind::channelPan: if (channelOk) channelPanAuto[(std::size_t) t.index] = a; else used = false; break;
            case AutomationTargetKind::mixerVolume: if (trackOk) mixerVolumeAuto[(std::size_t) t.index] = a; else used = false; break;
            case AutomationTargetKind::mixerPan: if (trackOk) mixerPanAuto[(std::size_t) t.index] = a; else used = false; break;

            case AutomationTargetKind::channelParam:
                if (channelOk && song.channels[(std::size_t) t.index].instrument == InstrumentKind::plugin)
                    pluginParams.push_back ({ a, t.index, 0, 0, t.param });
                else
                    used = false;
                break;

            case AutomationTargetKind::effectParam:
            {
                const auto slotOk = trackOk && t.slot >= 0 && t.slot < (int) song.mixer[(std::size_t) t.index].effects.size();

                if (! slotOk)
                    used = false;
                else if (song.mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind == EffectKind::plugin)
                    pluginParams.push_back ({ a, -1, t.index, t.slot, t.param });
                else if (t.param >= 0 && t.param < maxEffectParams)
                    effectParamAuto[(std::size_t) t.index][(std::size_t) t.slot][(std::size_t) t.param] = a;
                else
                    used = false;
                break;
            }

            case AutomationTargetKind::none: used = false; break;
        }

        hasAutomation = hasAutomation || used;
    }
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
    automationValues.fill (std::numeric_limits<float>::quiet_NaN());
    scratch.assign ((std::size_t) std::max (64, maxBlockSize) * 2, 0.0f);
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

float RackRenderer::automated (int automation, float base) const noexcept
{
    if (automation < 0)
        return base;

    const auto v = automationValues[(std::size_t) automation];
    return std::isnan (v) ? base : v;
}

namespace
{
// Valor de uma automação numa posição da música: dentro de um clip, a curva; entre clips,
// o último valor do clip anterior; antes do primeiro, o valor inicial dele. Só depende da
// posição, então tocar de qualquer ponto (ou exportar) dá o mesmo resultado.
float automationValueAt (const Song& song, const Automation& automation, const std::vector<int>& clips, double tick) noexcept
{
    const Clip* before = nullptr;

    for (auto i : clips) // ordenados por início
    {
        const auto& c = song.playlist[(std::size_t) i];

        if (tick < (double) c.start)
            break;

        if (tick < (double) c.end())
            return automation.valueAt (tick - (double) c.start + (double) c.offset);

        before = &c;
    }

    if (before != nullptr)
        return automation.valueAt ((double) (before->length + before->offset));

    const auto& first = song.playlist[(std::size_t) clips.front()];
    return automation.valueAt ((double) first.offset);
}
} // namespace

void RackRenderer::updateAutomation (const RackState& state, double startTick) noexcept
{
    const auto& song = state.song;

    // no modo pattern a Playlist não toca: os valores ficam como estavam
    if (song.mode != PlayMode::song)
        return;

    const auto length = (double) std::max<std::int64_t> (1, song.songLength());
    const auto tick = std::fmod (std::max (0.0, startTick), length);

    for (std::size_t a = 0; a < state.automationClips.size(); ++a)
        if (! state.automationClips[a].empty())
            automationValues[a] = automationValueAt (song, song.automations[a], state.automationClips[a], tick);

    for (const auto& b : state.pluginParams)
    {
        const auto v = automationValues[(std::size_t) b.automation];

        if (std::isnan (v))
            continue;

        if (b.channel >= 0)
        {
            if (b.channel < (int) state.instruments.size())
                state.instruments[(std::size_t) b.channel]->setParameter (b.param, v);
        }
        else if (b.track < (int) state.effects.size() && b.slot < (int) state.effects[(std::size_t) b.track].size())
            state.effects[(std::size_t) b.track][(std::size_t) b.slot]->setParameter (b.param, v);
    }
}

void RackRenderer::renderSegment (const RackState& state, int offset, int n) noexcept
{
    const auto& channels = state.song.channels;
    const auto tracks = std::max<int> (1, (int) state.song.mixer.size());
    const auto automation = state.hasAutomation;

    for (std::size_t i = 0; i < state.instruments.size() && i < channels.size(); ++i)
    {
        const auto& c = channels[i];
        const auto track = std::clamp (c.mixerTrack, 0, tracks - 1);
        const auto volume = automation ? automated (state.channelVolumeAuto[i], c.volume) : c.volume;
        const auto pan = automation ? automated (state.channelPanAuto[i], (c.pan + 1.0f) * 0.5f) * 2.0f - 1.0f : c.pan;
        float gl = 0.0f, gr = 0.0f;
        panGains (pan, volume, gl, gr);

        if (i < state.channelDelays.size() && state.channelDelays[i] != nullptr)
        {
            // PDC: este canal chega antes dos outros da trilha; atrasa até alinhar
            auto* sl = scratch.data();
            auto* sr = scratch.data() + blockCapacity;
            std::memset (sl, 0, sizeof (float) * (std::size_t) n);
            std::memset (sr, 0, sizeof (float) * (std::size_t) n);
            state.instruments[i]->render (sl, sr, n, gl, gr);
            state.channelDelays[i]->process (sl, sr, n);

            auto* dl = trackBuffer (track, 0) + offset;
            auto* dr = trackBuffer (track, 1) + offset;

            for (int k = 0; k < n; ++k)
            {
                dl[k] += sl[k];
                dr[k] += sr[k];
            }
        }
        else
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
    {
        if (! slots[i].enabled)
            continue;

        if (state.hasAutomation && slots[i].kind != EffectKind::plugin)
        {
            auto params = slots[i].params; // cópia de 6 floats, sem alocação
            const auto& autos = state.effectParamAuto[(std::size_t) track][i];

            for (std::size_t p = 0; p < params.size(); ++p)
                params[p] = automated (autos[p], params[p]);

            effects[i]->process (trackBuffer (track, 0), trackBuffer (track, 1), n, params, state.song.bpm);
        }
        else
            effects[i]->process (trackBuffer (track, 0), trackBuffer (track, 1), n, slots[i].params, state.song.bpm);
    }
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

    // Blocos maiores que os buffers do mixer são feitos em pedaços. Com automação, pedaços
    // pequenos para os valores andarem suavemente.
    const auto chunk = state->hasAutomation ? std::min (blockCapacity, 128) : blockCapacity;

    for (int done = 0; done < numSamples; done += chunk)
    {
        const auto n = std::min (chunk, numSamples - done);
        processChunk (*state, playing, startTick + done * tps, n, left + done, right + done, meters);
        resumeAudio = false;
    }
}

void RackRenderer::processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                                 float* left, float* right, MeterBank* meters) noexcept
{
    const auto& song = state.song;
    const auto tracks = std::max<int> (1, (int) song.mixer.size());

    if (state.hasAutomation)
        updateAutomation (state, startTick);

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

    // 2) inserts em ordem de roteamento (quem envia antes de quem recebe) -> destinos
    float* masterL = trackBuffer (0, 0);
    float* masterR = trackBuffer (0, 1);

    for (const auto t : state.mixerOrder)
    {
        if (t <= 0 || t >= tracks)
            continue;

        const auto& track = song.mixer[(std::size_t) t];

        if (track.muted || ! state.mixerAudible[(std::size_t) t])
            continue;

        runEffects (state, t, numSamples);

        const auto volume = state.hasAutomation ? automated (state.mixerVolumeAuto[(std::size_t) t], track.volume) : track.volume;
        const auto pan = state.hasAutomation ? automated (state.mixerPanAuto[(std::size_t) t], (track.pan + 1.0f) * 0.5f) * 2.0f - 1.0f
                                             : track.pan;
        float gl = 0.0f, gr = 0.0f;
        panGains (pan, volume / unityFader, gl, gr);
        const auto* l = trackBuffer (t, 0);
        const auto* r = trackBuffer (t, 1);

        const auto* delays = (std::size_t) t < state.sendDelays.size() ? &state.sendDelays[(std::size_t) t] : nullptr;

        auto sendTo = [&] (float* dl, float* dr, float level, std::size_t k) {
            const auto sl = gl * level, sr = gr * level;
            DelayLine* delay = delays != nullptr && k < delays->size() ? (*delays)[k].get() : nullptr;

            if (delay == nullptr)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    dl[i] += l[i] * sl;
                    dr[i] += r[i] * sr;
                }
                return;
            }

            // PDC: esta aresta é mais curta que a mais lenta que chega no destino
            auto* xl = scratch.data();
            auto* xr = scratch.data() + blockCapacity;

            for (int i = 0; i < numSamples; ++i)
            {
                xl[i] = l[i] * sl;
                xr[i] = r[i] * sr;
            }

            delay->process (xl, xr, numSamples);

            for (int i = 0; i < numSamples; ++i)
            {
                dl[i] += xl[i];
                dr[i] += xr[i];
            }
        };

        if (track.toMaster)
            sendTo (masterL, masterR, 1.0f, 0);

        for (std::size_t k = 0; k < track.sends.size(); ++k)
        {
            const auto& send = track.sends[k];

            if (send.target > 0 && send.target < tracks && send.target != t && send.level > 0.0f)
                sendTo (trackBuffer (send.target, 0), trackBuffer (send.target, 1), send.level, k + 1);
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
        const auto volume = state.hasAutomation ? automated (state.mixerVolumeAuto[0], master.volume) : master.volume;
        const auto pan = state.hasAutomation ? automated (state.mixerPanAuto[0], (master.pan + 1.0f) * 0.5f) * 2.0f - 1.0f : master.pan;
        panGains (pan, master.muted ? 0.0f : volume / unityFader, gl, gr);
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
