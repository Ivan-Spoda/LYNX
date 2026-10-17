#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// 7-band parametric EQ (like FL's Parametric EQ 2). Its window is the native effect editor in FL's arrangement
// (ui/PluginEditors.h, core/PluginLayout.cpp): the response curve with one handle per band (drag = frequency and gain,
// wheel = Q / bandwidth, right-click = type and slope, double-click = 0 dB), the output fader and one column per band
// (type, gain fader, FREQ and BW knobs). These are the EQ's drawing helpers and the window entry points.
namespace ParametricEqWindows
{
void open (AudioEngine& engine, int track, int slot);
void closeAll(); // closes every native effect window
} // namespace ParametricEqWindows

// The editor alone (for --snapshot).
std::unique_ptr<juce::Component> makeParametricEqEditor (AudioEngine& engine, int track, int slot);

// Draws the EQ curve (also used in the mixer panel thumbnail); the graph spans ±rangeDb.
void drawParametricEqCurve (juce::Graphics& g, juce::Rectangle<float> area, const EffectParams& params, double sampleRate,
                            bool enabled, int highlightBand = -1, double rangeDb = 24.0, bool labels = true);

namespace peqview
{
juce::Colour bandColour (int band);
float xForHz (juce::Rectangle<float> area, double hz);
double hzForX (juce::Rectangle<float> area, float x);
float yForDb (juce::Rectangle<float> area, double db, double rangeDb);
double dbForY (juce::Rectangle<float> area, float y, double rangeDb);
juce::String bandTypeName (int type);
bool hasGain (const EffectParams& params, int band);
// the handle of a band in the graph
juce::Point<float> handlePosition (juce::Rectangle<float> area, const EffectParams& params, int band, double rangeDb);
int bandAt (juce::Rectangle<float> area, const EffectParams& params, juce::Point<float> p, double rangeDb);
// right-click menu of a band: type, then FL's orders or the cut slope; `set` applies (param, value) changes
void showBandMenu (juce::Component& target, const EffectParams& params, int band, std::function<void (std::vector<std::pair<int, float>>)> set);
// one line about the band (type, frequency, gain, width / order)
juce::String bandText (const EffectParams& params, int band);
} // namespace peqview
} // namespace lynx
