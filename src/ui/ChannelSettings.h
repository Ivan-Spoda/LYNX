#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// Janela de ajustes do sampler de um canal (afinação, ataque, release, ao contrário,
// "cut itself"), como o Channel settings do FL. Uma janela por canal.
void openChannelSettings (AudioEngine& engine, int channel);
void closeChannelSettings();

// Janela de modulação do canal (envelopes de corte/afinação e LFOs), do synth ou do sampler.
void openChannelModulation (AudioEngine& engine, int channel);

// Só o conteúdo da janela (para o modo --snapshot).
std::unique_ptr<juce::Component> makeChannelSettingsPanel (AudioEngine& engine, int channel);
std::unique_ptr<juce::Component> makeChannelModulationPanel (AudioEngine& engine, int channel);
} // namespace lynx
