#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// Janela de ajustes do sampler de um canal (afinação, ataque, release, ao contrário,
// "cut itself"), como o Channel settings do FL. Uma janela por canal.
void openChannelSettings (AudioEngine& engine, int channel);
void closeChannelSettings();

// Só o conteúdo da janela (para o modo --snapshot).
std::unique_ptr<juce::Component> makeChannelSettingsPanel (AudioEngine& engine, int channel);
} // namespace lynx
