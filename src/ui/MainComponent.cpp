#include "ui/MainComponent.h"
#include "ui/WindowOwner.h"

#include "engine/PluginSandbox.h"

#include "engine/Text.h"

#include "core/Templates.h"
#include "engine/MidiFileIo.h"
#include "engine/ProjectFile.h"
#include "ui/AutomationUi.h"
#include "ui/ChannelSettings.h"
#include "ui/ExportDialog.h"
#include "ui/ParametricEqEditor.h"
#include "ui/PluginWindow.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// PC keyboard as a piano, in FL's layout: Z S X D C V G B H N J M = C5..B5,
// Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P = C6..E7.
int keyCodeToNote (int keyCode)
{
    static const char lower[] = "ZSXDCVGBHNJM";
    static const char upper[] = "Q2W3ER5T6Y7UI9O0P";
    const auto c = (char) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) keyCode);

    for (int i = 0; lower[i] != 0; ++i)
        if (lower[i] == c)
            return 60 + i;

    for (int i = 0; upper[i] != 0; ++i)
        if (upper[i] == c)
            return 72 + i;

    return -1;
}

constexpr int rackTab = 0;
constexpr int pianoRollTab = 1;
constexpr int playlistTab = 2;
constexpr int mixerTab = 3;
} // namespace

MainComponent::MainComponent (AudioEngine& e)
    : engine (e),
      rack (e),
      pianoRoll (e),
      playlist (e),
      mixer (e),
      browser (e)
{
    browser.onOpenProject = [this] (const juce::File& file) { confirmDiscard ([this, file] { openFile (file); }); };
    addAndMakeVisible (browser);
    logo.setImage (logoImage(), juce::RectanglePlacement::centred);
    logo.setTooltip ("LYNX " JUCE_APPLICATION_VERSION_STRING);
    addAndMakeVisible (logo);

    newButton.setTooltip (tr ("New project") + " (Ctrl+N)");
    newButton.onClick = [this] { newProject(); };
    openButton.setTooltip (tr ("Open project") + " (Ctrl+O)");
    openButton.onClick = [this] { chooseFileToOpen(); };
    saveButton.setTooltip (tr ("Save project") + " (Ctrl+S)");
    saveButton.onClick = [this] { save (false); };
    exportButton.setTooltip (tr ("Export audio: WAV, FLAC or OGG") + " (Ctrl+R)");
    exportButton.onClick = [this] { exportAudio(); };

    playButton.setTooltip (tr ("Play / pause") + " (Space)");
    playButton.onClick = [this] { togglePlay(); };
    stopButton.setTooltip (tr ("Stop and return to the start"));
    stopButton.onClick = [this]
    {
        engine.stop();
        engine.rewind();
    };
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
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordMenuButton));
    };

    // Pattern / Song: a two-segment switch (L toggles)
    patternModeButton.setText (tr ("Pattern"));
    songModeButton.setText (tr ("Song"));
    patternModeButton.setCorners (true, false);
    songModeButton.setCorners (false, true);
    for (auto* b : { &patternModeButton, &songModeButton })
    {
        b->setOnColour (theme::colour::bg6);
        b->setIconColourWhenOn (theme::colour::textPrimary);
    }
    patternModeButton.setTooltip (tr ("Pattern mode: plays the current pattern in a loop") + " (L)");
    songModeButton.setTooltip (tr ("Song mode: plays the Playlist arrangement") + " (L)");
    patternModeButton.onClick = [this] {
        if (engine.getSong().mode != PlayMode::pattern)
            toggleMode();
    };
    songModeButton.onClick = [this] {
        if (engine.getSong().mode != PlayMode::song)
            toggleMode();
    };

    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setOnColour (theme::colour::bg6);
    metronomeButton.setIconColourWhenOn (theme::colour::accent);
    metronomeButton.setTooltip (tr ("Metronome"));
    metronomeButton.onClick = [this] { engine.getTransport().metronome.store (metronomeButton.getToggleState()); };
    settingsButton.setTooltip (tr ("Audio settings"));
    settingsButton.onClick = [this] { showAudioSettings(); };

    bpmSlider.setRange (minTempo, maxTempo, 0.1);
    bpmSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    bpmSlider.addMouseListener (this, true);
    bpmSlider.setTooltip (tr ("Tempo (drag or type; right-click: tempo automation)"));
    bpmSlider.setValue (engine.getSong().bpm, juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { engine.editSong ([v = bpmSlider.getValue()] (Song& s) { s.bpm = v; }); };
    bpmSlider.setColour (juce::Slider::textBoxTextColourId, theme::colour::textPrimary);
    bpmSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 32);

    patternBox.setTooltip (tr ("Current pattern"));
    patternBox.onChange = [this]
    {
        const auto index = patternBox.getSelectedItemIndex();

        if (index >= 0 && index != engine.getSong().currentPattern)
            engine.editSong ([index] (Song& s) { s.currentPattern = index; }, false);
    };

    newPatternButton.setTooltip (tr ("New pattern"));
    newPatternButton.onClick = [this]
    {
        engine.editSong ([] (Song& s) {
            auto& p = s.patterns.emplace_back();
            p.name = "Pattern " + std::to_string (s.patterns.size());
            s.currentPattern = (int) s.patterns.size() - 1;
        });
    };

    swingSlider.setRange (0.0, 1.0, 0.01);
    swingSlider.setDoubleClickReturnValue (true, 0.0);
    swingSlider.setValue (engine.getSong().swing, juce::dontSendNotification);
    swingSlider.onValueChange = [this] {
        engine.editSong ([v = (float) swingSlider.getValue()] (Song& s) { s.swing = v; });
        swingSlider.setTooltip (tr ("Swing: {0}%", { juce::String (juce::roundToInt (swingSlider.getValue() * 100.0)) }));
    };
    swingSlider.setTooltip (tr ("Swing: {0}%", { "0" }));
    swingLabel.setText (tr ("Swing"), juce::dontSendNotification);
    swingLabel.setFont (theme::font (theme::type::caption, true));
    swingLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    swingLabel.setJustificationType (juce::Justification::centredRight);

    patternMenuButton.setTooltip (tr ("Pattern: rename, color, clone, length, delete"));
    patternMenuButton.onClick = [this] { showPatternMenu(); };

    positionLabel.setFont (theme::monoFont (17.0f));
    positionLabel.setJustificationType (juce::Justification::centred);
    positionLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);
    positionLabel.setTooltip (tr ("Position: bar : beat : step"));
    positionLabel.setText ("1 : 1 : 1", juce::dontSendNotification);
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setFont (theme::font (theme::type::small));
    cpuLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    hintLabel.setFont (theme::font (theme::type::small));
    hintLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    hintLabel.setMinimumHorizontalScale (1.0f);
    hintLabel.setText (tr ("F1 help   Space play/pause   Z..M / Q..P play the channel   F5 Playlist   F6 Channel Rack   F7 Piano Roll   F8 Browser   F9 Mixer   L pattern/song"),
                       juce::dontSendNotification);

    rack.onOpenPianoRoll = [this] { openPianoRoll(); };
    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (true, true);

    tabs.setTabBarDepth (36);
    tabs.setOutline (0);
    tabs.setIndent (6);
    tabs.addTab ("Channel Rack", theme::colour::bg2, &rackViewport, false);
    tabs.addTab ("Piano Roll", theme::colour::bg2, &pianoRoll, false);
    tabs.addTab ("Playlist", theme::colour::bg2, &playlistArea, false);
    tabs.addTab ("Mixer", theme::colour::bg2, &mixer, false);
    {
        const Icon tabIcons[] = { Icon::channelRack, Icon::pianoRoll, Icon::playlist, Icon::mixer };
        const char* tabTips[] = { "F6", "F7", "F5", "F9" };
        for (int i = 0; i < 4; ++i)
            if (auto* b = tabs.getTabbedButtonBar().getTabButton (i))
            {
                b->getProperties().set (theme::LookAndFeel::iconProperty, (int) tabIcons[i]);
                b->setTooltip (tabTips[i]);
            }
    }
    playlist.onOpenPattern = [this] { tabs.setCurrentTabIndex (rackTab); };
    picker.onOpenPattern = [this] { tabs.setCurrentTabIndex (rackTab); };

    // recent files (stored in %APPDATA%\LYNX)
    recent.setMaxNumberOfItems (12);
    recent.restoreFromString (PluginHost::settingsFolder().getChildFile ("recentes.txt").loadFileAsString());
    addAndMakeVisible (menuBar);

    for (auto* c : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &playButton, &stopButton, &recordButton, &recordMenuButton,
                                                             &patternModeButton, &songModeButton, &metronomeButton, &bpmSlider, &patternBox, &newPatternButton,
                                                             &patternMenuButton, &swingLabel, &swingSlider, &settingsButton, &positionLabel, &cpuLabel, &hintLabel, &tabs, &scope })
    {
        addAndMakeVisible (c);
        c->setMouseClickGrabsKeyboardFocus (false); // the PC keyboard stays with the main window
    }

    for (auto* b : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &playButton, &stopButton, &recordButton, &recordMenuButton,
                                                             &patternModeButton, &songModeButton, &metronomeButton, &newPatternButton, &patternMenuButton, &settingsButton })
        b->setWantsKeyboardFocus (false);

    for (auto* b : { &newButton, &openButton, &saveButton, &exportButton, &recordMenuButton, &newPatternButton, &patternMenuButton, &settingsButton })
        b->setFlat (true);

    engine.addChangeListener (this);
    refreshToolbar();

    setWantsKeyboardFocus (true);
    setSize (1180, 642);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
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

    // BPM inset behind the value
    {
        auto r = bpmSlider.getBounds().toFloat();
        r = r.withWidth (80.0f);
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md);
    }

    // status bar
    const auto status = getLocalBounds().removeFromBottom (statusHeight);
    g.setColour (colour::bg1);
    g.fillRect (status);
    g.setColour (colour::border);
    g.fillRect (status.withHeight (1));

    if (browserVisible)
    {
        g.setColour (colour::border);
        g.fillRect (browser.getRight(), bar.getBottom(), 1, status.getY() - bar.getBottom());
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    menuBar.setBounds (area.removeFromTop (menuHeight));
    auto toolbar = area.removeFromTop (toolbarHeight).reduced (10, 9);
    dividers.clear();
    const auto h = toolbar.getHeight(); // 32

    auto place = [&toolbar] (juce::Component& c, int width, int gapAfter = 2) {
        c.setBounds (toolbar.removeFromLeft (width));
        toolbar.removeFromLeft (gapAfter);
    };
    auto divide = [&] {
        toolbar.removeFromLeft (7);
        dividers.push_back (toolbar.getX());
        toolbar.removeFromLeft (8);
    };

    place (logo, h, 6);
    divide();
    place (newButton, h);
    place (openButton, h);
    place (saveButton, h);
    place (exportButton, h, 0);
    divide();
    place (playButton, h + 6);
    place (stopButton, h + 6);
    place (recordButton, h + 6, 0);
    place (recordMenuButton, 18, 0);
    divide();
    place (patternModeButton, patternModeButton.getIdealWidth (h), 1);
    place (songModeButton, songModeButton.getIdealWidth (h), 0);
    divide();
    place (metronomeButton, h, 8);
    place (bpmSlider, 128, 0);
    divide();
    place (patternBox, 150, 2);
    place (newPatternButton, h, 0);
    place (patternMenuButton, h, 0);
    divide();
    place (swingLabel, juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (theme::type::caption, true)), swingLabel.getText())) + 4, 4);
    place (swingSlider, h, 0);

    settingsButton.setBounds (toolbar.removeFromRight (h));
    toolbar.removeFromRight (8);

    if (toolbar.getWidth() > 230)
        scope.setBounds (toolbar.removeFromRight (juce::jmin (180, toolbar.getWidth() - 140)));
    else
        scope.setBounds ({});

    toolbar.removeFromRight (8);
    positionLabel.setBounds (toolbar.getWidth() >= 110 ? toolbar.removeFromRight (juce::jmin (toolbar.getWidth(), 140)) : juce::Rectangle<int>());

    auto status = area.removeFromBottom (statusHeight).reduced (10, 0);
    cpuLabel.setBounds (status.removeFromRight (90));
    hintLabel.setBounds (status);

    if (browserVisible)
        browser.setBounds (area.removeFromLeft (250));

    browser.setVisible (browserVisible);
    tabs.setBounds (area.withTrimmedLeft (browserVisible ? 1 : 0));
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = key.getModifiers().isCommandDown();

    if (key == juce::KeyPress::spaceKey)
    {
        togglePlay();
        return true;
    }

    if (tabs.getCurrentTabIndex() == pianoRollTab && pianoRoll.handleKey (key))
        return true;

    if (tabs.getCurrentTabIndex() == playlistTab && playlist.handleKey (key))
        return true;

    if (cmd && key.getKeyCode() == 'Z')
    {
        key.getModifiers().isShiftDown() ? engine.redo() : engine.undo();
        return true;
    }

    if (cmd && key.getKeyCode() == 'Y')
    {
        engine.redo();
        return true;
    }

    if (cmd && key.getKeyCode() == 'S')
    {
        save (key.getModifiers().isShiftDown());
        return true;
    }

    if (cmd && key.getKeyCode() == 'R')
    {
        exportAudio();
        return true;
    }

    if (cmd && key.getKeyCode() == 'O')
    {
        chooseFileToOpen();
        return true;
    }

    if (cmd && key.getKeyCode() == 'N')
    {
        newProject();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F5Key)
    {
        tabs.setCurrentTabIndex (playlistTab);
        return true;
    }

    if (! cmd && (key.getKeyCode() == 'L' || key.getKeyCode() == 'l'))
    {
        toggleMode();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F1Key)
    {
        showHelp();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F8Key)
    {
        browserVisible = ! browserVisible;
        resized();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F9Key)
    {
        tabs.setCurrentTabIndex (mixerTab);
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F6Key)
    {
        tabs.setCurrentTabIndex (rackTab);
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::F7Key)
    {
        openPianoRoll();
        return true;
    }

    if (! cmd && ! key.getModifiers().isAltDown())
    {
        const auto note = keyCodeToNote (key.getKeyCode());

        if (note >= 0)
        {
            if (heldKeys.insert (key.getKeyCode()).second)
                engine.sendLiveNote (engine.getSelectedChannel(), note, 0.8f, true, true);
            return true;
        }
    }

    return false;
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && (e.eventComponent == &bpmSlider || bpmSlider.isParentOf (e.eventComponent)))
    {
        juce::PopupMenu menu;
        addAutomationItem (menu, engine, { AutomationTargetKind::tempo, 0 });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bpmSlider));
    }
}

bool MainComponent::keyStateChanged (bool)
{
    // JUCE has no per-key "keyReleased"; check which held keys were released.
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (*it))
        {
            engine.sendLiveNote (engine.getSelectedChannel(), keyCodeToNote (*it), 0.0f, false, true);
            it = heldKeys.erase (it);
        }
        else
            ++it;
    }

    return false;
}

void MainComponent::togglePlay()
{
    engine.getTransport().playing.load() ? engine.stop() : engine.play();
}

void MainComponent::toggleMode()
{
    engine.editSong ([] (Song& s) { s.mode = s.mode == PlayMode::song ? PlayMode::pattern : PlayMode::song; }, false);
}

void MainComponent::openPianoRoll()
{
    tabs.setCurrentTabIndex (pianoRollTab);
    pianoRoll.scrollToNotes();
}

void MainComponent::selectTabForSnapshot (int tab)
{
    if (tab == pianoRollTab)
    {
        const auto& song = engine.getSong();
        std::vector<int> counts (song.channels.size(), 0);

        for (const auto& n : song.patterns[(std::size_t) song.currentPattern].notes)
            if (n.channel >= 0 && n.channel < (int) counts.size())
                ++counts[(std::size_t) n.channel];

        if (! counts.empty())
            engine.setSelectedChannel ((int) (std::max_element (counts.begin(), counts.end()) - counts.begin()));

        openPianoRoll();
    }
    else
        tabs.setCurrentTabIndex (tab);

    if (tab == playlistTab)
        playlist.resized(); // update the scrollbars for the loaded song

    if (tab == mixerTab)
        mixer.selectFirstTrackWithEffects();
}

void MainComponent::refreshToolbar()
{
    const auto& song = engine.getSong();
    patternBox.clear (juce::dontSendNotification);

    for (std::size_t i = 0; i < song.patterns.size(); ++i)
        patternBox.addItem (juce::String::fromUTF8 (song.patterns[i].name.c_str()), (int) i + 1);

    patternBox.setSelectedItemIndex (song.currentPattern, juce::dontSendNotification);
    bpmSlider.setValue (song.bpm, juce::dontSendNotification);
    swingSlider.setValue (song.swing, juce::dontSendNotification);
    refreshModeButton();
}

void MainComponent::showPatternMenu()
{
    const auto& song = engine.getSong();
    const auto index = song.currentPattern;
    const auto& pattern = song.patterns[(std::size_t) index];

    juce::PopupMenu colours;
    colours.addItem (tr ("Automatic"), true, pattern.colour == 0,
                     [this, index] { engine.editSong ([index] (Song& s) { s.patterns[(std::size_t) index].colour = 0; }); });
    colours.addSeparator();

    // palette in the spirit of FL's
    static const std::uint32_t palette[] = { 0xff485156, 0xffa14547, 0xffa16f45, 0xffa19d45, 0xff6ca145, 0xff45a175,
                                             0xff45a1a0, 0xff457ca1, 0xff4f45a1, 0xff8a5096, 0xffa1458a, 0xff6b3866 };

    for (auto c : palette)
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
    menu.addItem (tr ("Rename..."), [this, index] { renamePattern (index); });
    menu.addSubMenu (tr ("Color"), colours);
    menu.addSubMenu (tr ("Length"), lengths);
    menu.addItem (tr ("Clone"), [this, index] {
        engine.editSong ([index] (Song& s) {
            auto copy = s.patterns[(std::size_t) index];
            copy.name += " (copy)";
            s.patterns.insert (s.patterns.begin() + index + 1, std::move (copy));

            // clips pointing to patterns after the new one shift by one position
            s.forEachClipList ([index] (std::vector<Clip>& clips) {
                for (auto& c : clips)
                    if (c.kind == ClipKind::pattern && c.pattern > index)
                        ++c.pattern;
            });

            s.currentPattern = index + 1;
        });
    });
    juce::PopupMenu signatures;

    for (int beats : { 2, 3, 4, 5, 6, 7, 8, 9, 12 })
        signatures.addItem (juce::String (beats) + "/4", true, song.beatsPerBar == beats,
                            [this, beats] { engine.editSong ([beats] (Song& s) { s.beatsPerBar = beats; }); });

    menu.addSubMenu (tr ("Song time signature"), signatures);
    menu.addSeparator();
    menu.addItem (tr ("Delete pattern"), song.patterns.size() > 1, false,
                  [this, index] { engine.editSong ([index] (Song& s) { s.removePattern (index); }); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternMenuButton));
}

void MainComponent::renamePattern (int index)
{
    auto* window = new juce::AlertWindow (tr ("Rename pattern"), {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String::fromUTF8 (engine.getSong().patterns[(std::size_t) index].name.c_str()));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, index, window] (int result) {
                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();

                                 if (result == 1 && ! name.empty() && index < (int) engine.getSong().patterns.size())
                                     engine.editSong ([index, name] (Song& s) { s.patterns[(std::size_t) index].name = name; });
                             }),
                             true);
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

    if (namesChanged || patternBox.getSelectedItemIndex() != song.currentPattern || std::abs (bpmSlider.getValue() - song.bpm) > 1e-9
        || std::abs (swingSlider.getValue() - song.swing) > 1e-6)
        refreshToolbar();
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

    // MIDI learn waiting for a controller
    static const auto normalHint = hintLabel.getText();
    const auto hint = engine.isLearningMidi() ? tr ("Move a knob/fader on the MIDI controller to link it to the parameter...") : normalHint;
    if (hintLabel.getText() != hint)
        hintLabel.setText (hint, juce::dontSendNotification);
    const auto playing = transport.playing.load();
    playButton.setIcon (playing ? Icon::pause : Icon::play);
    playButton.setToggleState (playing, juce::dontSendNotification);
    cpuLabel.setText ("CPU " + juce::String (engine.getDeviceManager().getCpuUsage() * 100.0, 1) + "%",
                      juce::dontSendNotification);

    // If a dialog stole focus, give it back so the PC keyboard keeps playing.
    if (! hasKeyboardFocus (true) && isShowing() && juce::Process::isForegroundProcess()
        && juce::Component::getCurrentlyModalComponent() == nullptr)
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
    return { tr ("File"), tr ("Edit"), tr ("View"), tr ("Options"), tr ("Help") };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    if (index == 0)
    {
        menu.addItem (tr ("New") + "\tCtrl+N", [this] { newProject(); });

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
        menu.addItem (tr ("Open...") + "\tCtrl+O", [this] { chooseFileToOpen(); });

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

        menu.addItem (tr ("Save") + "\tCtrl+S", [this] { save (false); });
        menu.addItem (tr ("Save as...") + "\tCtrl+Shift+S", [this] { save (true); });
        menu.addSeparator();
        menu.addItem (tr ("Export audio...") + "\tCtrl+R", [this] { exportAudio(); });
        menu.addItem (tr ("Open autosave folder"), [] {
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LYNX/autosave").startAsProcess();
        });
        menu.addSeparator();
        menu.addItem (tr ("Exit"), [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    }
    else if (index == 1)
    {
        menu.addItem (tr ("Undo") + "\tCtrl+Z", engine.canUndo(), false, [this] { engine.undo(); });
        menu.addItem (tr ("Redo") + "\tCtrl+Y", engine.canRedo(), false, [this] { engine.redo(); });
    }
    else if (index == 2)
    {
        menu.addItem ("Playlist\tF5", [this] { tabs.setCurrentTabIndex (playlistTab); });
        menu.addItem ("Channel Rack\tF6", [this] { tabs.setCurrentTabIndex (rackTab); });
        menu.addItem ("Piano Roll\tF7", [this] { openPianoRoll(); });
        menu.addItem ("Mixer\tF9", [this] { tabs.setCurrentTabIndex (mixerTab); });
        menu.addItem ("Browser\tF8", true, browserVisible, [this] {
            browserVisible = ! browserVisible;
            resized();
            repaint();
        });
    }
    else if (index == 3)
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
        menu.addItem (tr ("Metronome"), true, engine.getTransport().metronome.load(), [this] {
            metronomeButton.setToggleState (! metronomeButton.getToggleState(), juce::sendNotification);
        });
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
        menu.addItem (tr ("Shortcuts") + "\tF1", [this] { showHelp(); });
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
          { { TR_NOOP ("Space"), TR_NOOP ("Play / pause") },
            { "Ctrl+Z / Ctrl+Y", TR_NOOP ("Undo / redo") },
            { "Ctrl+N / Ctrl+O", TR_NOOP ("New project / open") },
            { "Ctrl+S / Ctrl+Shift+S", TR_NOOP ("Save / save as") },
            { "Ctrl+R", TR_NOOP ("Export audio") },
            { "F5 F6 F7 F9", TR_NOOP ("Playlist, Channel Rack, Piano Roll, Mixer") },
            { "F8", TR_NOOP ("Show or hide the Browser") },
            { "L", TR_NOOP ("Pattern / song mode") },
            { "Z..M  Q..P", TR_NOOP ("PC keyboard as a piano (selected channel)") } } },
        { TR_NOOP ("Channel Rack and Mixer"),
          { { TR_NOOP ("Right-click the LED"), TR_NOOP ("Solo the channel") },
            { TR_NOOP ("Right-click the name"), TR_NOOP ("Channel menu") },
            { TR_NOOP ("Right-click a knob/fader"), TR_NOOP ("Automation or link to a MIDI controller") },
            { TR_NOOP ("Arrow under a strip"), TR_NOOP ("Send from the selected track to this one") },
            { TR_NOOP ("Right-click the BPM"), TR_NOOP ("Tempo automation") } } },
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
        Sheet() { setSize (920, 466); }

        void paint (juce::Graphics& g) override
        {
            using namespace theme;
            g.fillAll (colour::bg2);
            const auto columnWidth = (getWidth() - 3 * 24) / 2;
            int column = 0, y = 20;

            for (std::size_t i = 0; i < sections.size(); ++i)
            {
                if (i == 2) // second column
                {
                    column = 1;
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
