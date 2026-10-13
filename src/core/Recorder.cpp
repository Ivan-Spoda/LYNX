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
        const auto inSong = position % songLength; // a música toca em loop

        for (const auto& c : song.playlist)
            if (c.kind == ClipKind::pattern && c.pattern == song.currentPattern && inSong >= c.start && inSong < c.end())
                return inSong - c.start + c.offset;
    }

    return position % std::max<std::int64_t> (1, loopLength);
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
        // nota repetida sem "off" no meio: fecha a anterior
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
    n.length = std::max<std::int64_t> (ticksPerStep / 8, (std::int64_t) std::llround (endTick - h.tick));
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
