#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace daw
{
// Piano Roll do canal selecionado na pattern atual.
//  - clique esquerdo no vazio cria uma nota (com a última duração usada) e já permite arrastar
//  - arrastar o corpo move; arrastar a borda direita muda a duração
//  - clique direito apaga (arrastar apaga várias)
//  - Alt desliga o snap; roda do mouse rola, Shift+roda rola na horizontal, Ctrl+roda dá zoom
//  - teclado à esquerda toca a nota
// Notas de outros canais aparecem apagadas ao fundo, como "ghost notes" no FL.
class PianoRollComponent final : public juce::Component,
                                 private juce::ChangeListener,
                                 private juce::Timer,
                                 private juce::ScrollBar::Listener
{
public:
    explicit PianoRollComponent (AudioEngine& engine);
    ~PianoRollComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Centraliza a vista nas notas do canal (chamado ao abrir).
    void scrollToNotes();

    static constexpr int keyboardWidth = 64;
    static constexpr int timelineHeight = 22;
    static constexpr int rowHeight = 14;
    static constexpr int scrollBarSize = 12;

private:
    enum class Drag { none, move, resize, erase, keyboard };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::Rectangle<int> gridArea() const;
    double tickToX (double tick) const;
    double xToTick (int x) const;
    int keyToY (int key) const;
    int yToKey (int y) const;
    std::int64_t snap (double tick, bool enabled) const;
    std::int64_t viewLengthTicks() const;
    int noteAt (int x, int y, bool& onRightEdge) const;
    void updateScrollBars();
    void previewOn (int key, float velocity);
    void previewOff();

    AudioEngine& engine;
    juce::ScrollBar hBar { false }, vBar { true };

    double pixelsPerTick = 24.0 / ticksPerStep; // 24 px por step
    double scrollTicks = 0.0;
    int scrollY = (127 - 84) * rowHeight;       // começa mostrando por volta do C6..C3

    Drag drag = Drag::none;
    int dragNote = -1;
    std::int64_t grabOffset = 0;  // tick da nota - tick do mouse no início do arraste
    std::int64_t lastLength = ticksPerStep * 2;
    int previewKey = -1;
    double playheadTick = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
} // namespace daw
