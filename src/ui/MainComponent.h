#pragma once

#include <set>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/BrowserComponent.h"
#include "ui/ChannelRackComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/MixerComponent.h"
#include "ui/PlaylistComponent.h"

namespace lynx
{
class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private juce::MenuBarModel,
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
    void mouseDown (const juce::MouseEvent& e) override; // clique direito no BPM: automação de tempo

    // Abre .lynx ou .flp (também usado pela linha de comando).
    void openFile (const juce::File& file);

    // Pergunta "salvar alterações?" se houver; `then` roda se o usuário não cancelar.
    void confirmDiscard (std::function<void()> then);

    // Para o modo --snapshot: 0 = Channel Rack, 1 = Piano Roll (no canal com mais notas), 2 = Playlist, 3 = Mixer.
    void selectTabForSnapshot (int tab);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePlay();
    void showAudioSettings();
    void showHelp();

    // barra de menus
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}
    void rememberRecent (const juce::File& file);
    juce::MenuBarComponent menuBar { this };
    juce::RecentlyOpenedFilesList recent;
    static constexpr int menuHeight = 22;
    void newProject();
    void chooseFileToOpen();
    void chooseFileToOpenNow();
    void save (bool askForFile);
    void exportAudio();
    void refreshToolbar();
    void updateTitle();
    void autosave();
    int autosaveTicks = 0;
    bool lastModified = false;
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
    juce::TextButton recordMenuButton { "v" };
    juce::ToggleButton metronomeButton { "Metro" };
    juce::Slider bpmSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ComboBox patternBox;
    juce::TextButton newPatternButton { "+" };
    juce::TextButton patternMenuButton { "..." };
    juce::Slider swingSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label swingLabel;
    juce::TextButton settingsButton { "Audio..." };
    juce::Label positionLabel;

    // osciloscópio da saída, na barra (como o do FL)
    class Scope final : public juce::Component
    {
    public:
        explicit Scope (AudioEngine& e) : engine (e) { setInterceptsMouseClicks (false, false); }
        void update();
        void paint (juce::Graphics& g) override;

    private:
        AudioEngine& engine;
        std::array<float, 1024> samples {};
    };
    Scope scope { engine };
    juce::Label cpuLabel;
    juce::Label hintLabel;

    ChannelRackComponent rack;
    juce::Viewport rackViewport;
    PianoRollComponent pianoRoll;
    PlaylistComponent playlist;
    PatternPicker picker { engine };
    PlaylistArea playlistArea { picker, playlist };
    MixerComponent mixer;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    BrowserComponent browser;
    bool browserVisible = true;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentFile;
    juce::String projectName; // nome sugerido para exportar (também de .flp importado)
    std::set<int> heldKeys; // teclas do teclado do PC tocando notas agora (keyCode)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace lynx
