#pragma once

// Recording of notes played live (MIDI or PC keyboard) into the current pattern.
// Runs on the UI thread; events arrive with the absolute transport position.

#include <cstdint>
#include <map>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct RecordedEvent
{
    double tick = 0.0; // absolute transport position (may go past the end of the loop)
    int channel = 0;
    int key = 60;
    float velocity = 0.8f;
    bool on = true;
};

// Where an absolute transport position falls within the current pattern: in pattern mode,
// the position within the loop; in song mode, within the current pattern's clip that is
// playing there (or, without a clip, as if it were the pattern loop).
std::int64_t patternPositionForTick (const Song& song, double tick, std::int64_t loopLength);

// --- recording options (docs/WORKFLOW_FL.md P2: recording set) ---------------------------------

enum class RecordMode : std::uint8_t
{
    overdub, // recorded notes are added to what the pattern has
    replace  // a channel's first recorded note of a take clears that channel's notes in the pattern
};

// Commits finished notes of a take into `pattern`: starts quantised to the nearest multiple of `grid`
// (grid <= 1: as played; lengths kept). In replace mode, `clearedChannels` remembers the channels already
// cleared during this take, so later notes of the same take stay.
void commitTake (Pattern& pattern, std::vector<Note> notes, RecordMode mode, std::int64_t grid, std::vector<int>& clearedChannels);

// Song mode: makes sure a clip of `pattern` plays at `tick`. If none does, one is placed at the start of
// that bar, on the first track that is free there. Returns true when a clip was added.
bool placePatternClipAt (Song& song, int pattern, std::int64_t tick);

// Length of a count-in of `bars` bars at the song position `tick` (bars follow the time signature there).
std::int64_t countInLength (const Song& song, int bars, std::int64_t tick = 0);

class NoteRecorder
{
public:
    // Starts a take. `loopLength` = pattern loop length at the start of the take
    // (does not change while the recorded notes grow the pattern).
    void begin (std::int64_t loopLength);
    bool isActive() const noexcept { return active; }

    // Delivers an event; when it's the "off" of a held note, the finished note goes to finished().
    void add (const Song& song, const RecordedEvent& e);

    // Closes the notes still held at position `tick` (playback stopped) and ends the take.
    void end (const Song& song, double tick);

    // Notes completed since the last call.
    std::vector<Note> takeFinished();

private:
    struct Held
    {
        double tick;
        float velocity;
    };

    void finish (const Song& song, int channel, int key, const Held& held, double endTick);

    bool active = false;
    std::int64_t loop = 0;
    std::map<std::pair<int, int>, Held> held; // (channel, key)
    std::vector<Note> finished;
};
} // namespace lynx
