#pragma once

// Converte notas de uma pattern em eventos com offset exato em samples dentro
// de um bloco de áudio. Roda na thread de áudio: sem alocação, sem locks.

#include <array>
#include <cstdint>

#include "core/Song.h"

namespace daw
{
struct NoteEvent
{
    enum class Type : std::uint8_t { on, off };

    int sampleOffset = 0;   // 0 .. numSamples-1 dentro do bloco
    Type type = Type::on;
    int channel = 0;
    int key = 60;
    float velocity = 0.0f;
};

// Buffer de capacidade fixa. Eventos além da capacidade são descartados
// (e contados), nunca alocam.
class EventBuffer
{
public:
    static constexpr int capacity = 2048;

    void clear() noexcept { count = 0; dropped = 0; }
    void add (const NoteEvent& e) noexcept;
    void sortByOffset() noexcept;

    int size() const noexcept { return count; }
    int droppedCount() const noexcept { return dropped; }
    const NoteEvent& operator[] (int i) const noexcept { return events[(std::size_t) i]; }
    const NoteEvent* begin() const noexcept { return events.data(); }
    const NoteEvent* end() const noexcept { return events.data() + count; }

private:
    std::array<NoteEvent, capacity> events {};
    int count = 0;
    int dropped = 0;
};

inline double ticksPerSample (double bpm, double sampleRate) noexcept
{
    return bpm * ppq / (60.0 * sampleRate);
}

// Gera os eventos da pattern (tocando em loop) no intervalo [startTick, startTick + numSamples * tps).
// Uma nota que começa em t gera "on" em t e "off" em t + length.
void schedulePatternLoop (const Pattern& pattern,
                          std::int64_t patternLength,
                          double startTick,
                          int numSamples,
                          double ticksPerSampleValue,
                          EventBuffer& out) noexcept;
} // namespace daw
