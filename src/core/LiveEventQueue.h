#pragma once

// Fila lock-free de uma thread produtora (UI) para uma consumidora (áudio),
// para notas tocadas ao vivo: teclado do PC, preview do Piano Roll, MIDI.

#include <array>
#include <atomic>
#include <cstddef>

#include "core/Scheduler.h"

namespace lynx
{
class LiveEventQueue
{
public:
    static constexpr std::size_t capacity = 256; // potência de 2

    // UI. Retorna false se a fila estiver cheia (o evento é descartado).
    bool push (const NoteEvent& e) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto next = (w + 1) & (capacity - 1);

        if (next == readIndex.load (std::memory_order_acquire))
            return false;

        events[w] = e;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    // Áudio.
    bool pop (NoteEvent& out) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);

        if (r == writeIndex.load (std::memory_order_acquire))
            return false;

        out = events[r];
        readIndex.store ((r + 1) & (capacity - 1), std::memory_order_release);
        return true;
    }

private:
    std::array<NoteEvent, capacity> events {};
    std::atomic<std::size_t> writeIndex { 0 };
    std::atomic<std::size_t> readIndex { 0 };
};
} // namespace lynx
