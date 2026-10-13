#pragma once

// Estado salvo (0xD5) dos efeitos nativos do FL nos slots do mixer -> parâmetros dos efeitos
// internos do LYNX. Formatos e fórmulas em docs/analise/REA_rodada7.md (clean-room).

#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Monta o slot a partir do nome do plugin do FL e do blob. Sem blob utilizável, devolve o efeito
// interno parecido com os parâmetros padrão; nullopt quando não há efeito parecido.
// `bpm` converte tempos sincronizados (ex.: predelay do Reeverb 2) para milissegundos.
std::optional<EffectSlot> effectFromFl (const std::string& pluginName, const std::vector<std::uint8_t>& state, double bpm = 120.0);

// Preset de efeito do FL (.fst de Plugin presets\Effects): o efeito interno com os ajustes do preset.
std::optional<EffectSlot> effectFromFlPreset (const std::vector<std::uint8_t>& fileBytes, double bpm = 120.0);

// Plugins do FL cujos presets dão este efeito interno (para listar as pastas de presets).
std::vector<std::string> flPluginsForEffect (EffectKind kind);
// Automação de um parâmetro de efeito nativo do FL: o nosso parâmetro equivalente e a conversão do
// valor normalizado do FL (0..1 na faixa do parâmetro dele) para o nosso. nullopt = sem equivalente.
struct FlParamMap
{
    int param = 0;
    std::function<float (float)> convert;
};
std::optional<FlParamMap> effectParamFromFl (const std::string& pluginName, int flParam);

// Fruity Love Philter -> efeitos do LYNX (REA_rodadas 16 §2 e 17 §15): cada unidade ligada vira um EQ
// paramétrico (tipo, corte e Q pelas fórmulas do FL a 44,1 kHz) + ganho/pan. Uma unidade: `slots` em
// série (e `paramMap` traduz a automação do FL para eles); várias em paralelo (NEXT = 0): um contêiner.
struct LovePhilterConversion
{
    std::vector<EffectSlot> slots;
    // automação do parâmetro k do FL -> (índice dentro de `slots`, parâmetro nosso, conversão do valor)
    std::function<std::optional<std::tuple<int, int, std::function<float (float)>>> (int k)> paramMap;
    std::vector<std::string> notes; // o que ficou de fora (para o relatório)
};
std::optional<LovePhilterConversion> lovePhilterEffects (const std::vector<std::uint8_t>& state);

// Valor atual (0..1) da saída `output` de um controlador parado (Fruity X-Y Controller, Control
// Surface; REA_rodada15 §11): um link dele vira um valor fixo no alvo. nullopt se não der para ler.
std::optional<float> controllerConstantOutput (const std::string& pluginName, const std::vector<std::uint8_t>& state, int output);
} // namespace lynx
