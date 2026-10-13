#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>

#include "core/Song.h"
#include "flp/Flp.h"

namespace lynx
{
// Pastas para onde apontam as variáveis que o FL grava nos caminhos de sample.
struct FlPathRoots
{
    std::string factoryData; // %FLStudioFactoryData%  ex.: C:\Program Files\Image-Line\FL Studio 2026
    std::string data;        // %FLStudioData%         ex.: ...\FL Studio 2026\Data
    std::string userData;    // %FLStudioUserData%     ex.: Documentos\Image-Line\FL Studio
};

std::string resolveFlPath (const std::string& flPath, const FlPathRoots& roots);

// O que o import fez com cada coisa do FL que não é "nota e sample": para o usuário saber por que
// algo soa diferente.
struct FlpImportReport
{
    std::map<std::string, int> converted;    // geradores convertidos de verdade (3x Osc, FPC, DirectWave...)
    std::map<std::string, int> approximated; // aproximados pelo synth interno (Harmless, DX10...)
    std::map<std::string, int> unsupported;  // sem equivalente: tocam com um preset do synth pelo nome
    std::map<std::string, int> droppedEffects; // efeitos sem equivalente (saem do mixer)
    int missingSamples = 0;
};

struct FlpImportOptions
{
    FlPathRoots roots;
    std::function<bool (const std::string& path)> fileExists; // sem isto, nenhum sample é usado

    // Opcionais (fatias do Fruity Slicer/Slicex): bytes de um arquivo, duração de um sample em
    // quadros (0 = desconhecida) e gravação de um WAV embutido no projeto (devolve o caminho).
    std::function<std::vector<std::uint8_t> (const std::string& path)> readFile;
    std::function<std::int64_t (const std::string& path)> sampleFrames;
    std::function<std::string (const std::vector<std::uint8_t>& wav, const std::string& name)> saveEmbedded;

    FlpImportReport* report = nullptr; // opcional
};

// Converte um projeto do FL Studio para o nosso modelo. Canais sampler cujo
// arquivo existe viram samplers; os demais usam um instrumento interno escolhido
// pelo nome. Patterns são reescaladas para o nosso PPQ. Plugins ainda não.
Song importFlp (const flp::Project& project, const FlpImportOptions& options = {});

InstrumentKind guessInstrument (const std::string& channelName, flp::ChannelType type);

// Osciladores do synth interno a partir do estado (0xD5) do 3x Osc, versões 10 a 14
// (docs/analise/REA_rodada4.md §1-2). Mantém ADSR e filtro de `base`. Falso se o blob não serve.
bool synthFrom3xOsc (const std::vector<std::uint8_t>& state, SynthSettings& base);

// Filtro do canal do FL (Mod X/Y 0..256, tipo 0..7 do 0xDB) como corte em Hz e ressonância 0..1
// do filtro do synth/sampler (REA_rodada4 §3); `mode` 0 passa-baixas, 1 passa-banda, 2 passa-altas, 3 rejeita-faixa.
void synthFilterFromChannel (int modX, int modY, int type, float& cutoffHz, float& resonance, int& mode);

// Oitavas que o envelope de corte do FL (amount ±128 em unidades de Mod X) desloca a partir do Mod X do canal.
float filterEnvelopeOctaves (int modX, int type, int amount);

// Um pad de um sampler de bateria do FL (FPC e afins) que vira um canal próprio.
struct DrumPad
{
    std::string name;
    int key = 60;                   // tecla que dispara o pad no canal original
    std::vector<SampleZone> layers; // camadas por velocity (tocam na altura do sample)
    float gain = 1.0f;              // ganho linear do pad (multiplica o do canal)
    float pan = 0.0f, tune = 0.0f;  // pan −1..1; afinação em semitons
    int cutGroup = 0, cutBy = 0;    // tocar este pad corta os pads com cutBy == cutGroup (0 = nenhum)
    int mixerOffset = 0;            // trilha do mixer relativa à do canal
};

// Troca o canal `channel` por um canal sampler por pad (logo depois dele, no mesmo insert do mixer)
// e move as notas pela tecla: a nota do pad vai para o canal dele, na tecla 60. Notas de teclas sem
// pad somem. Os índices de canal das patterns, automações e clips são ajustados.
void splitDrumChannel (Song& song, int channel, const std::vector<DrumPad>& pads);

// Preset de canal do FL (.fst) do 3x Osc como ajustes do synth interno (osciladores, envelope,
// filtro, LFOs, porta). nullopt se o arquivo não é de 3x Osc.
std::optional<SynthSettings> synthFromFlPreset (const std::vector<std::uint8_t>& fileBytes);

// Efeito interno parecido com um efeito nativo do FL (pelo nome interno do plugin).
std::optional<EffectKind> effectForFlPlugin (const std::string& name);

// Alvo nosso para um controlID do FL (canal/mixer). `channelMap` = canal do FL -> nosso.
AutomationTarget automationTargetFor (flp::ControlId id, const std::map<int, int>& channelMap, int mixerTracks);
} // namespace lynx
