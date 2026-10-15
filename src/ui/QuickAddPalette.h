#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Quick-add palette (docs/WORKFLOW_FL.md P2, B9; F8): every instrument (built-in and scanned plugins) on
// the left, every effect on the right, filtered as you type (the Browser's search syntax). Double-click
// or Enter adds the item: an instrument as a new channel, an effect into the selected mixer insert.
struct QuickAddTargets
{
    std::function<void (ChannelDef)> addInstrument;
    std::function<void (EffectSlot)> addEffect;
    bool effectsFirst = false; // opened from the Mixer: typing and Enter go to the effects list
};

std::unique_ptr<juce::Component> makeQuickAddPalette (AudioEngine& engine, QuickAddTargets targets);
} // namespace lynx
