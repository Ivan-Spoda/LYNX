#pragma once

// Estado (0xD5) dos geradores do FL baseados em samples: FPC (bateria por pads) e DirectWave
// (multi-sample). Formatos em docs/analise/REA_rodada9.md (clean-room).

#include <cstdint>
#include <optional>
#include <string>
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

// Sample fatiado do Fruity Slicer / Slicex: o arquivo (ou o WAV embutido no projeto) e, no Slicer,
// as posições das fatias em quadros. No Slicex as fatias vêm dos pontos "cue" do próprio WAV.
struct SlicedSample
{
    std::string path;
    std::vector<std::uint8_t> embeddedWav;
    std::vector<std::int64_t> positions; // Slicer: início de cada fatia (quadros)
    float pitchCents = 0.0f;
};
std::optional<SlicedSample> fruitySlicer (const std::vector<std::uint8_t>& state);
std::optional<SlicedSample> slicex (const std::vector<std::uint8_t>& state);

// Pontos "cue" (quadros) de um WAV, em ordem; `frames` recebe a duração quando o áudio é PCM/float.
std::vector<std::int64_t> wavCuePoints (const std::vector<std::uint8_t>& wav, std::int64_t& frames);
} // namespace lynx
