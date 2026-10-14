#pragma once

// Physical-model instruments rebuilt from FL Studio's Sakura (strings) and Drumpad (drum) engines, following the
// behaviour decoded in docs/analise/REA_rodada28.md (clean-room; checked against the installation's DLL renders).
// Both run at their engine's internal rate (Sakura 48 kHz, Drumpad 38 kHz) and are resampled linearly to the
// host rate, as the originals do. Parameters are FL's raw state values (ChannelDef::modelParams).

#include <memory>
#include <string>

#include "core/Instruments.h"

namespace lynx
{
constexpr int sakuraParamCount = 146; // "Skr1" state floats (0…113 are parameters)
constexpr int drumpadParamCount = 38;

std::unique_ptr<Instrument> makeSakuraInstrument();
std::unique_ptr<Instrument> makeDrumpadInstrument();
// BooBass reads its multisample from the user's own BooBass_Eng_x64.dll (ChannelDef::samplePath, SHA-256 checked);
// modelParams = the 3 raw knobs (0…65535: Bass, Mid, Treble)
std::unique_ptr<Instrument> makeBooBassInstrument();
// true if `path` is the decoded BooBass_Eng_x64.dll build (its multisample can be read)
bool booBassDllUsable (const std::string& path);
} // namespace lynx
