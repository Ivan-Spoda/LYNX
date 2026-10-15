#include "ui/MainComponent.h"

#include "ui/HistoryPanel.h"
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

// JUCE key event -> LYNX key map chord
keys::Chord toChord (const juce::KeyPress& key)
{
    keys::Chord c;
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();
    c.mods = (std::uint8_t) ((mods.isCommandDown() ? keys::ctrl : 0) | (mods.isShiftDown() ? keys::shift : 0) | (mods.isAltDown() ? keys::alt : 0));

    struct Pair
    {
        int juceCode, key;
    };
    const Pair specials[] = {
        { juce::KeyPress::spaceKey, keys::space },       { juce::KeyPress::returnKey, keys::enter },     { juce::KeyPress::escapeKey, keys::escape },
        { juce::KeyPress::tabKey, keys::tab },           { juce::KeyPress::backspaceKey, keys::backspace }, { juce::KeyPress::deleteKey, keys::del },
        { juce::KeyPress::insertKey, keys::insert },     { juce::KeyPress::upKey, keys::up },             { juce::KeyPress::downKey, keys::down },
        { juce::KeyPress::leftKey, keys::left },         { juce::KeyPress::rightKey, keys::right },       { juce::KeyPress::pageUpKey, keys::pageUp },
        { juce::KeyPress::pageDownKey, keys::pageDown }, { juce::KeyPress::homeKey, keys::home },         { juce::KeyPress::endKey, keys::end },
        { juce::KeyPress::numberPadAdd, keys::numpadPlus }, { juce::KeyPress::numberPadSubtract, keys::numpadMinus },
        { juce::KeyPress::F1Key, keys::f1 },   { juce::KeyPress::F2Key, keys::f2 },   { juce::KeyPress::F3Key, keys::f3 },
        { juce::KeyPress::F4Key, keys::f4 },   { juce::KeyPress::F5Key, keys::f5 },   { juce::KeyPress::F6Key, keys::f6 },
        { juce::KeyPress::F7Key, keys::f7 },   { juce::KeyPress::F8Key, keys::f8 },   { juce::KeyPress::F9Key, keys::f9 },
        { juce::KeyPress::F10Key, keys::f10 }, { juce::KeyPress::F11Key, keys::f11 }, { juce::KeyPress::F12Key, keys::f12 },
    };

    for (const auto& p : specials)
        if (code == p.juceCode)
        {
            c.key = p.key;
            return c;
        }

    if (code > ' ' && code < 0x7f)
        c.key = (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);

    return c;
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
    browser.onOpenProject = [this] (const juce::File& file) { confirmDiscard ([this, file] { openFile (file); }); };
    browser.onLoadIntoChannel = [this] (const juce::File& file) { return rack.loadSampleIntoSelected (file); };
    logo.setImage (logoImage(), juce::RectanglePlacement::centred);
    logo.setTooltip ("LYNX " JUCE_APPLICATION_VERSION_STRING);
    addAndMakeVisible (logo);

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

    // transport: Play toggles play / stop-and-back; Stop returns to the start; double-click Stop = all notes off
    playButton.setTooltip (tr ("Play; pressed again, stops and goes back to where it started") + " (Space; Ctrl+Space pauses)");
    playButton.onClick = [this] { playStop(); };
    stopButton.setTooltip (tr ("Stop and return to the start (double-click: all notes off)"));
    stopButton.onClick = [this] { stopAndRewind(); };
    stopButton.onDoubleClick = [this] { engine.panic(); };
    recordButton.setOnColour (theme::colour::record);
    recordButton.onClick = [this] { engine.setRecording (! engine.isRecording()); };
    recordMenuButton.setTooltip (tr ("What to record"));
    recordMenuButton.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem (tr ("Notes (MIDI and PC keyboard) into the current pattern"), false, true, [] {});
        menu.addItem (tr ("Audio from the sound card input (becomes a Playlist clip)"), true, engine.isAudioRecordingArmed(), [this] {
            engine.setAudioRecordingArmed (! engine.isAudioRecordingArmed());

            if (engine.getLastRecordingError().isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Audio input"),
                                                        engine.getLastRecordingError());
        });
        menu.addSeparator();
        menu.addItem (tr ("Open recordings folder"), [] {
            juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Recordings").createDirectory();
            juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Recordings").startAsProcess();
        });
        menu.addSubMenu (tr ("Recording"), makeRecordOptionsMenu());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordMenuButton));
    };

    // Pattern / Song: a two-segment switch (L toggles); right-click shows the matching panel
    patternModeButton.setText (tr ("Pattern"));
    songModeButton.setText (tr ("Song"));
    patternModeButton.setCorners (true, false);
    songModeButton.setCorners (false, true);
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

    settingsButton.setTooltip (tr ("Audio settings"));
    settingsButton.onClick = [this] { showAudioSettings(); };

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
    swingSlider.setTooltip (tr ("Swing"));
    swingLabel.setText (tr ("Swing"), juce::dontSendNotification);
    swingLabel.setFont (theme::font (theme::type::caption, true));
    swingLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    swingLabel.setJustificationType (juce::Justification::centredRight);

    patternMenuButton.setTooltip (tr ("Pattern: rename, color, clone, insert, split, length, delete"));
    patternMenuButton.onClick = [this] { showPatternMenu(); };

    positionLabel.setFont (theme::monoFont (17.0f));
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);
    positionLabel.setTooltip (tr ("Position: bar : beat : step"));
    positionLabel.setText ("1 : 1 : 1", juce::dontSendNotification);
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setFont (theme::font (theme::type::small));
    cpuLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    cpuLabel.setTooltip (tr ("Audio processing load"));

    // the workspace panels
    rack.onOpenPianoRoll = [this] { openPianoRoll(); };
    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (true, true);
    workspace.setContent (Panel::playlist, playlistArea, "Playlist", Icon::playlist);
    workspace.setContent (Panel::channelRack, rackViewport, "Channel Rack", Icon::channelRack);
    workspace.setContent (Panel::pianoRoll, pianoRoll, "Piano Roll", Icon::pianoRoll);
    workspace.setContent (Panel::mixer, mixer, "Mixer", Icon::mixer);
    workspace.setContent (Panel::browser, browser, "Browser", Icon::browser);
    workspace.setTitleTooltip (Panel::channelRack, tr ("Shift+wheel here changes the pattern"));
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
    picker.onOpenPattern = [this] { showPanel (Panel::channelRack); };
    ui::setHint (pianoRoll, tr ("Piano Roll: click adds a note, drag moves, right-click deletes, Ctrl+drag selects"));
    ui::setHint (playlist, tr ("Playlist: click paints the current pattern, right-click deletes, Ctrl+drag selects"));

    // recent files (stored in %APPDATA%\LYNX)
    recent.setMaxNumberOfItems (12);
    recent.restoreFromString (PluginHost::settingsFolder().getChildFile ("recentes.txt").loadFileAsString());
    addAndMakeVisible (menuBar);

    for (auto* c : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &playButton, &stopButton, &recordButton, &recordMenuButton,
                                                             &patternModeButton, &songModeButton, &metronomeButton, &typingPianoButton, &tempoDisplay, &patternBox, &newPatternButton,
                                                             &patternMenuButton, &swingLabel, &swingSlider, &settingsButton, &positionLabel, &cpuLabel, &hintBar, &workspace, &scope })
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

    for (auto* b : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &playButton, &stopButton, &recordButton, &recordMenuButton,
                                                             &patternModeButton, &songModeButton, &metronomeButton, &typingPianoButton, &newPatternButton, &patternMenuButton, &settingsButton })
        b->setWantsKeyboardFocus (false);

    for (auto* b : { &newButton, &openButton, &saveButton, &exportButton, &recordMenuButton, &newPatternButton, &patternMenuButton, &settingsButton })
        b->setFlat (true);

    ui::installSliderContract();
    addAndMakeVisible (gridBox); // global grid (editors)
    engine.addChangeListener (this);
    setTypingPiano (typingPiano);
    refreshToolbar();
    refreshPanelButtons();

    fittedChannelCount = (int) engine.getSong().channels.size();
    hintBar.setHint ({ idleHint(), {}, false, false });
    setWantsKeyboardFocus (true);
    setSize (1180, 642);
    startTimerHz (30);
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
    engine.removeChangeListener (this);
    stopTimer();
}

void MainComponent::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg1);

    // toolbar
    const auto bar = juce::Rectangle<int> (0, menuHeight, getWidth(), toolbarHeight);
    g.setColour (colour::bg3);
    g.fillRect (bar);
    g.setColour (colour::border);
    g.fillRect (bar.withHeight (1).withY (bar.getBottom() - 1));

    for (auto x : dividers)
        g.fillRect (x, bar.getY() + 12, 1, bar.getHeight() - 24);

    // transport position: an inset display
    if (! positionLabel.getBounds().isEmpty())
    {
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (positionLabel.getBounds().toFloat(), radius::md);
    }

    // status bar
    const auto status = getLocalBounds().removeFromBottom (statusHeight);
    g.setColour (colour::bg1);
    g.fillRect (status);
    g.setColour (colour::border);
    g.fillRect (status.withHeight (1));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    menuBar.setBounds (area.removeFromTop (menuHeight));
    auto toolbar = area.removeFromTop (toolbarHeight).reduced (10, 9);
    dividers.clear();
    const auto h = toolbar.getHeight(); // 32

    // the settings button is pinned to the right; groups that don't fit in a narrow window are hidden
    settingsButton.setBounds (toolbar.removeFromRight (h));
    toolbar.removeFromRight (8);

    auto place = [&toolbar] (juce::Component& c, int width, int gapAfter = 2) {
        const auto fits = width <= toolbar.getWidth();
        c.setVisible (fits);
        c.setBounds (fits ? toolbar.removeFromLeft (width) : juce::Rectangle<int>());
        toolbar.removeFromLeft (juce::jmin (gapAfter, toolbar.getWidth()));
    };
    auto divide = [&] {
        if (toolbar.getWidth() < 15 + h)
            return;
        toolbar.removeFromLeft (7);
        dividers.push_back (toolbar.getX());
        toolbar.removeFromLeft (8);
    };

    // below ~1440 px the file buttons (also in the File menu and on Ctrl+N/O/S/R) and the panel icons
    // (F5..F9, View menu) give their room to the editors' grid and the pattern group
    const auto wide = getWidth() >= 1440;
    place (logo, h, 6);
    divide();
    place (newButton, wide ? h : 100000, wide ? 2 : 0);
    place (openButton, wide ? h : 100000, wide ? 2 : 0);
    place (saveButton, wide ? h : 100000, wide ? 2 : 0);
    place (exportButton, wide ? h : 100000, 0);
    if (wide)
        divide();
    place (playButton, h + 6);
    place (stopButton, h + 6);
    place (recordButton, h + 6, 0);
    place (recordMenuButton, 18, 0);
    divide();
    place (patternModeButton, patternModeButton.getIdealWidth (h), 1);
    place (songModeButton, songModeButton.getIdealWidth (h), 0);
    divide();
    place (metronomeButton, h, 2);
    place (typingPianoButton, h, 8);
    place (tempoDisplay, 112, 0);
    const auto showPattern = toolbar.getWidth() >= 15 + 150 + 2 + 2 * h; // the pattern group shows whole or not at all
    if (showPattern)
        divide();
    place (patternBox, showPattern ? 150 : 100000, 2);
    place (newPatternButton, showPattern ? h : 100000, 0);
    place (patternMenuButton, showPattern ? h : 100000, 0);

    const auto swingWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (theme::type::caption, true)), swingLabel.getText())) + 4;
    const auto showSwing = toolbar.getWidth() >= 15 + swingWidth + 4 + h;
    if (showSwing)
        divide();
    place (swingLabel, showSwing ? swingWidth : 100000, 4);
    place (swingSlider, showSwing ? h : 100000, 0);

    // global grid of the Piano Roll and Playlist (shown whole or not at all)
    const auto showGrid = toolbar.getWidth() >= 15 + 130 + 132; // before the scope, which yields its room
    if (showGrid)
        divide();
    place (gridBox, showGrid ? 130 : 100000, 0);

    // panel shortcuts (F5..F9), pinned next to the settings button when there is room
    {
        const auto panelsWidth = workspace::panelCount * (h - 4) + 15;
        const auto showPanels = wide && toolbar.getWidth() >= panelsWidth + 132;
        auto right = showPanels ? toolbar.removeFromRight (panelsWidth) : juce::Rectangle<int>();

        if (showPanels)
        {
            right.removeFromRight (7);
            dividers.push_back (right.getRight() + 3);
        }

        for (auto panel : panelOrder)
        {
            auto& b = panelButtons[(std::size_t) panel];
            b.setVisible (showPanels);
            b.setBounds (showPanels ? right.removeFromLeft (h - 4) : juce::Rectangle<int>());
        }
    }

    toolbar.removeFromLeft (juce::jmin (8, toolbar.getWidth()));

    if (toolbar.getWidth() > 210)
        scope.setBounds (toolbar.removeFromRight (juce::jmin (180, toolbar.getWidth() - 132)));
    else
        scope.setBounds ({});

    toolbar.removeFromRight (juce::jmin (8, toolbar.getWidth()));
    positionLabel.setBounds (toolbar.getWidth() >= 110 ? toolbar.removeFromRight (juce::jmin (toolbar.getWidth(), 124)) : juce::Rectangle<int>());

    auto status = area.removeFromBottom (statusHeight).reduced (10, 0);
    cpuLabel.setBounds (status.removeFromRight (90));
    hintBar.setBounds (status);

    workspace.setBounds (area);
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
    const auto chord = toChord (key);

    // 1. the typing piano (its keys win over plain-letter shortcuts while it is on)
    if (keys::KeyMap::playsNote (chord, typingPiano))
    {
        const auto note = keys::typingPianoNote (chord.key);

        if (heldKeys.find (key.getKeyCode()) == heldKeys.end())
        {
            const auto channels = engine.getSelectedChannels();
            heldKeys[key.getKeyCode()] = channels;

            for (auto c : channels)
                engine.sendLiveNote (c, note, 0.8f, true, true);
        }
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
    }
    else if (is ("record.waitForInput"))
    {
        auto o = engine.getRecordOptions();
        o.waitForInput = ! o.waitForInput;
        engine.setRecordOptions (o);
    }
    else if (is ("rack.graphEditor")) rack.toggleGraphLanes();
    else if (is ("rack.rotateLeft")) rack.rotateSelected (-1);
    else if (is ("rack.rotateRight")) rack.rotateSelected (1);
    else if (is ("browser.previous")) browser.step (-1, false);
    else if (is ("browser.next")) browser.step (1, false);
    else if (is ("browser.swapPrevious")) browser.step (-1, true);
    else if (is ("browser.swapNext")) browser.step (1, true);
    else if (is ("browser.load")) browser.loadSelected();
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
    // JUCE has no per-key "keyReleased"; check which held keys were released.
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            const auto note = keys::typingPianoNote ((int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) it->first));

            for (auto c : it->second)
                engine.sendLiveNote (c, note, 0.0f, false, true);

            it = heldKeys.erase (it);
        }
        else
            ++it;
    }

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
    for (const auto& [code, channels] : heldKeys)
        for (auto c : channels)
            engine.sendLiveNote (c, keys::typingPianoNote ((int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code)), 0.0f, false);

    heldKeys.clear();
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

void MainComponent::editKeyMap()
{
    const auto file = PluginHost::settingsFolder().getChildFile ("keys.json");

    if (! file.existsAsFile())
        file.replaceWithText (juce::String (keys::KeyMap().toJson()));

    file.startAsProcess();
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
    }
}

void MainComponent::saveUiSettings()
{
    if (! persistent)
        return;

    uiSettingsFile().replaceWithText ("typingPiano=" + juce::String (typingPiano ? 1 : 0) + "\nkeepEditorsOpen="
                                      + juce::String (ChannelRackComponent::keepEditorsOpen ? 1 : 0) + "\n");
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
    engine.rewind();
    playStartTicks = 0.0;
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

juce::PopupMenu MainComponent::makeRecordOptionsMenu()
{
    const auto o = engine.getRecordOptions();
    auto set = [this] (std::function<void (AudioEngine::RecordOptions&)> change) {
        auto options = engine.getRecordOptions();
        change (options);
        engine.setRecordOptions (options);
    };

    juce::PopupMenu menu;
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
    return menu;
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
    g.setColour (colour::bg0);
    g.fillRoundedRectangle (area, radius::md);

    if (dragging)
    {
        g.setColour (colour::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (area.reduced (0.5f), radius::md, 1.0f);
    }

    const auto bpm = engine.getSong().bpm;
    const auto whole = juce::String ((int) std::floor (bpm + 1e-9));
    const auto fraction = juce::String (bpm - std::floor (bpm + 1e-9), 3).fromFirstOccurrenceOf (".", false, false).paddedRight ('0', 3).substring (0, 3);

    const juce::Font big (monoFont (19.0f)), small (monoFont (12.0f)), caption (font (type::caption, true));
    const auto wholeWidth = juce::GlyphArrangement::getStringWidth (big, whole);
    const auto fracWidth = juce::GlyphArrangement::getStringWidth (small, "." + fraction);
    const auto unitWidth = juce::GlyphArrangement::getStringWidth (caption, "BPM");
    auto x = area.getCentreX() - (wholeWidth + fracWidth + 6.0f + unitWidth) / 2.0f;
    const auto baseline = area.getCentreY() + 6.0f;

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
                          isOnDecimals (p.x) ? tr ("drag the decimals for fine changes, wheel, double-click to type")
                                             : tr ("drag the whole number for BPM (Ctrl: fine), wheel, double-click to type"));
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

// --- hint strip ----------------------------------------------------------------------------------

void MainComponent::HintBar::setHint (const ui::Hint& hint)
{
    if (hint.text != current.text || hint.value != current.value || hint.automatable != current.automatable || hint.hasMenu != current.hasMenu)
    {
        current = hint;
        repaint();
    }
}

void MainComponent::HintBar::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds();

    // capability markers (dim when not applicable, so the text does not jump)
    if (current.automatable || current.hasMenu)
    {
        icons::draw (g, Icon::automate, r.removeFromLeft (18).toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                     current.automatable ? colour::automation : colour::textDisabled.withAlpha (0.35f));
        r.removeFromLeft (2);
        icons::draw (g, Icon::rightClick, r.removeFromLeft (18).toFloat().withSizeKeepingCentre (14.0f, 14.0f),
                     current.hasMenu ? colour::textSecondary : colour::textDisabled.withAlpha (0.35f));
        r.removeFromLeft (8);
    }

    if (current.value.isNotEmpty())
    {
        const juce::Font valueFont (font (type::small, true));
        const auto w = juce::jmin (r.getWidth() / 3, juce::roundToInt (juce::GlyphArrangement::getStringWidth (valueFont, current.value)) + 16);
        auto valueArea = r.removeFromRight (w).reduced (0, 4);
        g.setColour (colour::bg3);
        g.fillRoundedRectangle (valueArea.toFloat(), radius::sm);
        g.setColour (colour::textPrimary);
        g.setFont (valueFont);
        g.drawFittedText (current.value, valueArea, juce::Justification::centred, 1, 0.8f);
        r.removeFromRight (8);
    }

    g.setColour (colour::textSecondary);
    g.setFont (font (type::small));
    g.drawFittedText (current.text, r, juce::Justification::centredLeft, 1, 0.9f);
}

juce::String MainComponent::idleHint() const
{
    return tr ("F1 shortcuts   F5-F9 panels   Space play/stop   Ctrl+T typing piano ({0})", { typingPiano ? tr ("on") : tr ("off") });
}

void MainComponent::Scope::update()
{
    if (! isShowing() || getWidth() <= 0)
        return;

    engine.copyScope (samples.data(), (int) samples.size());
    repaint();
}

void MainComponent::Scope::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (theme::colour::bg0);
    g.fillRoundedRectangle (area, theme::radius::md);
    g.setColour (theme::colour::border);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX() + 4.0f, area.getRight() - 4.0f);

    juce::Path wave;
    const auto mid = area.getCentreY();
    const auto half = area.getHeight() * 0.45f;

    for (int x = 0; x < (int) area.getWidth(); ++x)
    {
        const auto index = (std::size_t) (x * (int) samples.size() / juce::jmax (1, (int) area.getWidth()));
        const auto y = mid - juce::jlimit (-1.0f, 1.0f, samples[index]) * half;

        if (x == 0)
            wave.startNewSubPath (area.getX(), y);
        else
            wave.lineTo (area.getX() + (float) x, y);
    }

    g.setColour (theme::colour::accent.withAlpha (0.9f));
    g.strokePath (wave, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MainComponent::timerCallback()
{
    scope.update();
    updateRecordQuantise();

    if (engine.isModified() != lastModified)
    {
        lastModified = engine.isModified();
        updateTitle();
    }

    // autosave every 2 minutes when there are changes
    if (++autosaveTicks >= 30 * 120)
    {
        autosaveTicks = 0;

        if (engine.isModified())
            autosave();
    }

    auto& transport = engine.getTransport();
    const auto& song = engine.getSong();
    const auto loop = song.mode == PlayMode::song ? song.songLength()
                                                   : song.patterns[(std::size_t) song.currentPattern].effectiveLength (song.beatsPerBar);
    const auto ticks = std::fmod (transport.positionTicks.load(), (double) juce::jmax<std::int64_t> (1, loop));
    const auto beats = ticks / ppq;
    int bar = 0, beat = 0;

    if (song.mode == PlayMode::song) // in song mode the Playlist ruler applies (with time signature changes)
    {
        const auto current = song.barAt ((std::int64_t) ticks);
        bar = current.number;
        beat = (int) ((ticks - (double) current.tick) / ppq) + 1;
    }
    else
    {
        const auto beatsPerBar = juce::jmax (1, transport.beatsPerBar.load());
        bar = (int) (beats / beatsPerBar) + 1;
        beat = (int) std::fmod (beats, (double) beatsPerBar) + 1;
    }

    const auto step = (int) (std::fmod (beats, 1.0) * stepsPerBeat) + 1;

    positionLabel.setText (juce::String::formatted ("%d : %d : %d", bar, beat, step), juce::dontSendNotification);

    if (! heldKeys.empty()) // keys released while a detached panel window had the focus
        keyStateChanged (false);

    // hint strip: the control under the mouse (live while it is dragged); MIDI learn has priority
    {
        ui::Hint hint;

        if (engine.isLearningMidi())
            hint.text = tr ("Move a knob/fader on the MIDI controller to link it to the parameter...");
        else if (isShowing())
        {
            const auto source = juce::Desktop::getInstance().getMainMouseSource();

            if (auto* under = source.getComponentUnderMouse())
                hint = ui::hintFor (under, under->getLocalPoint (nullptr, source.getScreenPosition()).roundToInt());
        }

        if (hint.isEmpty())
            hint.text = idleHint();

        hintBar.setHint (hint);
    }

    const auto playing = transport.playing.load();
    playButton.setToggleState (playing, juce::dontSendNotification);
    cpuLabel.setText ("CPU " + juce::String (engine.getDeviceManager().getCpuUsage() * 100.0, 1) + "%",
                      juce::dontSendNotification);

    // If a dialog stole focus, give it back so the PC keyboard keeps playing (unless a detached panel has it).
    if (! hasKeyboardFocus (true) && isShowing() && juce::Process::isForegroundProcess()
        && juce::Component::getCurrentlyModalComponent() == nullptr && ! workspace.isDetachedWindowActive())
        grabKeyboardFocus();
}

void MainComponent::updateTitle()
{
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
    const auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LYNX/autosave");
    folder.createDirectory();
    const auto name = projectName.isNotEmpty() ? juce::File::createLegalFileName (projectName) : juce::String ("untitled");
    juce::String error;
    engine.capturePluginStates();
    ProjectFile::save (engine.getSong(), folder.getChildFile (name + ProjectFile::extension), error);
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
        if (! engine.importFlpFile (file, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not open"), error);
            return;
        }

        currentFile = juce::File(); // imported: "Save" asks where to write the .lynx
        projectName = file.getFileNameWithoutExtension();
        rememberRecent (file);
        importedSuffix = " " + tr ("(imported from FL Studio)");
        updateTitle();

        showImportReport();
        return;
    }

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
    projectName = file.getFileNameWithoutExtension();
    importedSuffix = {};
    rememberRecent (file);
    updateTitle();
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
        menu.addItem (tr ("Audio settings..."), [this] { showAudioSettings(); });
        menu.addItem (tr ("Scan for plugins (VST3 and CLAP)..."), [this] {
            scanPluginsWithProgress (engine, [] (int found) {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Plugins"),
                                                        found == 1 ? tr ("1 new plugin found.") : tr ("{0} new plugins found.", { juce::String (found) }));
            });
        });
        menu.addItem (tr ("Run plugins in a separate process (protects LYNX from crashes)"), true, pluginSandboxEnabled(), [] {
            setPluginSandboxEnabled (! pluginSandboxEnabled());
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Plugins"),
                                                    pluginSandboxEnabled()
                                                        ? tr ("On: plugins opened from now on run in a separate process. "
                                                              "If one crashes, it goes silent and LYNX keeps running. Reopen the project to apply this to plugins already open.")
                                                        : tr ("Off: plugins opened from now on run inside LYNX."));
        });
        menu.addItem (tr ("Record input audio"), true, engine.isAudioRecordingArmed(), [this] {
            engine.setAudioRecordingArmed (! engine.isAudioRecordingArmed());
        });
        menu.addItem (tr ("Metronome") + shortcutText ("transport.metronome"), true, engine.getTransport().metronome.load(), [this] {
            metronomeButton.setToggleState (! metronomeButton.getToggleState(), juce::sendNotification);
        });
        menu.addSubMenu (tr ("Recording"), makeRecordOptionsMenu());
        menu.addItem (tr ("Keep channel editors open (a channel button click doesn't close the previous editor)"), true,
                      ChannelRackComponent::keepEditorsOpen, [this] {
                          ChannelRackComponent::keepEditorsOpen = ! ChannelRackComponent::keepEditorsOpen;
                          saveUiSettings();
                      });
        menu.addSeparator();
        menu.addItem (tr ("Edit keyboard shortcuts (keys.json)..."), [this] { editKeyMap(); });
        menu.addItem (tr ("Reload keyboard shortcuts"), [this] { loadKeyMap(); });
        menu.addSeparator();

        juce::PopupMenu languages;
        for (const auto& language : i18n::languages())
        {
            const juce::String id (language.id);
            languages.addItem (juce::String::fromUTF8 (language.nativeName), true, i18n::saved() == id, [this, id] { showLanguageChoice (id); });
        }
        menu.addSubMenu (tr ("Language"), languages);
    }
    else
    {
        menu.addItem (tr ("Shortcuts") + shortcutText ("help.shortcuts"), [this] { showHelp(); });
        menu.addItem (tr ("Last FL Studio import report"), [this] {
            const auto& r = engine.getImportReport();
            if (r.converted.empty() && r.approximated.empty() && r.unsupported.empty() && r.droppedEffects.empty() && r.missingSamples == 0
                && engine.getMissingPlugins().isEmpty())
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
    if (! missing.isEmpty())
        text << tr ("Plugins not found (Options > Scan for plugins):") << "\n  " << missing.joinIntoString ("\n  ") << "\n\n";
    if (! r.droppedEffects.empty())
        text << tr ("Effects with no equivalent (left out):") << "\n" << list (r.droppedEffects) << "\n\n";
    if (r.missingSamples > 0)
        text << tr ("Samples missing on this machine: {0}", { juce::String (r.missingSamples) }) << "\n";

    if (text.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("FL Studio import"), text.trimEnd());
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(), 0, 0, 2, 2, false, false, true, false);
    selector->setSize (500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = tr ("Audio settings");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}
} // namespace lynx
