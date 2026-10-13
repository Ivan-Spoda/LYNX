#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Presets do synth interno (nome, ajustes). O primeiro é o padrão.
std::vector<std::pair<std::string, SynthSettings>> synthPresets();

// Preset que combina com o nome de um canal ("Bass", "Pad", "808"...), para dar um som
// razoável aos sintetizadores do FL que viram o synth interno na importação.
SynthSettings presetForChannelName (const std::string& channelName);
} // namespace lynx
