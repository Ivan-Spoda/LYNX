#pragma once

// Estado (0xD5) de sintetizadores simples do FL aproximados pelo synth interno de 3 osciladores.
// Formatos e fórmulas em docs/analise/REA_rodada10.md (clean-room).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
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

// Fruity Envelope Controller (REA_rodada15 §10): 8 articuladores; a saída k é o articulador k, que
// segue a última nota do canal dentro da sua faixa de teclas. Até 64 pontos por envelope.
struct ControllerArticulator
{
    bool bipolar = false, envOn = false, tempo = false; // tempo: tempos (semínimas) em vez de segundos
    float base = 0.5f, envLevel = 1.0f;
    int sustain = -1, lowKey = 0, highKey = 256;
    int count = 0;
    std::array<float, 64> time {}, level {}; // tempo acumulado de cada ponto e nível 0..1
};
using EnvelopeControllerData = std::array<ControllerArticulator, 8>;
std::optional<EnvelopeControllerData> parseEnvelopeController (const std::vector<std::uint8_t>& state);

// Valor (0..1) do articulador `sinceOn` depois do disparo; `sinceRelease` < 0 = tecla ainda presa.
// Tempos na unidade do articulador (segundos, ou tempos com `tempo`).
float controllerValue (const ControllerArticulator& a, double sinceOn, double sinceRelease) noexcept;

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

// Kepler (REA_rodada13 §2, modo "Kepler-1"): serra/pulso/sub/ruído, VCF com o Env1, LFO, chorus.
bool synthFromKepler (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);

// Wave Traveller (§4): o sample (caminho com as variáveis do FL) e a nota-raiz; o scratch fica de fora.
std::optional<std::pair<std::string, int>> waveTravellerSample (const std::vector<std::uint8_t>& state);

// FLEX (§1.1): nome do pacote e do preset, do estado do plugin. Os pacotes do FLEX são protegidos e o
// LYNX não os abre: o canal ganha um patch do synth escolhido por esses nomes.
std::optional<std::pair<std::string, std::string>> flexPresetName (const std::vector<std::uint8_t>& state);

// PoiZone, BassDrum, Toxic Biohazard (REA_rodada15 §12): PoiZone mapeia bem (2 osc + ruído, 2 ADSR,
// filtro, LFO); BassDrum vira um bumbo (seno com queda de afinação + ruído); Toxic, FM de 2 operadores.
bool synthFromPoiZone (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);
bool synthFromBassDrum (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);
bool synthFromToxic (const std::vector<std::uint8_t>& state, SynthSettings& synth, float& gain);

// BooBass (REA_rodada10 §5): serra + seno grave, corte pelo Treble.
bool synthFromBooBass (const std::vector<std::uint8_t>& state, SynthSettings& synth);

// Drumpad (§6): nome do patch ("War Drum Lo 01"); `drumToneForName` dá um tambor genérico pelo nome
// (os bumbos/caixas/chimbais usam os instrumentos de bateria do LYNX).
std::optional<std::string> drumpadPatchName (const std::vector<std::uint8_t>& state);
SynthSettings drumToneForName (const std::string& patchName);
std::vector<std::string> drumSampleCandidates (const std::string& soundName);
} // namespace lynx
