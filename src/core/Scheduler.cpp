#include "core/Scheduler.h"

#include <algorithm>
#include <cmath>

namespace lynx
{
void EventBuffer::add (const NoteEvent& e) noexcept
{
    if (count < capacity)
        events[(std::size_t) count++] = e;
    else
        ++dropped;
}

void EventBuffer::sortByOffset() noexcept
{
    // "off" antes de "on" no mesmo sample, para uma nota repetida não ser cortada
    std::stable_sort (events.begin(), events.begin() + count, [] (const NoteEvent& a, const NoteEvent& b) {
        if (a.sampleOffset != b.sampleOffset)
            return a.sampleOffset < b.sampleOffset;
        return a.type == NoteEvent::Type::off && b.type == NoteEvent::Type::on;
    });
}

namespace
{
// Emite um evento para cada repetição do tick `t` (dentro da pattern) que cai em [start, end).
void emitOccurrences (double t, std::int64_t loopLength, double start, double end, double tps, int numSamples,
                      NoteEvent event, EventBuffer& out) noexcept
{
    const auto len = (double) loopLength;
    // primeira repetição k com t + k*len >= start
    auto k = std::ceil ((start - t) / len);

    for (auto tick = t + k * len; tick < end; tick += len)
    {
        event.sampleOffset = std::clamp ((int) std::floor ((tick - start) / tps), 0, numSamples - 1);
        out.add (event);
    }
}
} // namespace

void schedulePatternLoop (const Pattern& pattern,
                          std::int64_t patternLength,
                          double startTick,
                          int numSamples,
                          double tps,
                          EventBuffer& out) noexcept
{
    if (numSamples <= 0 || patternLength <= 0 || tps <= 0.0)
        return;

    const auto endTick = startTick + numSamples * tps;

    for (const auto& note : pattern.notes)
    {
        if (note.start >= patternLength)
            continue; // fora do loop

        NoteEvent on;
        on.type = NoteEvent::Type::on;
        on.channel = note.channel;
        on.key = note.key;
        on.velocity = note.velocity;
        emitOccurrences ((double) note.start, patternLength, startTick, endTick, tps, numSamples, on, out);

        NoteEvent off = on;
        off.type = NoteEvent::Type::off;
        off.velocity = 0.0f;
        const auto length = std::max<std::int64_t> (note.length, 1);
        emitOccurrences ((double) (note.start + length), patternLength, startTick, endTick, tps, numSamples, off, out);
    }

    out.sortByOffset();
}
void scheduleSong (const Song& song, double startTick, int numSamples, double tps, EventBuffer& out, bool resume) noexcept
{
    if (numSamples <= 0 || tps <= 0.0 || song.playlist.empty())
        return;

    const auto loop = song.songLength();
    const auto endTick = startTick + numSamples * tps;

    // Janela do bloco dentro do loop; se atravessar o fim, vira duas janelas.
    const auto loopStart = std::fmod (startTick, (double) loop);
    const auto loopEnd = loopStart + (endTick - startTick);

    auto overlaps = [&] (double from, double to) {
        return (from < loopEnd && to > loopStart) || (loopEnd > (double) loop && from < loopEnd - (double) loop);
    };

    const auto secondsPerTick = 60.0 / (std::max (song.bpm, 1.0) * ppq);

    for (const auto& clip : song.playlist)
    {
        if (clip.kind == ClipKind::audio)
        {
            if (clip.channel < 0 || clip.channel >= (int) song.channels.size() || clip.length <= 0)
                continue;

            NoteEvent e;
            e.channel = clip.channel;
            e.velocity = 1.0f;

            if (resume && loopStart > (double) clip.start && loopStart < (double) clip.end())
            {
                e.type = NoteEvent::Type::on;
                e.sampleOffset = 0;
                e.startSeconds = ((double) clip.offset + loopStart - (double) clip.start) * secondsPerTick;
                out.add (e);
            }

            if (! overlaps ((double) clip.start, (double) clip.end() + 1.0))
                continue;

            e.type = NoteEvent::Type::on;
            e.startSeconds = (double) clip.offset * secondsPerTick;
            emitOccurrences ((double) clip.start, loop, startTick, endTick, tps, numSamples, e, out);

            e.type = NoteEvent::Type::off;
            e.startSeconds = 0.0;
            emitOccurrences ((double) clip.end(), loop, startTick, endTick, tps, numSamples, e, out);
            continue;
        }

        if (clip.pattern < 0 || clip.pattern >= (int) song.patterns.size() || clip.length <= 0)
            continue;

        // folga de um tick para pegar o "off" que cai exatamente no fim do clip
        if (! overlaps ((double) clip.start, (double) clip.end() + 1.0))
            continue;

        for (const auto& note : song.patterns[(std::size_t) clip.pattern].notes)
        {
            if (note.start < clip.offset || note.start >= clip.offset + clip.length)
                continue;

            const auto on = clip.start + (note.start - clip.offset);
            const auto off = std::min (on + std::max<std::int64_t> (note.length, 1), clip.end());

            NoteEvent e;
            e.type = NoteEvent::Type::on;
            e.channel = note.channel;
            e.key = note.key;
            e.velocity = note.velocity;
            emitOccurrences ((double) on, loop, startTick, endTick, tps, numSamples, e, out);

            e.type = NoteEvent::Type::off;
            e.velocity = 0.0f;
            emitOccurrences ((double) off, loop, startTick, endTick, tps, numSamples, e, out);
        }
    }

    out.sortByOffset();
}
} // namespace lynx
