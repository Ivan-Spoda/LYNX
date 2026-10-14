#pragma once

// Physical-model instruments rebuilt from FL Studio's Sakura (strings) and Drumpad (drum) engines, following the
// behaviour decoded in docs/analise/REA_rodada28.md (clean-room; checked against the installation's DLL renders).
// Both run at their engine's internal rate (Sakura 48 kHz, Drumpad 38 kHz) and are resampled linearly to the
// host rate, as the originals do. Parameters are FL's raw state values (ChannelDef::modelParams).

#include <memory>

#include "core/Instruments.h"

namespace lynx
{
constexpr int sakuraParamCount = 146; // "Skr1" state floats (0…113 are parameters)
constexpr int drumpadParamCount = 38;

std::unique_ptr<Instrument> makeSakuraInstrument();
std::unique_ptr<Instrument> makeDrumpadInstrument();
} // namespace lynx
