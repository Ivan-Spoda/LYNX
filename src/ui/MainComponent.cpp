#include "ui/MainComponent.h"
#include "ui/SetupWizard.h"
#include "core/FlPluginDb.h"

#include "core/EditorPolish.h"

#include "ui/HistoryPanel.h"
#include "ui/MissingFilesDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/PluginManager.h"
#include "ui/ShortcutManager.h"
#include "ui/WaveformView.h"
#include "ui/QuickAddPalette.h"
#include "ui/ToolWindow.h"
#include "ui/WindowOwner.h"

#include "engine/PluginSandbox.h"

#include "engine/Text.h"

#include "core/RackOps.h"
#include "core/Templates.h"
#include "engine/MidiFileIo.h"
#include "engine/ProjectFile.h"
#include "ui/AutomationUi.h"
#include "ui/ChannelSettings.h"
#include "ui/Dialogs.h"
#include "ui/EditorTools.h"
#include "ui/ExportDialog.h"
#include "ui/ParametricEqEditor.h"
#include "ui/PluginWindow.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"

#include "core/HintMarkup.h"
#include "core/SongStats.h"

namespace lynx
{
namespace
{
using workspace::Panel;

constexpr Panel panelOrder[] = { Panel::playlist, Panel::channelRack, Panel::pianoRoll, Panel::mixer, Panel::browser };

const char* panelAction (Panel p)
{
    switch (p)
    {
        case Panel::playlist: return "panel.playlist";
        case Panel::channelRack: return "panel.channelRack";
        case Panel::pianoRoll: return "panel.pianoRoll";
        case Panel::mixer: return "panel.mixer";
        case Panel::browser: return "panel.browser";
    }
    return "";
}

juce::String panelName (Panel p)
{
    switch (p)
    {
        case Panel::playlist: return "Playlist";
        case Panel::channelRack: return "Channel Rack";
        case Panel::pianoRoll: return "Piano Roll";
        case Panel::mixer: return "Mixer";
        case Panel::browser: return "Browser";
    }
    return {};
}
} // namespace

MainComponent::MainComponent (AudioEngine& e, bool restoreUserSettings)
    : persistent (restoreUserSettings),
      engine (e),
      rack (e),
      pianoRoll (e),
      playlist (e),
      mixer (e),
      browser (e)
{
    heldKeys.setSender ([this] (int channel, int key, float velocity, bool on) { engine.sendLiveNote (channel, key, velocity, on, true); });
    browser.onOpenProject = [this] (const juce::File& file) { confirmDiscard ([this, file] { openFile (file); }); };
    browser.onLoadIntoChannel = [this] (const juce::File& file) { return rack.loadSampleIntoSelected (file); };
    if (persistent)
    {
        loadUiSettings();
        loadKeyMap();
    }

    newButton.setTooltip (tr ("New project") + " (Ctrl+N)");
    newButton.onClick = [this] { newProject(); };
    openButton.setTooltip (tr ("Open project") + " (Ctrl+O)");
    openButton.onClick = [this] { chooseFileToOpen(); };
    saveButton.setTooltip (tr ("Save project") + " (Ctrl+S)");
    saveButton.onClick = [this] { save (false); };
    exportButton.setTooltip (tr ("Export audio: WAV, FLAC or OGG") + " (Ctrl+R)");
    exportButton.onClick = [this] { exportAudio(); };
    auto withKey = [this] (const juce::String& text, const char* id) {
        const auto key = shortcutText (id).trim();
        return key.isEmpty() ? text : text + " (" + key + ")";
    };
    undoButton.setTooltip (withKey (tr ("Undo"), "edit.undo"));
    undoButton.onClick = [this] { engine.undo(); };
    undoButton.onRightClick = [this] { showHistory(); };
    redoButton.setTooltip (withKey (tr ("Redo"), "edit.redo"));
    redoButton.onClick = [this] { engine.redo(); };
    redoButton.onRightClick = [this] { showHistory(); };
    pluginPickerButton.setTooltip (withKey (tr ("Plugin picker: add an instrument or plugin as a new channel"), "panel.quickAdd"));
    pluginPickerButton.onClick = [this] { showQuickAdd(); };

    // transport: Play toggles play / stop-and-back; Stop returns to the start; double-click Stop = all notes off
    playButton.setTooltip (tr ("Play; pressed again, stops and goes back to where it started") + " (Space; Ctrl+Space pauses)");
    playButton.onClick = [this] { playStop(); };
    stopButton.setTooltip (tr ("Stop and return to the start (double-click: all notes off)"));
    stopButton.onClick = [this] { stopAndRewind(); };
    stopButton.onDoubleClick = [this] { engine.panic(); };
    stopButton.onRightClick = [this] { showStopMenu(); };
    output.onModeChanged = [this] { saveUiSettings(); };
    recordButton.setOnColour (theme::colour::record);
    recordButton.setIconColour (theme::colour::record.withMultipliedSaturation (0.8f));
    recordButton.onClick = [this] { engine.setRecording (! engine.isRecording()); };
    recordMenuButton.setTooltip (tr ("Recording options: what to record"));
    recordMenuButton.onClick = [this] { showRecordMenu(); };
    recordButton.onRightClick = [this] { showRecordMenu(); };

    // Pattern / Song: a two-segment switch (L toggles); right-click shows the matching panel
    // as in FL, a two-line switch in front of the transport buttons: pattern on top, song below
    patternModeButton.setText (tr ("Pattern"));
    songModeButton.setText (tr ("Song"));
    for (auto* b : { &patternModeButton, &songModeButton })
    {
        b->setOnColour (theme::colour::bg6);
        b->setIconColourWhenOn (theme::colour::textPrimary);
    }
    patternModeButton.setTooltip (tr ("Pattern mode: plays the current pattern in a loop") + " (L)\n" + tr ("Right-click: show the Channel Rack"));
    songModeButton.setTooltip (tr ("Song mode: plays the Playlist arrangement") + " (L)\n" + tr ("Right-click: show the Playlist"));
    patternModeButton.onClick = [this] {
        if (engine.getSong().mode != PlayMode::pattern)
            toggleMode();
    };
    songModeButton.onClick = [this] {
        if (engine.getSong().mode != PlayMode::song)
            toggleMode();
    };
    patternModeButton.onRightClick = [this] { showPanel (Panel::channelRack); };
    songModeButton.onRightClick = [this] { showPanel (Panel::playlist); };

    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setOnColour (theme::colour::bg6);
    metronomeButton.setIconColourWhenOn (theme::colour::accent);
    metronomeButton.setTooltip (tr ("Metronome"));
    metronomeButton.onClick = [this] { engine.getTransport().metronome.store (metronomeButton.getToggleState()); };

    typingPianoButton.setOnColour (theme::colour::bg6);
    typingPianoButton.setIconColourWhenOn (theme::colour::accent);
    typingPianoButton.onClick = [this] { setTypingPiano (! typingPiano); };

    // count-in and wait-for-input switches next to the metronome, as in FL's recording panel
    for (auto* b : { &countInButton, &waitInputButton })
    {
        b->setOnColour (theme::colour::bg6);
        b->setIconColourWhenOn (theme::colour::accent);
    }
    countInButton.onClick = [this] { runAction ("record.countIn"); };
    waitInputButton.onClick = [this] { runAction ("record.waitForInput"); };

    for (int i = 0; i < workspace::panelCount; ++i)
    {
        const auto panel = (Panel) i;
        auto& b = panelButtons[(std::size_t) i];
        b.setFlat (true);
        b.setOnColour (theme::colour::bg5);
        b.setIconColourWhenOn (theme::colour::textPrimary);
        b.setTooltip (panelName (panel) + " (" + keys::toString (keyMap.chordsFor (panelAction (panel)).empty() ? keys::Chord {}
                                                                                                          : keyMap.chordsFor (panelAction (panel)).front()) + ")\n"
                      + tr ("Click: show, focus or hide.  Right-click: show maximized"));
        b.onClick = [this, panel] { togglePanel (panel); };
        b.onRightClick = [this, panel] {
            showPanel (panel);
            if (! workspace.isMaximised (panel) && ! workspace.isDetached (panel))
                workspace.toggleMaximised (panel);
            refreshPanelButtons();
        };
    }

    patternBox.setTooltip (tr ("Current pattern (Numpad +/-: next/previous, F4: next empty, F2: rename)"));
    patternBox.setScrollWheelEnabled (true);
    gridBox.setScrollWheelEnabled (true);
    patternBox.onChange = [this]
    {
        const auto index = patternBox.getSelectedItemIndex();

        if (index >= 0 && index != engine.getSong().currentPattern)
            selectPattern (index);
    };

    newPatternButton.setTooltip (tr ("New pattern"));
    newPatternButton.onClick = [this]
    {
        engine.editSong ([] (Song& s) {
            Pattern p;
            p.name = newPatternName (s);
            s.patterns.push_back (std::move (p));
            s.currentPattern = (int) s.patterns.size() - 1;
        });
    };

    swingSlider.setRange (0.0, 1.0, 0.01);
    swingSlider.setDoubleClickReturnValue (true, 0.0);
    swingSlider.setValue (engine.getSong().swing, juce::dontSendNotification);
    swingSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    swingSlider.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getDoubleValue() / 100.0; };
    swingSlider.onValueChange = [this] { engine.editSong ([v = (float) swingSlider.getValue()] (Song& s) { s.swing = v; }); };
    swingSlider.setTooltip (tr ("Swing of the Channel Rack (all channels)"));
    swingSlider.setMouseClickGrabsKeyboardFocus (false);
    swingSlider.setWantsKeyboardFocus (false);

    patternMenuButton.setTooltip (tr ("Pattern: rename, color, clone, insert, split, length, delete"));
    patternMenuButton.onClick = [this] { showPatternMenu(); };


    // the workspace panels
    rack.onOpenPianoRoll = [this] { openPianoRoll(); };
    rack.onOpenMixerInsert = [this] (int insert) { // double-click on a channel's route box
        showPanel (Panel::mixer);
        mixer.selectTrack (insert);
    };
    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (true, true);
    workspace.setContent (Panel::playlist, playlistArea, "Playlist", Icon::playlist);
    workspace.setContent (Panel::channelRack, rackViewport, "Channel Rack", Icon::channelRack);
    workspace.setContent (Panel::pianoRoll, pianoRoll, "Piano Roll", Icon::pianoRoll);
    workspace.setContent (Panel::mixer, mixer, "Mixer", Icon::mixer);
    workspace.setContent (Panel::browser, browser, "Browser", Icon::browser);
    workspace.setTitleTooltip (Panel::channelRack, tr ("Shift+wheel here changes the pattern"));
    workspace.setTitleAccessory (Panel::channelRack, &swingSlider, 26); // FL keeps the global swing in the rack's title bar
    workspace.onPanelMenu = [this] (Panel panel, juce::PopupMenu& menu) { addPanelMenuItems (panel, menu); };
    workspace.onFocusChanged = [this] (Panel) { refreshPanelButtons(); };
    workspace.onKeyInDetachedWindow = [this] (const juce::KeyPress& key) { return keyPressed (key); };
    workspace.onTitleWheel = [this] (Panel panel, const juce::MouseEvent& ev, const juce::MouseWheelDetails& wheel) {
        if (panel == Panel::channelRack && ev.mods.isShiftDown())
            selectPattern (stepPattern (engine.getSong(), (wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX) > 0.0f ? -1 : 1));
    };
    workspace.onTitleRightClick = [this] (Panel panel, const juce::MouseEvent&) {
        if (panel == Panel::channelRack)
            showPatternMenu();
    };
    workspace.onDefaultLayout = [this] { fitChannelRack (true); };
    workspace.setPersistent (persistent);
    playlist.onOpenPattern = [this] { showPanel (Panel::channelRack); };
    playlist.onOpenPianoRoll = [this] {
        // edit a channel that plays in this pattern
        const auto& song = engine.getSong();
        const auto& notes = song.patterns[(std::size_t) song.currentPattern].notes;
        const auto channel = engine.getSelectedChannel();
        if (! notes.empty() && std::none_of (notes.begin(), notes.end(), [channel] (const Note& n) { return n.channel == channel; }))
            engine.setSelectedChannel (notes.front().channel);
        openPianoRoll();
    };
    picker.onOpenPattern = [this] { showPanel (Panel::channelRack); };
    playlist.onRenamePattern = [this] (int p) { showRenamePattern (p); };
    playlist.onOpenInSampler = [this] (int channel) {
        // the audio clip's file in a new Sampler channel (to play it from notes), its settings open
        const auto& song = engine.getSong();
        if (channel < 0 || channel >= (int) song.channels.size())
            return;
        ChannelDef c;
        c.name = song.channels[(std::size_t) channel].name;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = song.channels[(std::size_t) channel].samplePath;
        c.colour = song.channels[(std::size_t) channel].colour;
        const auto added = rack.addChannel (c, false);
        showPanel (Panel::channelRack);
        rack.toggleEditor (added, false);
    };
    ui::setHint (pianoRoll, tr ("Piano Roll: [LMB] add a note, drag to move  [RMB] delete  [Ctrl][LMB] drag to select  [Ctrl][Wheel] zoom"));
    ui::setHint (playlist, tr ("Playlist: [LMB] paint the current pattern  [RMB] delete  [Ctrl][LMB] drag to select  [Ctrl][Wheel] zoom"));

    // recent files (stored in %APPDATA%\LYNX)
    recent.setMaxNumberOfItems (12);
    recent.restoreFromString (PluginHost::settingsFolder().getChildFile ("recentes.txt").loadFileAsString());
    addAndMakeVisible (menuBar);

    for (auto* c : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton, &playButton, &stopButton,
                                                             &recordButton, &recordMenuButton, &patternModeButton, &songModeButton, &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton,
                                                             &tempoDisplay, &patternBox, &newPatternButton, &patternMenuButton, &pluginPickerButton, &timeDisplay,
                                                             &songPosition, &masterVolume, &masterPitch, &perfMeter, &output, &hintBar, &workspace })
    {
        addAndMakeVisible (c);
        c->setMouseClickGrabsKeyboardFocus (false); // the PC keyboard stays with the main window
    }

    for (auto& b : panelButtons)
    {
        addAndMakeVisible (b);
        b.setMouseClickGrabsKeyboardFocus (false);
        b.setWantsKeyboardFocus (false);
    }

    for (auto* b : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton, &playButton, &stopButton,
                                                             &recordButton, &recordMenuButton, &patternModeButton, &songModeButton, &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton,
                                                             &newPatternButton, &patternMenuButton, &pluginPickerButton })
        b->setWantsKeyboardFocus (false);

    for (auto* b : { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton, &recordMenuButton, &newPatternButton, &patternMenuButton,
                     &pluginPickerButton, &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton })
        b->setFlat (true);

    // a light header: every toolbar button is flat; "on" is a faint wash, in the accent for the states that
    // matter while working (playing, recording, metronome, recording options)
    {
        using namespace theme;
        const auto onWash = colour::accent.withAlpha (0.16f), onIcon = colour::accentHover;
        for (auto* b : { &playButton, &stopButton, &recordButton, &patternModeButton, &songModeButton })
            b->setFlat (true);
        playButton.setOnColour (onWash);
        playButton.setIconColourWhenOn (onIcon);
        recordButton.setOnColour (colour::record.withAlpha (0.2f));
        recordButton.setIconColourWhenOn (colour::record);
        for (auto* b : { &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton, &patternModeButton, &songModeButton })
        {
            b->setOnColour (onWash);
            b->setIconColourWhenOn (onIcon);
        }
        for (auto& b : panelButtons)
        {
            b.setOnColour (colour::textPrimary.withAlpha (0.08f));
            b.setIconColourWhenOn (colour::textPrimary);
        }
    }
    quickTips = std::make_unique<ui::QuickTipWindow>();

    ui::installSliderContract();
    addAndMakeVisible (gridBox); // global grid (editors)
    engine.addChangeListener (this);
    setTypingPiano (typingPiano);
    refreshRecordOptionButtons();
    refreshToolbar();
    refreshPanelButtons();
    setToolbarLayout (toolbarLayout, false); // icon sizes and row height (loaded with the UI options)
    if (persistent && toolbarLayout.uiScale != 100)
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) toolbarLayout.uiScale / 100.0f);

    setControlHints();
    fittedChannelCount = (int) engine.getSong().channels.size();
    hintBar.setHint ({ idleHint(), {}, false, false });
    setWantsKeyboardFocus (true);
    setSize (1180, 642);
    startTimerHz (30);

    // new or updated plugins since the last run: an incremental scan in the background (unchanged files are
    // skipped), a few seconds after start so it doesn't compete with loading; the first run asks in the wizard
    if (persistent && setupWizardDone() && backgroundScanAtStart())
        juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<MainComponent> (this)] {
            if (safe != nullptr)
                safe->engine.getPluginHost().startBackgroundScan();
        });
}

MainComponent::~MainComponent()
{
    saveUiSettings();
    workspace.saveState();
    ui::uninstallSliderContract();
    PluginWindows::closeAll(); // plugin UIs before the engine
    ParametricEqWindows::closeAll();
    closeAutomationEditors();
    closeChannelSettings();
    closeAllToolWindows(); // note tools, history, quick-add (they edit the song)
    engine.removeChangeListener (this);
    stopTimer();
}

void MainComponent::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg1);

    // the toolbar: one band, its panels separated by hairlines
    const auto bar = juce::Rectangle<int> (0, 0, getWidth(), toolbarHeight);
    g.setColour (colour::bg3);
    g.fillRect (bar);
    g.setColour (colour::border);
    g.fillRect (bar.withHeight (1).withY (bar.getBottom() - 1));

    g.setColour (colour::border.brighter (0.08f));
    for (auto x : dividers)
        g.fillRect (x, bar.getY() + 12, 1, bar.getHeight() - 24);

    // play, stop and record share one quiet well
    if (! transportArea.isEmpty())
    {
        g.setColour (colour::bg2);
        g.fillRoundedRectangle (transportArea.toFloat(), radius::lg);
        g.setColour (colour::border);
        g.drawRoundedRectangle (transportArea.toFloat().reduced (0.5f), radius::lg, 1.0f);
    }

    // the hint bar sits in an inset well under the menu
    if (! hintBar.getBounds().isEmpty())
    {
        g.setColour (colour::bg1);
        g.fillRoundedRectangle (hintBar.getBounds().toFloat(), radius::md);
    }

    // the project name, beside the menus (FL's title/hint panel: the title above the hint bar)
    if (! titleArea.isEmpty())
    {
        const auto name = projectName.isNotEmpty() ? projectName : tr ("Untitled");
        g.setFont (font (type::small, true));
        g.setColour (colour::textSecondary);
        g.drawFittedText ((engine.isModified() ? juce::String ("* ") : juce::String()) + name, titleArea, juce::Justification::centredRight, 1, 0.8f);
    }

    // captions that name the snap and pattern selectors
    g.setFont (font (type::caption - 1.0f, true));
    g.setColour (colour::textDisabled);
    if (! snapLabelArea.isEmpty())
        g.drawText (tr ("SNAP"), snapLabelArea, juce::Justification::centredLeft, true);
    if (! patternLabelArea.isEmpty())
        g.drawText (tr ("PATTERN"), patternLabelArea, juce::Justification::centredLeft, true);
}

void MainComponent::resized()
{
    using toolbarlayout::Group;
    dividers.clear();
    const auto bar = juce::Rectangle<int> (0, 0, getWidth(), toolbarHeight);
    auto toolbar = bar.reduced (8, 3);
    const auto row1 = toolbar.getY(), row2 = toolbar.getY() + rowHeight + 2, h = rowHeight;
    const auto big = h + 6;                                   // transport buttons: one slim row, centred on both
    const auto bigWidth = h + 4;
    constexpr int gap = 17;                                   // a divider and the space around it
    const auto grow = h - toolbarlayout::rowHeight (toolbarlayout::IconSize::medium); // wider panels for bigger rows

    // panel widths
    auto& lf = getLookAndFeel();
    int menuWidth = 0;
    for (const auto& name : getMenuBarNames())
        menuWidth += lf.getMenuBarItemWidth (menuBar, 0, name);

    const auto modeWidth = juce::jmax (patternModeButton.getIdealWidth (h - 2), songModeButton.getIdealWidth (h - 2));
    const int tempoWidth = 90 + grow, timeWidth = 110 + grow, captionWidth = 50, gridWidth = 148;

    std::array<int, toolbarlayout::groupCount> widths {};
    widths[(std::size_t) Group::transport] = modeWidth + 8 + 3 * (bigWidth + 4) + 4 + tempoWidth + 6 + timeWidth;
    widths[(std::size_t) Group::master] = 92;
    widths[(std::size_t) Group::record] = 3 * h + 4;
    widths[(std::size_t) Group::snap] = captionWidth + gridWidth;
    widths[(std::size_t) Group::windows] = 3 * h + 4;
    widths[(std::size_t) Group::file] = 3 * h + 4;
    widths[(std::size_t) Group::meters] = 118;
    widths[(std::size_t) Group::output] = 116 + grow * 2;

    const auto room = toolbar.getWidth();
    const auto minHint = juce::jmax (menuWidth + 8, 320), maxHint = 620; // room for most hints on two lines
    const auto visible = toolbarlayout::fit (toolbarLayout, widths, gap, room, minHint);
    int used = 0;
    for (int i = 0; i < toolbarlayout::groupCount; ++i)
        used += visible[(std::size_t) i] ? widths[(std::size_t) i] + gap : 0;

    const auto hintWidth = juce::jlimit (juce::jmin (minHint, juce::jmax (160, room - used)), maxHint, room - used);
    // the spare room goes before the first of the meters / output in the toolbar order (they sit on the right)
    auto spare = juce::jmax (0, room - used - hintWidth);

    auto take = [&toolbar] (int width) { return toolbar.removeFromLeft (juce::jmin (width, toolbar.getWidth())); };
    auto divide = [&] {
        take (8);
        dividers.push_back (toolbar.getX());
        take (9);
    };
    auto hide = [] (std::initializer_list<juce::Component*> components) {
        for (auto* c : components)
        {
            c->setVisible (false);
            c->setBounds ({});
        }
    };
    auto put = [] (juce::Component& c, juce::Rectangle<int> r) {
        c.setVisible (true);
        c.setBounds (r);
    };
    // a block of icon buttons, three per row on both rows
    auto grid = [&] (juce::Rectangle<int> panel, std::initializer_list<juce::Component*> buttons) {
        int i = 0;
        for (auto* b : buttons)
        {
            const auto x = panel.getX() + (i % 3) * (h + 2);
            put (*b, juce::Rectangle<int> (x, i < 3 ? row1 : row2, h, h));
            ++i;
        }
    };

    // 1. the menu (and the project name beside it) over the hint bar; always first
    {
        auto column = take (hintWidth);
        const auto menu = column.withY (row1).withHeight (h).removeFromLeft (juce::jmin (menuWidth + 8, column.getWidth()));
        put (menuBar, menu);
        titleArea = column.withY (row1).withHeight (h).withTrimmedLeft (menu.getWidth() + 12).withTrimmedRight (4);
        if (titleArea.getWidth() < 60)
            titleArea = {};
        put (hintBar, column.withY (row2).withHeight (h).reduced (0, 1));
    }

    snapLabelArea = patternLabelArea = transportArea = {};

    for (const auto& entry : toolbarLayout.entries)
    {
        const auto group = entry.group;

        if (! visible[(std::size_t) group])
        {
            switch (group)
            {
                case Group::transport:
                    hide ({ &patternModeButton, &songModeButton, &playButton, &stopButton, &recordButton, &tempoDisplay, &timeDisplay, &songPosition });
                    break;
                case Group::master: hide ({ &masterVolume, &masterPitch }); break;
                case Group::record: hide ({ &metronomeButton, &typingPianoButton, &recordMenuButton, &countInButton, &waitInputButton }); break;
                case Group::snap: hide ({ &gridBox, &patternBox, &newPatternButton, &patternMenuButton }); break;
                case Group::windows:
                    for (auto& b : panelButtons)
                        hide ({ &b });
                    hide ({ &pluginPickerButton });
                    break;
                case Group::file: hide ({ &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton }); break;
                case Group::meters: hide ({ &perfMeter }); break;
                case Group::output: hide ({ &output }); break;
                case Group::count_: break;
            }
            continue;
        }

        if (toolbarlayout::rightAligned (group) && spare > 0)
        {
            take (spare);
            spare = 0;
        }

        divide();
        auto panel = take (widths[(std::size_t) group]);

        switch (group)
        {
            case Group::transport: // Pattern/Song switch, play, stop, record, tempo, time display with the song position under it
            {
                auto mode = panel.removeFromLeft (modeWidth);
                put (patternModeButton, mode.withY (row1).withHeight (h).reduced (0, 1));
                put (songModeButton, mode.withY (row2).withHeight (h).reduced (0, 1));
                panel.removeFromLeft (8);
                const auto y = panel.getCentreY() - big / 2;

                transportArea = juce::Rectangle<int> (panel.getX(), y, 3 * bigWidth + 2 * 4, big);
                for (auto* b : { &playButton, &stopButton, &recordButton })
                {
                    put (*b, juce::Rectangle<int> (panel.getX(), y, bigWidth, big).reduced (0, 3));
                    panel.removeFromLeft (bigWidth + 4);
                }

                panel.removeFromLeft (4);
                const auto displays = juce::Rectangle<int> (panel.getX(), row1 + 3, panel.getWidth(), 2 * h - 6);
                const auto displayHeight = displays.getHeight() - 10;
                put (tempoDisplay, displays.withWidth (tempoWidth).withHeight (displayHeight));
                put (timeDisplay, displays.withTrimmedLeft (tempoWidth + 6).withWidth (timeWidth).withHeight (displayHeight));
                put (songPosition, displays.withTop (displays.getY() + displayHeight + 1).withWidth (tempoWidth + 6 + timeWidth));
                break;
            }
            case Group::master: // master volume over master pitch
                put (masterVolume, panel.withY (row1).withHeight (h).reduced (0, 1));
                put (masterPitch, panel.withY (row2).withHeight (h).reduced (0, 1));
                break;
            case Group::record: // metronome, count-in and wait for input over "what to record" and typing piano
                grid (panel, { &metronomeButton, &countInButton, &waitInputButton, &recordMenuButton, &typingPianoButton });
                break;
            case Group::snap: // global snap over the pattern selector, each with its name
            {
                auto first = juce::Rectangle<int> (panel.getX(), row1, panel.getWidth(), h);
                snapLabelArea = first.removeFromLeft (captionWidth);
                put (gridBox, first.reduced (0, 1));
                auto second = juce::Rectangle<int> (panel.getX(), row2, panel.getWidth(), h);
                patternLabelArea = second.removeFromLeft (captionWidth);
                put (patternMenuButton, second.removeFromRight (h));
                put (newPatternButton, second.removeFromRight (h));
                second.removeFromRight (2);
                put (patternBox, second.reduced (0, 1));
                break;
            }
            case Group::windows: // the windows and the plugin picker
                grid (panel, { &panelButtons[(std::size_t) Panel::playlist], &panelButtons[(std::size_t) Panel::channelRack],
                               &panelButtons[(std::size_t) Panel::pianoRoll], &panelButtons[(std::size_t) Panel::mixer],
                               &panelButtons[(std::size_t) Panel::browser], &pluginPickerButton });
                break;
            case Group::file: // new, open, save over export, undo, redo
                grid (panel, { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton });
                break;
            case Group::meters: put (perfMeter, panel.withY (row1).withHeight (2 * h + 2)); break;
            case Group::output: put (output, panel.withY (row1).withHeight (2 * h + 2)); break;
            case Group::count_: break;
        }
    }

    workspace.setBounds (getLocalBounds().withTrimmedTop (toolbarHeight));
}

// --- toolbar layout ------------------------------------------------------------------------------

void MainComponent::setToolbarLayout (const toolbarlayout::Layout& layout, bool save)
{
    const auto scaleChanged = layout.uiScale != toolbarLayout.uiScale;
    toolbarLayout = layout;
    const auto size = toolbarLayout.iconSize;
    rowHeight = toolbarlayout::rowHeight (size);
    toolbarHeight = 2 * rowHeight + 8;

    const auto fraction = toolbarlayout::iconFraction (size), maxPx = toolbarlayout::iconMaxPx (size);
    for (auto* b : { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton, &recordMenuButton, &newPatternButton,
                     &patternMenuButton, &pluginPickerButton, &metronomeButton, &typingPianoButton, &countInButton, &waitInputButton })
        b->setIconSizing (fraction, maxPx);
    for (auto& b : panelButtons)
        b.setIconSizing (fraction, maxPx);
    for (auto* b : { &playButton, &stopButton, &recordButton }) // solid transport glyphs, a little smaller than the line icons
        b->setIconSizing (0.5f, maxPx - 2.0f);

    ToolStrip::iconSize = size;
    pianoRoll.resized();
    playlist.resized();

    if (save && scaleChanged)
        juce::Desktop::getInstance().setGlobalScaleFactor ((float) toolbarLayout.uiScale / 100.0f);

    resized();
    repaint();

    if (save)
        saveUiSettings();
}

juce::PopupMenu MainComponent::makeToolbarMenu()
{
    using toolbarlayout::Group;
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Toolbar"));

    for (const auto& entry : toolbarLayout.entries)
    {
        const auto group = entry.group;
        menu.addItem (tr (toolbarlayout::groupName (group)), true, entry.shown, [this, group] {
            auto layout = toolbarLayout;
            layout.setShown (group, ! layout.isShown (group));
            setToolbarLayout (layout);
        });
    }

    menu.addSeparator();
    juce::PopupMenu sizes;
    const char* sizeNames[] = { TR_NOOP ("Small icons"), TR_NOOP ("Medium icons"), TR_NOOP ("Large icons") };
    for (int i = 0; i < 3; ++i)
        sizes.addItem (tr (sizeNames[i]), true, (int) toolbarLayout.iconSize == i, [this, i] {
            auto layout = toolbarLayout;
            layout.iconSize = (toolbarlayout::IconSize) i;
            setToolbarLayout (layout);
        });
    menu.addSubMenu (tr ("Icon size"), sizes);

    juce::PopupMenu scales;
    for (auto percent : toolbarlayout::uiScales)
        scales.addItem (juce::String (percent) + "%", true, toolbarLayout.uiScale == percent, [this, percent] {
            auto layout = toolbarLayout;
            layout.uiScale = percent;
            setToolbarLayout (layout);
        });
    menu.addSubMenu (tr ("UI scale"), scales);

    menu.addSeparator();
    menu.addItem (tr ("Customize toolbar..."), [this] { showCustomizeToolbar(); });
    menu.addItem (tr ("Reset toolbar to default"), ! toolbarLayout.isDefault(), false, [this] {
        setToolbarLayout (toolbarlayout::Layout::makeDefault());
    });
    return menu;
}

void MainComponent::showToolbarMenu()
{
    makeToolbarMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void MainComponent::showCustomizeToolbar()
{
    auto panel = std::make_unique<toolbar::CustomizePanel> (toolbarLayout);
    panel->onChange = [safe = juce::Component::SafePointer<MainComponent> (this)] (const toolbarlayout::Layout& layout) {
        if (safe != nullptr)
            safe->setToolbarLayout (layout);
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel.release());
    options.dialogTitle = tr ("Customize toolbar");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    // a right-click on the toolbar's background (between the panels): show / hide groups, icon size, customize
    if (e.mods.isPopupMenu() && e.y < toolbarHeight)
        showToolbarMenu();
}

juce::Rectangle<int> MainComponent::getToolStripAreaForSnapshot (bool playlistStrip)
{
    auto& editor = playlistStrip ? (juce::Component&) playlist : (juce::Component&) pianoRoll;
    if (! editor.isVisible() || editor.getParentComponent() == nullptr || ! isParentOf (&editor))
        return {};
    const auto area = getLocalArea (&editor, editor.getLocalBounds());
    return area.withHeight (40).withWidth (juce::jmin (area.getWidth(), 900)).getIntersection (getLocalBounds());
}

// --- keys --------------------------------------------------------------------------------------

keys::Context MainComponent::focusContext() const
{
    switch (workspace.getFocused())
    {
        case Panel::playlist: return keys::Context::playlist;
        case Panel::channelRack: return keys::Context::channelRack;
        case Panel::pianoRoll: return keys::Context::pianoRoll;
        case Panel::mixer: return keys::Context::mixer;
        case Panel::browser: return keys::Context::browser;
    }
    return keys::Context::global;
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto chord = chordFromKeyPress (key);

    // 1. the typing piano (its keys win over plain-letter shortcuts while it is on)
    if (keys::KeyMap::playsNote (chord, typingPiano))
    {
        const auto note = keys::typingPianoNote (chord.key);

        if (! heldKeys.hasTag (key.getKeyCode())) // key repeat: the note is already sounding
            for (auto c : engine.getSelectedChannels())
                heldKeys.press (c, note, 0.8f, key.getKeyCode());
        return true;
    }

    // 2. the focused panel's own keys
    const auto focused = workspace.getFocused();
    const auto panelShown = workspace.isPanelVisible (focused);

    if (panelShown && focused == Panel::pianoRoll && pianoRoll.handleKey (key))
        return true;

    if (panelShown && focused == Panel::playlist && playlist.handleKey (key))
        return true;

    // 3. the key map
    const auto action = keyMap.find (chord, focusContext(), typingPiano);
    return ! action.empty() && runAction (action);
}

bool MainComponent::runAction (const std::string& id)
{
    auto is = [&id] (const char* name) { return id == name; };

    if (is ("transport.playStop")) playStop();
    else if (is ("transport.pause")) playPause();
    else if (is ("transport.toggleMode")) toggleMode();
    else if (is ("view.typingPiano")) setTypingPiano (! typingPiano);
    else if (is ("panel.playlist")) togglePanel (Panel::playlist);
    else if (is ("panel.channelRack")) togglePanel (Panel::channelRack);
    else if (is ("panel.pianoRoll"))
    {
        togglePanel (Panel::pianoRoll);
        if (workspace.isPanelVisible (Panel::pianoRoll))
            pianoRoll.scrollToNotes();
    }
    else if (is ("panel.browser")) togglePanel (Panel::browser);
    else if (is ("panel.mixer")) togglePanel (Panel::mixer);
    else if (is ("panel.maximisePlaylist"))
    {
        workspace.toggleMaximised (Panel::playlist);
        refreshPanelButtons();
    }
    else if (is ("pattern.next")) selectPattern (stepPattern (engine.getSong(), 1));
    else if (is ("pattern.previous")) selectPattern (stepPattern (engine.getSong(), -1));
    else if (is ("pattern.nextEmpty")) nextEmptyPattern();
    else if (is ("pattern.rename")) showRenamePattern (engine.getSong().currentPattern);
    else if (is ("pattern.insert")) insertPatternHere();
    else if (is ("pattern.clone")) clonePatternNow();
    else if (is ("pattern.moveUp")) movePatternNow (-1);
    else if (is ("pattern.moveDown")) movePatternNow (1);
    else if (id.rfind ("pattern.select", 0) == 0) selectPattern (std::stoi (id.substr (14)) - 1);
    else if (is ("panel.quickAdd")) showQuickAdd();
    else if (is ("edit.history")) showHistory();
    else if (is ("browser.search"))
    {
        showPanel (Panel::browser);
        browser.focusSearch();
    }
    else if (is ("transport.record")) engine.setRecording (! engine.isRecording());
    else if (is ("transport.metronome"))
    {
        engine.getTransport().metronome.store (! engine.getTransport().metronome.load());
        refreshModeButton();
    }
    else if (is ("record.countIn"))
    {
        auto o = engine.getRecordOptions();
        o.countInBars = o.countInBars > 0 ? 0 : 1;
        engine.setRecordOptions (o);
        refreshRecordOptionButtons();
    }
    else if (is ("record.waitForInput"))
    {
        auto o = engine.getRecordOptions();
        o.waitForInput = ! o.waitForInput;
        engine.setRecordOptions (o);
        refreshRecordOptionButtons();
    }
    else if (is ("rack.graphEditor")) rack.toggleGraphLanes();
    else if (is ("rack.rotateLeft")) rack.rotateSelected (-1);
    else if (is ("rack.rotateRight")) rack.rotateSelected (1);
    else if (is ("browser.previous")) browser.step (-1, false);
    else if (is ("browser.next")) browser.step (1, false);
    else if (is ("browser.swapPrevious")) browser.step (-1, true);
    else if (is ("browser.swapNext")) browser.step (1, true);
    else if (is ("browser.load")) browser.loadSelected();
    else if (is ("browser.closeFolder")) browser.openFolder (false);
    else if (is ("browser.openFolder")) browser.openFolder (true);
    else if (id.rfind ("pianoRoll.", 0) == 0) return pianoRoll.runAction (id);
    else if (id.rfind ("playlist.", 0) == 0) return playlist.runAction (id);
    else if (id.rfind ("mixer.", 0) == 0) return mixer.runAction (id);
    else if (is ("file.new")) newProject();
    else if (is ("file.open")) chooseFileToOpen();
    else if (is ("file.save")) save (false);
    else if (is ("file.saveAs")) save (true);
    else if (is ("file.export")) exportAudio();
    else if (is ("edit.undo")) engine.undo();
    else if (is ("edit.redo")) engine.redo();
    else if (is ("help.shortcuts")) showHelp();
    else if (is ("rack.selectPrevious")) rack.selectRelative (-1, false);
    else if (is ("rack.selectNext")) rack.selectRelative (1, false);
    else if (is ("rack.extendUp")) rack.selectRelative (-1, true);
    else if (is ("rack.extendDown")) rack.selectRelative (1, true);
    else if (is ("rack.moveUp")) rack.moveSelected (-1);
    else if (is ("rack.moveDown")) rack.moveSelected (1);
    else if (is ("rack.selectAll")) rack.selectAll();
    else if (is ("rack.clone")) rack.cloneSelected();
    else if (is ("rack.deleteSelected")) rack.deleteSelected();
    else if (is ("rack.sendToInserts")) rack.sendSelectedToInserts();
    else if (id.rfind ("rack.mute", 0) == 0) rack.toggleMute (std::stoi (id.substr (9)) - 1);
    else if (id.rfind ("rack.solo", 0) == 0) rack.solo (std::stoi (id.substr (9)) - 1);
    else
        return false;

    return true;
}

bool MainComponent::keyStateChanged (bool)
{
    // JUCE has no per-key "keyReleased"; check which held keys were released (each held note remembers its
    // channel and key, so the selection changing meanwhile can't leave one sounding)
    std::vector<int> released;
    heldKeys.forEachTag ([&released] (int code) {
        if (! juce::KeyPress::isKeyCurrentlyDown (code))
            released.push_back (code);
    });

    for (auto code : released)
        heldKeys.releaseTag (code);

    return false;
}

juce::String MainComponent::shortcutText (const char* actionId) const
{
    const auto& chords = keyMap.chordsFor (actionId);
    return chords.empty() ? juce::String() : "\t" + juce::String (keys::toString (chords.front()));
}

void MainComponent::setTypingPiano (bool on)
{
    typingPiano = on;
    EditorGlobals::get().typingPiano = on; // the editors' single-letter tool keys only work while it is off
    typingPianoButton.setToggleState (on, juce::dontSendNotification);
    typingPianoButton.setTooltip ((on ? tr ("Typing keyboard as a piano: on (Z..M and Q..P play the selected channels)")
                                      : tr ("Typing keyboard as a piano: off (letter and number keys are shortcuts)"))
                                  + juce::String (" (") + juce::String (keys::toString (keyMap.chordsFor ("view.typingPiano").empty() ? keys::Chord {}
                                                                                                                                 : keyMap.chordsFor ("view.typingPiano").front()))
                                  + ")");

    // release anything still sounding from the keyboard
    heldKeys.releaseAll();
}

void MainComponent::loadKeyMap()
{
    keyMap.resetToDefaults();
    const auto file = PluginHost::settingsFolder().getChildFile ("keys.json");

    if (file.existsAsFile())
    {
        std::vector<std::string> errors;
        if (! keyMap.loadJson (file.loadFileAsString().toStdString(), &errors))
            juce::Logger::writeToLog ("keys.json is not a JSON object; using the default keys");
        for (const auto& e : errors)
            juce::Logger::writeToLog ("keys.json: " + juce::String (e));
    }
}

// --- UI options ----------------------------------------------------------------------------------

juce::File MainComponent::uiSettingsFile() const
{
    return PluginHost::settingsFolder().getChildFile ("ui-options.txt");
}

void MainComponent::loadUiSettings()
{
    juce::StringArray lines;
    lines.addLines (uiSettingsFile().loadFileAsString());

    for (const auto& line : lines)
    {
        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto on = line.fromFirstOccurrenceOf ("=", false, false).trim() == "1";

        if (key == "typingPiano")
            typingPiano = on;
        else if (key == "keepEditorsOpen")
            ChannelRackComponent::keepEditorsOpen = on;
        else if (key == "waveSpectral") // ("waveColours", the old spectral-by-default key, is ignored)
            ui::WaveformCache::spectral = on;
        else if (key == "waveStereo")
            ui::WaveformCache::stereo = on;
        else if (key == "rememberSeek")
            EditorGlobals::get().rememberSeek = on;
        else if (key == "scopeMode")
            toolbar::OutputMonitor::mode = juce::jlimit (0, toolbar::OutputMonitor::modeCount - 1,
                                                         line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "toolbarGroups")
            toolbarLayout.orderFromString (line.fromFirstOccurrenceOf ("=", false, false).trim().toStdString());
        else if (key == "toolbarIcons")
            toolbarLayout.iconSize = toolbarlayout::Layout::iconSizeFromString (line.fromFirstOccurrenceOf ("=", false, false).trim().toStdString());
        else if (key == "uiScale")
            toolbarLayout.uiScale = toolbarlayout::clampScale (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "editPatternsIn")
            PlaylistComponent::editPatternsIn = juce::jlimit (0, 2, line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "autosaveMinutes")
            autosaveMinutes = prefs::clampAutosaveMinutes (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "autosaveKeep")
            autosaveKeep = prefs::clampAutosaveKeep (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
        else if (key == "undoLevels")
            engine.setUndoLevels (line.fromFirstOccurrenceOf ("=", false, false).trim().getIntValue());
    }

    startup = StartupOptions::load();
}

void MainComponent::saveUiSettings()
{
    if (! persistent)
        return;

    uiSettingsFile().replaceWithText ("typingPiano=" + juce::String (typingPiano ? 1 : 0) + "\nkeepEditorsOpen="
                                      + juce::String (ChannelRackComponent::keepEditorsOpen ? 1 : 0) + "\neditPatternsIn="
                                      + juce::String (PlaylistComponent::editPatternsIn) + "\nsplash=" + juce::String (startup.splash ? 1 : 0)
                                      + "\nstartupSound=" + juce::String (startup.sound ? 1 : 0)
                                      + "\nwaveSpectral=" + juce::String (ui::WaveformCache::spectral ? 1 : 0)
                                      + "\nwaveStereo=" + juce::String (ui::WaveformCache::stereo ? 1 : 0)
                                      + "\nrememberSeek=" + juce::String (EditorGlobals::get().rememberSeek ? 1 : 0)
                                      + "\nscopeMode=" + juce::String (toolbar::OutputMonitor::mode)
                                      + "\ntoolbarGroups=" + juce::String (toolbarLayout.orderToString())
                                      + "\ntoolbarIcons=" + toolbarlayout::Layout::iconSizeId (toolbarLayout.iconSize)
                                      + "\nuiScale=" + juce::String (toolbarLayout.uiScale)
                                      + "\nautosaveMinutes=" + juce::String (autosaveMinutes) + "\nautosaveKeep=" + juce::String (autosaveKeep)
                                      + "\nundoLevels=" + juce::String (engine.getUndoLevels()) + "\n");
}

// --- transport -----------------------------------------------------------------------------------

void MainComponent::playStop()
{
    auto& transport = engine.getTransport();

    if (engine.isWaitingForInput() || engine.isCountingIn()) // pressed again: cancel
    {
        engine.stop();
        return;
    }

    if (transport.playing.load())
    {
        engine.stop();
        engine.seek (playStartTicks); // back to where playback started
    }
    else
    {
        playStartTicks = transport.positionTicks.load();
        engine.startPlayback();
    }
}

void MainComponent::playPause()
{
    if (engine.getTransport().playing.load() || engine.isWaitingForInput() || engine.isCountingIn())
        engine.stop();
    else
    {
        playStartTicks = engine.getTransport().positionTicks.load();
        engine.startPlayback();
    }
}

void MainComponent::stopAndRewind()
{
    engine.stop();
    const auto& globals = EditorGlobals::get();
    const auto back = polish::stopReturnTick (globals.rememberSeek && engine.getSong().mode == PlayMode::song, globals.seekMark);

    if (back > 0) // "Remember seek time": back to the position clicked on the Playlist ruler
        engine.seek ((double) back);
    else
        engine.rewind();

    playStartTicks = (double) back;
}

void MainComponent::showStopMenu()
{
    auto& globals = EditorGlobals::get();
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Stop"));
    menu.addItem (tr ("Remember seek time: Stop goes back to the position clicked on the Playlist ruler"), true, globals.rememberSeek, [this] {
        EditorGlobals::get().rememberSeek = ! EditorGlobals::get().rememberSeek;
        saveUiSettings();
    });
    menu.addSeparator();
    menu.addItem (tr ("All notes off (panic)") + "\t" + tr ("double-click"), [this] { engine.panic(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&stopButton));
}

void MainComponent::toggleMode()
{
    engine.editSong ([] (Song& s) { s.mode = s.mode == PlayMode::song ? PlayMode::pattern : PlayMode::song; }, false);
}

// --- panels --------------------------------------------------------------------------------------

void MainComponent::togglePanel (Panel panel)
{
    workspace.toggle (panel);
    refreshPanelButtons();
}

void MainComponent::fitChannelRack (bool width)
{
    rack.updateSize();
    const auto scrollbar = rackViewport.getScrollBarThickness();
    const auto maxWidth = juce::jmax (300, workspace.getWidth() - 2 * 16 - 260);
    const auto contentWidth = juce::jmin (rack.getWidth(), maxWidth);
    const auto needsHorizontal = rack.getWidth() > contentWidth;
    workspace.fitPanelContent (Panel::channelRack, width ? contentWidth + scrollbar : 0, rack.getHeight() + (needsHorizontal ? scrollbar : 0));
    fittedChannelCount = (int) engine.getSong().channels.size();
}

void MainComponent::showPanel (Panel panel)
{
    workspace.show (panel);
    refreshPanelButtons();
}

void MainComponent::refreshPanelButtons()
{
    for (int i = 0; i < workspace::panelCount; ++i)
        panelButtons[(std::size_t) i].setToggleState (workspace.isPanelVisible ((Panel) i), juce::dontSendNotification);
}

void MainComponent::openPianoRoll()
{
    showPanel (Panel::pianoRoll);
    pianoRoll.scrollToNotes();
}

void MainComponent::saveLayoutAs()
{
    auto* window = new juce::AlertWindow (tr ("Save layout as"), tr ("Name of the layout:"), juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", tr ("My layout"));
    window->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result) {
                                 const auto name = window->getTextEditorContents ("name").trim();
                                 if (result == 1 && name.isNotEmpty())
                                     workspace.saveLayoutAs (name);
                             }),
                             true);
    keepAboveMainWindow (*window);
}

juce::PopupMenu MainComponent::layoutsMenu()
{
    juce::PopupMenu menu;
    auto files = Workspace::layoutsFolder().findChildFiles (juce::File::findFiles, false, "*.txt");
    files.sort();

    for (const auto& file : files)
        menu.addItem (file.getFileNameWithoutExtension(), [this, file] {
            workspace.loadLayout (file);
            refreshPanelButtons();
        });

    if (files.isEmpty())
        menu.addItem (tr ("(none saved yet)"), false, false, [] {});

    menu.addSeparator();
    menu.addItem (tr ("Open layouts folder"), [] {
        Workspace::layoutsFolder().createDirectory();
        Workspace::layoutsFolder().startAsProcess();
    });
    return menu;
}

void MainComponent::selectTabForSnapshot (int tab)
{
    const Panel panels[] = { Panel::channelRack, Panel::pianoRoll, Panel::playlist, Panel::mixer };
    const auto panel = panels[juce::jlimit (0, 3, tab)];

    if (panel == Panel::pianoRoll)
    {
        const auto& song = engine.getSong();
        std::vector<int> counts (song.channels.size(), 0);

        for (const auto& n : song.patterns[(std::size_t) song.currentPattern].notes)
            if (n.channel >= 0 && n.channel < (int) counts.size())
                ++counts[(std::size_t) n.channel];

        if (! counts.empty())
            engine.setSelectedChannel ((int) (std::max_element (counts.begin(), counts.end()) - counts.begin()));
    }

    // each view on its own: the panel maximized over the workspace
    workspace.resetLayout();
    workspace.show (panel);
    workspace.toggleMaximised (panel);
    refreshPanelButtons();

    if (panel == Panel::pianoRoll)
        pianoRoll.scrollToNotes();

    if (panel == Panel::playlist)
        playlist.resized(); // update the scrollbars for the loaded song

    if (panel == Panel::mixer)
        mixer.selectFirstTrackWithEffects();
}

void MainComponent::showDesktopForSnapshot (bool extraPanels)
{
    workspace.resetLayout();

    if (extraPanels)
    {
        workspace.show (Panel::mixer);
        workspace.show (Panel::pianoRoll);
        pianoRoll.scrollToNotes();
        workspace.show (Panel::channelRack);
    }

    refreshPanelButtons();
}

// --- patterns ------------------------------------------------------------------------------------

void MainComponent::selectPattern (int index)
{
    if (index >= 0 && index < (int) engine.getSong().patterns.size() && index != engine.getSong().currentPattern)
        engine.editSong ([index] (Song& s) { s.currentPattern = index; }, false);
}

void MainComponent::nextEmptyPattern()
{
    engine.editSong ([] (Song& s) { gotoNextEmptyPattern (s); });
    showRenamePattern (engine.getSong().currentPattern);
}

void MainComponent::insertPatternHere()
{
    engine.editSong ([] (Song& s) { insertPattern (s, s.currentPattern, newPatternName (s)); });
}

// --- window menus ------------------------------------------------------------------------------

// Each window's own items, at the top of the menu in its title bar (FL keeps an editor's options in the
// small menu at the left of its title bar). Items run the key map's actions, so they show their keys.
void MainComponent::addPanelMenuItems (Panel panel, juce::PopupMenu& menu)
{
    auto action = [this] (juce::PopupMenu& m, const char* id, const juce::String& text, bool enabled = true, bool ticked = false) {
        m.addItem (text + shortcutText (id), enabled, ticked, [this, id] { runAction (id); });
    };
    auto zoomMenu = [&action] (const juce::String& prefix) {
        juce::PopupMenu zoom;
        const std::pair<const char*, const char*> items[] = {
            { "zoomIn", TR_NOOP ("Zoom in") }, { "zoomOut", TR_NOOP ("Zoom out") }, { "zoom1", TR_NOOP ("Zoom: one bar") },
            { "zoom2", TR_NOOP ("Zoom: four bars") }, { "zoom3", TR_NOOP ("Zoom: sixteen bars") },
            { "zoomAll", TR_NOOP ("Zoom to show everything") }, { "zoomSelection", TR_NOOP ("Zoom to the selection") },
            { "centrePlayhead", TR_NOOP ("Centre the view on the playhead") }
        };
        for (const auto& [id, text] : items)
            if (const auto* a = keys::findAction ((prefix + id).toStdString())) // the key map holds the id strings
                action (zoom, a->id, tr (text));
        return zoom;
    };

    switch (panel)
    {
        case Panel::channelRack:
        {
            menu.addSubMenu (tr ("Pattern"), makePatternMenu());
            menu.addSeparator();
            action (menu, "rack.graphEditor", tr ("Graph lanes (velocity, pan, pitch...)"), true, rack.areGraphLanesShown());
            action (menu, "rack.rotateLeft", tr ("Rotate the selected channels' steps left"));
            action (menu, "rack.rotateRight", tr ("Rotate the selected channels' steps right"));
            juce::PopupMenu sort;
            sort.addItem (tr ("By name"), [this] { rack.sortChannels (ChannelSort::name); });
            sort.addItem (tr ("By color"), [this] { rack.sortChannels (ChannelSort::colour); });
            sort.addItem (tr ("By mixer insert"), [this] { rack.sortChannels (ChannelSort::insert); });
            sort.addItem (tr ("By type"), [this] { rack.sortChannels (ChannelSort::type); });
            menu.addSubMenu (tr ("Sort channels"), sort);
            menu.addItem (tr ("Select unused channels"), [this] { rack.selectUnused(); });
            break;
        }
        case Panel::playlist:
            menu.addItem (tr ("Maximize or restore the Playlist") + shortcutText ("panel.maximisePlaylist"), [this] {
                workspace.toggleMaximised (Panel::playlist);
                refreshPanelButtons();
            });
            menu.addSeparator();
            action (menu, "playlist.ripple", tr ("Ripple editing (resizing and deleting move the later clips)"), true, playlist.isRippleOn());
            action (menu, "playlist.rippleDelete", tr ("Delete the selected clips and close the gaps"));
            menu.addSubMenu (tr ("Zoom"), zoomMenu ("playlist."));
            break;
        case Panel::pianoRoll:
        {
            juce::PopupMenu tools;
            for (const auto* id : { "pianoRoll.arpeggiate", "pianoRoll.strum", "pianoRoll.flip", "pianoRoll.randomise",
                                    "pianoRoll.scaleLevels", "pianoRoll.limit" })
                if (const auto* a = keys::findAction (id))
                    action (tools, a->id, tr (a->description));
            menu.addSubMenu (tr ("Tools"), tools);
            menu.addSubMenu (tr ("Zoom"), zoomMenu ("pianoRoll."));
            menu.addItem (tr ("Snap notes to the scale"), pianoRoll.hasScale(), pianoRoll.isScaleSnapOn(),
                          [this] { pianoRoll.setScaleSnap (! pianoRoll.isScaleSnapOn()); });
            break;
        }
        case Panel::mixer:
            action (menu, "mixer.moveLeft", tr ("Move the selected inserts left"));
            action (menu, "mixer.moveRight", tr ("Move the selected inserts right"));
            menu.addSeparator();
            action (menu, "mixer.linkChannels", tr ("Link the selected channels to the selected insert"));
            action (menu, "mixer.linkChannelsFrom", tr ("Link the selected channels to inserts starting from the selected one"));
            action (menu, "mixer.selectLinkedChannels", tr ("Select the channels linked to the selected insert"));
            break;
        case Panel::browser:
            action (menu, "browser.search", tr ("Search the Browser"));
            menu.addItem (tr ("Add folder..."), [this] { browser.addUserFolder(); });
            break;
        default:
            break;
    }
}

// --- quick-add palette and history -------------------------------------------------------------

void MainComponent::showQuickAdd()
{
    QuickAddTargets targets;
    targets.effectsFirst = workspace.getFocused() == Panel::mixer;
    targets.addInstrument = [this] (ChannelDef c) {
        rack.addChannel (std::move (c), true);
        showPanel (Panel::channelRack);
    };
    targets.addEffect = [this] (EffectSlot slot) {
        if (! mixer.addEffectToSelected (std::move (slot)))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Quick-add palette"),
                                                    tr ("The selected insert has no free effect slot."));
        else
            showPanel (Panel::mixer);
    };
    openToolWindow (makeQuickAddPalette (engine, std::move (targets)), tr ("Quick-add palette"), "quickadd");
}

std::unique_ptr<juce::Component> MainComponent::makeQuickAddForSnapshot()
{
    QuickAddTargets targets;
    targets.addInstrument = [] (ChannelDef) {};
    targets.addEffect = [] (EffectSlot) {};
    return makeQuickAddPalette (engine, std::move (targets));
}

void MainComponent::showHistory()
{
    if (auto* open = findToolWindow ("history"))
    {
        open->toFront (true);
        return;
    }

    openToolWindow (makeHistoryPanel (engine), tr ("Undo history"), "history");
}

// --- recording options ---------------------------------------------------------------------------

void MainComponent::updateRecordQuantise()
{
    // the global grid sets the record quantise; "none" and the zoom-dependent grids record as played
    std::int64_t ticks = 0;

    if (recordQuantiseOn)
    {
        const auto& option = grid::options()[(std::size_t) juce::jlimit (0, (int) grid::options().size() - 1, EditorGlobals::get().getGridOption())];
        if (option.kind == grid::Kind::ticks)
            ticks = option.ticks;
        else if (option.kind == grid::Kind::bar)
            ticks = (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    }

    if (engine.getRecordOptions().quantise != ticks)
    {
        auto o = engine.getRecordOptions();
        o.quantise = ticks;
        engine.setRecordOptions (o);
    }
}

void MainComponent::addRecordOptions (juce::PopupMenu& menu)
{
    const auto o = engine.getRecordOptions();
    auto set = [this] (std::function<void (AudioEngine::RecordOptions&)> change) {
        auto options = engine.getRecordOptions();
        change (options);
        engine.setRecordOptions (options);
        refreshRecordOptionButtons();
    };

    juce::PopupMenu countIn;
    countIn.addItem (tr ("Off"), true, o.countInBars == 0, [set] { set ([] (AudioEngine::RecordOptions& r) { r.countInBars = 0; }); });
    for (int bars : { 1, 2, 4 })
        countIn.addItem (bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) }), true, o.countInBars == bars,
                         [set, bars] { set ([bars] (AudioEngine::RecordOptions& r) { r.countInBars = bars; }); });
    menu.addSubMenu (tr ("Count-in") + shortcutText ("record.countIn"), countIn);
    menu.addItem (tr ("Wait for the first note to start") + shortcutText ("record.waitForInput"), true, o.waitForInput,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.waitForInput = ! r.waitForInput; }); });
    menu.addSeparator();
    menu.addItem (tr ("Overdub: add to the pattern"), true, o.mode == RecordMode::overdub,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.mode = RecordMode::overdub; }); });
    menu.addItem (tr ("Replace: a recorded channel's old notes are cleared"), true, o.mode == RecordMode::replace,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.mode = RecordMode::replace; }); });
    menu.addSeparator();
    menu.addItem (tr ("Quantise recorded notes to the global grid"), true, recordQuantiseOn, [this] {
        recordQuantiseOn = ! recordQuantiseOn;
        updateRecordQuantise();
    });
    menu.addItem (tr ("Song mode: place a pattern clip where recording starts"), true, o.placeSongClip,
                  [set] { set ([] (AudioEngine::RecordOptions& r) { r.placeSongClip = ! r.placeSongClip; }); });
}

void MainComponent::showRecordMenu()
{
    juce::PopupMenu menu;
    menu.addItem (tr ("Notes (MIDI and PC keyboard) into the current pattern"), false, true, [] {});
    menu.addItem (tr ("Audio from the sound card input (becomes a Playlist clip)"), true, engine.isAudioRecordingArmed(), [this] {
        engine.setAudioRecordingArmed (! engine.isAudioRecordingArmed());

        if (engine.getLastRecordingError().isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Audio input"), engine.getLastRecordingError());
    });
    menu.addSeparator();
    menu.addItem (tr ("Open recordings folder"), [] {
        juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Recordings").createDirectory();
        juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Recordings").startAsProcess();
    });
    menu.addSeparator();
    addRecordOptions (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (recordMenuButton.isVisible() ? (juce::Component*) &recordMenuButton
                                                                                                      : (juce::Component*) &recordButton));
}

void MainComponent::refreshRecordOptionButtons()
{
    auto withKey = [this] (const juce::String& text, const char* id) {
        const auto key = shortcutText (id).trim();
        return key.isEmpty() ? text : text + " (" + key + ")";
    };
    const auto o = engine.getRecordOptions();
    countInButton.setToggleState (o.countInBars > 0, juce::dontSendNotification);
    countInButton.setTooltip (withKey (o.countInBars > 0 ? tr ("Count-in before recording: {0} bar(s)", { juce::String (o.countInBars) })
                                                         : tr ("Count-in before recording: off"),
                                       "record.countIn"));
    waitInputButton.setToggleState (o.waitForInput, juce::dontSendNotification);
    waitInputButton.setTooltip (withKey (o.waitForInput ? tr ("Recording waits for the first note: on") : tr ("Recording waits for the first note: off"),
                                         "record.waitForInput"));
}

void MainComponent::clonePatternNow()
{
    engine.editSong ([] (Song& s) { clonePattern (s, s.currentPattern); });
}

void MainComponent::movePatternNow (int delta)
{
    engine.editSong ([delta] (Song& s) { movePattern (s, s.currentPattern, delta); });
}

void MainComponent::splitPatternByChannelNow()
{
    int created = 0;
    engine.editSong ([&created] (Song& s) { created = splitPatternByChannel (s, s.currentPattern); });

    if (created == 0)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Split by channel"), tr ("This pattern has no notes."));
}

void MainComponent::showRenamePattern (int index)
{
    if (index < 0 || index >= (int) engine.getSong().patterns.size())
        return;

    const auto& p = engine.getSong().patterns[(std::size_t) index];
    showNameAndColourDialog (tr ("Rename pattern"), juce::String::fromUTF8 (p.name.c_str()), p.colour,
                             [this, index] (const juce::String& name, std::uint32_t colour) {
                                 if (index < (int) engine.getSong().patterns.size())
                                     engine.editSong ([index, n = name.toStdString(), colour] (Song& s) {
                                         s.patterns[(std::size_t) index].name = n;
                                         s.patterns[(std::size_t) index].colour = colour;
                                     });
                             });
}

void MainComponent::refreshToolbar()
{
    const auto& song = engine.getSong();
    patternBox.clear (juce::dontSendNotification);

    for (std::size_t i = 0; i < song.patterns.size(); ++i)
        patternBox.addItem (juce::String::fromUTF8 (song.patterns[i].name.c_str()), (int) i + 1);

    patternBox.setSelectedItemIndex (song.currentPattern, juce::dontSendNotification);
    swingSlider.setValue (song.swing, juce::dontSendNotification);
    tempoDisplay.repaint();
    refreshModeButton();

    if (! song.patterns.empty())
        workspace.setTitle (Panel::channelRack, "Channel Rack  -  " + juce::String::fromUTF8 (song.patterns[(std::size_t) song.currentPattern].name.c_str()));
}

juce::PopupMenu MainComponent::makePatternMenu()
{
    const auto& song = engine.getSong();
    const auto index = song.currentPattern;
    const auto& pattern = song.patterns[(std::size_t) index];

    juce::PopupMenu colours;
    colours.addItem (tr ("Automatic"), true, pattern.colour == 0,
                     [this, index] { engine.editSong ([index] (Song& s) { s.patterns[(std::size_t) index].colour = 0; }); });
    colours.addSeparator();

    for (auto c : lynxPalette())
    {
        juce::Image swatch (juce::Image::ARGB, 16, 16, true);
        juce::Graphics (swatch).fillAll (juce::Colour (c));
        juce::PopupMenu::Item item;
        item.text = "#" + juce::String::toHexString ((juce::int64) (c & 0xFFFFFF)).paddedLeft ('0', 6).toUpperCase();
        item.image = std::make_unique<juce::DrawableImage> (swatch);
        item.isTicked = pattern.colour == c;
        item.action = [this, index, c] { engine.editSong ([index, c] (Song& s) { s.patterns[(std::size_t) index].colour = c; }); };
        colours.addItem (std::move (item));
    }

    juce::PopupMenu lengths;
    lengths.addItem (tr ("Automatic (from notes)"), true, pattern.lengthTicks == 0,
                     [this, index] { engine.editSong ([index] (Song& s) { s.patterns[(std::size_t) index].lengthTicks = 0; }); });

    for (int bars : { 1, 2, 4, 8, 16, 32 })
    {
        const auto ticks = (std::int64_t) bars * ppq * juce::jmax (1, song.beatsPerBar);
        lengths.addItem (bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) }), true, pattern.lengthTicks == ticks,
                         [this, index, ticks] { engine.editSong ([index, ticks] (Song& s) { s.patterns[(std::size_t) index].lengthTicks = ticks; }); });
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 (pattern.name.c_str()));
    menu.addItem (tr ("Next pattern") + shortcutText ("pattern.next"), index + 1 < (int) song.patterns.size(), false,
                  [this] { selectPattern (stepPattern (engine.getSong(), 1)); });
    menu.addItem (tr ("Previous pattern") + shortcutText ("pattern.previous"), index > 0, false,
                  [this] { selectPattern (stepPattern (engine.getSong(), -1)); });
    menu.addItem (tr ("Next empty pattern (and name it)") + shortcutText ("pattern.nextEmpty"), [this] { nextEmptyPattern(); });
    menu.addSeparator();
    menu.addItem (tr ("Rename and color...") + shortcutText ("pattern.rename"), [this, index] { showRenamePattern (index); });
    menu.addSubMenu (tr ("Color"), colours);
    menu.addSubMenu (tr ("Length"), lengths);
    menu.addItem (tr ("Insert an empty pattern before this one") + shortcutText ("pattern.insert"), [this] { insertPatternHere(); });
    menu.addItem (tr ("Clone") + shortcutText ("pattern.clone"), [this] { clonePatternNow(); });
    menu.addItem (tr ("Move up") + shortcutText ("pattern.moveUp"), index > 0, false, [this] { movePatternNow (-1); });
    menu.addItem (tr ("Move down") + shortcutText ("pattern.moveDown"), index + 1 < (int) song.patterns.size(), false, [this] { movePatternNow (1); });
    menu.addItem (tr ("Split by channel (one new pattern per channel)"), ! pattern.notes.empty(), false, [this] { splitPatternByChannelNow(); });
    juce::PopupMenu signatures;

    for (int beats : { 2, 3, 4, 5, 6, 7, 8, 9, 12 })
        signatures.addItem (juce::String (beats) + "/4", true, song.beatsPerBar == beats,
                            [this, beats] { engine.editSong ([beats] (Song& s) { s.beatsPerBar = beats; }); });

    menu.addSubMenu (tr ("Song time signature"), signatures);
    menu.addSeparator();
    menu.addItem (tr ("Delete pattern"), song.patterns.size() > 1, false,
                  [this, index] { engine.editSong ([index] (Song& s) { s.removePattern (index); }); });
    return menu;
}

void MainComponent::showPatternMenu()
{
    makePatternMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternMenuButton));
}

void MainComponent::refreshModeButton()
{
    const auto recording = engine.isRecording();
    recordButton.setToggleState (recording, juce::dontSendNotification);
    const auto inputs = engine.getActiveMidiInputs();
    recordButton.setTooltip ((engine.isAudioRecordingArmed() ? tr ("Record while playing: notes (PC keyboard and MIDI) and input audio")
                                                             : tr ("Record while playing: notes (PC keyboard and MIDI)"))
                             + "\n" + tr ("MIDI inputs: {0}", { inputs.isEmpty() ? tr ("none") : inputs.joinIntoString (", ") }));
    recordMenuButton.setToggleState (engine.isAudioRecordingArmed(), juce::dontSendNotification);
    recordMenuButton.setOnColour (theme::colour::accentMuted);

    const auto songMode = engine.getSong().mode == PlayMode::song;
    patternModeButton.setToggleState (! songMode, juce::dontSendNotification);
    songModeButton.setToggleState (songMode, juce::dontSendNotification);
    metronomeButton.setToggleState (engine.getTransport().metronome.load(), juce::dontSendNotification);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto& song = engine.getSong();
    bool namesChanged = patternBox.getNumItems() != (int) song.patterns.size();

    for (int i = 0; ! namesChanged && i < patternBox.getNumItems(); ++i)
        namesChanged = patternBox.getItemText (i) != juce::String::fromUTF8 (song.patterns[(std::size_t) i].name.c_str());

    refreshModeButton();
    tempoDisplay.repaint();

    if ((int) song.channels.size() != fittedChannelCount && fittedChannelCount >= 0) // a row was added or removed
        fitChannelRack (false);

    if (namesChanged || patternBox.getSelectedItemIndex() != song.currentPattern || std::abs (swingSlider.getValue() - song.swing) > 1e-6)
        refreshToolbar();
}

// --- tempo display -------------------------------------------------------------------------------

ui::ValueSpec MainComponent::TempoDisplay::spec() const
{
    ui::ValueSpec s;
    s.name = tr ("Tempo");
    s.minimum = minTempo;
    s.maximum = maxTempo;
    s.defaultValue = 130.0;
    s.toText = [] (double v) { return juce::String (v, 3) + " BPM"; };
    s.target = AutomationTarget { AutomationTargetKind::tempo, 0 };
    return s;
}

void MainComponent::TempoDisplay::setBpm (double bpm)
{
    bpm = controls::clampTempo (bpm);

    if (std::abs (bpm - engine.getSong().bpm) > 1e-9)
        engine.editSong ([bpm] (Song& s) { s.bpm = bpm; });

    repaint();
}

bool MainComponent::TempoDisplay::isOnDecimals (int x) const
{
    return decimalX > 0 && x > decimalX;
}

void MainComponent::TempoDisplay::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto area = getLocalBounds().toFloat();
    g.setColour (colour::bg1);
    g.fillRoundedRectangle (area, radius::md);

    if (dragging)
    {
        g.setColour (colour::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius::md, 1.0f);
    }

    const auto bpm = engine.getSong().bpm;
    const auto whole = juce::String ((int) std::floor (bpm + 1e-9));
    const auto fraction = juce::String (bpm - std::floor (bpm + 1e-9), 3).fromFirstOccurrenceOf (".", false, false).paddedRight ('0', 3).substring (0, 3);

    const juce::Font big (monoFont (18.0f)), small (monoFont (11.0f)), caption (font (type::caption - 1.0f));
    const auto wholeWidth = juce::GlyphArrangement::getStringWidth (big, whole);
    const auto fracWidth = juce::GlyphArrangement::getStringWidth (small, "." + fraction);
    const auto unitWidth = juce::GlyphArrangement::getStringWidth (caption, "BPM");
    auto x = area.getCentreX() - (wholeWidth + fracWidth + 6.0f + unitWidth) / 2.0f;
    const auto baseline = area.getCentreY() + 5.0f;

    g.setColour (colour::textPrimary);
    g.setFont (big);
    g.drawSingleLineText (whole, juce::roundToInt (x), juce::roundToInt (baseline));
    x += wholeWidth;
    decimalX = juce::roundToInt (x);
    g.setColour (dragOnDecimals && dragging ? colour::textPrimary : colour::textSecondary);
    g.setFont (small);
    g.drawSingleLineText ("." + fraction, juce::roundToInt (x), juce::roundToInt (baseline));
    x += fracWidth + 6.0f;
    g.setColour (colour::textDisabled);
    g.setFont (caption);
    g.drawSingleLineText ("BPM", juce::roundToInt (x), juce::roundToInt (baseline));
}

void MainComponent::TempoDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    if (ui::isResetClick (e))
    {
        setBpm (spec().defaultValue);
        return;
    }

    startBpm = engine.getSong().bpm;
    dragOnDecimals = isOnDecimals (e.x);
    dragging = true;
    repaint();
}

void MainComponent::TempoDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        setBpm (controls::tempoDrag (startBpm, -e.getDistanceFromDragStartY(), dragOnDecimals, e.mods.isCtrlDown()));
}

void MainComponent::TempoDisplay::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void MainComponent::TempoDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        ui::typeValue (*this, spec(), engine.getSong().bpm, [this] (double v) { setBpm (v); });
}

void MainComponent::TempoDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX;

    if (delta == 0.0f)
        return;

    const auto step = isOnDecimals (e.x) ? 0.01 : 1.0;
    setBpm (engine.getSong().bpm + (delta > 0.0f ? 1.0 : -1.0) * step * (e.mods.isCtrlDown() ? 0.1 : 1.0));
}

ui::Hint MainComponent::TempoDisplay::hintAt (juce::Point<int> p)
{
    return ui::valueHint (spec(), engine.getSong().bpm,
                          isOnDecimals (p.x) ? tr ("[LMB] drag the decimals for fine changes  [Wheel] 0.01  [2xLMB] type  [RMB] tap tempo, half, double")
                                             : tr ("[LMB] drag the whole number  [Ctrl] fine  [Wheel] 1 BPM  [2xLMB] type  [RMB] tap tempo, half, double"));
}

void MainComponent::TempoDisplay::showMenu()
{
    const auto bpm = engine.getSong().bpm;
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Tempo") + ": " + juce::String (bpm, 3) + " BPM");
    menu.addItem (tr ("Type value..."), [this, bpm] { ui::typeValue (*this, spec(), bpm, [this] (double v) { setBpm (v); }); });
    menu.addItem (tr ("Tap tempo..."), [this] { showTap(); });
    menu.addItem (tr ("Half ({0} BPM)", { juce::String (controls::halfTempo (bpm), 2) }), [this, bpm] { setBpm (controls::halfTempo (bpm)); });
    menu.addItem (tr ("Double ({0} BPM)", { juce::String (controls::doubleTempo (bpm), 2) }), [this, bpm] { setBpm (controls::doubleTempo (bpm)); });
    menu.addItem (tr ("Reset ({0})", { "130 BPM" }), [this] { setBpm (130.0); });
    menu.addSeparator();
    addAutomationItem (menu, engine, { AutomationTargetKind::tempo, 0 });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void MainComponent::TempoDisplay::showTap()
{
    // a callout with a big Tap pad: the tempo follows from the second tap on
    struct TapPad final : juce::Component
    {
        explicit TapPad (TempoDisplay& d) : display (d)
        {
            setSize (220, 120);
            setWantsKeyboardFocus (true);
        }

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            auto r = getLocalBounds().reduced (10);
            const auto pad = r.removeFromTop (70).toFloat();
            g.setColour (flash > 0 ? colour::accent : colour::bg4);
            g.fillRoundedRectangle (pad, radius::lg);
            icons::draw (g, Icon::tap, pad.withSizeKeepingCentre (26.0f, 26.0f).translated (-34.0f, 0.0f), colour::textPrimary);
            g.setColour (colour::textPrimary);
            g.setFont (font (type::title, true));
            g.drawText (tr ("Tap"), pad.withTrimmedLeft (40.0f), juce::Justification::centred);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::small));
            g.drawFittedText (taps.count() < 2 ? tr ("Click (or press T) on each beat")
                                               : tr ("{0} taps: {1} BPM", { juce::String (taps.count()), juce::String (lastBpm, 2) }),
                              r, juce::Justification::centred, 2);
        }

        void tap()
        {
            if (const auto bpm = taps.tap (juce::Time::getMillisecondCounterHiRes()))
            {
                lastBpm = *bpm;
                display.setBpm (*bpm);
            }
            flash = 1;
            repaint();
            juce::Timer::callAfterDelay (90, [safe = juce::Component::SafePointer<TapPad> (this)] {
                if (safe != nullptr)
                {
                    safe->flash = 0;
                    safe->repaint();
                }
            });
        }

        void mouseDown (const juce::MouseEvent&) override { tap(); }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.getKeyCode() == 'T' || key.getKeyCode() == 't' || key == juce::KeyPress::spaceKey)
            {
                tap();
                return true;
            }
            return false;
        }

        TempoDisplay& display;
        controls::TapTempo taps;
        double lastBpm = 0.0;
        int flash = 0;
    };

    auto pad = std::make_unique<TapPad> (*this);
    auto* raw = pad.get();
    juce::CallOutBox::launchAsynchronously (std::move (pad), getScreenBounds(), nullptr);
    raw->grabKeyboardFocus();
}

// --- hint bar ------------------------------------------------------------------------------------

juce::String MainComponent::idleHint() const
{
    if (engine.isWaitingForInput())
        return tr ("Recording: waiting for the first note ([Space] cancels)");

    if (engine.isCountingIn())
        return tr ("Recording: count-in...");

    if (const auto scan = engine.getPluginHost().backgroundScan(); scan.running)
        return tr ("Scanning for plugins in the background: {0}% {1}", { juce::String (juce::roundToInt (scan.progress * 100.0f)), scan.file });

    return tr ("Point at anything to see what it does here.  [F1] shortcuts  [F5]-[F9] windows  [Space] play/stop  "
               "[Ctrl][T] typing piano: {0}", { typingPiano ? tr ("on") : tr ("off") });
}

juce::String MainComponent::keyMarkup (const char* actionId) const
{
    const auto& chords = keyMap.chordsFor (actionId);

    if (chords.empty())
        return {};

    // "Ctrl+Shift+S" -> "[Ctrl][Shift][S]"; a key name that is not markup stays as text
    const auto text = juce::String (keys::toString (chords.front()));
    juce::String out;

    for (const auto& part : juce::StringArray::fromTokens (text, "+", ""))
    {
        if (! hintmarkup::isToken (part.toStdString()))
            return "(" + text + ")";
        out << "[" << part << "]";
    }

    return out;
}

void MainComponent::setControlHints()
{
    auto with = [this] (const juce::String& text, const char* actionId) {
        const auto key = keyMarkup (actionId);
        return key.isEmpty() ? text : text + "  " + key;
    };

    ui::setHint (newButton, with (tr ("New project"), "file.new"));
    ui::setHint (openButton, with (tr ("Open a project (.lynx, FL Studio .flp, MIDI)"), "file.open"));
    ui::setHint (saveButton, with (tr ("Save the project"), "file.save") + "  " + tr ("save as:") + " " + keyMarkup ("file.saveAs"));
    ui::setHint (exportButton, with (tr ("Export audio: WAV, FLAC or OGG"), "file.export"));
    ui::setHint (undoButton, with (tr ("Undo the last edit"), "edit.undo") + "  " + tr ("[RMB] undo history"), true);
    ui::setHint (redoButton, with (tr ("Redo"), "edit.redo") + "  " + tr ("[RMB] undo history"), true);
    ui::setHint (pluginPickerButton, with (tr ("Plugin picker: add an instrument or an effect by name"), "panel.quickAdd"));

    ui::setHint (playButton, with (tr ("Play: [LMB] play; again: stop and go back to where it started"), "transport.playStop")
                                 + "  " + keyMarkup ("transport.pause") + " " + tr ("pause"));
    ui::setHint (stopButton, tr ("Stop: [LMB] stop and go back to the start (or the remembered position)  [2xLMB] all notes off (panic)  "
                                 "[RMB] remember seek time"),
                 true);
    ui::setHint (recordButton, with (tr ("Record: [LMB] arm, then play to record notes (and audio, when armed)  [RMB] what to record"), "transport.record"),
                 true);
    ui::setHint (recordMenuButton, tr ("What to record: notes, input audio, count-in, overdub or replace, quantise  [LMB] menu"), true);
    ui::setHint (patternModeButton, with (tr ("Pattern mode: plays the current pattern in a loop  [RMB] show the Channel Rack"), "transport.toggleMode"));
    ui::setHint (songModeButton, with (tr ("Song mode: plays the Playlist arrangement  [RMB] show the Playlist"), "transport.toggleMode"));
    ui::setHint (metronomeButton, with (tr ("Metronome: clicks on every beat while playing"), "transport.metronome"));
    ui::setHint (countInButton, with (tr ("Count-in: one bar of clicks before recording starts (the menu has more lengths)"), "record.countIn"));
    ui::setHint (waitInputButton, with (tr ("Wait for input: recording starts with the first note you play"), "record.waitForInput"));
    ui::setHint (typingPianoButton, with (tr ("Typing keyboard as a piano: Z..M and Q..P play the selected channels; letter shortcuts pause"),
                                          "view.typingPiano"));
    ui::setHint (gridBox, tr ("Global snap: the grid of the Piano Roll and Playlist (when they follow it) and the quantise of recorded notes  "
                              "[LMB] choose  [Wheel] step  [RMB] length of new notes  [Backspace] snap on/off  [Alt] while dragging: no snap"),
                 true);
    ui::setHint (patternBox, tr ("Current pattern: what the Channel Rack and Piano Roll edit and Pattern mode plays  [LMB] choose  [Wheel] step  "
                                 "[F4] next empty  [F2] rename"));
    ui::setHint (newPatternButton, tr ("New pattern: an empty pattern after the last one"));
    ui::setHint (patternMenuButton, tr ("Pattern menu: rename, color, length, clone, insert, split by channel, delete"), true);

    const char* panelHelp[] = { TR_NOOP ("the arrangement: clips of patterns, audio and automation on tracks"),
                                TR_NOOP ("the instruments and their step sequencer"),
                                TR_NOOP ("the notes of the selected channel in the current pattern"),
                                TR_NOOP ("inserts, effects and routing"),
                                TR_NOOP ("samples, presets, plugins and projects to drag in") };
    for (int i = 0; i < workspace::panelCount; ++i)
        ui::setHint (panelButtons[(std::size_t) i],
                     with (panelName ((Panel) i) + ": " + tr (panelHelp[i]) + "  " + tr ("[LMB] show, focus or hide  [RMB] maximized"),
                           panelAction ((Panel) i)));

    setQuickTips();
}

std::vector<std::pair<juce::Rectangle<int>, juce::String>> MainComponent::getQuickTipsForSnapshot()
{
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> tips;
    std::function<void (juce::Component&)> visit = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (! child->isVisible())
                continue;
            if (const auto tip = ui::quickTipOf (*child); tip.isNotEmpty() && ! child->getBounds().isEmpty())
                tips.emplace_back (getLocalArea (child->getParentComponent(), child->getBounds()), tip);
            visit (*child);
        }
    };
    visit (*this);
    return tips;
}

// the small name bubble of every toolbar control (the hint bar keeps the full description)
void MainComponent::setQuickTips()
{
    auto tip = [this] (juce::Component& c, const juce::String& name, const char* actionId = nullptr) {
        ui::setQuickTip (c, name, actionId != nullptr ? shortcutText (actionId).trim() : juce::String());
    };

    tip (newButton, tr ("New project"), "file.new");
    tip (openButton, tr ("Open project"), "file.open");
    tip (saveButton, tr ("Save project"), "file.save");
    tip (exportButton, tr ("Export audio"), "file.export");
    tip (undoButton, tr ("Undo"), "edit.undo");
    tip (redoButton, tr ("Redo"), "edit.redo");
    tip (pluginPickerButton, tr ("Plugin picker"), "panel.quickAdd");
    tip (playButton, tr ("Play"), "transport.playStop");
    tip (stopButton, tr ("Stop"));
    tip (recordButton, tr ("Record"), "transport.record");
    tip (recordMenuButton, tr ("Recording options"));
    tip (patternModeButton, tr ("Pattern mode"), "transport.toggleMode");
    tip (songModeButton, tr ("Song mode"), "transport.toggleMode");
    tip (metronomeButton, tr ("Metronome"), "transport.metronome");
    tip (countInButton, tr ("Count-in"), "record.countIn");
    tip (waitInputButton, tr ("Wait for input"), "record.waitForInput");
    tip (typingPianoButton, tr ("Typing keyboard to piano"), "view.typingPiano");
    tip (tempoDisplay, tr ("Tempo (BPM)"));
    tip (timeDisplay, tr ("Song time"));
    tip (songPosition, tr ("Song position"));
    tip (masterVolume, tr ("Master volume"));
    tip (masterPitch, tr ("Master pitch"));
    tip (gridBox, tr ("Snap"));
    tip (patternBox, tr ("Pattern"));
    tip (newPatternButton, tr ("New pattern"));
    tip (patternMenuButton, tr ("Pattern options"));
    tip (perfMeter, tr ("CPU, memory and polyphony"));
    tip (output, tr ("Output monitor"));
    tip (swingSlider, tr ("Swing"));

    for (int i = 0; i < workspace::panelCount; ++i)
        tip (panelButtons[(std::size_t) i], tr (panelName ((Panel) i)), panelAction ((Panel) i));
}

void MainComponent::timerCallback()
{
    output.update();
    timeDisplay.update();
    songPosition.update();
    masterVolume.update();
    masterPitch.update();
    updateRecordQuantise();

    if (engine.isModified() != lastModified)
    {
        lastModified = engine.isModified();
        updateTitle();
    }

    // a backup copy every few minutes (Settings › General) when there are changes
    if (autosaveMinutes > 0 && ++autosaveTicks >= 30 * 60 * autosaveMinutes)
    {
        autosaveTicks = 0;

        if (engine.isModified())
            autosave();
    }

    if (const auto scan = engine.getPluginHost().backgroundScan(); scan.finished != scansSeen)
    {
        scansSeen = scan.finished;
        ui::postHint ({ scan.found == 1 ? tr ("Plugin scan finished: 1 new plugin.")
                                        : tr ("Plugin scan finished: {0} new plugins.", { juce::String (scan.found) }), {}, false, false },
                      6000);
    }

    auto& transport = engine.getTransport();

    if (! heldKeys.empty())
    {
        if (! juce::Process::isForegroundProcess()) // switched to another app with keys down: no key-up will come
            heldKeys.releaseAll();
        else // keys released while another window (a detached panel, a dialog) had the focus
            keyStateChanged (false);
    }

    // hint bar: a gesture's own posted hint, else the control under the mouse (while it is dragged, the
    // control that was pressed, at the point it was pressed, so its value updates live); MIDI learn and
    // recording states first
    if (! hintsFrozen)
    {
        ui::Hint hint;
        bool live = false;

        if (engine.isLearningMidi())
            hint.text = tr ("Move a knob/fader on the MIDI controller to link it to the parameter...");
        else if (engine.isWaitingForInput() || engine.isCountingIn())
            hint.text = idleHint();
        else if (auto posted = ui::postedHint())
        {
            hint = *posted;
            live = true;
        }
        else if (isShowing())
        {
            const auto source = juce::Desktop::getInstance().getMainMouseSource();

            if (auto* under = source.getComponentUnderMouse())
            {
                live = source.isDragging();
                const auto at = live ? source.getLastMouseDownPosition() : source.getScreenPosition();
                hint = ui::hintFor (under, under->getLocalPoint (nullptr, at).roundToInt());
            }
        }

        if (hint.isEmpty())
        {
            hint.text = idleHint();
            live = false;
        }

        hintBar.setHint (hint, live && hint.value.isNotEmpty());
    }

    const auto playing = transport.playing.load();
    playButton.setToggleState (playing, juce::dontSendNotification);

    // CPU / memory / polyphony, a few times a second
    if (++meterTicks >= 8 && perfMeter.isVisible() && ! hintsFrozen)
    {
        meterTicks = 0;
        const auto held = (int) heldKeys.size();
        const auto notes = playing ? songstats::soundingNotes (engine.getSong(), transport.positionTicks.load()) : 0;
        perfMeter.setValues (engine.getDeviceManager().getCpuUsage(), toolbar::PerformanceMeter::processMemoryBytes(), notes + held);
    }

    // If a dialog stole focus, give it back so the PC keyboard keeps playing (unless a detached panel has it).
    if (! hasKeyboardFocus (true) && isShowing() && juce::Process::isForegroundProcess()
        && juce::Component::getCurrentlyModalComponent() == nullptr && ! workspace.isDetachedWindowActive())
        grabKeyboardFocus();
}

void MainComponent::updateTitle()
{
    repaint (titleArea); // the project name in the toolbar

    if (auto* window = getTopLevelComponent())
        window->setName ((engine.isModified() ? "* " : "")
                         + (projectName.isNotEmpty() ? "LYNX - " + projectName : juce::String ("LYNX")) + importedSuffix);
}

std::unique_ptr<juce::AlertWindow> MainComponent::makeConfirmDiscardWindow()
{
    auto window = std::make_unique<juce::AlertWindow> (tr ("Save changes?"), tr ("The project has unsaved changes."),
                                                       juce::MessageBoxIconType::QuestionIcon);
    window->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Discard"), 2);
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    return window;
}

void MainComponent::confirmDiscard (std::function<void()> then)
{
    if (! engine.isModified())
    {
        then();
        return;
    }

    auto* window = makeConfirmDiscardWindow().release();
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, then] (int result) {
                                 if (result == 1)
                                 {
                                     save (false);
                                     if (! engine.isModified()) // saved (did not cancel "save as")
                                         then();
                                 }
                                 else if (result == 2)
                                     then();
                             }),
                             true);
}

void MainComponent::autosave()
{
    // backup copy of the open project, so work isn't lost if something crashes
    // (one file per save, "Name (date time).lynx"; only the newest few of each project are kept)
    const auto folder = autosaveFolder();
    folder.createDirectory();
    const auto name = projectName.isNotEmpty() ? juce::File::createLegalFileName (projectName) : juce::String ("untitled");
    const auto now = juce::Time::getCurrentTime();
    const auto stem = juce::String::fromUTF8 (prefs::backupName (name.toStdString(), now.getYear(), now.getMonth() + 1, now.getDayOfMonth(),
                                                                 now.getHours(), now.getMinutes(), now.getSeconds()).c_str());
    juce::String error;
    engine.capturePluginStates();
    if (! ProjectFile::save (engine.getSong(), folder.getChildFile (stem + ProjectFile::extension), error))
        return;

    std::vector<std::string> names;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + ProjectFile::extension))
        names.push_back (f.getFileNameWithoutExtension().toStdString());
    for (const auto& old : prefs::backupsToRemove (names, name.toStdString(), autosaveKeep))
        folder.getChildFile (juce::String::fromUTF8 (old.c_str()) + ProjectFile::extension).deleteFile();
}

void MainComponent::newProject()
{
    confirmDiscard ([this] {
        engine.stop();
        engine.rewind();
        engine.setSong (Song::makeDefault());
        currentFile = juce::File();
        projectName = {};
        importedSuffix = {};
        updateTitle();
    });
}

void MainComponent::chooseFileToOpen()
{
    confirmDiscard ([this] { chooseFileToOpenNow(); });
}

void MainComponent::chooseFileToOpenNow()
{
    fileChooser = std::make_unique<juce::FileChooser> (tr ("Open project"), currentFile.getParentDirectory(),
                                                       juce::String ("*") + ProjectFile::extension + ";*.dawproj;*.flp;*.mid;*.midi");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser) {
                                  if (chooser.getResult().existsAsFile())
                                      openFile (chooser.getResult());
                              });
}

void MainComponent::openFile (const juce::File& file)
{
    juce::String error;

    if (file.hasFileExtension ("mid;midi"))
    {
        auto song = MidiFileIo::loadSong (file, error);

        if (! song)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
            return;
        }

        engine.stop();
        engine.rewind();
        engine.setSong (std::move (*song));
        currentFile = juce::File(); // imported: "Save" asks where to write the .lynx
        projectName = file.getFileNameWithoutExtension();
        rememberRecent (file);

        importedSuffix = " " + tr ("(imported from MIDI)");
        updateTitle();
        return;
    }

    if (file.hasFileExtension ("flp"))
    {
        // the project's third-party plugins: known ones resolve from the plugin list or FL Studio's database; the
        // rest get a targeted scan of just their files first (no question asked)
        std::vector<FlpPluginUse> uses;
        if (juce::MemoryBlock data; file.loadFileAsData (data))
        {
            try
            {
                uses = flpForeignPlugins (flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() }));
            }
            catch (const flp::Error&)
            {
            }
        }
        if (! uses.empty() && persistent) // (command-line modes open synchronously, without scanning)
        {
            findProjectPlugins (engine, std::move (uses), [safe = juce::Component::SafePointer<MainComponent> (this), file] {
                if (safe != nullptr)
                    safe->importFlp (file);
            });
            return;
        }
        importFlp (file);
        return;
    }

    openLynxProject (file);
}

void MainComponent::importFlp (const juce::File& file)
{
    {
        juce::String error;
        if (! engine.importFlpFile (file, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
            return;
        }

        currentFile = juce::File(); // imported: "Save" asks where to write the .lynx
        importedFlpFile = file;
        projectName = file.getFileNameWithoutExtension();
        rememberRecent (file);
        importedSuffix = " " + tr ("(imported from FL Studio)");
        updateTitle();

        showMissingFiles (true);
    }
}

void MainComponent::openLynxProject (const juce::File& file)
{
    juce::String error;
    auto song = ProjectFile::load (file, error);

    if (! song)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
        return;
    }

    engine.stop();
    engine.rewind();
    engine.setSong (std::move (*song));
    currentFile = file;
    importedFlpFile = juce::File();
    projectName = file.getFileNameWithoutExtension();
    importedSuffix = {};
    rememberRecent (file);
    updateTitle();

    if (persistent)
    {
        showMissingFiles (true);
        if (engine.getSong().info.showOnOpen)
            openSettings (1);
    }
}

void MainComponent::openSettings (int page)
{
    const juce::Component::SafePointer<MainComponent> safe (this);
    SettingsHooks hooks;
    hooks.browserFoldersChanged = [safe] {
        if (safe != nullptr)
            safe->browser.reloadUserFolders();
    };
    hooks.getGeneral = [safe] {
        GeneralOptions o;
        if (safe == nullptr)
            return o;
        o.language = i18n::saved();
        o.uiScale = safe->toolbarLayout.uiScale;
        o.waveSpectral = ui::WaveformCache::spectral;
        o.waveStereo = ui::WaveformCache::stereo;
        o.splash = safe->startup.splash;
        o.startupSound = safe->startup.sound;
        o.scanAtStart = backgroundScanAtStart();
        o.typingPiano = safe->typingPiano;
        o.keepEditorsOpen = ChannelRackComponent::keepEditorsOpen;
        o.rememberSeek = EditorGlobals::get().rememberSeek;
        o.editPatternsIn = PlaylistComponent::editPatternsIn;
        o.undoLevels = safe->engine.getUndoLevels();
        o.autosaveMinutes = safe->autosaveMinutes;
        o.autosaveKeep = safe->autosaveKeep;
        o.pluginSandbox = pluginSandboxEnabled();
        return o;
    };
    hooks.setGeneral = [safe] (const GeneralOptions& o) {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        if (o.uiScale != self.toolbarLayout.uiScale)
        {
            auto layout = self.toolbarLayout;
            layout.uiScale = toolbarlayout::clampScale (o.uiScale);
            self.setToolbarLayout (layout, true);
        }
        if (o.waveSpectral != ui::WaveformCache::spectral || o.waveStereo != ui::WaveformCache::stereo)
        {
            ui::WaveformCache::spectral = o.waveSpectral;
            ui::WaveformCache::stereo = o.waveStereo;
            ui::WaveformCache::getInstance()->settingsChanged();
        }
        if (o.typingPiano != self.typingPiano)
            self.setTypingPiano (o.typingPiano);
        if (o.scanAtStart != backgroundScanAtStart())
            setBackgroundScanAtStart (o.scanAtStart);
        if (o.pluginSandbox != pluginSandboxEnabled())
            setPluginSandboxEnabled (o.pluginSandbox);
        ChannelRackComponent::keepEditorsOpen = o.keepEditorsOpen;
        EditorGlobals::get().rememberSeek = o.rememberSeek;
        PlaylistComponent::editPatternsIn = juce::jlimit (0, 2, o.editPatternsIn);
        self.startup.splash = o.splash;
        self.startup.sound = o.startupSound;
        self.engine.setUndoLevels (o.undoLevels);
        self.autosaveMinutes = prefs::clampAutosaveMinutes (o.autosaveMinutes);
        self.autosaveKeep = prefs::clampAutosaveKeep (o.autosaveKeep);
        self.autosaveTicks = 0;
        self.saveUiSettings();
        if (o.language != i18n::saved())
            self.showLanguageChoice (o.language);
    };
    hooks.manageShortcuts = [safe] {
        if (safe != nullptr)
            safe->showShortcutManager();
    };
    hooks.managePlugins = [safe] {
        if (safe != nullptr)
            safe->showPluginManager();
    };
    showSettings (engine, (SettingsPage) juce::jlimit (0, 5, page), std::move (hooks));
}

void MainComponent::showPluginManager()
{
    lynx::showPluginManager (engine, [safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe != nullptr)
            safe->openSettings ((int) SettingsPage::files);
    });
}

void MainComponent::showShortcutManager()
{
    lynx::showShortcutManager ([safe = juce::Component::SafePointer<MainComponent> (this)] {
        if (safe == nullptr)
            return;
        safe->loadKeyMap();
        safe->setControlHints();
        safe->menuItemsChanged();
    });
}

void MainComponent::showMissingFiles (bool afterOpening)
{
    MissingFilesInfo info;
    info.projectName = projectName;
    const auto fromFlp = importedFlpFile.existsAsFile() && currentFile == juce::File();
    const auto exists = [] (const std::string& path) { return juce::File::isAbsolutePath (juce::String::fromUTF8 (path.c_str())) && juce::File (juce::String::fromUTF8 (path.c_str())).existsAsFile(); };

    if (fromFlp)
    {
        // the import dropped the samples it couldn't find: its report keeps them (minus the ones located since)
        const auto& report = engine.getImportReport();
        for (const auto& m : report.missingFiles)
            info.files.push_back (m);
        info.plugins = engine.getMissingPluginDetails();
        info.autoRelocated = report.relocatedFiles;
        info.searchFolders.add (importedFlpFile.getParentDirectory().getFullPathName());
    }
    else
    {
        const auto& song = engine.getSong();
        info.files = findMissingSamples (song, exists);
        auto& host = engine.getPluginHost();
        auto addPlugin = [&info] (const PluginRef& ref, const std::string& user, const char* replacement) {
            const auto name = ref.name.empty() ? ref.id : ref.name;
            auto it = std::find_if (info.plugins.begin(), info.plugins.end(), [&] (const MissingPlugin& p) { return p.name == name; });
            if (it == info.plugins.end())
                it = info.plugins.insert (info.plugins.end(), MissingPlugin { name, {}, replacement });
            if (std::find (it->usedBy.begin(), it->usedBy.end(), user) == it->usedBy.end())
                it->usedBy.push_back (user);
        };
        for (const auto& c : song.channels)
            if (c.instrument == InstrumentKind::plugin && ! c.plugin.id.empty() && host.find (juce::String::fromUTF8 (c.plugin.id.c_str())) == nullptr)
                addPlugin (c.plugin, c.name, "Silence: the channel keeps the plugin's settings until it is installed");
        for (std::size_t t = 0; t < song.mixer.size(); ++t)
            for (const auto& fx : song.mixer[t].effects)
                if (fx.kind == EffectKind::plugin && ! fx.plugin.id.empty() && host.find (juce::String::fromUTF8 (fx.plugin.id.c_str())) == nullptr)
                    addPlugin (fx.plugin, song.mixer[t].name.empty() ? (t == 0 ? std::string ("Master") : "Insert " + std::to_string (t)) : song.mixer[t].name,
                               "Bypassed: the slot keeps the plugin's settings until it is installed");
        // installed, but failed to load: they play silence / are bypassed
        for (const auto& line : host.getLoadFailures())
            if (std::none_of (info.plugins.begin(), info.plugins.end(), [&] (const MissingPlugin& p) { return p.name == line.toStdString(); }))
                info.plugins.push_back ({ line.toStdString(), {}, "Silence: the plugin is installed but did not load" });
        if (currentFile.existsAsFile())
            info.searchFolders.add (currentFile.getParentDirectory().getFullPathName());
    }

    if (afterOpening && info.files.empty() && info.plugins.empty())
    {
        if (fromFlp)
            showImportReport();
        return;
    }

    auto safe = juce::Component::SafePointer<MainComponent> (this);
    const auto flp = fromFlp ? importedFlpFile : juce::File();
    showMissingFilesDialog (
        std::move (info),
        [safe, flp] (const std::map<std::string, std::string>& relocations) {
            if (safe == nullptr)
                return;
            if (flp != juce::File())
            {
                // FL import: imported again with the located samples (the channels become samplers, slices are read...)
                juce::String error;
                if (! safe->engine.importFlpFile (flp, error, relocations, true))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
            }
            else
                safe->engine.editSong ([&relocations] (Song& s) { applyRelocations (s, relocations); });
            safe->updateTitle();
        },
        [safe, afterOpening, fromFlp] {
            if (safe != nullptr && afterOpening && fromFlp)
                safe->showImportReport();
        });
}

void MainComponent::exportAudio()
{
    showExportDialog (engine, currentFile.getParentDirectory(), projectName);
}

void MainComponent::save (bool askForFile)
{
    auto write = [this] (juce::File file) {
        if (! file.hasFileExtension (ProjectFile::extension))
            file = file.withFileExtension (ProjectFile::extension);

        juce::String error;
        engine.capturePluginStates(); // plugin state lives in the plugins; copy it before saving

        if (ProjectFile::save (engine.getSong(), file, error))
        {
            engine.markSaved();
            rememberRecent (file);
            currentFile = file;
            projectName = file.getFileNameWithoutExtension();
            importedSuffix = {};
            updateTitle();
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"), error);
    };

    if (! askForFile && currentFile != juce::File())
    {
        write (currentFile);
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> (tr ("Save project"), currentFile.getParentDirectory(),
                                                       juce::String ("*") + ProjectFile::extension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [write] (const juce::FileChooser& chooser) {
                                  if (chooser.getResult() != juce::File())
                                      write (chooser.getResult());
                              });
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { tr ("File"), tr ("Edit"), tr ("View"), tr ("Patterns"), tr ("Options"), tr ("Help") };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    if (index == 0)
    {
        menu.addItem (tr ("New") + shortcutText ("file.new"), [this] { newProject(); });

        juce::PopupMenu templates;
        const auto names = templateNames();
        for (int i = 0; i < (int) names.size(); ++i)
            templates.addItem (juce::String::fromUTF8 (names[(std::size_t) i].c_str()), [this, i] {
                confirmDiscard ([this, i] {
                    engine.stop();
                    engine.rewind();
                    engine.setSong (makeTemplate (i));
                    currentFile = juce::File();
                    projectName = {};
                    importedSuffix = {};
                    updateTitle();
                });
            });
        menu.addSubMenu (tr ("New from template"), templates);
        menu.addItem (tr ("Open...") + shortcutText ("file.open"), [this] { chooseFileToOpen(); });

        juce::PopupMenu recentMenu;
        for (int i = 0; i < recent.getNumFiles(); ++i)
        {
            const auto file = recent.getFile (i);
            recentMenu.addItem (file.getFileName() + "  (" + file.getParentDirectory().getFullPathName() + ")", file.existsAsFile(),
                                false, [this, file] { confirmDiscard ([this, file] { openFile (file); }); });
        }
        if (recent.getNumFiles() == 0)
            recentMenu.addItem (tr ("(none)"), false, false, [] {});
        menu.addSubMenu (tr ("Open recent"), recentMenu);

        menu.addItem (tr ("Save") + shortcutText ("file.save"), [this] { save (false); });
        menu.addItem (tr ("Save as...") + shortcutText ("file.saveAs"), [this] { save (true); });
        menu.addSeparator();
        menu.addItem (tr ("Export audio...") + shortcutText ("file.export"), [this] { exportAudio(); });
        menu.addItem (tr ("Open autosave folder"), [] {
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LYNX/autosave").startAsProcess();
        });
        menu.addSeparator();
        menu.addItem (tr ("Exit"), [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    }
    else if (index == 1)
    {
        menu.addItem (tr ("Undo") + shortcutText ("edit.undo"), engine.canUndo(), false, [this] { engine.undo(); });
        menu.addItem (tr ("Redo") + shortcutText ("edit.redo"), engine.canRedo(), false, [this] { engine.redo(); });
        menu.addItem (tr ("Undo history...") + shortcutText ("edit.history"), [this] { showHistory(); });
        menu.addSeparator();
        menu.addItem (tr ("Quick-add palette...") + shortcutText ("panel.quickAdd"), [this] { showQuickAdd(); });
        menu.addItem (tr ("Search the Browser") + shortcutText ("browser.search"), [this] {
            showPanel (Panel::browser);
            browser.focusSearch();
        });
    }
    else if (index == 2)
    {
        for (auto panel : panelOrder)
            menu.addItem (panelName (panel) + shortcutText (panelAction (panel)), true, workspace.isPanelVisible (panel), [this, panel] { togglePanel (panel); });

        menu.addSeparator();
        menu.addItem (tr ("Maximize or restore the Playlist") + shortcutText ("panel.maximisePlaylist"), [this] {
            workspace.toggleMaximised (Panel::playlist);
            refreshPanelButtons();
        });
        {
            juce::PopupMenu detach;
            for (auto panel : panelOrder)
                detach.addItem (panelName (panel), true, workspace.isDetached (panel), [this, panel] {
                    workspace.setDetached (panel, ! workspace.isDetached (panel));
                    refreshPanelButtons();
                });
            menu.addSubMenu (tr ("Detached panels (own window)"), detach);
        }
        menu.addSeparator();
        menu.addItem (tr ("Reset layout"), [this] {
            workspace.resetLayout();
            refreshPanelButtons();
        });
        menu.addItem (tr ("Save layout as..."), [this] { saveLayoutAs(); });
        menu.addSubMenu (tr ("Load layout"), layoutsMenu());
        menu.addItem (tr ("Bring back off-screen panels"), [this] { workspace.bringBackOffscreen(); });
        menu.addSeparator();
        menu.addItem (tr ("Typing keyboard as a piano") + shortcutText ("view.typingPiano"), true, typingPiano, [this] { setTypingPiano (! typingPiano); });
    }
    else if (index == 3)
    {
        menu = makePatternMenu();
    }
    else if (index == 4)
    {
        // the Settings window's pages (Settings › General gathers the app options that were menu items here)
        menu.addItem (tr ("Audio settings..."), [this] { openSettings ((int) SettingsPage::audio); });
        menu.addItem (tr ("MIDI settings..."), [this] { openSettings ((int) SettingsPage::midi); });
        menu.addItem (tr ("General settings..."), [this] { openSettings ((int) SettingsPage::general); });
        menu.addItem (tr ("File settings (search folders)..."), [this] { openSettings ((int) SettingsPage::files); });
        menu.addItem (tr ("Project settings (tempo, time signature)..."), [this] { openSettings ((int) SettingsPage::project); });
        menu.addItem (tr ("Project info..."), [this] { openSettings ((int) SettingsPage::info); });
        menu.addSeparator();
        menu.addItem (tr ("Plugin manager..."), [this] { showPluginManager(); });
        const auto scanning = engine.getPluginHost().backgroundScan().running;
        menu.addItem (scanning ? tr ("Stop the plugin scan") : tr ("Scan for plugins (VST, VST3 and CLAP)"), [this, scanning] {
            if (scanning)
                engine.getPluginHost().cancelBackgroundScan();
            else
                engine.getPluginHost().startBackgroundScan(); // progress in the hint bar; LYNX stays usable
        });
        menu.addItem (tr ("Manage shortcuts..."), [this] { showShortcutManager(); });
        menu.addSeparator();
        menu.addItem (tr ("Record input audio"), true, engine.isAudioRecordingArmed(), [this] {
            engine.setAudioRecordingArmed (! engine.isAudioRecordingArmed());
        });
        menu.addItem (tr ("Metronome") + shortcutText ("transport.metronome"), true, engine.getTransport().metronome.load(), [this] {
            metronomeButton.setToggleState (! metronomeButton.getToggleState(), juce::sendNotification);
        });
        {
            juce::PopupMenu recording;
            addRecordOptions (recording);
            menu.addSubMenu (tr ("Recording"), recording);
        }
        menu.addSubMenu (tr ("Toolbar"), makeToolbarMenu());
        menu.addSeparator();
        menu.addItem (tr ("Setup wizard (language, audio, plugin folders)..."), [this] { showSetupWizard(); });
    }
    else
    {
        menu.addItem (tr ("Shortcuts") + shortcutText ("help.shortcuts"), [this] { showHelp(); });
        menu.addItem (tr ("Missing files..."), [this] { showMissingFiles (false); });
        menu.addItem (tr ("Last FL Studio import report"), [this] {
            const auto& r = engine.getImportReport();
            if (r.converted.empty() && r.approximated.empty() && r.unsupported.empty() && r.droppedEffects.empty() && r.missingSamples == 0
                && engine.getMissingPlugins().isEmpty() && engine.getMatchedPlugins().isEmpty() && engine.getPluginsWithoutState().isEmpty()
                && engine.getPluginHost().getLoadFailures().isEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("FL Studio import"),
                                                        tr ("Nothing to report (no .flp imported in this session, or everything was converted)."));
            else
                showImportReport();
        });
        menu.addItem (tr ("About LYNX"), [] {
            auto* window = makeAboutWindow().release();
            window->enterModalState (true, nullptr, true);
            keepAboveMainWindow (*window);
        });
    }

    return menu;
}

std::unique_ptr<juce::AlertWindow> MainComponent::makeAboutWindow()
{
    struct Logo final : juce::Component
    {
        Logo() { setSize (300, 150); }
        void paint (juce::Graphics& g) override
        {
            g.drawImageWithin (logoImage(), 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::centred);
        }
    };
    struct About final : juce::AlertWindow
    {
        About()
            : juce::AlertWindow ("LYNX " JUCE_APPLICATION_VERSION_STRING,
                                 tr ("Lightweight C++/JUCE DAW with the FL Studio workflow.\n"
                                     "Independent implementation: no Image-Line code was copied."),
                                 juce::MessageBoxIconType::NoIcon)
        {
            addCustomComponent (&logo);
        }
        Logo logo;
    };
    auto window = std::make_unique<About>();
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    return std::unique_ptr<juce::AlertWindow> (window.release());
}

void MainComponent::showLanguageChoice (const juce::String& id)
{
    if (id == i18n::saved())
        return;

    i18n::save (id);
    const auto restart = [this] {
        confirmDiscard ([] {
            juce::File::getSpecialLocation (juce::File::currentExecutableFile).startAsProcess();
            juce::JUCEApplication::quit();
        });
    };

    // the message is shown in the language just chosen
    const auto table = i18n::tableText (id);
    const juce::LocalisedStrings strings (table, false);
    auto say = [&] (const char* text) { return table.isEmpty() ? juce::String::fromUTF8 (text) : strings.translate (juce::String::fromUTF8 (text)); };

    auto* window = new juce::AlertWindow (say (TR_NOOP ("Language")), say (TR_NOOP ("The new language is applied when LYNX restarts.")),
                                          juce::MessageBoxIconType::InfoIcon);
    window->addButton (say (TR_NOOP ("Restart now")), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (say (TR_NOOP ("Later")), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([restart] (int result) {
                                 if (result == 1)
                                     restart();
                             }),
                             true);
}

void MainComponent::rememberRecent (const juce::File& file)
{
    recent.addFile (file);
    PluginHost::settingsFolder().getChildFile ("recentes.txt").replaceWithText (recent.toString());
    menuItemsChanged();
}

std::unique_ptr<juce::Component> MainComponent::makeHelpContent()
{
    // shortcut sheet: sections of "keys - action" rows, in two columns
    struct Row
    {
        const char* keys;
        const char* action;
    };
    struct Section
    {
        const char* title;
        std::vector<Row> rows;
    };

    static const std::vector<Section> sections {
        { TR_NOOP ("General"),
          { { TR_NOOP ("Space"), TR_NOOP ("Play / stop (back to where playback started)") },
            { "Ctrl+Space", TR_NOOP ("Play / pause") },
            { "Ctrl+Z / Ctrl+Y", TR_NOOP ("Undo / redo") },
            { "Ctrl+N / Ctrl+O", TR_NOOP ("New project / open") },
            { "Ctrl+S / Ctrl+Shift+S", TR_NOOP ("Save / save as") },
            { "Ctrl+R", TR_NOOP ("Export audio") },
            { "F5 F6 F7 F9 F8", TR_NOOP ("Panels: Playlist, Channel Rack, Piano Roll, Mixer, Browser") },
            { "Enter", TR_NOOP ("Maximize or restore the Playlist") },
            { "L", TR_NOOP ("Pattern / song mode") },
            { "Ctrl+T", TR_NOOP ("Typing keyboard as a piano on/off") },
            { "Z..M  Q..P", TR_NOOP ("PC keyboard as a piano (selected channels)") },
            { TR_NOOP ("Numpad +/-  F4  F2"), TR_NOOP ("Next/previous pattern, next empty, rename and color") },
            { "Ctrl+Shift+Ins", TR_NOOP ("Insert an empty pattern before the current one") } } },
        { TR_NOOP ("Workspace"),
          { { TR_NOOP ("Drag a panel title"), TR_NOOP ("Move (snaps; Alt: free)") },
            { TR_NOOP ("Double-click a title"), TR_NOOP ("Maximize / restore") },
            { TR_NOOP ("Title buttons"), TR_NOOP ("Detach into a window, maximize, close") },
            { TR_NOOP ("View menu"), TR_NOOP ("Reset, save and load layouts") } } },
        { TR_NOOP ("Channel Rack and Mixer"),
          { { TR_NOOP ("Click the name"), TR_NOOP ("Open/close the editor (Alt keeps others open)") },
            { TR_NOOP ("Ctrl+click the name"), TR_NOOP ("Audition the channel") },
            { TR_NOOP ("Selector / Up, Down"), TR_NOOP ("Select (Ctrl: add, Shift: range)") },
            { "Alt+Up / Alt+Down", TR_NOOP ("Move the selected channels") },
            { "Alt+C / Alt+Del", TR_NOOP ("Clone / delete the selected channels") },
            { "Ctrl+L", TR_NOOP ("Send the selected channels to free mixer inserts") },
            { TR_NOOP ("1..0 / Ctrl+1..0"), TR_NOOP ("Mute / solo channels 1-10 (typing piano off)") },
            { TR_NOOP ("Knobs and faders"), TR_NOOP ("Drag (Ctrl: fine), wheel, Alt+click or middle-click resets") },
            { TR_NOOP ("Right-click a knob/fader"), TR_NOOP ("Type value, reset, automation, MIDI link") },
            { TR_NOOP ("Right-click Pattern / Song"), TR_NOOP ("Show the Channel Rack / Playlist") },
            { TR_NOOP ("Double-click Stop"), TR_NOOP ("All notes off") } } },
        { "Piano Roll",
          { { TR_NOOP ("Click"), TR_NOOP ("Create a note (or chord, via the selector)") },
            { TR_NOOP ("Drag"), TR_NOOP ("Move; the right edge changes the length") },
            { TR_NOOP ("Right-click"), TR_NOOP ("Delete") },
            { TR_NOOP ("Ctrl+drag"), TR_NOOP ("Select (Ctrl+click adds to the selection)") },
            { TR_NOOP ("Shift+drag"), TR_NOOP ("Copy") },
            { "Ctrl+A/C/X/V", TR_NOOP ("Select all, copy, cut, paste") },
            { "Ctrl+B / Ctrl+Q", TR_NOOP ("Duplicate / quantize") },
            { "Ctrl+M / Ctrl+P", TR_NOOP ("Mute / portamento") },
            { TR_NOOP ("Arrows"), TR_NOOP ("Move (Shift = octave/bar)") },
            { TR_NOOP ("Alt / Ctrl+wheel"), TR_NOOP ("Disable snap / zoom") } } },
        { "Playlist",
          { { TR_NOOP ("Click"), TR_NOOP ("Paint the current pattern") },
            { TR_NOOP ("Drag on the ruler"), TR_NOOP ("Set the loop (right-click clears)") },
            { TR_NOOP ("Right-click a clip name"), TR_NOOP ("Split, make unique...") },
            { TR_NOOP ("Track LED"), TR_NOOP ("Mute the track") },
            { "Ctrl+A/C/X/V  Ctrl+B", TR_NOOP ("Edit and duplicate") } } },
    };

    struct Sheet final : juce::Component
    {
        Sheet() { setSize (1180, 520); }

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            g.fillAll (colour::bg2);
            const auto columnWidth = (getWidth() - 4 * 24) / 3;
            int column = 0, y = 20;

            for (std::size_t i = 0; i < sections.size(); ++i)
            {
                if (i == 2 || i == 3) // next column
                {
                    ++column;
                    y = 20;
                }

                const auto x = 24 + column * (columnWidth + 24);
                const auto& section = sections[i];
                g.setColour (colour::accent);
                g.fillRoundedRectangle ((float) x, (float) y + 4.0f, 3.0f, 12.0f, 1.5f);
                g.setColour (colour::textPrimary);
                g.setFont (font (type::title, true));
                g.drawText (tr (section.title), x + 10, y, columnWidth - 10, 20, juce::Justification::centredLeft);
                y += 28;

                for (const auto& row : section.rows)
                {
                    const auto keys = tr (row.keys);
                    g.setFont (font (type::small, true));
                    const auto keyWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), keys)) + 14;
                    g.setColour (colour::bg4);
                    g.fillRoundedRectangle ((float) x, (float) y + 1.0f, (float) keyWidth, 18.0f, radius::sm);
                    g.setColour (colour::textPrimary);
                    g.drawText (keys, x, y + 1, keyWidth, 18, juce::Justification::centred);
                    g.setColour (colour::textSecondary);
                    g.setFont (font (type::body));
                    g.drawFittedText (tr (row.action), x + keyWidth + 10, y, columnWidth - keyWidth - 10, 20, juce::Justification::centredLeft, 1, 0.85f);
                    y += 24;
                }

                y += 14;
            }
        }
    };

    return std::make_unique<Sheet>();
}

void MainComponent::showHelp()
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (makeHelpContent().release());
    options.dialogTitle = tr ("LYNX shortcuts");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

void MainComponent::showSetupWizard()
{
    lynx::showSetupWizard (engine, [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::String& language) {
        if (safe != nullptr)
            safe->showLanguageChoice (language);
    });
}

void MainComponent::showImportReport()
{
    // summary of what the .flp had that LYNX doesn't reproduce exactly (only when there is something to say)
    const auto& r = engine.getImportReport();
    const auto& missing = engine.getMissingPlugins();
    auto list = [] (const std::map<std::string, int>& items) {
        std::vector<std::pair<int, std::string>> sorted;
        for (const auto& [name, count] : items)
            sorted.push_back ({ count, name });
        std::sort (sorted.rbegin(), sorted.rend());
        juce::StringArray lines;
        for (const auto& [count, name] : sorted)
            lines.add ("  " + juce::String::fromUTF8 (name.c_str()) + (count > 1 ? " (" + juce::String (count) + ")" : juce::String()));
        return lines.joinIntoString ("\n");
    };

    juce::String text;

    if (! r.approximated.empty())
        text << tr ("Approximated by the built-in synth:") << "\n" << list (r.approximated) << "\n\n";
    if (! r.unsupported.empty())
        text << tr ("No equivalent (played with a synth preset):") << "\n" << list (r.unsupported) << "\n\n";
    if (const auto& matched = engine.getMatchedPlugins(); ! matched.isEmpty())
        text << tr ("Plugins found on this PC:") << "\n  " << matched.joinIntoString ("\n  ") << "\n\n";
    if (! missing.isEmpty())
        text << tr ("Plugins not installed on this PC (or in a folder LYNX doesn't scan; Options > Setup wizard):") << "\n  "
             << missing.joinIntoString ("\n  ") << "\n\n";
    if (const auto& lost = engine.getPluginsWithoutState(); ! lost.isEmpty())
        text << tr ("Plugins found but playing their default settings (the project saved them for the other format, VST2/VST3):") << "\n  "
             << lost.joinIntoString ("\n  ") << "\n\n";
    if (const auto failed = engine.getPluginHost().getLoadFailures(); ! failed.isEmpty())
        text << tr ("Plugins installed but not loaded (silent):") << "\n  " << failed.joinIntoString ("\n  ") << "\n\n";
    if (! r.droppedEffects.empty())
        text << tr ("Effects with no equivalent (left out):") << "\n" << list (r.droppedEffects) << "\n\n";
    if (r.missingSamples > 0)
        text << tr ("Samples missing on this machine: {0}", { juce::String (r.missingSamples) }) << "\n";
    if (r.relocatedSamples > 0)
        text << tr ("Samples found in another folder than the project says (FL Studio's folders or next to the project): {0}",
                    { juce::String (r.relocatedSamples) }) << "\n";

    if (text.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("FL Studio import"), text.trimEnd());
}

void MainComponent::showAudioSettings()
{
    openSettings ((int) SettingsPage::audio);
}
} // namespace lynx
