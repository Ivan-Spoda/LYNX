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

int Arpeggiator::nextIndex (const ArpSettings& s) noexcept
{
    // sequence: the held notes, repeated an octave higher over `range` octaves
    const auto octaves = std::clamp (s.range, 1, 4);
    const auto length = heldCount * octaves;

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

    const auto stepTicks = std::max (1.0, (double) s.stepBeats * ppq);
    const auto endTick = startTick + n * tps;
    auto nextInput = 0;

    // decision points: releasing the gated note and step boundaries, interleaved with the input
    auto boundary = std::ceil (startTick / stepTicks - 1e-9) * stepTicks;

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
                hold (e.key, e.velocity);
            else if (e.type == NoteEvent::Type::off)
                release (e.key);
            else
                emit (e); // slide, choke...

            // everything released: cut the arp note and restart the sequence
            if (heldCount == 0)
            {
                step = 0;
                repeatCount = 0;

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

            if (sounding >= 0)
            {
                e.type = NoteEvent::Type::off;
                e.key = sounding;
                emit (e);
            }

            const auto index = nextIndex (s);
            const auto key = std::clamp (heldKeys[(std::size_t) (index % heldCount)] + 12 * (index / heldCount), 0, 127);
            e.type = NoteEvent::Type::on;
            e.key = key;
            e.velocity = heldVelocity[(std::size_t) (index % heldCount)];
            emit (e);
            sounding = key;
            offTick = boundary + stepTicks * std::clamp ((double) s.gate, 0.05, 1.0);
        }

        boundary += stepTicks;
    }
}
} // namespace lynx
