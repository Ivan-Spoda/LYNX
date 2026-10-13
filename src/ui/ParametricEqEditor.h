#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Editor do EQ paramétrico de 7 bandas (como o Parametric EQ 2 do FL): curva de resposta com uma
// alça por banda. Arrastar a alça muda frequência e ganho; a roda do mouse muda o Q; clique direito
// escolhe o tipo e a inclinação; duplo clique zera o ganho.
namespace ParametricEqWindows
{
void open (AudioEngine& engine, int track, int slot);
void closeAll();
} // namespace ParametricEqWindows

// O editor sozinho (para o --snapshot).
std::unique_ptr<juce::Component> makeParametricEqEditor (AudioEngine& engine, int track, int slot);

// Desenha a curva do EQ (usado também na miniatura do painel do mixer).
void drawParametricEqCurve (juce::Graphics& g, juce::Rectangle<float> area, const EffectParams& params, double sampleRate,
                            bool enabled, int highlightBand = -1);
} // namespace lynx
