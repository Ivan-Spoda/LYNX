#pragma once

#include <functional>
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
//  - botão de envio (seta) na base: liga/desliga o envio da trilha selecionada para esta;
//    arrastar na vertical muda o nível do envio
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

    int getSelectedTrack() const noexcept { return selected; }
    void selectTrack (int track);
    std::function<void (int)> onSelectionChanged;

    static constexpr int stripWidth = 70;
    static constexpr int masterWidth = 86;

private:
    enum class Part { none, pan, fader, mute, solo, route, body, add };

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
    juce::Rectangle<int> routeArea (int track) const;
    void showMenu (int track);
    void showTrackEq (int track);

    AudioEngine& engine;
    int selected = 0;
    int visibleHeight = 400;

    Part dragPart = Part::none;
    int dragTrack = -1;
    float dragStartValue = 0.0f;

    std::vector<float> levels; // nível exibido por trilha*2, com queda suave

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrips)
};

// Painel de efeitos da trilha selecionada: 10 slots e os knobs do slot escolhido.
//  - clique num slot vazio: escolher efeito; clique num ocupado: editar os knobs
//  - LED do slot: liga/desliga (bypass); clique direito: trocar, mover, remover
//  - knob à direita do slot: dry/wet (arrastar; duplo clique volta a 100%)
class EffectsPanel final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit EffectsPanel (AudioEngine& engine);
    ~EffectsPanel() override;

    void setTrack (int track);
    void selectSlot (int slot)
    {
        selectedSlot = slot;
        repaint();
    }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::Rectangle<int> slotBounds (int slot) const;
    juce::Rectangle<int> mixBounds (int slot) const; // knob de dry/wet na linha do slot
    static constexpr int mixParam = -2;
    static constexpr int gateStepParam = -3; // desenhando os passos do gate de volume
    juce::Rectangle<int> knobBounds (int param) const;
    bool gateSelected() const;
    juce::Rectangle<int> gateStepsBounds() const;
    void setGateStep (juce::Point<int> p);
    void showGatePatterns();
    int knobAt (juce::Point<int> p) const;
    const std::vector<EffectSlot>* slots() const;
    void showAddMenu (int slot);
    void showSlotMenu (int slot);
    juce::Rectangle<int> pluginButtonBounds() const;
    void openPluginWindow (int slot);

    AudioEngine& engine;
    int track = 0;
    int selectedSlot = -1;
    int dragParam = -1;
    float dragStart = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanel)
};

class MixerComponent final : public juce::Component
{
public:
    explicit MixerComponent (AudioEngine& engine);
    void resized() override;

    // Seleciona a primeira trilha que tem efeitos (usado pelo modo --snapshot).
    void selectFirstTrackWithEffects();
    void selectTrack (int track) { strips.selectTrack (track); }
    void selectSlot (int slot) { effects.selectSlot (slot); }

    static constexpr int effectsWidth = 300;

private:
    AudioEngine& engine;
    MixerStrips strips;
    juce::Viewport viewport;
    EffectsPanel effects;
};
} // namespace lynx
