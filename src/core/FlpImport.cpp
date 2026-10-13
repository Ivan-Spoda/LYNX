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

Song importFlp (const flp::Project& project, const FlpImportOptions& options)
{
    Song song;
    song.bpm = project.tempo > 0.0 ? project.tempo : 140.0;

    // Canais do FL podem ter índices esparsos; mapeamos para índices densos.
    std::map<int, int> channelMap;

    for (const auto& c : project.channels)
    {
        // Clips de automação ainda não existem no nosso modelo.
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
        song.channels.push_back (std::move (def));
    }

    // Mixer: nomes do .flp; inserts suficientes para todas as rotas (mínimo 16, como um projeto novo).
    int highestTrack = 16;
    for (const auto& c : song.channels)
        highestTrack = std::max (highestTrack, c.mixerTrack);

    song.mixer = Song::defaultMixer (highestTrack);

    for (const auto& insert : project.mixer)
        if (insert.index > 0 && insert.index < (int) song.mixer.size() && ! insert.name.empty())
            song.mixer[(std::size_t) insert.index].name = insert.name;

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
            p.notes.push_back (note);
        }

        p.sortNotes();

        if (! p.notes.empty())
        {
            patternMap[src.index] = (int) song.patterns.size();
            song.patterns.push_back (std::move (p));
        }
    }

    // Playlist: só o primeiro arrangement; clips de pattern e de áudio
    // (clips de automação ainda não existem no nosso modelo).
    if (! project.arrangements.empty())
    {
        for (const auto& item : project.arrangements.front().items)
        {
            if (item.length == 0)
                continue;

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

                song.playlist.push_back (clip);
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
            song.playlist.push_back (clip);
        }

        std::sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });

        if (! song.playlist.empty())
            song.mode = PlayMode::song;
    }

    if (song.patterns.empty())
        song.patterns.emplace_back().name = "Pattern 1";

    // O .flp não diz qual pattern estava aberta; a mais cheia é o melhor ponto de partida.
    for (int i = 0; i < (int) song.patterns.size(); ++i)
        if (song.patterns[(std::size_t) i].notes.size() > song.patterns[(std::size_t) song.currentPattern].notes.size())
            song.currentPattern = i;

    return song;
}
} // namespace lynx
