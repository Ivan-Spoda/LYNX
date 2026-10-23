#pragma once

// State of FL's "Fruity Wrapper" (third-party VST2/VST3/CLAP plugins) inside the .flp.
// Format described in docs/analise/REA_rodada5.md (behavioral, clean-room).

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Reads the wrapper's 0xD5 blob (versions 5 to 12: u32 version + records {u32 id, u64 size, data}).
// False if the format is not a known one.
bool parseFlWrapper (const std::vector<std::uint8_t>& blob, ForeignPlugin& out);

// FL's wrapper processing options (record 0x02 flags, REA round 134) onto LYNX's plugin options: "Use fixed size
// buffers" (flag 0x2), "Process maximum size buffers" (fixed options bit 0), "Allow threaded processing" (0x1000,
// wrapper version 7+), "Make bridged" (0x40000, version 8+). Flag 0x400000 = the defaults. Nothing changes without
// the record.
void applyFlWrapperOptions (const ForeignPlugin& wrapper, PluginRef& ref);

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

// VST2 state FL stores in record 0x35 (REA_rodada5 §3.1, layouts per version in REA_rodada89 §2):
// i32 version (−9…−1); v ≤ −6: u8 flags; v ≤ −5: i32 size (−2 ⇒ i64 size follows); v = −7 with no chunk: one
// more i32; v ≤ −6: i32 current program; (v −5/−4: i32 program; v −4…−1: size as above); chunk[size];
// without a chunk: i32 n + n × f32 (0…1); then i32 m + program names (char[25]; one name when v ≤ −6).
struct FlVst2State
{
    std::int32_t version = -9;
    std::uint8_t flags = 0;          // bit0 = all programs saved, bit3 = the plugin uses chunks
    std::int32_t currentProgram = 0;
    std::vector<std::uint8_t> chunk; // effGetChunk result (FL applies it with effSetChunk as a bank)
    std::vector<float> params;       // numParams values, or numParams × numPrograms when flags bit0
    std::vector<std::string> programNames;

    bool allPrograms() const noexcept { return (flags & 1) != 0; }
    // FL applies the chunk as a single program only for version −6 states saved without "all programs"
    bool chunkIsProgram() const noexcept { return version == -6 && ! allPrograms(); }
};

// False for empty states, the "nothing to restore" marker (123456), versions ≥ 0 (not decoded) and truncated data.
bool parseFlVst2State (const std::vector<std::uint8_t>& state, FlVst2State& out);
// Writes the current (−9) layout; parseFlVst2State reads it back.
std::vector<std::uint8_t> writeFlVst2State (const FlVst2State& state);
} // namespace lynx
