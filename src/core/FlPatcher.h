#pragma once

// Patcher do FL (REA_rodada14 §1): o grafo de plugins guardado no estado (0xD5). O LYNX aproveita
// os casos simples: uma cadeia em série de efeitos (vira slots seguidos no mixer) e o gerador principal
// de um Patcher usado como instrumento.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lynx
{
struct PatcherNode
{
    std::string plugin;               // nome interno (o mesmo do 0xC9)
    std::vector<std::uint8_t> state;  // o mesmo formato do 0xD5 do plugin num slot/canal
    bool generator = false;
};

struct PatcherGraph
{
    std::vector<PatcherNode> nodes;
    std::vector<int> serialChain; // nós da entrada à saída, se o áudio for uma cadeia simples
    int mainGenerator = -1;       // gerador ligado à saída com mais ganho
};

std::optional<PatcherGraph> parsePatcher (const std::vector<std::uint8_t>& state);
} // namespace lynx
