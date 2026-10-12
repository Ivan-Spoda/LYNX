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
} // namespace lynx
