#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Built-in synth presets (name, settings). The first one is the default.
std::vector<std::pair<std::string, SynthSettings>> synthPresets();

// Preset matching a channel name ("Bass", "Pad", "808"...), to give a reasonable sound
// to FL synths that become the built-in synth on import.
SynthSettings presetForChannelName (const std::string& channelName);
} // namespace lynx
