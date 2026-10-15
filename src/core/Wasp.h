#pragma once

// FL Studio 2026's Wasp and Wasp XT (docs/analise/REA_rodada64.md §5). The 64-bit installation ships no Wasp engine:
// Plugins\Fruity\Generators\Wasp\Wasp_x64.dll and Wasp XT\Wasp XT_x64.dll are the same placeholder DLL, a generator that
// stores the saved state unchanged, ignores parameters, refuses every voice and renders nothing ("This plugin doesn't
// have a 64 bit version"). LYNX does the same: the channel keeps the raw state (record 0xD5) in ChannelDef::pluginState
// and plays silence.

#include <memory>

#include "core/Instruments.h"

namespace lynx
{
std::unique_ptr<Instrument> makeWaspInstrument();
} // namespace lynx
