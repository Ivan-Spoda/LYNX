#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// Janela "Exportar áudio": formato, taxa e o que exportar; depois pergunta o arquivo e
// renderiza numa thread de fundo com barra de progresso. `suggestedName` = nome sem extensão.
void showExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName);
} // namespace lynx
