#include "core/FlpImport.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace daw
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

Song importFlp (const flp::Project& project)
{
    Song song;
    song.bpm = project.tempo > 0.0 ? project.tempo : 140.0;

    // Canais do FL podem ter índices esparsos; mapeamos para índices densos.
    std::map<int, int> channelMap;

    for (const auto& c : project.channels)
    {
        // Clips de áudio e automação vivem na playlist, não no Channel Rack.
        if (c.type == flp::ChannelType::audioClip || c.type == flp::ChannelType::automationClip)
            continue;

        channelMap[c.index] = (int) song.channels.size();

        ChannelDef def;
        def.name = c.name.empty() ? "Canal " + std::to_string (c.index + 1) : c.name;
        def.instrument = guessInstrument (def.name, c.type);
        song.channels.push_back (std::move (def));
    }

    const auto filePpq = project.header.ppq > 0 ? (std::int64_t) project.header.ppq : 96;
    auto rescale = [filePpq] (std::int64_t ticks) { return ticks * ppq / filePpq; };

    for (const auto& src : project.patterns)
    {
        Pattern p;
        p.name = src.name.empty() ? "Pattern " + std::to_string (src.index) : src.name;

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
            song.patterns.push_back (std::move (p));
    }

    if (song.patterns.empty())
        song.patterns.emplace_back().name = "Pattern 1";

    return song;
}
} // namespace daw
