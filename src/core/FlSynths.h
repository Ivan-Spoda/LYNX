#pragma once

// Estado (0xD5) de sintetizadores simples do FL aproximados pelo synth interno de 3 osciladores.
// Formatos e fórmulas em docs/analise/REA_rodada10.md (clean-room).

#include <cstdint>
#include <optional>
#include <string>
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

// MiniSynth (do FL Studio Mobile; contêiner "SSLF" com 43 floats): tipo de oscilador, filtro
// ladder, envelopes, LFO, mono.
bool synthFromMiniSynth (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth);

// Fruity DX10 (FM de 2 operadores): portadora seno no osc 1, modulador no osc 3 (FM do synth) com o
// índice caindo de Init até Sustain; o "Mod Thru" soa no osc 2.
bool synthFromDX10 (const std::vector<std::uint8_t>& state, SynthSettings& synth);

// Sytrus (6 operadores FM/RM + 3 filtros, estado zlib; REA_rodada11): as 3 portadoras mais fortes
// viram os osciladores, FM forte vira onda mais rica + envelope de filtro, o primeiro filtro ativo
// vira o filtro do synth. `gain` recebe o Master level (linear).
bool synthFromSytrus (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth, float& gain);

// Harmor (REA_rodada11 §4), aproximação grosseira: cada parte (A/B) vira uma serra com a razão de
// frequência, filtro 1 (ou 2) com o envelope, envelope de volume, uníssono, mono/porta.
bool synthFromHarmor (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);

// GMS (REA_rodada12 §1): "SSLF" + preset .gmsynth; 3 osciladores de tabela, mistura em cascata,
// AM/sync, uníssono, ladder, 2 envelopes AD e 2 LFOs. `gain` = Mix Level × Amp Level.
bool synthFromGms (const std::vector<std::uint8_t>& state, double bpm, SynthSettings& synth, float& gain);

// Ogun / Autogun (§2), aproximação grosseira: forma pela "fullness", decay/release do timbre,
// uníssono e o filtro. O timbre aditivo e os articuladores ficam de fora.
bool synthFromOgun (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);

// Sakura (§4): corda aproximada (serra + triângulo, Hi-Cut, ADSR). Escalas [HIP].
bool synthFromSakura (const std::vector<std::uint8_t>& state, SynthSettings& synth);

// Fruity DrumSynth Live (§3): nome do som escolhido (ex. "909 Hat O") e caminhos (com
// %FLStudioData%) de samples de bateria do FL que o substituem, em ordem de preferência.
std::optional<std::string> drumSynthLiveSound (const std::vector<std::uint8_t>& state);
std::vector<std::string> drumSampleCandidates (const std::string& soundName);
} // namespace lynx
