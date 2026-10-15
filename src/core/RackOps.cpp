#include "core/RackOps.h"

#include <algorithm>
#include <numeric>

namespace lynx
{
const std::array<std::uint32_t, 12>& lynxPalette()
{
    static const std::array<std::uint32_t, 12> palette { 0xffd94a4fu, 0xffe07a3fu, 0xffd9b23cu, 0xff8fbf45u, 0xff3fae6au, 0xff35a8a0u,
                                                         0xff3d8fd1u, 0xff5b6ee0u, 0xff8a5cd6u, 0xffc25bb0u, 0xffd9668cu, 0xff7d8794u };
    return palette;
}

std::uint32_t randomPaletteColour (std::uint32_t current, std::uint32_t seed)
{
    const auto& palette = lynxPalette();
    // xorshift so consecutive seeds don't walk the palette in order
    auto x = seed * 2654435761u + 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    auto index = x % (std::uint32_t) palette.size();

    if (palette[index] == current)
        index = (index + 1 + (x >> 8) % (std::uint32_t) (palette.size() - 1)) % (std::uint32_t) palette.size();

    return palette[index];
}

// --- patterns ----------------------------------------------------------------------------------

bool isPatternEmpty (const Pattern& pattern) noexcept
{
    return pattern.notes.empty();
}

int stepPattern (const Song& song, int delta) noexcept
{
    if (song.patterns.empty())
        return 0;

    return std::clamp (song.currentPattern + delta, 0, (int) song.patterns.size() - 1);
}

int findNextEmptyPattern (const Song& song, int from) noexcept
{
    const auto count = (int) song.patterns.size();

    for (int i = 1; i < count; ++i)
    {
        const auto index = ((from + i) % count + count) % count;

        if (isPatternEmpty (song.patterns[(std::size_t) index]))
            return index;
    }

    return -1;
}

std::string newPatternName (const Song& song)
{
    for (int n = 1;; ++n)
    {
        const auto name = "Pattern " + std::to_string (n);

        if (std::none_of (song.patterns.begin(), song.patterns.end(), [&] (const Pattern& p) { return p.name == name; }))
            return name;
    }
}

int gotoNextEmptyPattern (Song& song)
{
    // the current pattern counts when it is empty and nothing else is
    auto index = findNextEmptyPattern (song, song.currentPattern);

    if (index < 0)
    {
        if (! song.patterns.empty() && isPatternEmpty (song.patterns[(std::size_t) song.currentPattern]))
            return song.currentPattern;

        Pattern p;
        p.name = newPatternName (song);
        song.patterns.push_back (std::move (p));
        index = (int) song.patterns.size() - 1;
    }

    song.currentPattern = index;
    return index;
}

namespace
{
// Shifts pattern clip references at or after `index` by `delta`.
void shiftPatternClips (Song& song, int index, int delta)
{
    song.forEachClipList ([index, delta] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.kind == ClipKind::pattern && c.pattern >= index)
                c.pattern += delta;
    });
}
} // namespace

int insertPattern (Song& song, int index, const std::string& name)
{
    index = std::clamp (index, 0, (int) song.patterns.size());
    shiftPatternClips (song, index, 1);
    Pattern p;
    p.name = name;
    song.patterns.insert (song.patterns.begin() + index, std::move (p));
    song.currentPattern = index;
    return index;
}

int splitPatternByChannel (Song& song, int index)
{
    if (index < 0 || index >= (int) song.patterns.size())
        return 0;

    const auto source = song.patterns[(std::size_t) index];
    std::vector<int> channels;

    for (const auto& n : source.notes)
        if (std::find (channels.begin(), channels.end(), n.channel) == channels.end())
            channels.push_back (n.channel);

    std::sort (channels.begin(), channels.end());
    std::vector<Pattern> created;

    for (auto channel : channels)
    {
        Pattern p;
        const auto channelName = channel >= 0 && channel < (int) song.channels.size() ? song.channels[(std::size_t) channel].name
                                                                                         : "Channel " + std::to_string (channel + 1);
        p.name = source.name + " - " + channelName;
        p.lengthTicks = source.lengthTicks != 0 ? source.lengthTicks : source.effectiveLength (song.beatsPerBar);
        p.colour = channel >= 0 && channel < (int) song.channels.size() ? song.channels[(std::size_t) channel].colour : source.colour;

        for (const auto& n : source.notes)
            if (n.channel == channel)
                p.notes.push_back (n);

        created.push_back (std::move (p));
    }

    if (created.empty())
        return 0;

    shiftPatternClips (song, index + 1, (int) created.size());
    song.patterns.insert (song.patterns.begin() + index + 1, created.begin(), created.end());

    if (song.currentPattern > index)
        song.currentPattern += (int) created.size();

    return (int) created.size();
}

// --- channel selection -------------------------------------------------------------------------

bool ChannelSelection::contains (int row) const noexcept
{
    return std::binary_search (rows.begin(), rows.end(), row);
}

void ChannelSelection::selectOnly (int row)
{
    rows = { row };
    anchor = row;
}

void ChannelSelection::toggle (int row)
{
    if (const auto it = std::lower_bound (rows.begin(), rows.end(), row); it != rows.end() && *it == row)
    {
        if (rows.size() > 1) // one channel always stays selected (it is the keyboard/MIDI target)
            rows.erase (it);
    }
    else
        rows.insert (it, row);

    anchor = row;
}

void ChannelSelection::extendTo (int row)
{
    rows.clear();

    for (int r = std::min (anchor, row); r <= std::max (anchor, row); ++r)
        rows.push_back (r);
}

void ChannelSelection::selectAll (int count)
{
    rows.resize ((std::size_t) std::max (0, count));
    std::iota (rows.begin(), rows.end(), 0);
}

void ChannelSelection::clamp (int count)
{
    rows.erase (std::remove_if (rows.begin(), rows.end(), [count] (int r) { return r < 0 || r >= count; }), rows.end());

    if (rows.empty() && count > 0)
        rows = { std::clamp (anchor, 0, count - 1) };

    anchor = std::clamp (anchor, 0, std::max (0, count - 1));
}

// --- channel operations ------------------------------------------------------------------------

void permuteChannels (Song& song, const std::vector<int>& newOrder)
{
    const auto count = (int) song.channels.size();

    if ((int) newOrder.size() != count)
        return;

    std::vector<int> newIndexOf ((std::size_t) count, -1); // old -> new

    for (int i = 0; i < count; ++i)
    {
        const auto old = newOrder[(std::size_t) i];

        if (old < 0 || old >= count || newIndexOf[(std::size_t) old] >= 0)
            return; // not a permutation

        newIndexOf[(std::size_t) old] = i;
    }

    auto map = [&] (int channel) { return channel >= 0 && channel < count ? newIndexOf[(std::size_t) channel] : channel; };

    std::vector<ChannelDef> reordered;
    reordered.reserve ((std::size_t) count);

    for (auto old : newOrder)
        reordered.push_back (std::move (song.channels[(std::size_t) old]));

    song.channels = std::move (reordered);

    for (auto& p : song.patterns)
    {
        for (auto& n : p.notes)
            n.channel = map (n.channel);

        p.sortNotes();
    }

    song.forEachClipList ([&] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            if (c.kind == ClipKind::audio)
                c.channel = map (c.channel);
    });

    song.forEachTarget ([&] (AutomationTarget& t) {
        if (t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
            || t.kind == AutomationTargetKind::channelParam)
            t.index = map (t.index);
    });

    for (auto& a : song.automations)
        if (a.source.channel >= 0)
            a.source.channel = map (a.source.channel);
}

bool moveChannels (Song& song, std::vector<int>& selection, int delta)
{
    const auto count = (int) song.channels.size();
    std::sort (selection.begin(), selection.end());
    selection.erase (std::unique (selection.begin(), selection.end()), selection.end());
    selection.erase (std::remove_if (selection.begin(), selection.end(), [count] (int r) { return r < 0 || r >= count; }), selection.end());

    if (selection.empty() || delta == 0 || (delta < 0 && selection.front() == 0) || (delta > 0 && selection.back() == count - 1))
        return false;

    std::vector<int> order ((std::size_t) count);
    std::iota (order.begin(), order.end(), 0);
    std::vector<char> selected ((std::size_t) count, 0);

    for (auto r : selection)
        selected[(std::size_t) r] = 1;

    // bubble each selected row one step, as a block (rows already blocked stay)
    if (delta < 0)
    {
        for (int i = 1; i < count; ++i)
            if (selected[(std::size_t) i] && ! selected[(std::size_t) i - 1])
            {
                std::swap (order[(std::size_t) i], order[(std::size_t) i - 1]);
                std::swap (selected[(std::size_t) i], selected[(std::size_t) i - 1]);
            }
    }
    else
    {
        for (int i = count - 2; i >= 0; --i)
            if (selected[(std::size_t) i] && ! selected[(std::size_t) i + 1])
            {
                std::swap (order[(std::size_t) i], order[(std::size_t) i + 1]);
                std::swap (selected[(std::size_t) i], selected[(std::size_t) i + 1]);
            }
    }

    permuteChannels (song, order);
    selection.clear();

    for (int i = 0; i < count; ++i)
        if (selected[(std::size_t) i])
            selection.push_back (i);

    return true;
}

int insertChannel (Song& song, int index, ChannelDef channel)
{
    const auto count = (int) song.channels.size();
    index = std::clamp (index, 0, count);
    song.channels.push_back (std::move (channel));

    // move the new last channel up to `index`
    std::vector<int> order;
    order.reserve ((std::size_t) count + 1);

    for (int i = 0; i < index; ++i)
        order.push_back (i);

    order.push_back (count);

    for (int i = index; i < count; ++i)
        order.push_back (i);

    permuteChannels (song, order);
    return index;
}

int cloneChannel (Song& song, int index)
{
    if (index < 0 || index >= (int) song.channels.size())
        return -1;

    auto copy = song.channels[(std::size_t) index];
    const auto base = copy.name;

    for (int n = 2;; ++n)
    {
        const auto name = base + " #" + std::to_string (n);

        if (std::none_of (song.channels.begin(), song.channels.end(), [&] (const ChannelDef& c) { return c.name == name; }))
        {
            copy.name = name;
            break;
        }
    }

    return insertChannel (song, index + 1, std::move (copy));
}

int removeChannels (Song& song, std::vector<int> channels)
{
    std::sort (channels.begin(), channels.end());
    channels.erase (std::unique (channels.begin(), channels.end()), channels.end());
    int removed = 0;

    for (auto it = channels.rbegin(); it != channels.rend(); ++it)
        if (*it >= 0 && *it < (int) song.channels.size())
        {
            song.removeChannel (*it);
            ++removed;
        }

    return removed;
}

bool isInsertFree (const Song& song, int track) noexcept
{
    if (track <= 0 || track >= (int) song.mixer.size())
        return false;

    if (std::any_of (song.channels.begin(), song.channels.end(), [track] (const ChannelDef& c) { return c.mixerTrack == track; }))
        return false;

    if (! song.mixer[(std::size_t) track].effects.empty())
        return false;

    for (int t = 1; t < (int) song.mixer.size(); ++t)
        if (t != track && song.routes (t, track))
            return false;

    return true;
}

std::vector<int> sendChannelsToFreeInserts (Song& song, const std::vector<int>& channels)
{
    std::vector<int> result;

    if (song.mixer.empty())
        song.mixer = Song::defaultMixer (0);

    for (auto channel : channels)
    {
        if (channel < 0 || channel >= (int) song.channels.size())
        {
            result.push_back (0);
            continue;
        }

        int track = 0;

        for (int t = 1; t < (int) song.mixer.size() && track == 0; ++t)
            if (isInsertFree (song, t))
                track = t;

        if (track == 0 && (int) song.mixer.size() < maxMixerTracks)
        {
            song.mixer.emplace_back();
            track = (int) song.mixer.size() - 1;
        }

        if (track > 0)
        {
            auto& c = song.channels[(std::size_t) channel];
            c.mixerTrack = track;
            song.mixer[(std::size_t) track].name = c.name;
            song.mixer[(std::size_t) track].colour = c.colour;
        }

        result.push_back (track);
    }

    return result;
}
} // namespace lynx
