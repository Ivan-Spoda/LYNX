#include "core/FlpImport.h"

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

        // envelope de volume (registro 1 do 0xDA) -> ADSR do sampler
        if (const auto& env = c.envelopes[1]; env.present && env.enabled)
        {
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

        // time-stretch (só reamostrando, por enquanto): o sample dura TIME/192 tempos × MUL
        if (c.stretchTime > 0.0f)
            def.sampler.stretchBeats = (float) (c.stretchTime / 192.0 * std::pow (2.0, c.stretchMul / 10000.0));
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
