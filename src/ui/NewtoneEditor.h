#pragma once

// LYNX's editors for a Newtone / Newtime mixer slot (round 142, issue #5): load a sample, analyse it, edit it, hear it.
//  - Newtone: the sample's notes on a pitch grid (LYNX's analysis, core/NewtoneFx.h analyseNotes). Drag a note up or
//    down to change its pitch (snapped to semitones; Alt = free), Ctrl+drag to move it in time; double-click snaps it
//    to the nearest semitone; right-click for its menu (snap, reset, mute). Up/Down move the selected note.
//  - Newtime: the sample's waveform with warp markers. Double-click adds a marker, dragging it moves where that point
//    of the sample plays (the sample stretches around it, pitch kept); right-click a marker to remove it.
// The edits are the slot's state (LYNX's own format); "Play with the song" is the plugin's slave mode, Preview plays
// the edited sample alone. Imported FL Newtone states open with their notes; editing one saves it in LYNX's format.

#include "engine/AudioEngine.h"

namespace lynx
{
namespace NewtoneWindows
{
bool handles (EffectKind kind); // newtone, newtime
void open (AudioEngine& engine, int track, int slot);
void closeAll();
} // namespace NewtoneWindows

// The editor alone (snapshots, tests)
std::unique_ptr<juce::Component> makeNewtoneEditor (AudioEngine& engine, int track, int slot);
} // namespace lynx
