#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Channel Rack no estilo do FL: uma linha por canal com mute, pan, volume, nome
// e a grade de steps da pattern atual.
//  - steps: clique esquerdo liga (arrastar pinta), clique direito apaga
//  - nome: clique seleciona e toca um preview, duplo clique abre o Piano Roll,
//    clique direito abre o menu do canal
//  - knobs: arrastar na vertical, duplo clique volta ao padrão
//  - arrastar um arquivo de áudio para uma linha transforma o canal em sampler
class ChannelRackComponent final : public juce::Component,
                                   public juce::FileDragAndDropTarget,
                                   private juce::ChangeListener,
                                   private juce::Timer
{
public:
    explicit ChannelRackComponent (AudioEngine& engine);
    ~ChannelRackComponent() override;

    std::function<void()> onOpenPianoRoll;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void updateSize();

    static constexpr int rowHeight = 30;
    static constexpr int muteWidth = 26;
    static constexpr int knobWidth = 28;
    static constexpr int routeWidth = 30;
    static constexpr int nameWidth = 150;
    static constexpr int stepWidth = 22;

private:
    enum class Area { none, mute, pan, volume, route, name, grid, addRow };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    Area areaAt (int x, int y, int& row) const;
    int numSteps() const;
    int nameLeft() const noexcept { return muteWidth + 2 * knobWidth + routeWidth + 2; }
    int gridLeft() const noexcept { return nameLeft() + nameWidth + 10; }
    juce::Rectangle<int> rowBounds (int row) const;
    juce::Rectangle<int> stepBounds (int row, int step) const;
    bool cellHasNote (int channel, int step) const;
    void applyPaint (const juce::MouseEvent& e);
    void showChannelMenu (int channel);
    void showAddChannelMenu();
    void chooseSample (int channel); // channel < 0 = novo canal
    void assignSample (int channel, const juce::File& file);
    void rename (int channel);
    void openPluginWindow (int channel);

    AudioEngine& engine;
    int playheadStep = -1;

    enum class PaintMode { none, add, remove } paintMode = PaintMode::none;
    int lastPaintedRow = -1, lastPaintedStep = -1;

    Area knobDrag = Area::none;
    int knobRow = -1;
    float knobStartValue = 0.0f;
    int routeStartValue = 0;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackComponent)
};
} // namespace lynx
