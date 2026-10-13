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

    // Ramos em paralelo da entrada à saída (todos no barramento 0, cada um uma cadeia simples; ramo
    // vazio = sinal seco), com o ganho dos cabos multiplicado. Vazio se o grafo for outro.
    struct Branch
    {
        std::vector<int> nodes;
        float gain = 1.0f;
        int band = 0; // barramento de saída do nó divisor `splitter` de onde o ramo sai
    };
    std::vector<Branch> branches;
    std::vector<int> preChain; // nós em série antes da divisão em ramos
    int splitter = -1;         // Frequency Splitter / Stereo Shaper que divide (os ramos começam nas saídas dele)
    int mainGenerator = -1;       // gerador ligado à saída com mais ganho
};

std::optional<PatcherGraph> parsePatcher (const std::vector<std::uint8_t>& state);
} // namespace lynx
