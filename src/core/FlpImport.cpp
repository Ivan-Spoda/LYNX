#include "core/FlpImport.h"

#include "core/Effects.h"
#include "core/FlWrapper.h"
#include "core/SynthPresets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace lynx
{
namespace
{
bool contains (const std::string& haystack, const char* needle)
{
    return haystack.find (needle) != std::string::npos;
}

std::string lowercase (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}
} // namespace

InstrumentKind guessInstrument (const std::string& channelName, flp::ChannelType type)
{
    const auto name = lowercase (channelName);

    if (type == flp::ChannelType::sampler || type == flp::ChannelType::unknown)
    {
        if (contains (name, "kick") || contains (name, "bd") || contains (name, "808"))
            return InstrumentKind::kick;
        if (contains (name, "snare") || contains (name, "snr") || contains (name, "rim"))
            return InstrumentKind::snare;
        if (contains (name, "clap") || contains (name, "snap"))
            return InstrumentKind::clap;
        if (contains (name, "hat") || contains (name, "hh") || contains (name, "ride") || contains (name, "cym")
            || contains (name, "crash") || contains (name, "shaker") || contains (name, "perc") || contains (name, "tamb"))
            return InstrumentKind::hat;
    }

    return InstrumentKind::synth;
}

std::string resolveFlPath (const std::string& flPath, const FlPathRoots& roots)
{
    const std::pair<const char*, const std::string*> variables[] = {
        { "%FLStudioFactoryData%", &roots.factoryData },
        { "%FLStudioUserData%", &roots.userData },
        { "%FLStudioData%", &roots.data },
    };

    for (const auto& [name, root] : variables)
    {
        const std::string variable (name);

        if (flPath.compare (0, variable.size(), variable) == 0)
            return root->empty() ? std::string() : *root + flPath.substr (variable.size());
    }

    return flPath;
}

std::optional<EffectKind> effectForFlPlugin (const std::string& name)
{
    static const std::pair<const char*, EffectKind> table[] = {
        { "Fruity Parametric EQ 2", EffectKind::parametricEq }, { "Fruity Parametric EQ", EffectKind::parametricEq },
        { "Fruity 7 Band EQ", EffectKind::parametricEq },
        { "Fruity Limiter", EffectKind::limiter },     { "Maximus", EffectKind::limiter },           { "Fruity Soft Clipper", EffectKind::distortion },
        { "Fruity Reeverb 2", EffectKind::reverb },    { "Fruity Reeverb", EffectKind::reverb },     { "LuxeVerb", EffectKind::reverb },
        { "Fruity Delay 3", EffectKind::delay },       { "Fruity Delay 2", EffectKind::delay },      { "Fruity Delay Bank", EffectKind::delay },
        { "Fruity Delay", EffectKind::delay },         { "Fruity Compressor", EffectKind::compressor },
        { "Fruity Multiband Compressor", EffectKind::compressor }, { "Soundgoodizer", EffectKind::compressor },
        { "Fruity WaveShaper", EffectKind::distortion }, { "Fruity Fast Dist", EffectKind::distortion },
        { "Fruity Blood Overdrive", EffectKind::distortion }, { "Distructor", EffectKind::distortion },
        { "Fruity Flangus", EffectKind::chorus },      { "Fruity Chorus", EffectKind::chorus },      { "Hyper Chorus", EffectKind::chorus },
        { "Vintage Chorus", EffectKind::chorus },      { "Fruity Flanger", EffectKind::chorus },
        { "Fruity Love Philter", EffectKind::filter }, { "Fruity Filter", EffectKind::filter },      { "Fruity Fast LP", EffectKind::filter },
    };

    for (const auto& [plugin, kind] : table)
        if (name == plugin)
            return kind;

    return std::nullopt;
}

AutomationTarget automationTargetFor (flp::ControlId id, const std::map<int, int>& channelMap, int mixerTracks)
{
    AutomationTarget t;

    if (id.isTempo())
    {
        t.kind = AutomationTargetKind::tempo;
        return t;
    }

    if (id.isChannel())
    {
        const auto channel = channelMap.find (id.channel());

        if (channel == channelMap.end())
            return t;

        t.index = channel->second;
        const auto p = id.param();

        if (p == 0)
            t.kind = AutomationTargetKind::channelVolume;
        else if (p == 1)
            t.kind = AutomationTargetKind::channelPan;
        else if (p >= flp::ControlId::pluginParamBase)
        {
            t.kind = AutomationTargetKind::channelParam;
            t.param = p - flp::ControlId::pluginParamBase;
        }

        return t;
    }

    if (id.isMixer() && id.track() >= 0 && id.track() < mixerTracks)
    {
        t.index = id.track();
        const auto p = id.param();

        if (p == flp::ControlId::mixerVolume)
            t.kind = AutomationTargetKind::mixerVolume;
        else if (p == flp::ControlId::mixerPan)
            t.kind = AutomationTargetKind::mixerPan;
        else if (p >= flp::ControlId::pluginParamBase)
        {
            t.kind = AutomationTargetKind::effectParam;
            t.slot = id.slot();
            t.param = p - flp::ControlId::pluginParamBase;
        }
    }

    return t;
}

bool synthFrom3xOsc (const std::vector<std::uint8_t>& state, SynthSettings& synth)
{
    // versão 10/11: 20 parâmetros + 4 bools (80 B); 12+: 21 parâmetros + 4 bools (+ HQ)
    if (state.size() < 84)
        return false;

    auto i32 = [&] (std::size_t offset) {
        return (std::int32_t) ((std::uint32_t) state[offset] | ((std::uint32_t) state[offset + 1] << 8)
                               | ((std::uint32_t) state[offset + 2] << 16) | ((std::uint32_t) state[offset + 3] << 24));
    };

    const auto version = i32 (0);

    if (version < 10 || version > 64)
        return false;

    // parâmetro i fica em 0x04 + 4i; (i + 1) = 7·osc + campo, campos [mix, pan, shape, coarse, fine, spo, detune]
    auto param = [&] (int osc, int field) { return i32 ((std::size_t) (4 + 4 * (7 * osc + field - 1))); };
    const auto boolsAt = version >= 12 ? (std::size_t) 0x58 : (std::size_t) 0x54;
    const bool am = version >= 11 && state.size() > boolsAt + 3 && state[boolsAt + 3] != 0;

    // níveis (§2.2): o osc 3 leva mix3/128, o osc 2 uma parte do resto, o osc 1 o que sobra
    const auto g3 = std::clamp (param (2, 0), 0, 128) / 128.0f;
    const auto rest = am ? 1.0f : 1.0f - g3;
    const auto g2 = rest * std::clamp (param (1, 0), 0, 128) / 128.0f;
    const std::array<float, 3> levels { std::max (0.0f, rest - g2), g2, am ? 0.0f : g3 }; // AM ainda não: osc 3 mudo

    for (int o = 0; o < 3; ++o)
    {
        auto& osc = synth.osc[(std::size_t) o];

        switch (param (o, 2))
        {
            case 0: osc.wave = Waveform::sine; break;
            case 1: osc.wave = Waveform::triangle; break;
            case 2: osc.wave = Waveform::square; break;
            case 5: osc.wave = Waveform::noise; break;
            default: osc.wave = Waveform::saw; break; // 3 serra, 4 serra arredondada, 6 custom sem sample
        }

        const auto coarse = std::clamp (param (o, 3), -24, 24);
        osc.octave = coarse / 12; // trunca em direção a zero; o resto vira semitons
        osc.semitones = coarse - osc.octave * 12;
        // o stereo detune só soa no canal direito: metade dele vale como desafinação média
        osc.detune = std::clamp ((float) std::clamp (param (o, 4), -100, 100) + std::clamp (param (o, 6), -50, 50) * 0.5f, -100.0f, 100.0f);
        osc.pan = std::clamp (param (o, 1), -64, 64) / 64.0f;
        osc.level = levels[(std::size_t) o];
    }

    return true;
}

namespace
{
// Corte (Hz) do filtro do canal do FL para Mod X normalizado `x` (REA_rodada4 §3); 20 kHz = aberto.
double channelFilterHz (int type, double x)
{
    constexpr double twoPi = 6.283185307179586;
    x = std::clamp (x, 0.0, 1.0);

    switch (type)
    {
        case 0:
            return x >= 1.0 ? 20000.0 : -44100.0 / twoPi * std::log (1.0 - std::min ((std::pow (601.0, x) - 1.0) / 600.0, 0.999));
        case 1:
        case 2:
        case 3:
        case 4:
        case 5: return ((std::pow (51.0, x) - 1.0) * 0.0596 + 0.02) * 44100.0 / twoPi;
        case 6:
        case 7: return 44100.0 / (twoPi / 2.0) * std::asin (std::min (1.0, (std::pow (26.0, x) - 1.0) * 0.04) / 2.0);
        default: return 20000.0;
    }
}
} // namespace

float filterEnvelopeOctaves (int modX, int type, int amount)
{
    const auto x = std::clamp (modX / 256.0, 0.0, 1.0);
    const auto from = std::clamp (channelFilterHz (type, x), 20.0, 20000.0);
    const auto to = std::clamp (channelFilterHz (type, x + amount / 128.0), 20.0, 20000.0);
    return (float) std::log2 (to / from);
}

void synthFilterFromChannel (int modX, int modY, int type, float& cutoffHz, float& resonance, int& mode)
{
    mode = type == 2 ? 1 : type == 3 ? 2 : type == 4 ? 3 : 0; // BP, HP, BS; o resto é passa-baixas
    const auto x = std::clamp (modX / 256.0, 0.0, 1.0);
    const auto r = std::clamp (modY / 256.0, 0.0, 1.0);
    auto fromQ = [] (double q) { return (float) std::clamp ((2.0 - 1.0 / q) / 1.9, 0.0, 1.0); }; // o SVF do synth: k = 1/Q
    resonance = 0.0f;

    if (type == 0) // Fast LP: 1 polo + realimentação; a realimentação cresce com Mod Y (aproximação)
        resonance = (float) (r * 0.9);
    else if (type >= 1 && type <= 5) // biquads LP/BP/HP/BS (LPx2 = o LP duas vezes)
        resonance = fromQ (r <= 0.0 ? 0.1 : (std::pow (51.0, 0.625 * r) - 1.0) * 0.4 + 0.1);
    else if (type == 6 || type == 7) // SVF de Chamberlin
        resonance = fromQ (1.0 / (r <= 0.0 ? 1.0 : (r < 1.0 ? 1.0 - 0.8 * r : 0.2)));

    cutoffHz = (float) std::clamp (channelFilterHz (type, x), 20.0, 20000.0);
}

Song importFlp (const flp::Project& project, const FlpImportOptions& options)
{
    Song song;
    song.bpm = project.tempo > 0.0 ? project.tempo : 140.0;
    song.beatsPerBar = std::clamp (project.beatsPerBar, 1, 32);
    song.swing = std::clamp (project.swing / 128.0f, 0.0f, 1.0f);

    // Canais do FL podem ter índices esparsos; mapeamos para índices densos.
    std::map<int, int> channelMap;

    for (const auto& c : project.channels)
    {
        // clips de automação viram Song::automations (mais abaixo), não canais
        if (c.type == flp::ChannelType::automationClip)
            continue;

        channelMap[c.index] = (int) song.channels.size();

        ChannelDef def;
        def.name = c.name.empty() ? "Canal " + std::to_string (c.index + 1) : c.name;
        def.instrument = guessInstrument (def.name, c.type);

        // sintetizador do FL que vira o synth interno: um preset que combine com o nome
        if (def.instrument == InstrumentKind::synth)
            def.synth = presetForChannelName (def.name);

        const auto isAudioClip = c.type == flp::ChannelType::audioClip;

        if ((c.type == flp::ChannelType::sampler || isAudioClip) && ! c.samplePath.empty() && options.fileExists)
        {
            const auto path = resolveFlPath (c.samplePath, options.roots);

            if (! path.empty() && options.fileExists (path))
            {
                def.instrument = isAudioClip ? InstrumentKind::audioClip : InstrumentKind::sampler;
                def.samplePath = path;
            }
        }

        if (isAudioClip && def.instrument != InstrumentKind::audioClip)
            def.instrument = InstrumentKind::audioClip; // sem arquivo: fica mudo, mas o clip continua na playlist

        def.mixerTrack = std::clamp (c.mixerTrack, 0, maxMixerTracks - 1);

        if (c.colour != 0)
            def.colour = 0xFF000000u | c.colour;

        if (c.hasLevels)
        {
            def.volume = std::clamp (c.volume / 12800.0f, 0.0f, 1.0f); // mesma curva (channelGain)
            def.pan = std::clamp ((c.pan - 6400) / 6400.0f, -1.0f, 1.0f);
            def.sampler.pitch = std::clamp (c.pitchCents / 100.0f, -24.0f, 24.0f);
        }

        // nota-raiz do sample: o nosso sampler toca na altura original em C5 (60)
        if (c.rootNote >= 0 && c.rootNote <= 127)
            def.sampler.pitch = std::clamp (def.sampler.pitch + (float) (60 - c.rootNote), -24.0f, 24.0f);

        def.swingMix = std::clamp (c.swingMix / 128.0f, 0.0f, 1.0f);
        def.cutGroup = c.cutGroup;
        def.cutBy = c.cutBy;

        // "cut itself" (grupo == cut by) e canal mono viram o "cut" do sampler
        if ((c.cutGroup != 0 && c.cutGroup == c.cutBy) || c.maxVoices == 1)
            def.sampler.cut = true;

        def.synth.mono = c.maxVoices == 1;

        // envelope de volume (registro 1 do 0xDA) -> ADSR do sampler
        const auto& volumeEnvelope = c.envelopes[1];
        const auto hasVolumeEnvelope = volumeEnvelope.present && volumeEnvelope.enabled;

        if (hasVolumeEnvelope)
        {
            const auto& env = volumeEnvelope;
            const auto beats = (env.flags & 1) != 0; // sincronizado com o tempo: unidades em tempos
            auto seconds = [&] (int v) {
                const auto t = flp::Channel::envelopeTime (v);
                return (float) (beats ? t * 2.0 * 60.0 / song.bpm : t); // 0,128 tempo em vez de 0,064 s
            };

            def.sampler.attack = std::clamp (seconds (env.attack), 0.0f, 2.0f);
            def.sampler.decay = std::clamp (seconds (env.decay), 0.0f, 10.0f);
            def.sampler.sustain = std::clamp (env.sustain / 128.0f, 0.0f, 1.0f);
            def.sampler.release = std::clamp (std::max (seconds (env.release), 0.002f), 0.0f, 4.0f); // envelope ligado: a nota acaba
        }

        // filtro do canal (Mod X/Y + tipo do 0xDB) vale para o sampler também
        synthFilterFromChannel (c.modX, c.modY, c.filterType, def.sampler.cutoff, def.sampler.resonance, def.sampler.filterType);

        // envelopes de corte (registro 2) e de afinação (registro 4) do canal
        auto modEnvelope = [&] (const flp::Channel::Envelope& env, float amount) {
            ModEnvelope m;
            if (! env.present || ! env.enabled || amount == 0.0f)
                return m;

            const auto beats = (env.flags & 1) != 0;
            auto seconds = [&] (int v) {
                const auto t = flp::Channel::envelopeTime (v);
                return (float) std::clamp (beats ? t * 2.0 * 60.0 / song.bpm : t, 0.0, 20.0);
            };
            m.amount = amount;
            m.attack = seconds (env.attack);
            m.hold = seconds (env.hold);
            m.decay = seconds (env.decay);
            m.sustain = std::clamp (env.sustain / 128.0f, 0.0f, 1.0f);
            m.release = seconds (env.release);
            return m;
        };

        const auto filterEnv = modEnvelope (c.envelopes[2], filterEnvelopeOctaves (c.modX, c.filterType, c.envelopes[2].amount));
        const auto pitchEnv = modEnvelope (c.envelopes[4], std::clamp (c.envelopes[4].amount, -2400, 2400) / 100.0f);
        def.sampler.filterEnv = filterEnv;
        def.sampler.pitchEnv = pitchEnv;

        // plugin de terceiros (VST/CLAP) no Fruity Wrapper: o app procura o mesmo plugin instalado;
        // até lá (ou se não achar) o canal fica no synth interno
        if (c.pluginName == "Fruity Wrapper" && parseFlWrapper (c.pluginState, def.plugin.foreign))
            def.plugin.name = def.plugin.foreign.name;

        // 3x Osc: osciladores do estado do plugin; envelope e filtro vêm do canal (REA_rodada4)
        if (def.instrument == InstrumentKind::synth && c.pluginName == "3x Osc" && synthFrom3xOsc (c.pluginState, def.synth))
        {
            if (hasVolumeEnvelope)
            {
                def.synth.attack = def.sampler.attack;
                def.synth.decay = def.sampler.decay;
                def.synth.sustain = def.sampler.sustain;
                def.synth.release = def.sampler.release;
            }
            else
            {
                // sem envelope o FL toca a nota como um portão, só com um declique curto
                def.synth.attack = 0.002f;
                def.synth.decay = 0.0f;
                def.synth.sustain = 1.0f;
                def.synth.release = 0.02f;
            }

            synthFilterFromChannel (c.modX, c.modY, c.filterType, def.synth.cutoff, def.synth.resonance, def.synth.filterType);

            def.synth.filterEnv = filterEnv;
            def.synth.pitchEnv = pitchEnv;
        }

        // trecho do sample e start offset
        if (std::isfinite (c.sampleStart) && std::isfinite (c.sampleEnd) && std::isfinite (c.startOffset))
        {
            const auto start = std::clamp (c.sampleStart, 0.0, 1.0), end = std::clamp (c.sampleEnd, 0.0, 1.0);

            if (end > start)
            {
                def.sampler.regionStart = (float) std::clamp (start + std::clamp (c.startOffset, 0.0, 1.0) * (end - start), 0.0, 1.0);
                def.sampler.regionEnd = (float) end;
            }
        }

        // time-stretch: o sample dura TIME/192 tempos × MUL
        if (c.stretchTime > 0.0f)
        {
            def.sampler.stretchBeats = (float) (c.stretchTime / 192.0 * std::pow (2.0, c.stretchMul / 10000.0));
            def.sampler.keepPitch = c.stretchMode != 0; // só o "Resample" do FL muda a altura
        }
        song.channels.push_back (std::move (def));
    }

    // Mixer: nomes, mute/solo, roteamento e níveis do .flp; inserts suficientes para todas as
    // rotas (mínimo 16, como um projeto novo). As trilhas extras do FL (500/501) ficam de fora.
    int highestTrack = 16;
    for (const auto& c : song.channels)
        highestTrack = std::max (highestTrack, c.mixerTrack);

    for (const auto& insert : project.mixer)
        if (insert.index < maxMixerTracks)
            for (auto target : insert.routes)
                if (target < maxMixerTracks && (insert.index > 0 || target > 0))
                    highestTrack = std::max ({ highestTrack, target, insert.index });

    song.mixer = Song::defaultMixer (std::min (highestTrack, maxMixerTracks - 1));
    const auto tracks = (int) song.mixer.size();

    for (const auto& insert : project.mixer)
    {
        if (insert.index < 0 || insert.index >= tracks)
            continue;

        auto& track = song.mixer[(std::size_t) insert.index];

        if (insert.index > 0 && ! insert.name.empty())
            track.name = insert.name;

        if (insert.colour != 0 && insert.colour != 0x485156) // o cinza padrão do FL
            track.colour = 0xFF000000u | insert.colour;

        track.muted = (insert.flags & 0x08) == 0;
        track.invertPhase = (insert.flags & 0x01) != 0;
        track.swapChannels = (insert.flags & 0x02) != 0;
        track.solo = insert.index > 0 && (insert.flags & 0x1000) != 0;

        if (insert.index > 0 && insert.hasRoutes)
        {
            track.toMaster = std::find (insert.routes.begin(), insert.routes.end(), 0) != insert.routes.end();

            // O nível vem do 0xE1 (abaixo). Sem registro, os templates do FL ligam todo insert às
            // trilhas de send do fim do mixer (100+) com nível 0; rotas para inserts comuns valem 100%.
            for (auto target : insert.routes)
                if (target > 0 && target < tracks && target != insert.index)
                    track.sends.push_back ({ target, target >= 100 ? 0.0f : 1.0f });
        }
    }

    // efeitos nativos do FL viram os efeitos internos parecidos (parâmetros no padrão: o estado
    // interno dos plugins do FL não é conhecido); os outros ficam de fora
    for (const auto& insert : project.mixer)
    {
        if (insert.index < 0 || insert.index >= tracks)
            continue;

        auto& track = song.mixer[(std::size_t) insert.index];

        for (const auto& slot : insert.slots)
        {
            ForeignPlugin foreign;
            const auto wrapped = slot.plugin == "Fruity Wrapper" && parseFlWrapper (slot.state, foreign);
            const auto kind = wrapped ? std::optional<EffectKind> (EffectKind::plugin) : effectForFlPlugin (slot.plugin);

            if (! kind || (int) track.effects.size() >= maxEffectSlots)
                continue;

            auto fx = makeEffectSlot (*kind);

            if (wrapped) // o app troca pelo plugin instalado; sem ele, o slot sai
            {
                fx.plugin.name = foreign.name;
                fx.plugin.foreign = std::move (foreign);
            }

            if (*kind == EffectKind::filter)
                fx.params[1] = 1.0f; // aberto: sem os ajustes do FL, melhor não abafar

            if (slot.plugin == "Fruity Soft Clipper")
                fx.params[0] = 0.15f; // drive baixo

            // bypass e dry/wet do slot (0xE1, p = 0x1F00 / 0x1F01)
            for (const auto& control : project.controls)
                if (control.id.isMixer() && control.id.track() == insert.index && control.id.slot() == slot.index)
                {
                    if (control.id.param() == 0x1F00)
                        fx.enabled = control.value != 0;
                    else if (control.id.param() == 0x1F01)
                        fx.mix = std::clamp (control.value / 12800.0f, 0.0f, 1.0f);
                }

            track.effects.push_back (fx);
        }
    }

    // ciclos não deveriam existir no arquivo; se existirem, o envio que fecha o ciclo cai
    for (int t = 1; t < tracks; ++t)
    {
        auto sends = std::move (song.mixer[(std::size_t) t].sends);
        song.mixer[(std::size_t) t].sends.clear();

        for (const auto& s : sends)
            song.setRoute (t, s.target, true, s.level);
    }

    for (const auto& control : project.controls)
    {
        if (! control.id.isMixer())
            continue;

        const auto t = control.id.track();
        const auto p = control.id.param();

        if (t < 0 || t >= tracks)
            continue;

        auto& track = song.mixer[(std::size_t) t];

        // faixas do FL: volume 0..16000 (12800 = 0 dB), pan ±6400, envio 0..16000 (12800 = 100%)
        if (p == flp::ControlId::mixerVolume)
            track.volume = std::clamp (control.value / 16000.0f, 0.0f, 1.0f);
        else if (p == flp::ControlId::mixerPan)
            track.pan = std::clamp (control.value / 6400.0f, -1.0f, 1.0f);
        else if (p == flp::ControlId::mixerStereo)
            track.stereo = std::clamp (control.value / 64.0f, -1.0f, 1.0f);
        else if (p >= flp::ControlId::mixerSendBase && p < flp::ControlId::mixerSendBase + tracks)
            for (auto& send : track.sends)
                if (send.target == p - flp::ControlId::mixerSendBase)
                    send.level = std::clamp (control.value / 12800.0f, 0.0f, 1.0f);
    }

    // o template do FL liga todo insert às trilhas de send com nível 0; isso não soa, só gasta CPU
    for (auto& t : song.mixer)
        t.sends.erase (std::remove_if (t.sends.begin(), t.sends.end(), [] (const MixerSend& s) { return s.level <= 0.0f; }),
                       t.sends.end());

    const auto filePpq = project.header.ppq > 0 ? (std::int64_t) project.header.ppq : 96;
    auto rescale = [filePpq] (std::int64_t ticks) { return ticks * ppq / filePpq; };

    std::map<int, int> patternMap; // índice no FL -> índice denso nosso

    for (const auto& src : project.patterns)
    {
        Pattern p;
        p.name = src.name.empty() ? "Pattern " + std::to_string (src.index) : src.name;

        if (src.colour && *src.colour != 0x485156) // o cinza padrão do FL vira cor automática
            p.colour = 0xFF000000u | *src.colour;

        for (const auto& n : src.notes)
        {
            const auto it = channelMap.find (n.channel);

            if (it == channelMap.end())
                continue;

            Note note;
            note.start = rescale (n.position);
            // Notas de step (duração 0) tocam por um step.
            note.length = n.length > 0 ? rescale (n.length) : ticksPerStep;
            note.channel = it->second;
            note.key = n.key; // FL e MIDI usam a mesma numeração; só o nome da oitava difere
            note.velocity = std::clamp (n.velocity / 128.0f, 0.0f, 1.0f);
            note.pan = std::clamp ((n.pan - 64) / 64.0f, -1.0f, 1.0f);
            note.finePitch = (float) std::clamp ((int) n.finePitch - 120, -120, 120);
            note.muted = n.muted();
            note.slide = n.slide();
            p.notes.push_back (note);
        }

        p.sortNotes();

        if (! p.notes.empty())
        {
            patternMap[src.index] = (int) song.patterns.size();
            song.patterns.push_back (std::move (p));
        }
    }

    // Clips de automação: um Automation por canal de automação do FL, com o alvo do link 0xE3.
    std::map<int, int> automationMap; // canal do FL -> índice em song.automations

    for (const auto& c : project.channels)
    {
        if (c.type != flp::ChannelType::automationClip || c.automation.empty())
            continue;

        Automation a;
        a.name = c.name.empty() ? "Automação " + std::to_string (c.index + 1) : c.name;

        for (const auto& link : project.automationLinks)
            if (link.channel == c.index)
            {
                a.target = automationTargetFor (link.target, channelMap, (int) song.mixer.size());
                break;
            }

        for (std::size_t i = 0; i < c.automation.size(); ++i)
        {
            const auto& p = c.automation[i];
            const auto tick = (std::int64_t) std::llround (p.position * ppq);
            auto value = (float) std::clamp (p.value, 0.0, 1.0);

            // o FL automatiza o tempo de 60 a 180 BPM; o nosso alvo cobre a faixa inteira do andamento
            if (a.target.kind == AutomationTargetKind::tempo)
                value = valueFromTempo (60.0 + 120.0 * value);

            // a curva do FL é do trecho que termina no ponto; a nossa fica no ponto da esquerda
            if (i > 0)
            {
                auto& previous = a.points.back();

                if (p.curve == 0 || p.curve == 1) // single/double curve: exponencial pela tensão
                    previous.tension = std::clamp (-p.tension, -1.0f, 1.0f);
                else if (p.curve == 2 && tick - previous.tick > 1) // hold: degrau logo antes do ponto
                    a.points.push_back ({ tick - 1, previous.value, 0.0f });
            }

            a.points.push_back ({ tick, value, 0.0f });
        }

        automationMap[c.index] = (int) song.automations.size();
        song.automations.push_back (std::move (a));
    }

    // Playlist: todos os arrangements (o primeiro fica aberto); clips de pattern, de áudio e de automação.
    std::vector<const std::vector<flp::PlaylistTrackInfo>*> importedTracks;

    if (! project.arrangements.empty())
        song.arrangements.clear();

    for (const auto& arrangement : project.arrangements)
    {
        std::vector<Clip> clips;

        for (const auto& item : arrangement.items)
        {
            if (item.length == 0)
                continue;

            if (item.kind == flp::PlaylistItem::Kind::channelClip && automationMap.count (item.index))
            {
                Clip clip;
                clip.kind = ClipKind::automation;
                clip.automation = automationMap[item.index];
                clip.track = item.track;
                clip.start = rescale (item.position);
                clip.length = rescale (item.length);

                // recorte em tempos (-1 = sem recorte)
                if (std::isfinite (item.startOffset) && item.startOffset > 0.0f)
                    clip.offset = (std::int64_t) std::llround (item.startOffset * ppq);

                clips.push_back (clip);
                continue;
            }

            if (item.kind == flp::PlaylistItem::Kind::channelClip)
            {
                const auto channel = channelMap.find (item.index);

                if (channel == channelMap.end() || song.channels[(std::size_t) channel->second].instrument != InstrumentKind::audioClip)
                    continue;

                Clip clip;
                clip.kind = ClipKind::audio;
                clip.channel = channel->second;
                clip.track = item.track;
                clip.start = rescale (item.position);
                clip.length = rescale (item.length);

                // O início do trecho vem em tempos ou em milissegundos, conforme o clip;
                // descobrimos pela unidade em que (fim - início) bate com a duração.
                const auto beats = (double) item.length / (double) filePpq;
                const auto span = (double) item.endOffset - (double) item.startOffset;

                if (std::isfinite (item.startOffset) && item.startOffset > 0.0f)
                {
                    if (std::abs (span - beats) < 0.02 * std::max (1.0, beats))
                        clip.offset = (std::int64_t) std::llround (item.startOffset * ppq);
                    else if (std::abs (span - beats * 60000.0 / song.bpm) < 0.02 * std::max (1.0, span))
                        clip.offset = (std::int64_t) std::llround (item.startOffset / 1000.0 * song.bpm / 60.0 * ppq);
                }

                clips.push_back (clip);
                continue;
            }

            const auto it = patternMap.find (item.index);

            if (it == patternMap.end())
                continue;

            Clip clip;
            clip.pattern = it->second;
            clip.track = item.track;
            clip.start = rescale (item.position);
            clip.length = rescale (item.length);
            clip.offset = item.patternStartTick > 0 ? rescale (item.patternStartTick) : 0;
            clips.push_back (clip);
        }

        std::sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });

        Arrangement a;
        a.name = arrangement.name.empty() ? "Arrangement " + std::to_string (song.arrangements.size() + 1) : arrangement.name;

        // marcadores; os de tipo 8 mudam o compasso (as ações de loop/pausa viram marcadores comuns)
        for (const auto& m : arrangement.markers)
        {
            Marker marker;
            marker.tick = rescale (m.position);
            marker.name = m.name;
            marker.name.erase (0, marker.name.find_first_not_of (' '));
            marker.name.erase (marker.name.find_last_not_of (' ') + 1);

            if (m.type == 8 && m.numerator >= 1 && m.numerator <= 32 && m.denominator >= 1 && m.denominator <= 32)
            {
                marker.numerator = m.numerator;
                marker.denominator = m.denominator;
            }

            a.markers.push_back (std::move (marker));
        }

        std::stable_sort (a.markers.begin(), a.markers.end(), [] (const Marker& x, const Marker& y) { return x.tick < y.tick; });
        importedTracks.push_back (&arrangement.tracks);

        if (song.arrangements.empty())
            song.playlist = std::move (clips); // o primeiro é o atual
        else
            a.clips = std::move (clips);

        song.arrangements.push_back (std::move (a));
    }

    // o .flp não diz qual arrangement estava aberto; o mais cheio costuma ser a música
    {
        int fullest = 0;
        std::size_t most = song.playlist.size();

        for (int i = 1; i < (int) song.arrangements.size(); ++i)
            if (song.arrangements[(std::size_t) i].clips.size() > most)
            {
                most = song.arrangements[(std::size_t) i].clips.size();
                fullest = i;
            }

        song.selectArrangement (fullest);

        // nomes e mute das trilhas da Playlist (do arrangement aberto)
        if (fullest < (int) importedTracks.size())
            for (const auto& t : *importedTracks[(std::size_t) fullest])
                if (t.index >= 0 && t.index < 500 && (! t.name.empty() || ! t.enabled))
                {
                    if ((int) song.playlistTracks.size() <= t.index)
                        song.playlistTracks.resize ((std::size_t) t.index + 1);

                    song.playlistTracks[(std::size_t) t.index] = { t.name, ! t.enabled };
                }
    }

    if (! song.playlist.empty())
        song.mode = PlayMode::song;


    if (song.patterns.empty())
        song.patterns.emplace_back().name = "Pattern 1";

    // O .flp não diz qual pattern estava aberta; a mais cheia é o melhor ponto de partida.
    for (int i = 0; i < (int) song.patterns.size(); ++i)
        if (song.patterns[(std::size_t) i].notes.size() > song.patterns[(std::size_t) song.currentPattern].notes.size())
            song.currentPattern = i;

    return song;
}
} // namespace lynx
