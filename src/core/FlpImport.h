#pragma once

#include "core/Song.h"
#include "flp/Flp.h"

namespace daw
{
// Converte um projeto do FL Studio para o nosso modelo: canais viram canais
// com instrumento interno escolhido pelo nome/tipo, patterns são reescaladas
// para o nosso PPQ. Plugins e samples ainda não são carregados.
Song importFlp (const flp::Project& project);

InstrumentKind guessInstrument (const std::string& channelName, flp::ChannelType type);
} // namespace daw
