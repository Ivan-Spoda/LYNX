#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Undo history (docs/WORKFLOW_FL.md P2, G13): the undo steps by name, oldest first, the current state
// marked; clicking an entry jumps there (later entries stay as redo steps until the next edit).
std::unique_ptr<juce::Component> makeHistoryPanel (AudioEngine& engine);
} // namespace lynx
