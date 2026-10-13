#pragma once

// Lock-free single-producer/single-consumer queues. Used for notes
// played live (UI -> audio, MIDI -> audio) and for recording (MIDI -> UI).

#include <array>
#include <atomic>
#include <cstddef>

#include "core/Scheduler.h"

namespace lynx
{
template <typename T, std::size_t Capacity>
class SpscQueue
{
public:
    static_assert ((Capacity & (Capacity - 1)) == 0, "capacity must be a power of 2");
    static constexpr std::size_t capacity = Capacity;

    // Producer. Returns false if the queue is full (the item is dropped).
    bool push (const T& item) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto next = (w + 1) & (capacity - 1);

        if (next == readIndex.load (std::memory_order_acquire))
            return false;

        items[w] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    // Consumer.
    bool pop (T& out) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);

        if (r == writeIndex.load (std::memory_order_acquire))
            return false;

        out = items[r];
        readIndex.store ((r + 1) & (capacity - 1), std::memory_order_release);
        return true;
    }

private:
    std::array<T, capacity> items {};
    std::atomic<std::size_t> writeIndex { 0 };
    std::atomic<std::size_t> readIndex { 0 };
};

using LiveEventQueue = SpscQueue<NoteEvent, 256>;
} // namespace lynx
