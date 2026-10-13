#include "flp/Flp.h"

#include <algorithm>
#include <cstring>

namespace flp
{
namespace
{
// IDs de evento (nomes descrevem o significado, não seguem nenhuma API)
enum : std::uint32_t
{
    evChannelType = 0x15,
    evChannelMixerTrackOld = 0x16, // u8, arquivos antigos
    evNewChannel = 0x40,
    evNewPattern = 0x41,
    evTempoCoarse = 0x42,       // legado: BPM inteiro
    evArrangement = 0x63,
    evChannelMixerTrack = 0x68,    // u16
    evPatternColour = 0x96,     // u32 0x00BBGGRR; 0x565148 = cinza padrão
    evTempo = 0x9C,             // BPM * 1000
    evChannelNameOld = 0xC0,    // FL <= 11
    evPatternName = 0xC1,
    evTitle = 0xC2,
    evSamplePath = 0xC4,
    evUrl = 0xC5,
    evVersion = 0xC7,
    evPluginInternalName = 0xC9,
    evChannelName = 0xCB,
    evMixerInsertName = 0xCC,
    evGenre = 0xCE,
    evArtist = 0xCF,
    evPatternNotes = 0xE0,
    evControlValues = 0xE1,     // registros de 12 B {u32 ?, u32 controlID, i32 valor}
    evAutomationLink = 0xE3,    // registros de 20 B {u32 canal<<16, u32 0, u32 controlID, u32 flags, u32 ?}
    evAutomationPoints = 0xEA,  // header 21 B + 24 B por ponto + 112 B
    evMixerRoutes = 0xEB,       // um byte por trilha de destino
    evPlaylistItems = 0xE9,
    evMixerInsertParams = 0xEC, // abre um novo insert do mixer
    evArrangementName = 0xF1,
};

constexpr std::size_t noteRecordSize = 24;
constexpr std::size_t automationHeaderSize = 21;
constexpr std::size_t automationPointSize = 24;
constexpr std::uint16_t playlistPatternBase = 20480;

constexpr std::uint32_t version (std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
{
    return major * 1'000'000u + minor * 1'000u + patch;
}

template <typename T>
T readLE (std::span<const std::uint8_t> s, std::size_t offset)
{
    T v {};
    std::memcpy (&v, s.data() + offset, sizeof (T)); // x86/ARM são little-endian
    return v;
}

void appendUtf8 (std::string& out, std::uint32_t cp)
{
    if (cp < 0x80)
        out += (char) cp;
    else if (cp < 0x800)
    {
        out += (char) (0xC0 | (cp >> 6));
        out += (char) (0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += (char) (0xE0 | (cp >> 12));
        out += (char) (0x80 | ((cp >> 6) & 0x3F));
        out += (char) (0x80 | (cp & 0x3F));
    }
    else
    {
        out += (char) (0xF0 | (cp >> 18));
        out += (char) (0x80 | ((cp >> 12) & 0x3F));
        out += (char) (0x80 | ((cp >> 6) & 0x3F));
        out += (char) (0x80 | (cp & 0x3F));
    }
}

std::string decodeUtf16 (std::span<const std::uint8_t> s)
{
    std::string out;
    out.reserve (s.size() / 2);

    for (std::size_t i = 0; i + 1 < s.size(); i += 2)
    {
        std::uint32_t cp = readLE<std::uint16_t> (s, i);

        if (cp == 0)
            break;

        if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < s.size())
        {
            const std::uint32_t low = readLE<std::uint16_t> (s, i + 2);

            if (low >= 0xDC00 && low < 0xE000)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }

        appendUtf8 (out, cp);
    }

    return out;
}

std::string decodeAnsi (std::span<const std::uint8_t> s)
{
    std::string out;

    for (auto b : s)
    {
        if (b == 0)
            break;

        appendUtf8 (out, b); // trata como Latin-1
    }

    return out;
}

template <typename T>
T& findOrAdd (std::vector<T>& items, int index)
{
    for (auto& item : items)
        if (item.index == index)
            return item;

    auto& item = items.emplace_back();
    item.index = index;
    return item;
}
} // namespace

// ---------------------------------------------------------------------------

EventReader::EventReader (std::span<const std::uint8_t> file)
    : bytes (file)
{
    if (bytes.size() < 22 || std::memcmp (bytes.data(), "FLhd", 4) != 0)
        throw Error ("não é um arquivo .flp (cabeçalho FLhd ausente)");

    const auto headerSize = readLE<std::uint32_t> (bytes, 4);

    if (headerSize < 6 || 8 + (std::size_t) headerSize + 8 > bytes.size())
        throw Error ("cabeçalho FLhd inválido");

    hdr.format = readLE<std::uint16_t> (bytes, 8);
    hdr.channelCount = readLE<std::uint16_t> (bytes, 10);
    hdr.ppq = readLE<std::uint16_t> (bytes, 12);

    pos = 8 + headerSize;

    if (std::memcmp (bytes.data() + pos, "FLdt", 4) != 0)
        throw Error ("bloco FLdt ausente");

    pos += 8; // o tamanho declarado do FLdt não é confiável; lemos até o fim do arquivo
}

bool EventReader::next (Event& out)
{
    if (pos >= bytes.size())
        return false;

    auto need = [this] (std::size_t n)
    {
        if (pos + n > bytes.size())
            throw Error ("arquivo truncado no offset " + std::to_string (pos));
    };

    out = {};
    out.id = bytes[pos++];

    if (out.id == 0xAC) // escape: ID estendido de 32 bits
    {
        need (4);
        out.id = readLE<std::uint32_t> (bytes, pos);
        pos += 4;
    }

    switch (out.sizeClass())
    {
        case 0:
            need (1);
            out.value = bytes[pos];
            pos += 1;
            break;

        case 1:
            need (2);
            out.value = readLE<std::uint16_t> (bytes, pos);
            pos += 2;
            break;

        case 2:
            need (4);
            out.value = readLE<std::uint32_t> (bytes, pos);
            pos += 4;
            break;

        default:
        {
            std::uint64_t size = 0;

            for (int shift = 0;; shift += 7)
            {
                need (1);

                if (shift > 28)
                    throw Error ("varint de tamanho inválido no offset " + std::to_string (pos));

                const auto b = bytes[pos++];
                size |= (std::uint64_t) (b & 0x7F) << shift;

                if ((b & 0x80) == 0)
                    break;
            }

            need ((std::size_t) size);
            out.data = bytes.subspan (pos, (std::size_t) size);
            pos += (std::size_t) size;
            break;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------

std::uint32_t parseVersionNumber (const std::string& text)
{
    std::uint32_t parts[3] = {};
    int part = 0;

    for (char c : text)
    {
        if (c == '.')
        {
            if (++part == 3)
                break;
        }
        else if (c >= '0' && c <= '9')
            parts[part] = parts[part] * 10 + (std::uint32_t) (c - '0');
        else
            break;
    }

    return version (parts[0], parts[1], parts[2]);
}

ControlId normalizeControlId (std::uint32_t raw)
{
    // antes do 24.2.99 a classe do mixer era 0x2... (o FL soma 0x50000000 ao carregar) e os
    // envios ficavam em 0x1F40 + destino
    if ((raw >> 28) == 2)
    {
        raw += 0x50000000u;
        const auto p = raw & 0xFFFF;

        if (p >= 0x1F40 && p < 0x1FC0)
            raw = (raw & 0xFFFF0000u) | (std::uint32_t) (ControlId::mixerSendBase + (int) (p - 0x1F40));
    }

    return { raw };
}

std::size_t playlistRecordSize (std::uint32_t versionNumber, std::size_t payloadSize)
{
    // Tamanhos observados nos demos do FL 2026; a troca 60 → 80 cai entre 24.1.0 e 24.2.99.
    const std::size_t preferred = versionNumber < version (20, 99, 1)   ? 32
                                  : versionNumber < version (24, 2, 99) ? 60
                                                                         : 80;

    if (payloadSize % preferred == 0)
        return preferred;

    for (std::size_t size : { 80u, 60u, 32u })
        if (payloadSize % size == 0)
            return size;

    return 0;
}

const Pattern* Project::findPattern (int index) const noexcept
{
    auto it = std::find_if (patterns.begin(), patterns.end(), [index] (auto& p) { return p.index == index; });
    return it != patterns.end() ? &*it : nullptr;
}

const Channel* Project::findChannel (int index) const noexcept
{
    auto it = std::find_if (channels.begin(), channels.end(), [index] (auto& c) { return c.index == index; });
    return it != channels.end() ? &*it : nullptr;
}

Project parseProject (std::span<const std::uint8_t> file)
{
    EventReader reader (file);
    Project project;
    project.header = reader.header();

    if (project.header.format != 0)
        project.warnings.push_back ("formato " + std::to_string (project.header.format) + " não é projeto");

    // Os eventos são contextuais: um NewChannel/NewPattern/Arrangement define
    // a quem pertencem os eventos seguintes, até o próximo seletor.
    enum class Section { global, channel, arrangement, mixer } section = Section::global;
    int currentChannel = -1, currentPattern = -1, currentArrangement = -1;
    bool hasFineTempo = false;
    std::string pendingInsertName;

    auto text = [&] (const Event& e)
    {
        return project.versionNumber > version (11, 5, 4) ? decodeUtf16 (e.data) : decodeAnsi (e.data);
    };
    auto channel = [&]() -> Channel& { return findOrAdd (project.channels, currentChannel); };
    auto pattern = [&]() -> Pattern& { return findOrAdd (project.patterns, currentPattern); };
    auto arrangement = [&]() -> Arrangement& { return findOrAdd (project.arrangements, currentArrangement); };

    Event e;

    while (reader.next (e))
    {
        switch (e.id)
        {
            case evVersion:
                project.version = decodeAnsi (e.data); // sempre ASCII: vem antes de sabermos a versão
                project.versionNumber = parseVersionNumber (project.version);
                break;

            case evTempo:
                project.tempo = e.value / 1000.0;
                hasFineTempo = true;
                break;

            case evTempoCoarse:
                if (! hasFineTempo)
                    project.tempo = e.value;
                break;

            case evTitle: project.title = text (e); break;
            case evArtist: project.artist = text (e); break;
            case evGenre: project.genre = text (e); break;
            case evUrl: project.url = text (e); break;

            case evNewPattern:
                currentPattern = (int) e.value;
                pattern();
                break;

            case evPatternName:
                if (currentPattern >= 0)
                    pattern().name = text (e);
                break;

            case evPatternColour:
                if (currentPattern >= 0)
                    pattern().colour = ((e.value & 0xFF) << 16) | (e.value & 0xFF00) | ((e.value >> 16) & 0xFF);
                break;

            case evPatternNotes:
            {
                if (currentPattern < 0)
                    break;

                if (e.data.size() % noteRecordSize != 0)
                {
                    project.warnings.push_back ("notas da pattern " + std::to_string (currentPattern)
                                                + " com tamanho inesperado; ignoradas");
                    break;
                }

                auto& notes = pattern().notes;

                for (std::size_t o = 0; o < e.data.size(); o += noteRecordSize)
                {
                    Note n;
                    n.position = readLE<std::uint32_t> (e.data, o + 0);
                    n.flags = readLE<std::uint16_t> (e.data, o + 4);
                    n.channel = readLE<std::uint16_t> (e.data, o + 6);
                    n.length = readLE<std::uint32_t> (e.data, o + 8);
                    n.key = readLE<std::uint16_t> (e.data, o + 12);
                    n.finePitch = e.data[o + 16];
                    n.release = e.data[o + 18];
                    n.pan = e.data[o + 20];
                    n.velocity = e.data[o + 21];
                    n.modX = e.data[o + 22];
                    n.modY = e.data[o + 23];
                    notes.push_back (n);
                }
                break;
            }

            case evNewChannel:
                section = Section::channel;
                currentChannel = (int) e.value;
                channel();
                break;

            case evChannelType:
                if (section == Section::channel)
                    channel().type = e.value <= 5 ? (ChannelType) e.value : ChannelType::unknown;
                break;

            case evChannelMixerTrackOld:
                if (section == Section::channel && channel().mixerTrack < 0)
                    channel().mixerTrack = (int) e.value;
                break;

            case evChannelMixerTrack:
                if (section == Section::channel)
                    channel().mixerTrack = (int) e.value;
                break;

            case evChannelName:
                if (section == Section::channel)
                    channel().name = text (e);
                break;

            case evChannelNameOld:
                if (section == Section::channel && channel().name.empty())
                    channel().name = text (e);
                break;

            case evPluginInternalName:
                // Também aparece nos slots do mixer; só interessa dentro de um canal.
                if (section == Section::channel && channel().pluginName.empty())
                    channel().pluginName = text (e);
                break;

            case evSamplePath:
                if (section == Section::channel)
                    channel().samplePath = text (e);
                break;

            case evArrangement:
                section = Section::arrangement;
                currentArrangement = (int) e.value;
                arrangement();
                break;

            case evArrangementName:
                if (section == Section::arrangement)
                    arrangement().name = text (e);
                break;

            case evPlaylistItems:
            {
                if (currentArrangement < 0)
                {
                    currentArrangement = 0;
                    arrangement(); // arquivos antigos não têm o evento de arrangement
                }

                const auto recordSize = playlistRecordSize (project.versionNumber, e.data.size());

                if (recordSize == 0)
                {
                    project.warnings.push_back ("itens da playlist com tamanho inesperado; ignorados");
                    break;
                }

                auto& items = arrangement().items;
                // A playlist passou de 199 para 500 trilhas no FL 12.9.
                const int trackCount = project.versionNumber < version (12, 9, 0) ? 199 : 500;

                for (std::size_t o = 0; o + recordSize <= e.data.size(); o += recordSize)
                {
                    PlaylistItem item;
                    item.position = readLE<std::uint32_t> (e.data, o + 0);
                    const auto base = readLE<std::uint16_t> (e.data, o + 4);
                    const auto index = readLE<std::uint16_t> (e.data, o + 6);
                    item.length = readLE<std::uint32_t> (e.data, o + 8);
                    const auto reversedTrack = readLE<std::uint16_t> (e.data, o + 12);
                    item.flags = readLE<std::uint16_t> (e.data, o + 18);
                    item.startOffset = readLE<float> (e.data, o + 24);
                    item.endOffset = readLE<float> (e.data, o + 28);
                    item.patternStartTick = readLE<std::int32_t> (e.data, o + 24);
                    item.patternEndTick = readLE<std::int32_t> (e.data, o + 28);

                    const auto patternBase = base != 0 ? base : playlistPatternBase;

                    if (index > patternBase)
                    {
                        item.kind = PlaylistItem::Kind::pattern;
                        item.index = index - patternBase;
                    }
                    else
                    {
                        item.kind = PlaylistItem::Kind::channelClip;
                        item.index = index;
                    }

                    item.track = reversedTrack < trackCount ? trackCount - 1 - reversedTrack : (int) reversedTrack;
                    items.push_back (item);
                }
                break;
            }

            case evControlValues:
                for (std::size_t o = 0; o + 12 <= e.data.size(); o += 12)
                    project.controls.push_back ({ normalizeControlId (readLE<std::uint32_t> (e.data, o + 4)),
                                                  readLE<std::int32_t> (e.data, o + 8) });
                break;

            case evAutomationLink:
                for (std::size_t o = 0; o + 20 <= e.data.size(); o += 20)
                    if ((readLE<std::uint32_t> (e.data, o + 12) & 8) != 0) // 8 = clip de automação (4 = controle remoto)
                        project.automationLinks.push_back ({ (int) (readLE<std::uint32_t> (e.data, o) >> 16),
                                                             normalizeControlId (readLE<std::uint32_t> (e.data, o + 8)) });
                break;

            case evAutomationPoints:
            {
                if (section != Section::channel || e.data.size() < automationHeaderSize)
                    break;

                const auto count = readLE<std::uint32_t> (e.data, 17);

                if (automationHeaderSize + (std::size_t) count * automationPointSize > e.data.size())
                {
                    project.warnings.push_back ("automação do canal " + std::to_string (currentChannel) + " truncada; ignorada");
                    break;
                }

                auto& points = channel().automation;
                points.clear();
                double position = 0.0;

                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const auto o = automationHeaderSize + (std::size_t) i * automationPointSize;
                    AutomationPoint p;
                    position += readLE<double> (e.data, o); // distância até o ponto anterior
                    p.position = position;
                    p.value = readLE<double> (e.data, o + 8);
                    p.tension = readLE<float> (e.data, o + 16);
                    p.curve = e.data[o + 20];
                    points.push_back (p);
                }
                break;
            }

            case evMixerRoutes:
                if (section == Section::mixer && ! project.mixer.empty())
                {
                    auto& insert = project.mixer.back();
                    insert.hasRoutes = true;
                    insert.routes.clear();

                    for (std::size_t i = 0; i < e.data.size(); ++i)
                    {
                        if (e.data[i] == 0)
                            continue;

                        // arquivos antigos tinham menos trilhas; as extras ("Current") viram 500/501
                        auto target = (int) i;
                        if (project.versionNumber < version (8, 5, 0) && target > 64)
                            target += 35;
                        else if (project.versionNumber < version (12, 9, 0) && target > 103)
                            target += 397;
                        else if (project.versionNumber < version (24, 2, 99) && target > 125)
                            target += 375;

                        insert.routes.push_back (target);
                    }
                }
                break;

            case evMixerInsertName:
                // O nome vem antes do evento que abre o insert ao qual pertence
                // (confirmado cruzando com a rota dos canais nos demos).
                pendingInsertName = text (e);
                break;

            case evMixerInsertParams:
            {
                section = Section::mixer;
                auto& insert = project.mixer.emplace_back();
                insert.index = (int) project.mixer.size() - 1;
                insert.name = std::move (pendingInsertName);
                pendingInsertName.clear();

                if (e.data.size() >= 8)
                    insert.flags = readLE<std::uint32_t> (e.data, 4);
                break;
            }

            default:
                break;
        }
    }

    for (auto& p : project.patterns)
        std::stable_sort (p.notes.begin(), p.notes.end(), [] (auto& a, auto& b) { return a.position < b.position; });

    return project;
}
} // namespace flp
