// flpdump: mostra o conteúdo de um projeto .flp.
// Uso: flpdump [--events] arquivo.flp

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "flp/Flp.h"

namespace
{
const char* typeName (flp::ChannelType t)
{
    switch (t)
    {
        case flp::ChannelType::sampler: return "sampler";
        case flp::ChannelType::native: return "nativo";
        case flp::ChannelType::instrument: return "instrumento";
        case flp::ChannelType::layer: return "layer";
        case flp::ChannelType::audioClip: return "audio clip";
        case flp::ChannelType::automationClip: return "automacao";
        case flp::ChannelType::unknown: break;
    }
    return "?";
}

void dumpEvents (const std::vector<std::uint8_t>& bytes)
{
    flp::EventReader reader (bytes);
    std::map<std::uint32_t, std::pair<int, std::size_t>> stats; // id -> (contagem, bytes)
    flp::Event e;

    while (reader.next (e))
    {
        auto& s = stats[e.id];
        ++s.first;
        s.second += e.data.size();
    }

    std::printf ("%-12s %8s %10s\n", "evento", "qtd", "bytes");

    for (auto& [id, s] : stats)
        std::printf ("0x%-10X %8d %10zu\n", id, s.first, s.second);
}

void dumpProject (const std::vector<std::uint8_t>& bytes)
{
    const auto p = flp::parseProject (bytes);

    std::printf ("Versao     : %s\n", p.version.c_str());
    std::printf ("PPQ        : %u\n", p.header.ppq);
    std::printf ("Tempo      : %.3f BPM\n", p.tempo);

    if (! p.title.empty()) std::printf ("Titulo     : %s\n", p.title.c_str());
    if (! p.artist.empty()) std::printf ("Artista    : %s\n", p.artist.c_str());
    if (! p.genre.empty()) std::printf ("Genero     : %s\n", p.genre.c_str());

    std::printf ("\nCanais (%zu)\n", p.channels.size());
    for (auto& c : p.channels)
        std::printf ("  %3d  %-11s  %-24s  plugin=%-16s  mixer=%d%s%s\n", c.index, typeName (c.type),
                     c.name.c_str(), c.pluginName.c_str(), c.mixerTrack,
                     c.samplePath.empty() ? "" : "  sample=", c.samplePath.c_str());

    std::printf ("\nPatterns (%zu)\n", p.patterns.size());
    for (auto& pat : p.patterns)
    {
        std::uint32_t end = 0;
        for (auto& n : pat.notes)
            end = std::max (end, n.position + n.length);

        std::printf ("  %3d  %-24s  %5zu notas  ate o tick %u\n", pat.index, pat.name.c_str(), pat.notes.size(), end);
    }

    for (auto& a : p.arrangements)
    {
        std::printf ("\nArrangement %d \"%s\": %zu itens na playlist\n", a.index, a.name.c_str(), a.items.size());

        int shown = 0;
        for (auto& item : a.items)
        {
            if (shown++ == 15)
            {
                std::printf ("  ...\n");
                break;
            }

            const bool isPattern = item.kind == flp::PlaylistItem::Kind::pattern;
            std::string what;

            if (isPattern)
            {
                const auto* pat = p.findPattern (item.index);
                what = "pattern " + std::to_string (item.index) + (pat && ! pat->name.empty() ? " (" + pat->name + ")" : "");
            }
            else
            {
                const auto* ch = p.findChannel (item.index);
                what = "clip do canal " + std::to_string (item.index) + (ch ? " (" + ch->name + ")" : "");
            }

            std::printf ("  trilha %3d  tick %7u  dur %6u  %s\n", item.track, item.position, item.length, what.c_str());
        }
    }

    std::printf ("\nMixer: %zu inserts\n", p.mixer.size());
    for (auto& m : p.mixer)
        if (! m.name.empty())
            std::printf ("  %3d  %s\n", m.index, m.name.c_str());

    for (auto& w : p.warnings)
        std::printf ("AVISO: %s\n", w.c_str());
}
} // namespace

int main (int argc, char** argv)
{
    bool events = false;
    const char* path = nullptr;

    for (int i = 1; i < argc; ++i)
    {
        if (std::string (argv[i]) == "--events")
            events = true;
        else
            path = argv[i];
    }

    if (path == nullptr)
    {
        std::fprintf (stderr, "uso: flpdump [--events] arquivo.flp\n");
        return 2;
    }

    std::ifstream in (path, std::ios::binary);

    if (! in)
    {
        std::fprintf (stderr, "nao foi possivel abrir %s\n", path);
        return 1;
    }

    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());

    try
    {
        events ? dumpEvents (bytes) : dumpProject (bytes);
    }
    catch (const flp::Error& e)
    {
        std::fprintf (stderr, "erro: %s\n", e.what());
        return 1;
    }

    return 0;
}
