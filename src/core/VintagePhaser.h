#pragma once

// FL Studio's Vintage Phaser, rebuilt from the behaviour decoded in docs/analise/REA_rodada29.md §3 (clean-room;
// the reference model matches the installation's engine bit-exactly on the linear paths). Parameters are the
// engine's 36 normalised values in the core's order (VST indices 22 and 23 swapped).

#include <memory>
#include <optional>
#include <vector>

#include "core/Effects.h"

namespace lynx
{
constexpr int vintagePhaserParamCount = 36;

std::unique_ptr<Effect> makeVintagePhaser();

// FL state (version + 36 raw ints [+ 1 byte]) → the effect's parameters; nullopt if not a Vintage Phaser state
std::optional<EffectSlot> vintagePhaserFromFl (const std::vector<std::uint8_t>& state);
} // namespace lynx
