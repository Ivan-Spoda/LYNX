#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
// Janelas secundárias (plugin, ajustes do canal, EQ, automação) ficam sempre acima da janela principal
// do LYNX, sem ficar acima dos outros programas: no Windows, a principal vira a "dona" delas.
void setMainWindowForOwnership (juce::Component* mainWindow);
void keepAboveMainWindow (juce::Component& window); // chamar depois de a janela estar visível
} // namespace lynx
