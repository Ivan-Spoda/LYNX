#pragma once

// Converts a pattern's notes into events with an exact sample offset within
// an audio block. Runs on the audio thread: no allocation, no locks.

#include <array>
#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct NoteEvent
{
    enum class Type : std::uint8_t { on, off, slide, choke }; // slide: glides the sounding notes to `key`; choke: cut group

    int sampleOffset = 0;   // 0 .. numSamples-1 within the block
    Type type = Type::on;
    int channel = 0;
    int key = 60;
    float velocity = 0.0f;
    double startSeconds = 0.0; // audio clips: where the sample starts playing from
    float pan = 0.0f;          // -1..1, from the note
    float cents = 0.0f;        // note's fine pitch
    float slideSamples = 0.0f; // slide: glide duration
    bool porta = false;        // the note inverts the channel's porta
    // audio clips: fades (in samples) and the position within the clip when the note starts
    float clipElapsed = 0.0f, clipRemaining = 0.0f, fadeIn = 0.0f, fadeOut = 0.0f;
};

// Fixed-capacity buffer. Events beyond capacity are dropped
// (and counted), never allocate.
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

// Generates the pattern's events (playing in a loop) in the interval [startTick, startTick + numSamples * tps).
// A note starting at t generates "on" at t and "off" at t + length.
void schedulePatternLoop (const Pattern& pattern,
                          std::int64_t patternLength,
                          double startTick,
                          int numSamples,
                          double ticksPerSampleValue,
                          EventBuffer& out,
                          float swing = 0.0f,
                          const std::vector<ChannelDef>* channels = nullptr) noexcept;
// Index built once per snapshot so scheduleSong doesn't scan every note on every block:
// song length and the longest note of each pattern. Requires notes sorted by start.
struct ScheduleIndex
{
    std::int64_t songLength = 0;
    std::vector<std::int64_t> maxNoteLength; // per pattern
};

ScheduleIndex makeScheduleIndex (const Song& song);

// Generates the Playlist's events (playing the whole song in a loop). Each pattern clip
// plays the pattern's notes that fall in [offset, offset + length); notes are cut at the
// end of the clip. Audio clips trigger the channel's sample at the start and stop at the end.
// `resume` = playback started or jumped to startTick: audio clips that were already
// in progress at that point start in the middle, on the first sample of the block.
void scheduleSong (const Song& song,
                   double startTick,
                   int numSamples,
                   double ticksPerSampleValue,
                   EventBuffer& out,
                   bool resume = false,
                   const ScheduleIndex* index = nullptr) noexcept;
} // namespace lynx
