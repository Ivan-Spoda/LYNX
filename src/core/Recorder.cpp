#include "core/Recorder.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

void quantizeTake (std::vector<Note>& notes, const TakeQuantize& q)
{
    const auto nearest = [] (std::int64_t t, std::int64_t g) { return std::max<std::int64_t> (0, (t + g / 2) / g * g); };

    for (auto& n : notes)
    {
        const auto end = n.start + std::max<std::int64_t> (1, n.length);

        if (q.stepSequencer)
        {
            n.start = nearest (n.start, ticksPerStep);
            n.length = ticksPerStep;
            continue;
        }

        if (q.grid <= 1)
            continue;

        if (q.starts)
            n.start = nearest (n.start, q.grid);

        if (q.end == NoteEnd::quantize)
            n.length = std::max (q.grid, nearest (end, q.grid) - n.start);
        else if (q.end == NoteEnd::keepEnd)
            n.length = std::max<std::int64_t> (1, end - n.start);
        // keepLength: the length as played
    }
}

void commitTake (Pattern& pattern, std::vector<Note> notes, RecordMode mode, const TakeQuantize& q, std::vector<int>& clearedChannels)
{
    quantizeTake (notes, q);
    commitTake (pattern, std::move (notes), mode, 0, clearedChannels);
}

std::vector<PunchRange> punchRanges (const std::vector<Marker>& markers)
{
    std::vector<const Marker*> sorted;
    for (const auto& m : markers)
        if (m.action == Marker::punchIn || m.action == Marker::punchOut)
            sorted.push_back (&m);
    std::stable_sort (sorted.begin(), sorted.end(), [] (const Marker* a, const Marker* b) { return a->tick < b->tick; });

    std::vector<PunchRange> out;
    std::int64_t openAt = -1;
    bool any = false;
    for (const auto* m : sorted)
    {
        if (m->action == Marker::punchIn)
        {
            if (openAt < 0)
                openAt = m->tick;
        }
        else if (openAt >= 0 || ! any) // a stop with no start before it: from the song start
        {
            const auto from = openAt >= 0 ? openAt : 0;
            if (m->tick > from)
                out.push_back ({ from, m->tick });
            openAt = -1;
        }
        any = true;
    }
    if (openAt >= 0)
        out.push_back ({ openAt, std::numeric_limits<std::int64_t>::max() / 4 });
    if (out.empty() && any) // only markers that open and close nothing: an empty range (nothing recorded)
        out.push_back ({ 0, 0 });
    return out;
}

bool inPunchRanges (const std::vector<PunchRange>& ranges, double tick) noexcept
{
    if (ranges.empty())
        return true;
    return std::any_of (ranges.begin(), ranges.end(), [tick] (const PunchRange& r) { return tick >= (double) r.in && tick < (double) r.out; });
}

std::vector<ClipPart> punchParts (const std::vector<PunchRange>& ranges, std::int64_t start, std::int64_t length, std::int64_t offset)
{
    if (ranges.empty())
        return { { start, length, offset } };

    std::vector<ClipPart> out;
    const auto end = start + length;
    for (const auto& r : ranges)
    {
        const auto from = std::max (start, r.in), to = std::min (end, r.out);
        if (to > from)
            out.push_back ({ from, to - from, offset + (from - start) });
    }
    return out;
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
