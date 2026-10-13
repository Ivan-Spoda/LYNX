#pragma once

// Recording of notes played live (MIDI or PC keyboard) into the current pattern.
// Runs on the UI thread; events arrive with the absolute transport position.

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
