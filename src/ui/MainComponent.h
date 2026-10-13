#pragma once

#include <set>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/ChannelRackComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/MixerComponent.h"
#include "ui/PlaylistComponent.h"

namespace lynx
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

    // Abre .lynx ou .flp (também usado pela linha de comando).
    void openFile (const juce::File& file);

    // Para o modo --snapshot: 0 = Channel Rack, 1 = Piano Roll (no canal com mais notas), 2 = Playlist, 3 = Mixer.
    void selectTabForSnapshot (int tab);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePlay();
    void showAudioSettings();
    void newProject();
    void chooseFileToOpen();
    void save (bool askForFile);
    void exportAudio();
    void refreshToolbar();
    void updateTitle();
    void openPianoRoll();
    void toggleMode();
    void refreshModeButton();
    void showPatternMenu();
    void renamePattern (int index);

    AudioEngine& engine;

    juce::TextButton newButton { "Novo" }, openButton { "Abrir" }, saveButton { "Salvar" }, exportButton { "Exportar" };
    juce::TextButton playButton { "Play" };
    juce::TextButton stopButton { "Stop" };
    juce::TextButton modeButton { "PAT" };
    juce::TextButton recordButton { "Rec" };
    juce::ToggleButton metronomeButton { "Metro" };
    juce::Slider bpmSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ComboBox patternBox;
    juce::TextButton newPatternButton { "+" };
    juce::TextButton patternMenuButton { "..." };
    juce::Slider swingSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label swingLabel;
    juce::TextButton settingsButton { "Audio..." };
    juce::Label positionLabel;
    juce::Label cpuLabel;
    juce::Label hintLabel;

    ChannelRackComponent rack;
    juce::Viewport rackViewport;
    PianoRollComponent pianoRoll;
    PlaylistComponent playlist;
    MixerComponent mixer;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentFile;
    juce::String projectName; // nome sugerido para exportar (também de .flp importado)
    std::set<int> heldKeys; // teclas do teclado do PC tocando notas agora (keyCode)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace lynx
