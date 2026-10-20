#pragma once

// Mixer workflow operations (docs/WORKFLOW_FL.md P2: insert multi-selection and reordering, routing and
// sidechain gestures, linking channels, effect slot reordering). Pure functions over the Song model:
// no JUCE, unit-tested in tests/WorkflowP2Tests.inc.

#include <vector>

#include "core/Song.h"

namespace lynx::mixerops
{
// Swaps inserts a and b (both >= 1): everything that refers to them by number follows (channel routing,
// sends, sidechain inputs, automation and MIDI targets, controller sources).
void swapTracks (Song& song, int a, int b);

// Moves the selected inserts left (delta < 0) or right by one place as a block; the Master never moves.
// `selection` is updated. Returns false when nothing moved.
bool moveTracks (Song& song, std::vector<int>& selection, int delta);

// Inserts a blank insert at `at` (1..size; FL's "Insert one"): the inserts from `at` on move one place right,
// everything that refers to them follows, and default names ("Insert n") follow their new number.
// False when the mixer is full or `at` is out of range.
bool insertTrack (Song& song, int at);

// Deletes insert `t` (FL's "Delete"): the following inserts move one place left and a blank insert takes the
// last place, so the count stays. Channels on it go to the Master, sends to it and sidechains from it are
// removed. False for the Master or out of range.
bool deleteTrack (Song& song, int t);

// Routes `channels` to insert `track` (all of them), or, with `startingHere`, to consecutive inserts from
// `track` on (adding inserts as needed, up to maxMixerTracks). Returns the insert of each channel.
std::vector<int> linkChannels (Song& song, const std::vector<int>& channels, int track, bool startingHere);

// Channels whose output goes to `track`.
std::vector<int> channelsOn (const Song& song, int track);

// Sends each source insert to `to` (0 = Master). `only`: the source stops sending anywhere else. Sources
// that would make a cycle are skipped. Returns how many were routed.
int routeTo (Song& song, const std::vector<int>& sources, int to, bool only);

// Sidechain link: a level-0 send from `from` to `to` (so `from` is processed first) and every compressor
// or vocoder on `to` without a side input listens to `from`. Returns how many effects now listen.
int sidechainTo (Song& song, int from, int to);

// Moves effect slot `from` to position `to` of the same insert (the others shift); automations follow.
bool moveEffectSlot (Song& song, int track, int from, int to);

// All effects of the insert on or off (the "effects" switch).
void setAllEffectsEnabled (Song& song, int track, bool enabled);
bool anyEffectEnabled (const Song& song, int track) noexcept;
} // namespace lynx::mixerops
