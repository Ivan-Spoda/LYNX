#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Editor windows of LYNX's native effects (the engines compatible with FL Studio's plugins). Each one follows the
// FL plugin's arrangement (core/PluginLayout.h: same controls, sections, captions and places) drawn in LYNX's own
// style, with a presets bar on top (FL presets of the plugin from the user's installation, previous / next) and
// LYNX's control contract on every control (drag, Ctrl = fine, wheel, Alt+click / double-click = reset, right-click
// = type value / automation / MIDI link). Effects without a studied arrangement get a generated grid of knobs.
namespace NativeEffectWindows
{
// can the slot's effect be edited in a native editor window? (not external plugins nor the Patcher container)
bool canOpen (EffectKind kind);
void open (AudioEngine& engine, int track, int slot);
void closeAll();
} // namespace NativeEffectWindows

// The editor alone at `scale` (1 = FL's size; for --snapshot / --plugui).
std::unique_ptr<juce::Component> makeNativeEffectEditor (AudioEngine& engine, int track, int slot, float scale = 1.0f, int page = 0);
} // namespace lynx
