#include "core/Song.h"

#include <algorithm>

namespace lynx
{
void Pattern::sortNotes()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
}

std::int64_t Pattern::effectiveLength (int beatsPerBar) const noexcept
{
    if (lengthTicks > 0)
        return lengthTicks;

    const std::int64_t bar = (std::int64_t) ppq * std::max (1, beatsPerBar);
    std::int64_t end = 0;

    for (auto& n : notes)
        end = std::max (end, n.start + std::max<std::int64_t> (n.length, 1));

    return std::max<std::int64_t> (bar, (end + bar - 1) / bar * bar);
}

bool Pattern::hasStep (int channel, int step) const noexcept
{
    const auto tick = (std::int64_t) step * ticksPerStep;
    return std::any_of (notes.begin(), notes.end(),
                        [&] (const Note& n) { return n.channel == channel && n.start == tick; });
}

void Pattern::toggleStep (int channel, int step)
{
    const auto tick = (std::int64_t) step * ticksPerStep;
    const auto it = std::find_if (notes.begin(), notes.end(),
                                  [&] (const Note& n) { return n.channel == channel && n.start == tick; });

    if (it != notes.end())
    {
        notes.erase (it);
        return;
    }

    Note n;
    n.start = tick;
    n.channel = channel;
    notes.push_back (n);
    sortNotes();
}

std::int64_t Song::songLength() const noexcept
{
    const std::int64_t bar = (std::int64_t) ppq * std::max (1, beatsPerBar);
    std::int64_t end = 0;

    for (const auto& c : playlist)
        end = std::max (end, c.end());

    return std::max<std::int64_t> (bar, (end + bar - 1) / bar * bar);
}

void Song::removePattern (int index)
{
    if (index < 0 || index >= (int) patterns.size())
        return;

    patterns.erase (patterns.begin() + index);
    playlist.erase (std::remove_if (playlist.begin(), playlist.end(),
                                    [index] (const Clip& c) { return c.kind == ClipKind::pattern && c.pattern == index; }),
                    playlist.end());

    for (auto& c : playlist)
        if (c.kind == ClipKind::pattern && c.pattern > index)
            --c.pattern;

    if (patterns.empty())
        patterns.emplace_back().name = "Pattern 1";

    currentPattern = std::clamp (currentPattern > index ? currentPattern - 1 : currentPattern, 0, (int) patterns.size() - 1);
}

std::vector<MixerTrack> Song::defaultMixer (int inserts)
{
    std::vector<MixerTrack> mixer ((std::size_t) std::clamp (inserts + 1, 1, maxMixerTracks));
    mixer[0].name = "Master";

    for (std::size_t i = 1; i < mixer.size(); ++i)
        mixer[i].name = "Insert " + std::to_string (i);

    return mixer;
}

int Song::nextFreeInsert() const
{
    for (int t = 1; t < (int) mixer.size(); ++t)
        if (std::none_of (channels.begin(), channels.end(), [t] (const ChannelDef& c) { return c.mixerTrack == t; }))
            return t;

    return std::max (0, (int) mixer.size() - 1);
}

void Song::removeChannel (int index)
{
    if (index < 0 || index >= (int) channels.size())
        return;

    channels.erase (channels.begin() + index);

    for (auto& p : patterns)
    {
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [index] (const Note& n) { return n.channel == index; }),
                       p.notes.end());

        for (auto& n : p.notes)
            if (n.channel > index)
                --n.channel;
    }

    playlist.erase (std::remove_if (playlist.begin(), playlist.end(),
                                    [index] (const Clip& c) { return c.kind == ClipKind::audio && c.channel == index; }),
                    playlist.end());

    for (auto& c : playlist)
        if (c.kind == ClipKind::audio && c.channel > index)
            --c.channel;
}

Song Song::makeDefault()
{
    Song song;
    song.mixer = defaultMixer();
    auto channel = [] (const char* name, InstrumentKind kind, float volume, float pan) {
        ChannelDef c;
        c.name = name;
        c.instrument = kind;
        c.volume = volume;
        c.pan = pan;
        return c;
    };

    song.channels = {
        channel ("Kick", InstrumentKind::kick, 0.9f, 0.0f),
        channel ("Clap", InstrumentKind::clap, 0.7f, 0.0f),
        channel ("Hat", InstrumentKind::hat, 0.5f, 0.1f),
        channel ("Snare", InstrumentKind::snare, 0.7f, 0.0f),
        channel ("Synth", InstrumentKind::synth, 0.5f, 0.0f),
    };

    for (std::size_t i = 0; i < song.channels.size(); ++i)
        song.channels[i].mixerTrack = (int) i + 1;

    auto& p = song.patterns.emplace_back();
    p.name = "Pattern 1";

    for (int step : { 0, 4, 8, 12 })
        p.toggleStep (0, step);

    for (int step : { 4, 12 })
        p.toggleStep (1, step);

    for (int step : { 2, 6, 10, 14 })
        p.toggleStep (2, step);

    return song;
}
} // namespace lynx
