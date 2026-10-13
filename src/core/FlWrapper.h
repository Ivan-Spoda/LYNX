#pragma once

// Estado do "Fruity Wrapper" do FL (plugins VST2/VST3/CLAP de terceiros) dentro do .flp.
// Formato descrito em docs/analise/REA_rodada5.md (comportamental, clean-room).

#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Lê o blob 0xD5 do wrapper (versões 5 a 12: u32 versão + registros {u32 id, u64 tamanho, dados}).
// Falso se não é um formato conhecido.
bool parseFlWrapper (const std::vector<std::uint8_t>& blob, ForeignPlugin& out);

// Estado VST3 que o FL guarda no registro 0x35 (u32 versão + registros: 3 componente, 2 controlador).
bool splitFlVst3State (const std::vector<std::uint8_t>& state, std::vector<std::uint8_t>& component,
                       std::vector<std::uint8_t>& controller);
} // namespace lynx
