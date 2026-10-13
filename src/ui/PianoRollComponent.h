#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Piano Roll do canal selecionado na pattern atual.
//  - clique esquerdo no vazio cria uma nota (com a última duração usada) e já permite arrastar
//  - arrastar o corpo move; arrastar a borda direita muda a duração (as notas selecionadas juntas)
//  - Ctrl+arrastar seleciona com um retângulo; Shift+clique põe/tira uma nota da seleção
//  - clique direito apaga (arrastar apaga várias)
//  - faixa de velocity embaixo: clicar/arrastar desenha a velocity (só nas selecionadas, se houver)
//  - Alt desliga o snap; roda do mouse rola, Shift+roda rola na horizontal, Ctrl+roda dá zoom
//  - teclado à esquerda toca a nota
//  - a faixa de baixo edita velocity, pan ou afinação fina (combo no canto dela)
//  - teclas (via handleKey): Delete, Ctrl+A/C/X/V/B/Q, Ctrl+M (mutar), Esc, setas (Shift = oitava/compasso)
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

    // Atalhos de edição; a janela principal repassa as teclas quando o Piano Roll está visível.
    bool handleKey (const juce::KeyPress& key);

    // Centraliza a vista nas notas do canal (chamado ao abrir).
    void scrollToNotes();

    // Para testes/snapshot: seleciona todas as notas do canal.
    void selectAll();

    static constexpr int keyboardWidth = 64;
    static constexpr int toolbarHeight = 28;
    static constexpr int timelineHeight = 22;
    static constexpr int headerHeight = toolbarHeight + timelineHeight;
    static constexpr int velocityHeight = 72;
    static constexpr int rowHeight = 14;
    static constexpr int scrollBarSize = 12;

private:
    enum class Drag { none, move, resize, erase, keyboard, select, velocity };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> velocityArea() const;
    double tickToX (double tick) const;
    double xToTick (int x) const;
    int keyToY (int key) const;
    int yToKey (int y) const;
    std::int64_t gridTicks() const;
    std::int64_t snap (double tick, bool enabled) const;
    std::int64_t viewLengthTicks() const;
    int noteAt (int x, int y, bool& onRightEdge) const;
    void updateScrollBars();
    void previewOn (int key, float velocity);
    void previewOff();

    bool isSelected (int index) const;
    std::vector<int> channelNotes() const;           // índices das notas do canal
    std::vector<int> selectionOrAll() const;         // seleção, ou todas do canal se vazia
    void setSelection (std::vector<int> indices);
    void sortNotes();                                // ordena mantendo a seleção (sem passo de desfazer)
    void editNotes (const std::function<void (Pattern&)>& edit);
    void applyVelocity (int fromX, int toX, int y);
    float laneValue (const Note& n) const;          // 0..1 da propriedade escolhida
    void setLaneValue (Note& n, float value) const; // inverso
    bool laneIsBipolar() const;
    void toggleMute();

    void deleteSelection();
    void copySelection();
    void paste();
    void duplicateSelection();
    void quantizeSelection();
    void nudge (std::int64_t ticks, int keys);

    AudioEngine& engine;
    juce::ScrollBar hBar { false }, vBar { true };
    juce::ComboBox snapBox;
    juce::TextButton midiButton { "MIDI" };
    juce::ComboBox laneBox; // o que a faixa de baixo edita: velocity, pan ou afinação fina
    juce::ComboBox chordBox; // acorde criado a cada clique (como o "stamp" do FL)
    juce::ComboBox scaleBox; // escala destacada nas linhas do teclado
    std::vector<int> chordIntervals() const;
    bool inScale (int key) const;
    std::unique_ptr<juce::FileChooser> chooser;
    void showMidiMenu();
    juce::Label hintLabel;

    double pixelsPerTick = 24.0 / ticksPerStep; // 24 px por step
    double scrollTicks = 0.0;
    int scrollY = (127 - 84) * rowHeight;       // começa mostrando por volta do C6..C3

    Drag drag = Drag::none;
    int dragNote = -1;              // nota agarrada (referência para snap e preview)
    std::vector<int> dragIndices;   // notas que acompanham o arraste
    std::vector<Note> dragOriginals;
    std::int64_t grabOffset = 0;    // tick da nota - tick do mouse no início do arraste
    std::int64_t lastLength = ticksPerStep * 2;
    int previewKey = -1;
    double playheadTick = -1.0;

    std::vector<int> selection;     // índices em Pattern::notes, ordenados
    std::vector<int> selectionBefore; // seleção no início de um Shift+retângulo
    juce::Rectangle<int> rubberBand;
    int lastVelocityX = 0;
    int knownPattern = -1, knownChannel = -1;
    std::size_t knownNoteCount = 0;

    static inline std::vector<Note> clipboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
} // namespace lynx
