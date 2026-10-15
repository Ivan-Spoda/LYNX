#pragma once

#include <map>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/KeyMap.h"
#include "core/ValueMath.h"
#include "engine/AudioEngine.h"
#include "ui/BrowserComponent.h"
#include "ui/Icons.h"
#include "ui/ChannelRackComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/MixerComponent.h"
#include "ui/PlaylistComponent.h"
#include "ui/HintBar.h"
#include "ui/Splash.h"
#include "ui/Toolbar.h"
#include "ui/ValueControl.h"
#include "ui/Workspace.h"

namespace lynx
{
class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private juce::MenuBarModel,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    // `restoreUserSettings`: load/save the workspace layout, key map and UI options from the user
    // settings (off for --snapshot, which must look the same on every machine).
    explicit MainComponent (AudioEngine& engine, bool restoreUserSettings = true);
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;

    // Opens .lynx or .flp (also used by the command line).
    void openFile (const juce::File& file);
    void showSetupWizard(); // first run, or Options > Setup wizard

    // Asks "save changes?" if there are any; `then` runs unless the user cancels.
    void confirmDiscard (std::function<void()> then);

    // For --snapshot mode: 0 = Channel Rack, 1 = Piano Roll (on the channel with the most notes), 2 = Playlist, 3 = Mixer,
    // each maximized on the workspace; showDesktopForSnapshot shows the default layout (with `extraPanels`, the
    // Piano Roll and Mixer floating too).
    void selectTabForSnapshot (int tab);
    void showDesktopForSnapshot (bool extraPanels);
    void waitForBrowserForSnapshot() { browser.waitForListing (5000); }
    juce::StringArray getMenuNamesForSnapshot() { return getMenuBarNames(); }
    juce::PopupMenu getMenuForSnapshot (int index) { return getMenuForIndex (index, {}); }
    juce::PopupMenu getPanelMenuForSnapshot (workspace::Panel panel)
    {
        juce::PopupMenu menu;
        addPanelMenuItems (panel, menu);
        return menu;
    }
    juce::PopupMenu getRecordMenuForSnapshot()
    {
        juce::PopupMenu menu;
        addRecordOptions (menu);
        return menu;
    }
    void setHintForSnapshot (const ui::Hint& hint, bool live = false)
    {
        hintsFrozen = true; // the timer would replace it with the hint under the (off-screen) mouse
        hintBar.setHint (hint, live);
    }
    juce::Rectangle<int> getToolbarBoundsForSnapshot() const { return { 0, 0, getWidth(), toolbarHeight }; }
    void setMetersForSnapshot (double cpu, std::int64_t memory, int polyphony) { perfMeter.setValues (cpu, memory, polyphony); }
    static std::unique_ptr<juce::Component> makeHelpContent();
    static std::unique_ptr<juce::AlertWindow> makeAboutWindow();
    std::unique_ptr<juce::AlertWindow> makeConfirmDiscardWindow();
    PianoRollComponent& getPianoRollForSnapshot() { return pianoRoll; }
    ChannelRackComponent& getRackForSnapshot() { return rack; }
    BrowserComponent& getBrowserForSnapshot() { return browser; }
    MixerComponent& getMixerForSnapshot() { return mixer; }
    std::unique_ptr<juce::Component> makeQuickAddForSnapshot();
    PlaylistComponent& getPlaylistForSnapshot() { return playlist; }
    void selectMixerTrackForSnapshot (int track, int slot = -1)
    {
        mixer.selectTrack (track);
        if (slot >= 0)
            mixer.selectSlot (slot);
    }

private:
    using Panel = workspace::Panel;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showAudioSettings();
    void showImportReport();
    void importFlp (const juce::File& file);
    void openLynxProject (const juce::File& file);
    int scansSeen = 0; // background plugin scans already announced in the hint bar
    void showHelp();

    // transport
    void playStop();     // Space: play, or stop and go back to where playback started
    void playPause();    // Ctrl+Space: pause / resume where it is
    void stopAndRewind(); // the Stop button
    double playStartTicks = 0.0;

    // key map and actions
    bool runAction (const std::string& id);
    keys::Context focusContext() const;
    void loadKeyMap();
    void editKeyMap();
    keys::KeyMap keyMap;
    bool typingPiano = false; // off by default so single-letter editor tools work (the manual does not state a default)
    void setTypingPiano (bool on);
    juce::String shortcutText (const char* actionId) const; // "\tF5" for menus ("" when unbound)

    // panels
    void togglePanel (Panel panel);
    void fitChannelRack (bool width); // the Channel Rack pop-up sizes itself to its rows (and steps)
    int fittedChannelCount = -1;
    void showPanel (Panel panel);
    void refreshPanelButtons();
    void saveLayoutAs();
    juce::PopupMenu layoutsMenu();

    // patterns
    void selectPattern (int index);
    void nextEmptyPattern();
    void insertPatternHere();
    void splitPatternByChannelNow();
    void clonePatternNow();
    void movePatternNow (int delta);

    // workflow P2 windows and menus
    void showQuickAdd();
    void addPanelMenuItems (Panel panel, juce::PopupMenu& menu); // a window's own items in its title-bar menu
    void showHistory();
    void addRecordOptions (juce::PopupMenu& menu); // count-in, wait, overdub/replace, quantise, song clip
    void updateRecordQuantise(); // record quantise follows the global grid
    bool recordQuantiseOn = true;
    void showRenamePattern (int index);

    // user settings (UI options)
    juce::File uiSettingsFile() const;
    void loadUiSettings();
    void saveUiSettings();
    bool persistent = true;
    StartupOptions startup; // splash screen and start-up sound (read by Main.cpp before this window exists)

    // menu bar
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int, int) override {}
    void rememberRecent (const juce::File& file);
    juce::PopupMenu makePatternMenu();
    juce::MenuBarComponent menuBar { this };
    juce::RecentlyOpenedFilesList recent;
    // The main toolbar is two rows high, laid out the way FL Studio arranges its panels (docs/WORKFLOW_FL.md,
    // section 5.1): the menu with the hint bar under it, transport (with the time display and the song
    // position slider), master volume and pitch, recording options, global snap and pattern selector, window
    // and file shortcuts, then CPU / memory / polyphony and the output monitor on the right. Panels drop
    // out by priority when the window is narrow; the hint bar takes the room that is left.
    static constexpr int rowHeight = 28;
    static constexpr int toolbarHeight = 2 * rowHeight + 10;
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

    AudioEngine& engine;

    IconButton newButton { "new", Icon::newFile }, openButton { "open", Icon::open }, saveButton { "save", Icon::save },
        exportButton { "export", Icon::exportAudio }, undoButton { "undo", Icon::undo }, redoButton { "redo", Icon::redo };
    IconButton playButton { "play", Icon::play };
    IconButton stopButton { "stop", Icon::stop };
    IconButton recordButton { "record", Icon::record };
    IconButton recordMenuButton { "recordMenu", Icon::mic };
    IconButton patternModeButton { "patternMode", Icon::pattern }, songModeButton { "songMode", Icon::song };
    IconButton metronomeButton { "metronome", Icon::metronome };
    IconButton typingPianoButton { "typingPiano", Icon::keyboard };
    IconButton countInButton { "countIn", Icon::countIn };
    IconButton waitInputButton { "waitInput", Icon::waitInput };
    void refreshRecordOptionButtons();
    void showRecordMenu();
    void setControlHints(); // the hint bar text of every toolbar control (markup, see ui/HintBar.h)
    juce::String keyMarkup (const char* actionId) const; // "[Ctrl][S]" for the action's first key ("" when unbound)

    // tempo: a number dragged by its digits (whole BPM or decimals), with tap tempo in its menu
    class TempoDisplay final : public juce::Component,
                               public ui::HintSource
    {
    public:
        explicit TempoDisplay (AudioEngine& e) : engine (e) {}
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
        ui::Hint hintAt (juce::Point<int> position) override;
        void showMenu();
        void showTap();

    private:
        bool isOnDecimals (int x) const;
        void setBpm (double bpm);
        ui::ValueSpec spec() const;
        AudioEngine& engine;
        double startBpm = 0.0;
        bool dragOnDecimals = false, dragging = false;
        int decimalX = 0; // x of the decimal point, from the last paint
    };
    TempoDisplay tempoDisplay { engine };

    juce::ComboBox patternBox;
    IconButton newPatternButton { "newPattern", Icon::plus };
    IconButton patternMenuButton { "patternMenu", Icon::more };
    juce::Slider swingSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; // in the Channel Rack's title bar
    // one icon per workspace panel (F5..F9): click toggles, right-click shows it maximized
    std::array<IconButton, workspace::panelCount> panelButtons { IconButton { "playlistPanel", Icon::playlist },
                                                                 IconButton { "rackPanel", Icon::channelRack },
                                                                 IconButton { "pianoRollPanel", Icon::pianoRoll },
                                                                 IconButton { "mixerPanel", Icon::mixer },
                                                                 IconButton { "browserPanel", Icon::browser } };
    IconButton pluginPickerButton { "pluginPicker", Icon::pluginPicker };
    GlobalGridBox gridBox; // global grid of the editors (see ui/EditorTools.h)
    std::vector<int> dividers; // x positions of the toolbar panel separators
    juce::Rectangle<int> snapLabelArea, patternLabelArea, titleArea;
    void showLanguageChoice (const juce::String& id);

    // the time display with the song position slider under it, master volume and pitch, meters
    toolbar::TimeDisplay timeDisplay { engine };
    toolbar::SongPositionBar songPosition { engine };
    toolbar::MasterKnob masterVolume { engine, toolbar::MasterKnob::Kind::volume };
    toolbar::MasterKnob masterPitch { engine, toolbar::MasterKnob::Kind::pitch };
    toolbar::PerformanceMeter perfMeter;
    toolbar::OutputMonitor output { engine };
    int meterTicks = 0;

    // hint bar: what the control under the mouse does, its live value, and markers for
    // "can be automated" and "has a right-click menu"
    ui::HintBar hintBar;
    bool hintsFrozen = false; // --snapshot: keep the hint that was set
    juce::String idleHint() const;

    ChannelRackComponent rack;
    juce::Viewport rackViewport;
    PianoRollComponent pianoRoll;
    PlaylistComponent playlist;
    PatternPicker picker { engine, playlist };
    PlaylistArea playlistArea { picker, playlist };
    MixerComponent mixer;
    BrowserComponent browser;
    Workspace workspace; // after the panels' contents: it is destroyed first

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File currentFile;
    juce::String importedSuffix; // " (imported from FL Studio)" in the title until saved as .lynx
    juce::String projectName; // suggested name for export (also from an imported .flp)
    std::map<int, std::vector<int>> heldKeys; // PC keyboard keys playing notes (keyCode -> channels)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
} // namespace lynx
