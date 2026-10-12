#pragma once

#include <functional>
#include <string>

#include "core/Song.h"
#include "flp/Flp.h"

namespace daw
{
// Pastas para onde apontam as variáveis que o FL grava nos caminhos de sample.
struct FlPathRoots
{
    std::string factoryData; // %FLStudioFactoryData%  ex.: C:\Program Files\Image-Line\FL Studio 2026
    std::string data;        // %FLStudioData%         ex.: ...\FL Studio 2026\Data
    std::string userData;    // %FLStudioUserData%     ex.: Documentos\Image-Line\FL Studio
};

std::string resolveFlPath (const std::string& flPath, const FlPathRoots& roots);

struct FlpImportOptions
{
    FlPathRoots roots;
    std::function<bool (const std::string& path)> fileExists; // sem isto, nenhum sample é usado
};

// Converte um projeto do FL Studio para o nosso modelo. Canais sampler cujo
// arquivo existe viram samplers; os demais usam um instrumento interno escolhido
// pelo nome. Patterns são reescaladas para o nosso PPQ. Plugins ainda não.
Song importFlp (const flp::Project& project, const FlpImportOptions& options = {});

InstrumentKind guessInstrument (const std::string& channelName, flp::ChannelType type);
} // namespace daw
