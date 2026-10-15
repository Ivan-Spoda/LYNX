#pragma once

// FL Studio's Maximus and Soundgoodizer as LYNX mixer effects, on the exact engine of core/HarmlessFx (rebuilt in
// docs/analise/REA_rodada36.md and REA_rodada40.md; clean-room). The effect slot carries everything in
// EffectSlot::state as LYNX's own small container:
//
//   "LXMX", u8 container version (1), u8 build (0 = Maximus / Shared SG_x64.dll, 1 = Soundgoodizer_x64.dll's copy),
//   i32 band-mix parameter (−1 = the state's own parameter 5), then the Maximus state bytes.
//
// A Maximus slot holds FL's own saved state; a Soundgoodizer slot holds the preset file a…d.dat read at import from
// the user's FL installation (never from this repository) and its Amount as the band mix.

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/Effects.h"
#include "core/HarmlessFx.h"

namespace lynx::maximusfx
{
struct Blob
{
    hlfx::MaximusBuild build = hlfx::MaximusBuild::shared;
    int mix = -1; // parameter 5; −1 = the state's own value
    std::vector<std::uint8_t> state;
};

std::vector<std::uint8_t> pack (const Blob& blob);
std::optional<Blob> unpack (const std::vector<std::uint8_t>& bytes);

// The engine's preset for a container (nullptr when the state is not supported: see makeMaximusPreset)
std::shared_ptr<const hlfx::MaximusPreset> presetOf (const Blob& blob);

// Soundgoodizer's state (0xD5, 12 bytes): version, preset 0…3 (A…D → Data\Presets\a…d.dat), amount 0…1000
struct SoundgoodizerState
{
    int preset = 0, amount = 600;
};
std::optional<SoundgoodizerState> parseSoundgoodizer (const std::vector<std::uint8_t>& state);
const char* soundgoodizerPresetFile (int preset); // "a.dat" … "d.dat"

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& slotState);
} // namespace lynx::maximusfx
