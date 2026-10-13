#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// Nome legível do que uma automação controla ("Kick - Volume", "Insert 3 - Pan"...).
juce::String describeAutomationTarget (AudioEngine& engine, const AutomationTarget& target);

// Cria uma automação do alvo, começando no valor atual, e um clip dela na Playlist
// (do início da música até o fim dela, numa trilha livre), como o "Create automation clip" do FL.
void createAutomationClip (AudioEngine& engine, const AutomationTarget& target);

// Abre (ou traz para frente) a janela de edição dos pontos de uma automação.
void openAutomationEditor (AudioEngine& engine, int automation);
void closeAutomationEditors();

// Só o editor (para o modo --snapshot).
std::unique_ptr<juce::Component> makeAutomationEditor (AudioEngine& engine, int automation);

// Item "Criar clip de automação" para menus de knob.
void addAutomationItem (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target);

// Submenu com os parâmetros de um plugin (instrumento do canal ou efeito do slot).
void addPluginParameterItems (juce::PopupMenu& menu, AudioEngine& engine, juce::AudioPluginInstance& plugin,
                              const AutomationTarget& base);
} // namespace lynx
