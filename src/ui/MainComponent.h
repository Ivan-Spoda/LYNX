#pragma once

#include <set>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/ChannelRackComponent.h"
#include "ui/PianoRollComponent.h"

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
    bool keyStateChanged (bool isKeyDown) override;

    // Abre .dawproj ou .flp (também usado pela linha de comando).
    void openFile (const juce::File& file);

    // Para o modo --snapshot: 0 = Channel Rack, 1 = Piano Roll (no canal com mais notas).
    void selectTabForSnapshot (int tab);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePlay();
    void showAudioSettings();
    void newProject();
    void chooseFileToOpen();
    void save (bool askForFile);
    void refreshToolbar();
    void updateTitle();
    void openPianoRoll();

    AudioEngine& engine;

    juce::TextButton newButton { "Novo" }, openButton { "Abrir" }, saveButton { "Salvar" };
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::ToggleButton metronomeButton { "Metro" };
    juce::Slider bpmSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ComboBox patternBox;
    juce::TextButton newPatternButton { "+" };
    juce::TextButton settingsButton { "Audio..." };
    juce::Label positionLabel;
    juce::Label cpuLabel;
    juce::Label hintLabel;

    ChannelRackComponent rack;
    juce::Viewport rackViewport;
    PianoRollComponent pianoRoll;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentFile;
    std::set<int> heldKeys; // teclas do teclado do PC tocando notas agora (keyCode)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace daw
