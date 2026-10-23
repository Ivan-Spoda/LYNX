#include "core/Recorder.h"

#include <algorithm>
#include <cmath>

namespace lynx
{
std::int64_t patternPositionForTick (const Song& song, double tick, std::int64_t loopLength)
{
    const auto position = (std::int64_t) std::floor (std::max (0.0, tick));

    if (song.mode == PlayMode::song)
    {
        const auto songLength = std::max<std::int64_t> (1, song.songLength());
        const auto inSong = position % songLength; // the song plays in a loop

        for (const auto& c : song.playlist)
            if (c.kind == ClipKind::pattern && c.pattern == song.currentPattern && inSong >= c.start && inSong < c.end())
                return inSong - c.start + c.offset;
    }

    return position % std::max<std::int64_t> (1, loopLength);
}

void commitTake (Pattern& pattern, std::vector<Note> notes, RecordMode mode, std::int64_t grid, std::vector<int>& clearedChannels)
{
    if (mode == RecordMode::replace)
        for (const auto& n : notes)
            if (std::find (clearedChannels.begin(), clearedChannels.end(), n.channel) == clearedChannels.end())
            {
                clearedChannels.push_back (n.channel);
                auto& existing = pattern.notes;
                existing.erase (std::remove_if (existing.begin(), existing.end(), [&n] (const Note& o) { return o.channel == n.channel; }),
                                existing.end());
            }

    if (grid > 1)
        for (auto& n : notes)
            n.start = std::max<std::int64_t> (0, (n.start + grid / 2) / grid * grid);

    pattern.notes.insert (pattern.notes.end(), notes.begin(), notes.end());
    pattern.sortNotes();
}

bool placePatternClipAt (Song& song, int pattern, std::int64_t tick)
{
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return false;

    tick = std::max<std::int64_t> (0, tick);

    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.pattern == pattern && tick >= c.start && tick < c.end())
            return false;

    const auto bar = song.barAt (tick);
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = pattern;
    clip.start = bar.tick;
    clip.length = song.patterns[(std::size_t) pattern].effectiveLength (song.beatsPerBar);

    for (int track = 0;; ++track)
    {
        const auto busy = std::any_of (song.playlist.begin(), song.playlist.end(), [&] (const Clip& c) {
            return c.track == track && c.start < clip.end() && c.end() > clip.start;
        });

        if (! busy)
        {
            clip.track = track;
            break;
        }
    }

    song.playlist.push_back (clip);
    return true;
}

std::int64_t countInLength (const Song& song, int bars, std::int64_t tick)
{
    return std::max (0, bars) * std::max<std::int64_t> (1, song.barAt (std::max<std::int64_t> (0, tick)).length);
}

void NoteRecorder::begin (std::int64_t loopLength)
{
    active = true;
    loop = std::max<std::int64_t> (1, loopLength);
    held.clear();
}

void NoteRecorder::add (const Song& song, const RecordedEvent& e)
{
    if (! active)
        return;

    const auto id = std::make_pair (e.channel, e.key);

    if (e.on)
    {
        // repeated note without an "off" in between: close the previous one
        if (auto it = held.find (id); it != held.end())
            finish (song, e.channel, e.key, it->second, e.tick);

        held[id] = { e.tick, e.velocity };
        return;
    }

    if (auto it = held.find (id); it != held.end())
    {
        finish (song, e.channel, e.key, it->second, e.tick);
        held.erase (it);
    }
}

void NoteRecorder::end (const Song& song, double tick)
{
    for (const auto& [id, h] : held)
        finish (song, id.first, id.second, h, tick);

    held.clear();
    active = false;
}

void NoteRecorder::finish (const Song& song, int channel, int key, const Held& h, double endTick)
{
    Note n;
    n.start = patternPositionForTick (song, h.tick, loop);
    // Song mode loops (the loop region, or the whole song): a note held across the loop's end ends after the wrap
    auto played = endTick - h.tick;
    if (played < 0.0 && song.mode == PlayMode::song)
        played += (double) (song.hasLoop() ? song.loopEnd - song.loopStart : std::max<std::int64_t> (1, song.songLength()));
    n.length = std::max<std::int64_t> (ticksPerStep / 8, (std::int64_t) std::llround (played));
    n.channel = channel;
    n.key = std::clamp (key, 0, 127);
    n.velocity = std::clamp (h.velocity, 0.0f, 1.0f);
    finished.push_back (n);
}

std::vector<Note> NoteRecorder::takeFinished()
{
    std::vector<Note> out;
    out.swap (finished);
    return out;
}
} // namespace lynx
