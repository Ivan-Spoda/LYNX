#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// 7-band parametric EQ editor (like FL's Parametric EQ 2): response curve with one
// handle per band. Dragging a handle changes frequency and gain; the mouse wheel changes Q; right-click
// picks the type and slope; double-click resets the gain.
namespace ParametricEqWindows
{
void open (AudioEngine& engine, int track, int slot);
void closeAll();
} // namespace ParametricEqWindows

// The editor alone (for --snapshot).
std::unique_ptr<juce::Component> makeParametricEqEditor (AudioEngine& engine, int track, int slot);

// Draws the EQ curve (also used in the mixer panel thumbnail).
void drawParametricEqCurve (juce::Graphics& g, juce::Rectangle<float> area, const EffectParams& params, double sampleRate,
                            bool enabled, int highlightBand = -1);
} // namespace lynx
