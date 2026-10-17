#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// Plugin performance monitor (FL's, interpreted; REA_rodada84): how much processing time each instrument and effect
// slot takes, as a share of real time (now, average, peak), with a search, Freeze, Reset and "Reset on play". The
// rack measures only while the window is open.
void showPluginMonitor (AudioEngine& engine);

// The content for --snapshot mode, filled with sample measurements.
std::unique_ptr<juce::Component> makePluginMonitorPanel (AudioEngine& engine);
} // namespace lynx
