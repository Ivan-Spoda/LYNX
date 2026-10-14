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

// VST3 parameter "slots" FL keeps in record 4 of that state (u32 N + N × i32 ParamID, −1 = empty slot).
// FL's automation index k of a VST3 plugin is a slot, not a parameter position (REA_rodada22 §2.3).
std::vector<std::int32_t> flVst3ParamSlots (const std::vector<std::uint8_t>& state);

// FL's slot assignment at load (REA_rodada22 §2.3): saved slots keep their ParamID; every ParamID of the
// plugin's current list (getParameterInfo order) without a slot gets the next free one. Returns
// slot → position in `paramIds` (−1 = the slot's parameter no longer exists).
std::vector<int> flVst3SlotPositions (const std::vector<std::int32_t>& savedSlots, const std::vector<std::uint32_t>& paramIds);
} // namespace lynx
