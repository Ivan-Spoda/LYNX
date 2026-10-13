#pragma once

// Estado (0xD5) dos geradores do FL baseados em samples: FPC (bateria por pads) e DirectWave
// (multi-sample). Formatos em docs/analise/REA_rodada9.md (clean-room).

#include <cstdint>
#include <optional>
#include <vector>

#include "core/FlpImport.h"

namespace lynx
{
// Pads do FPC que têm sample (caminhos ainda com as variáveis do FL, ex. %FLStudioData%).
std::optional<std::vector<DrumPad>> fpcPads (const std::vector<std::uint8_t>& state);

// Programa da DirectWave: as zonas (caminhos com as variáveis do FL) e o envelope de amplitude da
// primeira zona.
struct DirectWaveProgram
{
    std::vector<SampleZone> zones;
    float attack = 0.0f, decay = 0.0f, sustain = 1.0f, release = 0.0f; // segundos / 0..1
};
std::optional<DirectWaveProgram> directWaveProgram (const std::vector<std::uint8_t>& state);
} // namespace lynx
