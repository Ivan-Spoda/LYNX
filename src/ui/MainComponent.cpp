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
    newButton.onClick = [this] { newProject(); };
    logo.setImage (logoImage(), juce::RectanglePlacement::centred);
    logo.setTooltip ("LYNX " JUCE_APPLICATION_VERSION_STRING);
    addAndMakeVisible (logo);
    openButton.onClick = [this] { chooseFileToOpen(); };
    saveButton.onClick = [this] { save (false); };
    exportButton.setTooltip (utf8 ("Export audio: WAV, FLAC or OGG (Ctrl+R)"));
    exportButton.onClick = [this] { exportAudio(); };
    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this]
    {
        engine.stop();
        engine.rewind();
    };
    recordButton.setClickingTogglesState (false);
    recordButton.onClick = [this] { engine.setRecording (! engine.isRecording()); };
    recordMenuButton.setTooltip (utf8 ("What to record"));
    recordMenuButton.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem (utf8 ("Notes (MIDI and PC keyboard) into the current pattern"), false, true, [] {});
        menu.addItem (utf8 ("Audio from the sound card input (becomes a Playlist clip)"), true, engine.isAudioRecordingArmed(), [this] {
            engine.setAudioRecordingArmed (! engine.isAudioRecordingArmed());

            if (engine.getLastRecordingError().isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Audio input"),
                                                        engine.getLastRecordingError());
        });
        menu.addSeparator();
        menu.addItem (utf8 ("Open recordings folder"), [] {
            juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Recordings").createDirectory();
            juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Recordings").startAsProcess();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recordMenuButton));
    };

    modeButton.setTooltip ("Pattern or Song (L key)");
    modeButton.onClick = [this] { toggleMode(); };
    metronomeButton.onClick = [this] { engine.getTransport().metronome.store (metronomeButton.getToggleState()); };
    settingsButton.onClick = [this] { showAudioSettings(); };

    bpmSlider.setRange (minTempo, maxTempo, 0.1);
    bpmSlider.addMouseListener (this, true);
    bpmSlider.setTooltip (utf8 ("Tempo (right-click: create tempo automation)"));
    bpmSlider.setValue (engine.getSong().bpm, juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { engine.editSong ([v = bpmSlider.getValue()] (Song& s) { s.bpm = v; }); };

    patternBox.onChange = [this]
    {
        const auto index = patternBox.getSelectedItemIndex();

        if (index >= 0 && index != engine.getSong().currentPattern)
            engine.editSong ([index] (Song& s) { s.currentPattern = index; }, false);
    };

    newPatternButton.setTooltip ("New pattern");
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
        swingSlider.setTooltip ("Swing: " + juce::String (juce::roundToInt (swingSlider.getValue() * 100.0)) + "%");
    };
    swingSlider.setTooltip ("Swing: 0%");
    swingLabel.setText ("Swing", juce::dontSendNotification);
    swingLabel.setFont (juce::FontOptions (11.0f));
    swingLabel.setJustificationType (juce::Justification::centredRight);

    patternMenuButton.setTooltip ("Pattern: rename, color, clone, length, delete");
    patternMenuButton.onClick = [this] { showPatternMenu(); };

    positionLabel.setFont (juce::FontOptions (18.0f));
    positionLabel.setJustificationType (juce::Justification::centred);
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setFont (juce::FontOptions (12.0f));
    cpuLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    hintLabel.setText (utf8 ("F1 help   Space: play/pause   Z..M / Q..P: play the channel   F5 playlist   F6 rack   F7 piano roll   F8 browser   F9 mixer   L pattern/song   Ctrl+Z/Y undo/redo   Ctrl+S save   Ctrl+O open   Ctrl+R export"),
                       juce::dontSendNotification);

    rack.onOpenPianoRoll = [this] { openPianoRoll(); };
    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (true, true);

    tabs.setTabBarDepth (28);
    tabs.setOutline (0);
    tabs.addTab ("Channel Rack", theme::toolbar, &rackViewport, false);
    tabs.addTab ("Piano Roll", theme::toolbar, &pianoRoll, false);
    tabs.addTab ("Playlist", theme::toolbar, &playlistArea, false);
    tabs.addTab ("Mixer", theme::toolbar, &mixer, false);
    playlist.onOpenPattern = [this] { tabs.setCurrentTabIndex (rackTab); };
    picker.onOpenPattern = [this] { tabs.setCurrentTabIndex (rackTab); };

    // recent files (stored in %APPDATA%\LYNX)
    recent.setMaxNumberOfItems (12);
    recent.restoreFromString (PluginHost::settingsFolder().getChildFile ("recentes.txt").loadFileAsString());
    addAndMakeVisible (menuBar);

    for (auto* c : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &playButton, &stopButton, &recordButton, &recordMenuButton,
                                                             &modeButton, &metronomeButton, &bpmSlider, &patternBox, &newPatternButton, &patternMenuButton, &swingLabel, &swingSlider,
                                                             &settingsButton, &positionLabel, &cpuLabel, &hintLabel, &tabs, &scope })
    {
        addAndMakeVisible (c);
        c->setMouseClickGrabsKeyboardFocus (false); // the PC keyboard stays with the main window
    }

    for (auto* b : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &exportButton, &playButton, &stopButton, &recordButton, &recordMenuButton,
                                                             &modeButton, &metronomeButton, &newPatternButton, &patternMenuButton, &settingsButton })
        b->setWantsKeyboardFocus (false);

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
    g.fillAll (theme::window);
    g.setColour (theme::toolbar);
    g.fillRect (0, menuHeight, getWidth(), 46);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    menuBar.setBounds (area.removeFromTop (menuHeight));
    auto toolbar = area.removeFromTop (46).reduced (8);

    auto place = [&toolbar] (juce::Component& c, int width, int gapAfter = 4) {
        c.setBounds (toolbar.removeFromLeft (width));
        toolbar.removeFromLeft (gapAfter);
    };

    place (logo, 30, 8);
    place (newButton, 50);
    place (openButton, 52);
    place (saveButton, 56);
    place (exportButton, 66, 12);
    place (playButton, 54);
    place (stopButton, 50, 4);
    place (recordButton, 40, 0);
    place (recordMenuButton, 16, 4);
    place (modeButton, 52, 8);
    place (metronomeButton, 64);
    place (bpmSlider, 124, 10);
    place (patternBox, 140);
    place (newPatternButton, 28);
    place (patternMenuButton, 28, 6);
    place (swingLabel, 38, 0);
    place (swingSlider, 30);

    settingsButton.setBounds (toolbar.removeFromRight (74));
    toolbar.removeFromRight (6);

    if (toolbar.getWidth() > 220)
        scope.setBounds (toolbar.removeFromRight (juce::jmin (160, toolbar.getWidth() - 110)));
    else
        scope.setBounds ({});

    positionLabel.setBounds (toolbar);

    auto status = area.removeFromBottom (20).reduced (8, 0);
    cpuLabel.setBounds (status.removeFromRight (90));
    hintLabel.setBounds (status);
    if (browserVisible)
        browser.setBounds (area.removeFromLeft (240).reduced (4, 4));

    browser.setVisible (browserVisible);
    tabs.setBounds (area.reduced (6, 4));
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
    colours.addItem (utf8 ("Automatic"), true, pattern.colour == 0,
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
    lengths.addItem (utf8 ("Automatic (from notes)"), true, pattern.lengthTicks == 0,
                     [this, index] { engine.editSong ([index] (Song& s) { s.patterns[(std::size_t) index].lengthTicks = 0; }); });

    for (int bars : { 1, 2, 4, 8, 16, 32 })
    {
        const auto ticks = (std::int64_t) bars * ppq * juce::jmax (1, song.beatsPerBar);
        lengths.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), true, pattern.lengthTicks == ticks,
                         [this, index, ticks] { engine.editSong ([index, ticks] (Song& s) { s.patterns[(std::size_t) index].lengthTicks = ticks; }); });
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 (pattern.name.c_str()));
    menu.addItem ("Rename...", [this, index] { renamePattern (index); });
    menu.addSubMenu ("Color", colours);
    menu.addSubMenu ("Length", lengths);
    menu.addItem ("Clone", [this, index] {
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

    menu.addSubMenu (utf8 ("Song time signature"), signatures);
    menu.addSeparator();
    menu.addItem ("Delete pattern", song.patterns.size() > 1, false,
                  [this, index] { engine.editSong ([index] (Song& s) { s.removePattern (index); }); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternMenuButton));
}

void MainComponent::renamePattern (int index)
{
    auto* window = new juce::AlertWindow ("Rename pattern", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String::fromUTF8 (engine.getSong().patterns[(std::size_t) index].name.c_str()));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

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
    recordButton.setColour (juce::TextButton::buttonColourId, recording ? juce::Colour (0xffd03030) : theme::toolbar.brighter (0.1f));
    const auto inputs = engine.getActiveMidiInputs();
    recordButton.setTooltip (utf8 ("Record while playing: notes (PC keyboard and MIDI)")
                             + (engine.isAudioRecordingArmed() ? utf8 (" and input audio") : juce::String())
                             + utf8 ("\nMIDI inputs: ") + (inputs.isEmpty() ? utf8 ("none") : inputs.joinIntoString (", ")));
    recordMenuButton.setColour (juce::TextButton::buttonColourId,
                                engine.isAudioRecordingArmed() ? juce::Colour (0xffa03030) : theme::toolbar.brighter (0.1f));

    const auto songMode = engine.getSong().mode == PlayMode::song;
    modeButton.setButtonText (songMode ? "SONG" : "PAT");
    modeButton.setColour (juce::TextButton::buttonColourId, songMode ? theme::accentGreen.darker (0.5f) : theme::accentOrange.darker (0.5f));
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
    g.setColour (theme::knobBackground);
    g.fillRoundedRectangle (area, 3.0f);

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

    g.setColour (theme::accentGreen.withAlpha (0.85f));
    g.strokePath (wave, juce::PathStrokeType (1.2f));
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
    const auto hint = engine.isLearningMidi() ? utf8 ("Move a knob/fader on the MIDI controller to link it to the parameter...") : normalHint;
    if (hintLabel.getText() != hint)
        hintLabel.setText (hint, juce::dontSendNotification);
    playButton.setButtonText (transport.playing.load() ? "Pause" : "Play");
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
                         + (projectName.isNotEmpty() ? "LYNX - " + projectName : juce::String ("LYNX")));
}

void MainComponent::confirmDiscard (std::function<void()> then)
{
    if (! engine.isModified())
    {
        then();
        return;
    }

    auto* window = new juce::AlertWindow (utf8 ("Save changes?"),
                                          utf8 ("The project has unsaved changes."),
                                          juce::MessageBoxIconType::QuestionIcon);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Discard", 2);
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
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
        updateTitle();
    });
}

void MainComponent::chooseFileToOpen()
{
    confirmDiscard ([this] { chooseFileToOpenNow(); });
}

void MainComponent::chooseFileToOpenNow()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Open project", currentFile.getParentDirectory(),
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
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Could not open"), error);
            return;
        }

        engine.stop();
        engine.rewind();
        engine.setSong (std::move (*song));
        currentFile = juce::File(); // imported: "Save" asks where to write the .lynx
        projectName = file.getFileNameWithoutExtension();
        rememberRecent (file);

        if (auto* window = getTopLevelComponent())
            window->setName ("LYNX - " + projectName + " (imported from MIDI)");
        return;
    }

    if (file.hasFileExtension ("flp"))
    {
        if (! engine.importFlpFile (file, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Could not open"), error);
            return;
        }

        currentFile = juce::File(); // imported: "Save" asks where to write the .lynx
        projectName = file.getFileNameWithoutExtension();
        rememberRecent (file);
        if (auto* window = getTopLevelComponent())
            window->setName ("LYNX - " + file.getFileNameWithoutExtension() + " (imported from FL)");

        showImportReport();
        return;
    }

    auto song = ProjectFile::load (file, error);

    if (! song)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Could not open"), error);
        return;
    }

    engine.stop();
    engine.rewind();
    engine.setSong (std::move (*song));
    currentFile = file;
    projectName = file.getFileNameWithoutExtension();
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
            updateTitle();
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Could not save"), error);
    };

    if (! askForFile && currentFile != juce::File())
    {
        write (currentFile);
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> ("Save project", currentFile.getParentDirectory(),
                                                       juce::String ("*") + ProjectFile::extension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [write] (const juce::FileChooser& chooser) {
                                  if (chooser.getResult() != juce::File())
                                      write (chooser.getResult());
                              });
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", utf8 ("Options"), "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    if (index == 0)
    {
        menu.addItem ("New\tCtrl+N", [this] { newProject(); });

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
                    updateTitle();
                });
            });
        menu.addSubMenu ("New from template", templates);
        menu.addItem ("Open...\tCtrl+O", [this] { chooseFileToOpen(); });

        juce::PopupMenu recentMenu;
        for (int i = 0; i < recent.getNumFiles(); ++i)
        {
            const auto file = recent.getFile (i);
            recentMenu.addItem (file.getFileName() + "  (" + file.getParentDirectory().getFullPathName() + ")", file.existsAsFile(),
                                false, [this, file] { confirmDiscard ([this, file] { openFile (file); }); });
        }
        if (recent.getNumFiles() == 0)
            recentMenu.addItem ("(none)", false, false, [] {});
        menu.addSubMenu ("Open recent", recentMenu);

        menu.addItem ("Save\tCtrl+S", [this] { save (false); });
        menu.addItem ("Save as...\tCtrl+Shift+S", [this] { save (true); });
        menu.addSeparator();
        menu.addItem (utf8 ("Export audio...\tCtrl+R"), [this] { exportAudio(); });
        menu.addItem ("Open autosave folder", [] {
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LYNX/autosave").startAsProcess();
        });
        menu.addSeparator();
        menu.addItem ("Exit", [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    }
    else if (index == 1)
    {
        menu.addItem ("Undo\tCtrl+Z", engine.canUndo(), false, [this] { engine.undo(); });
        menu.addItem ("Redo\tCtrl+Y", engine.canRedo(), false, [this] { engine.redo(); });
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
        });
    }
    else if (index == 3)
    {
        menu.addItem (utf8 ("Audio settings..."), [this] { showAudioSettings(); });
        menu.addItem (utf8 ("Scan for plugins (VST3 and CLAP)..."), [this] {
            scanPluginsWithProgress (engine, [] (int found) {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Plugins",
                                                        juce::String (found) + utf8 (" new plugin(s) found."));
            });
        });
        menu.addItem (utf8 ("Run plugins in a separate process (protects LYNX from crashes)"), true, pluginSandboxEnabled(), [] {
            setPluginSandboxEnabled (! pluginSandboxEnabled());
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Plugins",
                                                    utf8 (pluginSandboxEnabled()
                                                              ? "On: plugins opened from now on run in a separate process. "
                                                                "If one crashes, it goes silent and LYNX keeps running. Reopen the project to apply this to plugins already open."
                                                              : "Off: plugins opened from now on run inside LYNX."));
        });
        menu.addItem (utf8 ("Record input audio"), true, engine.isAudioRecordingArmed(), [this] {
            engine.setAudioRecordingArmed (! engine.isAudioRecordingArmed());
        });
        menu.addItem (utf8 ("Metronome"), true, engine.getTransport().metronome.load(), [this] {
            metronomeButton.setToggleState (! metronomeButton.getToggleState(), juce::sendNotification);
        });
    }
    else
    {
        menu.addItem ("Shortcuts\tF1", [this] { showHelp(); });
        menu.addItem (utf8 ("Last FL import report"), [this] {
            const auto& r = engine.getImportReport();
            if (r.converted.empty() && r.approximated.empty() && r.unsupported.empty() && r.droppedEffects.empty() && r.missingSamples == 0
                && engine.getMissingPlugins().isEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, utf8 ("FL Studio import"),
                                                        utf8 ("Nothing to report (no .flp imported in this session, or everything was converted)."));
            else
                showImportReport();
        });
        menu.addItem ("About LYNX", [] {
            struct Logo final : juce::Component
            {
                Logo() { setSize (300, 150); }
                void paint (juce::Graphics& g) override
                {
                    g.drawImageWithin (logoImage(), 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::centred);
                }
            };
            auto* logo = new Logo();
            auto* window = new juce::AlertWindow ("LYNX " JUCE_APPLICATION_VERSION_STRING,
                                                  utf8 ("Lightweight C++/JUCE DAW with the FL Studio workflow.\n"
                                                        "Independent implementation: no Image-Line code was copied."),
                                                  juce::MessageBoxIconType::NoIcon);
            window->addCustomComponent (logo);
            window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([logo] (int) { delete logo; }), true);
            keepAboveMainWindow (*window);
        });
    }

    return menu;
}

void MainComponent::rememberRecent (const juce::File& file)
{
    recent.addFile (file);
    PluginHost::settingsFolder().getChildFile ("recentes.txt").replaceWithText (recent.toString());
    menuItemsChanged();
}

void MainComponent::showHelp()
{
    const auto text = utf8 (
        "GENERAL\n"
        "  Space   play/pause        Ctrl+Z / Ctrl+Y  undo / redo\n"
        "  Ctrl+N new   Ctrl+O open   Ctrl+S save   Ctrl+Shift+S save as   Ctrl+R export\n"
        "  F5 Playlist   F6 Channel Rack   F7 Piano Roll   F8 browser   F9 Mixer   L pattern/song\n"
        "  Z..M and Q..P   PC keyboard as a piano (selected channel)\n\n"
        "PIANO ROLL\n"
        "  click creates a note (or chord, via the selector), drag moves, right edge changes the length\n"
        "  right-click deletes   Ctrl+drag selects   Ctrl+click adds to selection   Shift+drag copies\n"
        "  Ctrl+A/C/X/V   Ctrl+B duplicate   Ctrl+Q quantize   Ctrl+M mute   Ctrl+P porta   Delete remove\n"
        "  arrows move (Shift = octave/bar)   Alt disables snap   Ctrl+wheel zoom\n"
        "  bottom lane: velocity / pan / pitch   Slide button: sliding notes\n\n"
        "PLAYLIST\n"
        "  click paints the current pattern   drag on the ruler sets the loop (right-click clears)\n"
        "  right-click the clip name: split, make unique...   track LED mutes\n"
        "  Ctrl+A/C/X/V   Ctrl+B duplicate   Delete remove\n\n"
        "CHANNEL RACK AND MIXER\n"
        "  right-click the channel LED: solo   right-click the name: channel menu\n"
        "  right-click any knob/fader: automation or link to a MIDI controller\n"
        "  arrow at the bottom of a mixer strip: send from the selected track to this one\n"
        "  right-click the BPM: tempo automation");

    auto* label = new juce::Label ({}, text);
    label->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    label->setJustificationType (juce::Justification::topLeft);
    label->setSize (760, 520);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (label);
    options.dialogTitle = utf8 ("LYNX shortcuts");
    options.dialogBackgroundColour = theme::panel;
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
        text << utf8 ("Approximated by the built-in synth:\n") << list (r.approximated) << "\n\n";
    if (! r.unsupported.empty())
        text << utf8 ("No equivalent (played with a synth preset):\n") << list (r.unsupported) << "\n\n";
    if (! missing.isEmpty())
        text << utf8 ("Plugins not found (Options > Scan for plugins):\n  ") << missing.joinIntoString ("\n  ") << "\n\n";
    if (! r.droppedEffects.empty())
        text << utf8 ("Effects with no equivalent (left out):\n") << list (r.droppedEffects) << "\n\n";
    if (r.missingSamples > 0)
        text << utf8 ("Samples missing on this machine: ") << r.missingSamples << "\n";

    if (text.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, utf8 ("FL Studio import"), text.trimEnd());
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(), 0, 0, 2, 2, false, false, true, false);
    selector->setSize (500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = utf8 ("Audio settings");
    options.useNativeTitleBar = true;
    options.resizable = false;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}
} // namespace lynx
