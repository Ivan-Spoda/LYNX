#pragma once

// Channel arpeggiator (like the one in FL's Channel settings): takes the notes being
// held on the channel and plays one at a time, on the transport grid. No allocation: state
// and fixed-size buffers, created outside the audio thread.

#include <array>

#include "core/Scheduler.h"
#include "core/Song.h"

namespace lynx
{
class Arpeggiator
{
public:
    static constexpr int maxHeld = 32;
    static constexpr int maxEvents = 512;

    // Processes a span: `in` are the channel events (on/off) with offsets within the span; the output
    // (arp on/off, plus everything that isn't on/off) goes to `out`, in order.
    void process (int channel, const NoteEvent* in, int inCount, double startTick, double tps, int numSamples,
                  const ArpSettings& settings, NoteEvent* out, int& outCount) noexcept;

    void reset() noexcept;

    std::array<NoteEvent, maxEvents> input {}, output {};

private:
    void hold (int key, float velocity) noexcept;
    void release (int key) noexcept;
    int nextIndex (const ArpSettings& settings) noexcept; // within the sequence of heldCount * octaves

    std::array<int, maxHeld> heldKeys {};
    std::array<float, maxHeld> heldVelocity {};
    int heldCount = 0;
    int step = 0;          // position in the sequence
    int repeatCount = 0;
    int sounding = -1;     // key currently sounding (-1 = none)
    double offTick = -1.0; // when the sounding key is released (gate)
    std::uint32_t random = 0x2545F491u;
};
} // namespace lynx
