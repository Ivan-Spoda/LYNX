#pragma once

// State of FL's "Fruity Wrapper" (third-party VST2/VST3/CLAP plugins) inside the .flp.
// Format described in docs/analise/REA_rodada5.md (behavioral, clean-room).

#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Reads the wrapper's 0xD5 blob (versions 5 to 12: u32 version + records {u32 id, u64 size, data}).
// False if the format is not a known one.
bool parseFlWrapper (const std::vector<std::uint8_t>& blob, ForeignPlugin& out);

// VST3 state FL stores in record 0x35 (u32 version + records: 3 component, 2 controller).
bool splitFlVst3State (const std::vector<std::uint8_t>& state, std::vector<std::uint8_t>& component,
                       std::vector<std::uint8_t>& controller);
} // namespace lynx
