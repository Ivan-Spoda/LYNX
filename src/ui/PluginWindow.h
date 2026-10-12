#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Janela com a interface própria de um plugin. Mantém o plugin vivo enquanto
// está aberta (mesmo que o canal seja removido), e só uma janela por plugin.
namespace PluginWindows
{
// `owner` é o adaptador (PluginInstrument/PluginEffect) que contém o plugin.
void open (std::shared_ptr<void> owner, juce::AudioPluginInstance& plugin, const juce::String& title);
void closeAll();
} // namespace PluginWindows

// Varre as pastas de VST3 com uma janela de progresso. `onDone(novos)` roda na thread da UI.
void scanPluginsWithProgress (AudioEngine& engine, std::function<void (int)> onDone);

// Menus de plugins conhecidos (instrumentos ou efeitos). `onChosen` recebe a referência.
void addPluginItems (juce::PopupMenu& menu, AudioEngine& engine, bool instruments, std::function<void (PluginRef)> onChosen);
} // namespace lynx
