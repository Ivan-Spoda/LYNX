#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Playlist (arranjo) no estilo do FL: trilhas na vertical, tempo na horizontal.
//  - clique esquerdo no vazio "pinta" a pattern atual (e já permite arrastar)
//  - arrastar um clip move (tempo e trilha); a borda direita muda a duração
//  - clicar num clip torna a pattern dele a atual; duplo clique abre no Channel Rack
//  - clique direito apaga (arrastar apaga vários)
//  - clicar na régua posiciona a reprodução e liga o modo Song
//  - arrastar um arquivo de áudio para a Playlist cria um clip de áudio
//  - Alt desliga o snap (1 tempo); roda rola, Shift+roda horizontal, Ctrl+roda zoom
//  - Ctrl+arrastar seleciona com retângulo; Shift+clique soma; clips selecionados andam juntos
//  - arrastar na régua marca a região de loop; clique direito na régua desliga o loop
//  - clique direito na faixa do nome de um clip abre o menu dele (tornar única etc.)
//  - teclas (via handleKey): Delete, Ctrl+A/C/X/V/B, Esc
class PlaylistComponent final : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                private juce::ChangeListener,
                                private juce::Timer,
                                private juce::ScrollBar::Listener
{
public:
    explicit PlaylistComponent (AudioEngine& engine);
    ~PlaylistComponent() override;

    std::function<void()> onOpenPattern;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Atalhos de edição; a janela principal repassa as teclas quando a Playlist está visível.
    bool handleKey (const juce::KeyPress& key);

    // arrastar arquivos de áudio cria clips de áudio (e um canal para cada um)
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    static constexpr int headerWidth = 96;
    static constexpr int timelineHeight = 24;
    static constexpr int trackHeight = 38;
    static constexpr int scrollBarSize = 12;

    static juce::Colour patternColour (const Song& song, int pattern);

private:
    enum class Drag { none, move, resize, erase, seek, loop, select };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::Rectangle<int> gridArea() const;
    double tickToX (double tick) const;
    double xToTick (int x) const;
    int trackToY (int track) const;
    int yToTrack (int y) const;
    int numTracks() const;
    std::int64_t snap (double tick, bool enabled) const;
    int clipAt (int x, int y, bool& onRightEdge) const;
    void updateScrollBars();
    bool isSelected (int index) const;
    void setSelection (std::vector<int> indices);
    void sortClips(); // ordena mantendo a seleção (sem passo de desfazer)
    void showClipMenu (int clip);
    void deleteSelection();
    void copySelection();
    void pasteClips();
    void duplicateSelection();
    void seekTo (int x);
    void paintAudioClip (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> bounds);
    const std::vector<float>* waveformFor (const std::string& path);

    static constexpr std::size_t waveformBucket = 256; // frames por ponto da forma de onda
    std::map<std::string, std::vector<float>> waveforms;
    std::map<std::string, double> waveformRates;

    AudioEngine& engine;
    juce::ScrollBar hBar { false }, vBar { true };

    double pixelsPerTick = 96.0 / (4 * ppq); // 96 px por compasso de 4 tempos
    double scrollTicks = 0.0;
    int scrollY = 0;

    Drag drag = Drag::none;
    int dragClip = -1;
    std::vector<int> dragIndices;
    std::vector<Clip> dragOriginals;
    std::vector<int> selection, selectionBefore;
    juce::Rectangle<int> rubberBand;
    std::int64_t loopAnchor = 0;
    std::size_t knownClipCount = 0;
    static inline std::vector<Clip> clipboard;
    std::int64_t grabOffset = 0;
    double playheadTick = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistComponent)
};
} // namespace lynx
