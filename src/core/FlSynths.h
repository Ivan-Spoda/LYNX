#pragma once

// Estado (0xD5) de sintetizadores simples do FL aproximados pelo synth interno de 3 osciladores.
// Formatos e fórmulas em docs/analise/REA_rodada10.md (clean-room).

#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Harmless: osciladores (timbre, máscara ímpar, sub, uníssono), envelope de volume, filtro com
// envelope, LFO, mono/porta. `gain` recebe o volume mestre do Harmless (linear). `bpm` converte os
// tempos em segundos para os parâmetros sincronizados.
bool synthFromHarmless (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth, float& gain);

// Fruit Kick: seno com envelope de afinação e decaimento de amplitude.
bool synthFromFruitKick (const std::vector<std::uint8_t>& state, SynthSettings& synth);
} // namespace lynx
