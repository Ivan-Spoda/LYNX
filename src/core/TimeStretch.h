#pragma once

// Time-stretch que preserva a altura (WSOLA: overlap-add com busca de similaridade).
// Feito fora da thread de áudio, uma vez por (sample, razão), como os modos "offline" do
// FL (docs/analise/REA_rodada3.md §5).

#include <memory>

#include "core/Instruments.h"

namespace lynx
{
// `ratio` = duração de saída / duração de entrada (2 = o dobro do tempo, mesma altura).
std::shared_ptr<SampleData> timeStretch (const SampleData& input, double ratio);
} // namespace lynx
