#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// Janela "Exportar áudio": formato, taxa e o que exportar; depois pergunta o arquivo e
// renderiza numa thread de fundo com barra de progresso. `suggestedName` = nome sem extensão.
void showExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName);

// "Renderizar para áudio" (congelar): o insert vira um clip de áudio num insert novo com o mesmo
// roteamento, e o original é mutado. Roda numa thread de fundo com progresso.
void bounceInsertToAudio (AudioEngine& engine, int track);

// Só o painel (para o modo --snapshot).
std::unique_ptr<juce::Component> makeExportPanel (AudioEngine& engine);
} // namespace lynx
