#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// Clip properties (FL's "Clip properties", interpreted; REA_rodada84): the clip's source (pattern / audio file /
// automation) with its name and colour, track, start, length and offset, mute, and for audio clips gain, fades,
// pitch and stretch. With several clips selected, mute and gain can change them all. `afterEdit` runs after each
// change (the Playlist re-sorts its clips).
void showClipProperties (AudioEngine& engine, int clip, std::vector<int> selection, std::function<void()> afterEdit = {});

// Arrangements (FL's "Merge arrangement", with the arrangement list around it): select, add, duplicate, rename,
// remove and reorder arrangements, and merge one into the current one (insert / merge / replace at the start, the
// playback position or the end).
void showArrangements (AudioEngine& engine, bool mergeFirst = false);

// Panels for --snapshot mode.
std::unique_ptr<juce::Component> makeClipPropertiesPanel (AudioEngine& engine, int clip, std::vector<int> selection);
std::unique_ptr<juce::Component> makeArrangementsPanel (AudioEngine& engine);
} // namespace lynx
