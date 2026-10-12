#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Mixer no estilo do FL: uma faixa vertical por trilha (Master primeiro).
//  - fader: arrastar na vertical; duplo clique volta a 0 dB
//  - pan: arrastar o knob; duplo clique centraliza
//  - LED verde = mute (clique), "S" = solo
//  - clique numa faixa seleciona; clique direito abre o menu (renomear, rotear o canal
//    selecionado para esta trilha); "+" no fim adiciona um insert
//  - embaixo de cada faixa aparecem os canais que chegam nela
class MixerStrips final : public juce::Component,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit MixerStrips (AudioEngine& engine);
    ~MixerStrips() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    void updateSize();
    void setVisibleHeight (int height);

    static constexpr int stripWidth = 70;
    static constexpr int masterWidth = 86;

private:
    enum class Part { none, pan, fader, mute, solo, body, add };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    int stripX (int track) const noexcept;
    int stripWidthOf (int track) const noexcept { return track == 0 ? masterWidth : stripWidth; }
    int trackAt (int x) const noexcept;
    Part partAt (int track, juce::Point<int> p) const;
    juce::Rectangle<int> faderArea (int track) const;
    juce::Rectangle<int> panArea (int track) const;
    juce::Rectangle<int> muteArea (int track) const;
    juce::Rectangle<int> soloArea (int track) const;
    void showMenu (int track);

    AudioEngine& engine;
    int selected = 0;
    int visibleHeight = 400;

    Part dragPart = Part::none;
    int dragTrack = -1;
    float dragStartValue = 0.0f;

    std::vector<float> levels; // nível exibido por trilha*2, com queda suave

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrips)
};

class MixerComponent final : public juce::Component
{
public:
    explicit MixerComponent (AudioEngine& engine);
    void resized() override;

private:
    MixerStrips strips;
    juce::Viewport viewport;
};
} // namespace lynx
