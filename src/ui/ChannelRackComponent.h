#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace daw
{
// Channel Rack no estilo do FL: uma linha por canal com mute, nome e a grade
// de steps da pattern atual. Clique esquerdo liga steps (arrastar pinta),
// clique direito apaga. Clique no nome abre o menu do canal.
class ChannelRackComponent final : public juce::Component,
                                   private juce::ChangeListener,
                                   private juce::Timer
{
public:
    explicit ChannelRackComponent (AudioEngine& engine);
    ~ChannelRackComponent() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    // Tamanho que o conteúdo precisa (o pai coloca isto num Viewport).
    void updateSize();

    static constexpr int rowHeight = 28;
    static constexpr int muteWidth = 26;
    static constexpr int nameWidth = 150;
    static constexpr int stepWidth = 22;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    int numSteps() const;
    int gridLeft() const noexcept { return muteWidth + nameWidth + 8; }
    juce::Rectangle<int> stepBounds (int row, int step) const;
    bool cellHasNote (int channel, int step) const;
    void applyPaint (const juce::MouseEvent& e);
    void showChannelMenu (int channel);
    void addChannel();

    AudioEngine& engine;
    int playheadStep = -1;

    enum class PaintMode { none, add, remove } paintMode = PaintMode::none;
    int lastPaintedRow = -1, lastPaintedStep = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelRackComponent)
};
} // namespace daw
