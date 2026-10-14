#pragma once

#include <set>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/BrowserComponent.h"
#include "ui/Icons.h"
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
    void mouseDown (const juce::MouseEvent& e) override; // right-click on BPM: tempo automation

    // Opens .lynx or .flp (also used by the command line).
    void openFile (const juce::File& file);

    // Asks "save changes?" if there are any; `then` runs unless the user cancels.
    void confirmDiscard (std::function<void()> then);

    // For --snapshot mode: 0 = Channel Rack, 1 = Piano Roll (on the channel with the most notes), 2 = Playlist, 3 = Mixer.
    void selectTabForSnapshot (int tab);
    void waitForBrowserForSnapshot() { browser.waitForListing (5000); }
    juce::StringArray getMenuNamesForSnapshot() { return getMenuBarNames(); }
    juce::PopupMenu getMenuForSnapshot (int index) { return getMenuForIndex (index, {}); }
    static std::unique_ptr<juce::Component> makeHelpContent();
    static std::unique_ptr<juce::AlertWindow> makeAboutWindow();
    std::unique_ptr<juce::AlertWindow> makeConfirmDiscardWindow();
    PianoRollComponent& getPianoRollForSnapshot() { return pianoRoll; }
    PlaylistComponent& getPlaylistForSnapshot() { return playlist; }
    void selectMixerTrackForSnapshot (int track, int slot = -1)
    {
        mixer.selectTrack (track);
        if (slot >= 0)
            mixer.selectSlot (slot);
    }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePlay();
    void showAudioSettings();
    void showImportReport();
    void showHelp();

    // menu bar
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}
    void rememberRecent (const juce::File& file);
    juce::MenuBarComponent menuBar { this };
    juce::RecentlyOpenedFilesList recent;
    static constexpr int menuHeight = 26;
    static constexpr int toolbarHeight = 50;
    static constexpr int statusHeight = 24;
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

    juce::ImageComponent logo; // the LYNX icon in the corner of the bar
    IconButton newButton { "new", Icon::newFile }, openButton { "open", Icon::open }, saveButton { "save", Icon::save },
        exportButton { "export", Icon::exportAudio };
    IconButton playButton { "play", Icon::play };
    IconButton stopButton { "stop", Icon::stop };
    IconButton recordButton { "record", Icon::record };
    IconButton recordMenuButton { "recordMenu", Icon::chevronDown };
    IconButton patternModeButton { "patternMode", Icon::pattern }, songModeButton { "songMode", Icon::song };
    IconButton metronomeButton { "metronome", Icon::metronome };
    juce::Slider bpmSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ComboBox patternBox;
    IconButton newPatternButton { "newPattern", Icon::plus };
    IconButton patternMenuButton { "patternMenu", Icon::more };
    juce::Slider swingSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label swingLabel;
    IconButton settingsButton { "settings", Icon::settings };
    GlobalGridBox gridBox; // global grid of the editors (see ui/EditorTools.h)
    std::vector<int> dividers; // x positions of the toolbar group separators
    void showLanguageChoice (const juce::String& id);
    juce::Label positionLabel;

    // output oscilloscope in the bar (like FL's)
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
    PatternPicker picker { engine, playlist };
    PlaylistArea playlistArea { picker, playlist };
    MixerComponent mixer;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    BrowserComponent browser;
    bool browserVisible = true;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentFile;
    juce::String importedSuffix; // " (imported from FL Studio)" in the title until saved as .lynx
    juce::String projectName; // suggested name for export (also from an imported .flp)
    std::set<int> heldKeys; // PC keyboard keys currently playing notes (keyCode)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace lynx
