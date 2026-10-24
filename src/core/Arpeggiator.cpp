#include "core/Arpeggiator.h"

#include <algorithm>
#include <cmath>

namespace lynx
{
void Arpeggiator::reset() noexcept
{
    heldCount = 0;
    step = 0;
    repeatCount = 0;
    sounding = -1;
    offTick = -1.0;
    nextStep = -1.0;
    sliding = false;
}

void Arpeggiator::hold (int key, float velocity) noexcept
{
    for (int i = 0; i < heldCount; ++i)
        if (heldKeys[(std::size_t) i] == key)
            return;

    if (heldCount == maxHeld)
        return;

    // keep sorted by pitch
    auto i = heldCount++;
    while (i > 0 && heldKeys[(std::size_t) i - 1] > key)
    {
        heldKeys[(std::size_t) i] = heldKeys[(std::size_t) i - 1];
        heldVelocity[(std::size_t) i] = heldVelocity[(std::size_t) i - 1];
        --i;
    }

    heldKeys[(std::size_t) i] = key;
    heldVelocity[(std::size_t) i] = velocity;
}

void Arpeggiator::release (int key) noexcept
{
    for (int i = 0; i < heldCount; ++i)
        if (heldKeys[(std::size_t) i] == key)
        {
            for (int j = i; j + 1 < heldCount; ++j)
            {
                heldKeys[(std::size_t) j] = heldKeys[(std::size_t) j + 1];
                heldVelocity[(std::size_t) j] = heldVelocity[(std::size_t) j + 1];
            }

            --heldCount;
            return;
        }
}

int Arpeggiator::sequenceLength (const ArpSettings& s) const noexcept
{
    // one pass over the notes (the held ones, or the chord on the lowest), FL's Repeat times per octave, over `range`
    const auto notes = s.chord.empty() ? heldCount : (heldCount > 0 ? (int) s.chord.size() : 0);
    return notes * std::clamp (s.range, 1, 4) * std::max (1, s.octaveRepeat);
}

int Arpeggiator::keyAt (const ArpSettings& s, int index) const noexcept
{
    const auto notes = s.chord.empty() ? heldCount : (int) s.chord.size();
    if (notes <= 0)
        return 60;
    const auto perOctave = notes * std::max (1, s.octaveRepeat);
    const auto octave = index / perOctave;
    const auto note = index % notes;
    const auto key = s.chord.empty() ? heldKeys[(std::size_t) note] : heldKeys[0] + s.chord[(std::size_t) note];
    return std::clamp (key + 12 * octave, 0, 127);
}

int Arpeggiator::nextIndex (const ArpSettings& s) noexcept
{
    const auto length = sequenceLength (s);

    if (length == 0)
        return -1;

    auto index = 0;

    switch (s.direction)
    {
        case 2: // down
            index = length - 1 - step % length;
            break;
        case 3: // up and down (without repeating the ends)
        {
            const auto cycle = std::max (1, 2 * length - 2);
            const auto p = step % cycle;
            index = p < length ? p : cycle - p;
            break;
        }
        case 5: // up and down, the ends twice (FL's "sticky")
        {
            const auto cycle = 2 * length;
            const auto p = step % cycle;
            index = p < length ? p : cycle - 1 - p;
            break;
        }
        case 4: // random
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            index = (int) (random % (std::uint32_t) length);
            break;
        default: // up
            index = step % length;
            break;
    }

    if (++repeatCount >= std::max (1, s.repeat))
    {
        repeatCount = 0;
        ++step;
    }

    return index;
}

void Arpeggiator::process (int channel, const NoteEvent* in, int inCount, double startTick, double tps, int n,
                           const ArpSettings& s, NoteEvent* out, int& outCount) noexcept
{
    outCount = 0;
    auto emit = [&] (const NoteEvent& e) {
        if (outCount < maxEvents)
            out[outCount++] = e;
    };

    // FL: a note lasts time · gate; past 100 % the next one waits for it (the step is time · max(1, gate)) [EXEC]
    const auto gate = std::clamp ((double) s.gate, 0.05, 128.0 / 48.0);
    const auto stepTicks = std::max (1.0, (double) s.stepBeats * ppq * std::max (1.0, gate));
    const auto noteTicks = std::max (1.0, (double) s.stepBeats * ppq * gate);
    const auto endTick = startTick + n * tps;
    auto nextInput = 0;

    // decision points: releasing the gated note and the steps (FL counts them from the first held note [EXEC: a
    // chord off the grid arpeggiates from its own start]), interleaved with the input
    auto boundary = nextStep >= 0.0 ? nextStep : endTick + 1.0;

    while (true)
    {
        const auto inputOffset = nextInput < inCount ? in[nextInput].sampleOffset : n;
        const auto boundaryOffset = boundary < endTick ? (int) std::floor ((boundary - startTick) / tps) : n;
        const auto offOffset = sounding >= 0 && offTick >= 0.0 && offTick < endTick ? (int) std::floor ((std::max (offTick, startTick) - startTick) / tps) : n;
        const auto nextOffset = std::min ({ inputOffset, boundaryOffset, offOffset });

        if (nextOffset >= n)
            break;

        if (offOffset == nextOffset && offOffset <= boundaryOffset && sounding >= 0)
        {
            NoteEvent off;
            off.type = NoteEvent::Type::off;
            off.key = sounding;
            off.channel = channel;
            off.sampleOffset = std::clamp (offOffset, 0, n - 1);
            emit (off);
            sounding = -1;
            offTick = -1.0;
            continue;
        }

        if (inputOffset == nextOffset && inputOffset <= boundaryOffset)
        {
            const auto& e = in[nextInput++];

            if (e.type == NoteEvent::Type::on)
            {
                if (heldCount == 0 && nextStep < 0.0)
                    boundary = nextStep = startTick + e.sampleOffset * tps; // the first step plays at once
                hold (e.key, e.velocity);
            }
            else if (e.type == NoteEvent::Type::off)
                release (e.key);
            else
                emit (e); // slide, choke...

            // everything released: cut the arp note and restart the sequence
            if (heldCount == 0)
            {
                step = 0;
                repeatCount = 0;
                nextStep = -1.0;
                boundary = endTick + 1.0;
                sliding = false;

                if (sounding >= 0)
                {
                    NoteEvent off = e;
                    off.type = NoteEvent::Type::off;
                    off.key = sounding;
                    emit (off);
                    sounding = -1;
                    offTick = -1.0;
                }
            }

            continue;
        }

        // step boundary: next arp note
        if (heldCount > 0)
        {
            NoteEvent e;
            e.channel = channel;
            e.sampleOffset = std::clamp (boundaryOffset, 0, n - 1);

            const auto index = nextIndex (s);
            const auto key = keyAt (s, index);
            const auto velocity = heldVelocity[(std::size_t) (s.chord.empty() ? index % heldCount : 0)];

            if (s.slide && sliding && sounding >= 0)
            {
                // FL's Slide: the sounding note glides to the next one (no new attack); it keeps its own key
                e.type = NoteEvent::Type::slide;
                e.key = key;
                e.slideSamples = (float) (std::min (noteTicks, stepTicks) * 0.25 / tps);
                emit (e);
            }
            else
            {
                if (sounding >= 0)
                {
                    e.type = NoteEvent::Type::off;
                    e.key = sounding;
                    emit (e);
                }
                e.type = NoteEvent::Type::on;
                e.key = key;
                e.velocity = velocity;
                emit (e);
                sounding = key;
                sliding = s.slide;
            }
            // slide: the note holds until the chord is released
            offTick = s.slide ? -1.0 : boundary + noteTicks;
        }

        boundary += stepTicks;
        nextStep = boundary;
    }
}
} // namespace lynx
