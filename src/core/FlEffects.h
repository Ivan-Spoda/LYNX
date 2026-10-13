#pragma once

// Estado salvo (0xD5) dos efeitos nativos do FL nos slots do mixer -> parâmetros dos efeitos
// internos do LYNX. Formatos e fórmulas em docs/analise/REA_rodada7.md (clean-room).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Monta o slot a partir do nome do plugin do FL e do blob. Sem blob utilizável, devolve o efeito
// interno parecido com os parâmetros padrão; nullopt quando não há efeito parecido.
// `bpm` converte tempos sincronizados (ex.: predelay do Reeverb 2) para milissegundos.
std::optional<EffectSlot> effectFromFl (const std::string& pluginName, const std::vector<std::uint8_t>& state, double bpm = 120.0);
} // namespace lynx
