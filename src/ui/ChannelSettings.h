#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// Sampler settings window for a channel (pitch, attack, release, reverse,
// "cut itself"), like FL's Channel settings. One window per channel.
void openChannelSettings (AudioEngine& engine, int channel);
void closeChannelSettings();

// Channel button toggle (Channel Rack): which kinds have a settings window, and whether it is open.
bool hasChannelSettings (InstrumentKind kind) noexcept;
bool isChannelSettingsOpen (int channel);
void closeChannelSettings (int channel);

// Channel modulation window (cutoff/pitch envelopes and LFOs), for the synth or the sampler.
void openChannelModulation (AudioEngine& engine, int channel);

// Window content only (for --snapshot mode).
std::unique_ptr<juce::Component> makeChannelSettingsPanel (AudioEngine& engine, int channel);
std::unique_ptr<juce::Component> makeChannelModulationPanel (AudioEngine& engine, int channel);
} // namespace lynx
