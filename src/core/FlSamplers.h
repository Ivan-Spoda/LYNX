#pragma once

// Estado (0xD5) dos geradores do FL baseados em samples: FPC (bateria por pads) e DirectWave
// (multi-sample). Formatos em docs/analise/REA_rodada9.md (clean-room).

#include <cstdint>
#include <functional>
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
    std::vector<std::vector<std::uint8_t>> embeddedWav; // por zona: WAV do áudio embutido no projeto (vazio = não tem)
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

// FL Keys: o nome do conjunto de samples (o .wti em Plugins\Fruity\Generators\FL Keys\Wavtables).
std::optional<std::string> flKeysSet (const std::vector<std::uint8_t>& state);

// Zonas de um .wti (INI) do FL Keys. `folder` é a pasta do .wti; amostras "raw" (um .dat só) são
// recortadas de `rawData` e entregues a `saveWav` (que devolve o caminho do WAV gravado).
std::vector<SampleZone> flKeysZones (const std::string& wti, const std::string& folder, const std::vector<std::uint8_t>& rawData,
                                     const std::function<std::string (const std::vector<std::uint8_t>&, const std::string&)>& saveWav);

// WAV PCM 16 bits mono com estas amostras (para os samples "raw").
std::vector<std::uint8_t> makeWav16Mono (const std::int16_t* samples, std::size_t count, int sampleRate);

// WAV float 32 bits intercalado a partir de canais separados (planar).
std::vector<std::uint8_t> makeWavFloat (const std::vector<std::vector<float>>& channels, int sampleRate);

// Pontos "cue" (quadros) de um WAV, em ordem; `frames` recebe a duração quando o áudio é PCM/float.
std::vector<std::int64_t> wavCuePoints (const std::vector<std::uint8_t>& wav, std::int64_t& frames);
} // namespace lynx
