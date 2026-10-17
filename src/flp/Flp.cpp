#include "flp/Flp.h"

#include <algorithm>
#include <cstring>

namespace flp
{
namespace
{
// Event IDs (names describe the meaning, they do not follow any API)
enum : std::uint32_t
{
    evSwing = 0x0B,
    evPanLaw = 0x17,
    evTimeSignatureNumerator = 0x11,
    evChannelSwingMix = 0x61,
    evChannelCutGroups = 0x84,
    evSampleRootNote = 0x87,
    evFileFlags = 0xA9,
    evChannelParams = 0xD7,
    evChannelEnvelope = 0xDA,
    evPluginState = 0xD5,       // plugin blob (channel generator or slot effect)
    evChannelPolyphony = 0xDD,
    evChannelType = 0x15,
    evChannelEnabled = 0x00,    // u8: 0 = the channel is switched off in the Channel Rack
    evPluginNew = 0xD4,         // plugin record: i32 mixer insert (or channel), i32 slot, i32 kind (2 = effect), ...
    evChannelMixerTrackOld = 0x16, // u8, old files
    evNewChannel = 0x40,
    evNewPattern = 0x41,
    evTempoCoarse = 0x42,       // legacy: integer BPM
    evArrangement = 0x63,
    evChannelMixerTrack = 0x68,    // u16
    evPatternColour = 0x96,     // u32 0x00BBGGRR; 0x565148 = default gray
    evTempo = 0x9C,             // BPM * 1000
    evChannelNameOld = 0xC0,    // FL <= 11
    evPatternName = 0xC1,
    evTitle = 0xC2,
    evComments = 0xC3,
    evSamplePath = 0xC4,
    evUrl = 0xC5,
    evVersion = 0xC7,
    evPluginInternalName = 0xC9,
    evChannelName = 0xCB,
    evMixerInsertName = 0xCC,
    evGenre = 0xCE,
    evArtist = 0xCF,
    evPatternNotes = 0xE0,
    evControlValues = 0xE1,     // 12 B records {u32 ?, u32 controlID, i32 value}
    evAutomationLink = 0xE3,    // 20 B records {u32 channel<<16, u32 0, u32 controlID, u32 flags, u32 ?}
    evAutomationPoints = 0xEA,  // header 21 B + 24 B per point + 112 B
    evMixerRoutes = 0xEB,       // one byte per destination track
    evChannelLevels = 0xDB,
    evChannelColour = 0x80,
    evMixerInsertColour = 0x95, // u32 0x00BBGGRR; comes before the 0xEC that opens the insert
    evMixerSlotIndex = 0x62,    // u16: closes the effect slot described by the preceding events     // u32 0x00BBGGRR (also appears in mixer slots)     // i32 pan, i32 volume, i32 pitch (cents), ...
    evPlaylistTrack = 0xEE,     // u32 index (1..500), u32 colour, u32 icon, u8 enabled, ...
    evPlaylistTrackName = 0xEF,
    evPlaylistItems = 0xE9,
    evMixerInsertParams = 0xEC, // opens a new mixer insert
    evArrangementName = 0xF1,
    evTimeMarker = 0x94,          // u32 type << 24 | position
    evTimeMarkerNumerator = 0x21, // u8
    evTimeMarkerDenominator = 0x22,
    evTimeMarkerName = 0xCD,
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
    std::memcpy (&v, s.data() + offset, sizeof (T)); // x86/ARM are little-endian
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

        appendUtf8 (out, b); // treat as Latin-1
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
        throw Error ("not a .flp file (missing FLhd header)");

    const auto headerSize = readLE<std::uint32_t> (bytes, 4);

    if (headerSize < 6 || 8 + (std::size_t) headerSize + 8 > bytes.size())
        throw Error ("invalid FLhd header");

    hdr.format = readLE<std::uint16_t> (bytes, 8);
    hdr.channelCount = readLE<std::uint16_t> (bytes, 10);
    hdr.ppq = readLE<std::uint16_t> (bytes, 12);

    pos = 8 + headerSize;

    if (std::memcmp (bytes.data() + pos, "FLdt", 4) != 0)
        throw Error ("missing FLdt chunk");

    pos += 8; // the declared FLdt size is not reliable; we read to the end of the file
}

bool EventReader::next (Event& out)
{
    if (pos >= bytes.size())
        return false;

    auto need = [this] (std::size_t n)
    {
        if (pos + n > bytes.size())
            throw Error ("file truncated at offset " + std::to_string (pos));
    };

    out = {};
    out.id = bytes[pos++];

    if (out.id == 0xAC) // escape: 32-bit extended ID
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
                    throw Error ("invalid size varint at offset " + std::to_string (pos));

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

double Channel::envelopeTime (int v) noexcept
{
    return v < 101 ? 0.0 : 0.064 * (std::pow (1000.0, v / 65536.0) - 1.0);
}

ControlId normalizeControlId (std::uint32_t raw)
{
    // before 24.2.99 the mixer class was 0x2... (FL adds 0x50000000 on load) and the
    // sends were at 0x1F40 + destination. Tracks >= 64 overflow into 0x3... (same arithmetic).
    if ((raw >> 28) == 2 || (raw >> 28) == 3)
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
    // Sizes observed in the FL 2026 demos; the 60 → 80 change falls between 24.1.0 and 24.2.99.
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

Project parseProject (std::span<const std::uint8_t> file, bool channelPreset)
{
    EventReader reader (file);
    Project project;
    project.header = reader.header();

    if (project.header.format != 0 && ! channelPreset)
        project.warnings.push_back ("format " + std::to_string (project.header.format) + " is not a project");

    // Events are contextual: a NewChannel/NewPattern/Arrangement determines
    // who the following events belong to, until the next selector.
    enum class Section { global, channel, arrangement, mixer } section = Section::global;
    int currentChannel = -1, currentPattern = -1, currentArrangement = -1;

    if (channelPreset)
    {
        section = Section::channel;
        currentChannel = 0;
    }
    bool hasFineTempo = false;
    bool hasTimeSignature = false;
    std::string pendingInsertName;
    std::string pendingSlotPlugin;
    std::vector<std::uint8_t> pendingSlotState;
    int pendingSlotIndex = -1; // from 0xD4: FL <= 11 files have no 0x62, the next slot/route/insert closes the slot
    auto closeOldSlot = [&] {
        if (pendingSlotIndex >= 0 && ! pendingSlotPlugin.empty() && ! project.mixer.empty())
            project.mixer.back().slots.push_back ({ pendingSlotIndex, pendingSlotPlugin, std::move (pendingSlotState) });
        if (pendingSlotIndex >= 0)
        {
            pendingSlotPlugin.clear();
            pendingSlotState.clear();
        }
        pendingSlotIndex = -1;
    };
    std::uint32_t pendingInsertColour = 0;

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
                project.version = decodeAnsi (e.data); // always ASCII: comes before we know the version
                project.versionNumber = parseVersionNumber (project.version);
                break;

            case evTempo:
                project.tempo = e.value / 1000.0;
                hasFineTempo = true;
                break;

            case evSwing:
                project.swing = (int) e.value;
                break;

            case evPanLaw:
                project.panLaw = (int) e.value;
                break;

            case evFileFlags:
                project.flagsA9 = e.value;
                break;

            case evChannelSwingMix:
                if (section == Section::channel)
                    channel().swingMix = (int) e.value;
                break;

            case evChannelCutGroups:
                if (section == Section::channel)
                {
                    channel().cutGroup = (int) (e.value & 0xFFFF);
                    channel().cutBy = (int) (e.value >> 16);
                }
                break;

            case evSampleRootNote:
                if (section == Section::channel)
                    channel().rootNote = (int) e.value;
                break;

            case evChannelPolyphony:
                if (section == Section::channel && e.data.size() >= 4)
                {
                    auto& c = channel();
                    c.maxVoices = readLE<std::int32_t> (e.data, 0);

                    if (e.data.size() >= 9)
                    {
                        c.slideTime = readLE<std::int32_t> (e.data, 4);
                        c.monoFlag = (e.data[8] & 1) != 0;
                        c.portaFlag = (e.data[8] & 2) != 0;
                    }
                }
                break;

            case evChannelEnvelope:
                if (section == Section::channel && e.data.size() >= 32)
                {
                    auto& c = channel();

                    if (c.envelopeCount < 5)
                    {
                        auto& env = c.envelopes[(std::size_t) c.envelopeCount];
                        env.present = true;
                        env.flags = readLE<std::int32_t> (e.data, 0);
                        env.enabled = readLE<std::int32_t> (e.data, 4) != 0;
                        env.delay = readLE<std::int32_t> (e.data, 8);
                        env.attack = readLE<std::int32_t> (e.data, 12);
                        env.hold = readLE<std::int32_t> (e.data, 16);
                        env.decay = readLE<std::int32_t> (e.data, 20);
                        env.sustain = readLE<std::int32_t> (e.data, 24);
                        env.release = readLE<std::int32_t> (e.data, 28);
                        env.rawSize = (int) std::min<std::size_t> (e.data.size(), env.raw.size());
                        std::copy_n (e.data.begin(), env.rawSize, env.raw.begin());

                        if (e.data.size() >= 36)
                            env.amount = readLE<std::int32_t> (e.data, 32);

                        if (e.data.size() >= 0x38)
                        {
                            env.lfoDelay = readLE<std::int32_t> (e.data, 0x24);
                            env.lfoAttack = readLE<std::int32_t> (e.data, 0x28);
                            env.lfoAmount = readLE<std::int32_t> (e.data, 0x2C);
                            env.lfoSpeed = readLE<std::int32_t> (e.data, 0x30);
                            env.lfoShape = readLE<std::int32_t> (e.data, 0x34);
                        }
                    }

                    ++c.envelopeCount;
                }
                break;

            case evChannelParams:
                if (section == Section::channel && e.data.size() >= 0x70)
                {
                    auto& c = channel();
                    c.stretchTimeRaw = readLE<std::uint32_t> (e.data, 0x60);
                    c.stretchMul = readLE<std::int32_t> (e.data, 0x68);
                    c.stretchMode = readLE<std::int32_t> (e.data, 0x6C);

                    if (e.data.size() >= 0x98)
                    {
                        c.sampleStart = readLE<double> (e.data, 0x80);
                        c.sampleEnd = readLE<double> (e.data, 0x88);
                        c.startOffset = readLE<double> (e.data, 0x90);
                    }
                }
                break;

            case evTimeSignatureNumerator:
                if (! hasTimeSignature && e.value >= 1 && e.value <= 32)
                {
                    project.beatsPerBar = (int) e.value;
                    hasTimeSignature = true; // the following ones belong to patterns
                }
                break;

            case evTempoCoarse:
                if (! hasFineTempo)
                    project.tempo = e.value;
                break;

            case evTitle: project.title = text (e); break;
            case evComments: project.comments = text (e); break;
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
                    project.warnings.push_back ("notes of pattern " + std::to_string (currentPattern)
                                                + " have an unexpected size; ignored");
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
                    n.repeats = e.data[o + 17];
                    n.extra = e.data[o + 19];
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

            case evChannelEnabled:
                if (section == Section::channel)
                    channel().enabled = e.value != 0;
                break;

            case evPluginNew:
                if (section == Section::mixer && e.data.size() >= 8)
                    pendingSlotIndex = readLE<std::int32_t> (e.data, 4);
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
                // appears in channels (generator) and in mixer slots (effect, closed by 0x62)
                if (section == Section::channel && channel().pluginName.empty())
                    channel().pluginName = text (e);
                else if (section == Section::mixer)
                {
                    closeOldSlot();
                    pendingSlotPlugin = text (e);
                }
                break;

            case evMixerSlotIndex:
                if (section == Section::mixer && ! project.mixer.empty() && ! pendingSlotPlugin.empty())
                    project.mixer.back().slots.push_back ({ (int) e.value, pendingSlotPlugin, std::move (pendingSlotState) });
                pendingSlotPlugin.clear();
                pendingSlotState.clear();
                pendingSlotIndex = -1;
                break;

            case evPluginState:
                if (section == Section::channel && channel().pluginState.empty())
                    channel().pluginState.assign (e.data.begin(), e.data.end());
                else if (section == Section::mixer)
                    pendingSlotState.assign (e.data.begin(), e.data.end());
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

            case evTimeMarker:
                if (currentArrangement < 0)
                {
                    currentArrangement = 0;
                    arrangement();
                }
                arrangement().markers.push_back ({ e.value & 0xFFFFFFu, (int) (e.value >> 24), 4, 4, {} });
                break;

            case evTimeMarkerNumerator:
            case evTimeMarkerDenominator:
                if (currentArrangement >= 0 && ! arrangement().markers.empty())
                    (e.id == evTimeMarkerNumerator ? arrangement().markers.back().numerator : arrangement().markers.back().denominator)
                        = (int) e.value;
                break;

            case evTimeMarkerName:
                if (currentArrangement >= 0 && ! arrangement().markers.empty())
                    arrangement().markers.back().name = text (e);
                break;

            case evPlaylistItems:
            {
                if (currentArrangement < 0)
                {
                    currentArrangement = 0;
                    arrangement(); // old files do not have the arrangement event
                }

                const auto recordSize = playlistRecordSize (project.versionNumber, e.data.size());

                if (recordSize == 0)
                {
                    project.warnings.push_back ("playlist items with unexpected size; ignored");
                    break;
                }

                auto& items = arrangement().items;
                // The playlist went from 199 to 500 tracks in FL 12.9.
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

            case evChannelColour:
                if (section == Section::channel)
                    channel().colour = ((e.value & 0xFF) << 16) | (e.value & 0xFF00) | ((e.value >> 16) & 0xFF);
                break;

            case evChannelLevels:
                if (section == Section::channel && e.data.size() >= 12)
                {
                    auto& c = channel();
                    c.hasLevels = true;
                    c.pan = readLE<std::int32_t> (e.data, 0);
                    c.volume = readLE<std::int32_t> (e.data, 4);
                    c.pitchCents = readLE<std::int32_t> (e.data, 8);

                    if (e.data.size() >= 24)
                    {
                        c.modX = readLE<std::int32_t> (e.data, 12);
                        c.modY = readLE<std::int32_t> (e.data, 16);
                        c.filterType = readLE<std::int32_t> (e.data, 20);
                    }
                }
                break;

            case evPlaylistTrack:
                if (currentArrangement >= 0 && e.data.size() >= 13)
                {
                    PlaylistTrackInfo t;
                    t.index = (int) readLE<std::uint32_t> (e.data, 0) - 1;
                    t.enabled = e.data[12] != 0;
                    arrangement().tracks.push_back (t);
                }
                break;

            case evPlaylistTrackName:
                if (currentArrangement >= 0 && ! arrangement().tracks.empty())
                    arrangement().tracks.back().name = text (e);
                break;

            case evControlValues:
                for (std::size_t o = 0; o + 12 <= e.data.size(); o += 12)
                    project.controls.push_back ({ normalizeControlId (readLE<std::uint32_t> (e.data, o + 4)),
                                                  readLE<std::int32_t> (e.data, o + 8) });
                break;

            case evAutomationLink:
                for (std::size_t o = 0; o + 20 <= e.data.size(); o += 20)
                    if ((readLE<std::uint32_t> (e.data, o + 12) & 8) != 0) // 8 = automation clip (4 = remote control)
                    {
                        const auto source = readLE<std::uint32_t> (e.data, o);
                        const auto target = normalizeControlId (readLE<std::uint32_t> (e.data, o + 8));

                        if ((source & 0xFFFFu) >= 0x8000u) // output of a controller, not a clip
                            project.controllerLinks.push_back ({ normalizeControlId (source), target });
                        else
                            project.automationLinks.push_back ({ (int) (source >> 16), target });
                    }
                break;

            case evAutomationPoints:
            {
                if (section != Section::channel || e.data.size() < automationHeaderSize)
                    break;

                const auto count = readLE<std::uint32_t> (e.data, 17);

                if (automationHeaderSize + (std::size_t) count * automationPointSize > e.data.size())
                {
                    project.warnings.push_back ("automation of channel " + std::to_string (currentChannel) + " truncated; ignored");
                    break;
                }

                auto& points = channel().automation;
                points.clear();
                double position = 0.0;

                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const auto o = automationHeaderSize + (std::size_t) i * automationPointSize;
                    AutomationPoint p;
                    position += readLE<double> (e.data, o); // distance to the previous point
                    p.position = position;
                    p.value = readLE<double> (e.data, o + 8);
                    p.tension = readLE<float> (e.data, o + 16);
                    p.curve = e.data[o + 20];
                    points.push_back (p);
                }
                break;
            }

            case evMixerRoutes:
                if (section == Section::mixer)
                    closeOldSlot();
                if (section == Section::mixer && ! project.mixer.empty())
                {
                    auto& insert = project.mixer.back();
                    insert.hasRoutes = true;
                    insert.routes.clear();

                    for (std::size_t i = 0; i < e.data.size(); ++i)
                    {
                        if (e.data[i] == 0)
                            continue;

                        // old files had fewer tracks; the extra ones ("Current") become 500/501
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

            case evMixerInsertColour:
                pendingInsertColour = ((e.value & 0xFF) << 16) | (e.value & 0xFF00) | ((e.value >> 16) & 0xFF);
                break;

            case evMixerInsertName:
                // The name comes before the event that opens the insert it belongs to
                // (confirmed by cross-checking with the channel routing in the demos).
                pendingInsertName = text (e);
                break;

            case evMixerInsertParams:
            {
                if (section == Section::mixer)
                    closeOldSlot();
                section = Section::mixer;
                auto& insert = project.mixer.emplace_back();
                insert.index = (int) project.mixer.size() - 1;
                insert.name = std::move (pendingInsertName);
                pendingInsertName.clear();
                insert.colour = pendingInsertColour;
                pendingInsertColour = 0;

                if (e.data.size() >= 8)
                    insert.flags = readLE<std::uint32_t> (e.data, 4);
                break;
            }

            default:
                break;
        }
    }

    if (section == Section::mixer)
        closeOldSlot();

    // the stretch time is int in files without bit 0 of 0xA9, float in newer ones
    for (auto& ch : project.channels)
    {
        if ((project.flagsA9 & 1) == 0)
            ch.stretchTime = (float) (std::int32_t) ch.stretchTimeRaw;
        else
            std::memcpy (&ch.stretchTime, &ch.stretchTimeRaw, sizeof (float));

        if (! std::isfinite (ch.stretchTime) || ch.stretchTime < 0.0f)
            ch.stretchTime = 0.0f;
    }

    for (auto& p : project.patterns)
        std::stable_sort (p.notes.begin(), p.notes.end(), [] (auto& a, auto& b) { return a.position < b.position; });

    return project;
}
} // namespace flp
