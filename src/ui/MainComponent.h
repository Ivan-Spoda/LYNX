#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/ChannelRackComponent.h"

namespace daw
{
class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    explicit MainComponent (AudioEngine& engine);
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePlay();
    void showAudioSettings();
    void openFlp();
    void refreshPatternList();

    AudioEngine& engine;

    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::ToggleButton metronomeButton { "Metro" };
    juce::Slider bpmSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ComboBox patternBox;
    juce::TextButton newPatternButton { "+" };
    juce::TextButton openButton { "Abrir .flp" };
    juce::TextButton settingsButton { "Audio..." };
    juce::Label positionLabel;
    juce::Label cpuLabel;

    ChannelRackComponent rack;
    juce::Viewport rackViewport;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace daw
