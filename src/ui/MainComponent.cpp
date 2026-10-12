#include "ui/MainComponent.h"

#include "engine/Text.h"

#include "engine/ProjectFile.h"
#include "ui/Theme.h"

namespace daw
{
namespace
{
// Teclado do PC como piano, no layout do FL: Z S X D C V G B H N J M = C5..B5,
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
} // namespace

MainComponent::MainComponent (AudioEngine& e)
    : engine (e),
      rack (e),
      pianoRoll (e)
{
    newButton.onClick = [this] { newProject(); };
    openButton.onClick = [this] { chooseFileToOpen(); };
    saveButton.onClick = [this] { save (false); };
    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this]
    {
        engine.stop();
        engine.rewind();
    };
    metronomeButton.onClick = [this] { engine.getTransport().metronome.store (metronomeButton.getToggleState()); };
    settingsButton.onClick = [this] { showAudioSettings(); };

    bpmSlider.setRange (20.0, 999.0, 0.1);
    bpmSlider.setValue (engine.getSong().bpm, juce::dontSendNotification);
    bpmSlider.onValueChange = [this] { engine.editSong ([v = bpmSlider.getValue()] (Song& s) { s.bpm = v; }); };

    patternBox.onChange = [this]
    {
        const auto index = patternBox.getSelectedItemIndex();

        if (index >= 0 && index != engine.getSong().currentPattern)
            engine.editSong ([index] (Song& s) { s.currentPattern = index; });
    };

    newPatternButton.setTooltip ("Nova pattern");
    newPatternButton.onClick = [this]
    {
        engine.editSong ([] (Song& s) {
            auto& p = s.patterns.emplace_back();
            p.name = "Pattern " + std::to_string (s.patterns.size());
            s.currentPattern = (int) s.patterns.size() - 1;
        });
    };

    positionLabel.setFont (juce::FontOptions (20.0f));
    positionLabel.setJustificationType (juce::Justification::centred);
    cpuLabel.setJustificationType (juce::Justification::centredRight);
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    hintLabel.setText (utf8 ("Espaço: play/pause   Z..M / Q..P: tocar o canal selecionado   F6 rack   F7 piano roll   Ctrl+S salvar   Ctrl+O abrir"),
                       juce::dontSendNotification);

    rack.onOpenPianoRoll = [this] { openPianoRoll(); };
    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (true, true);

    tabs.setTabBarDepth (28);
    tabs.setOutline (0);
    tabs.addTab ("Channel Rack", theme::toolbar, &rackViewport, false);
    tabs.addTab ("Piano Roll", theme::toolbar, &pianoRoll, false);

    for (auto* c : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &playButton, &stopButton,
                                                             &metronomeButton, &bpmSlider, &patternBox, &newPatternButton,
                                                             &settingsButton, &positionLabel, &cpuLabel, &hintLabel, &tabs })
    {
        addAndMakeVisible (c);
        c->setMouseClickGrabsKeyboardFocus (false); // o teclado do PC fica com a janela principal
    }

    for (auto* b : std::initializer_list<juce::Component*> { &newButton, &openButton, &saveButton, &playButton, &stopButton,
                                                             &metronomeButton, &newPatternButton, &settingsButton })
        b->setWantsKeyboardFocus (false);

    engine.addChangeListener (this);
    refreshToolbar();

    setWantsKeyboardFocus (true);
    setSize (1180, 620);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    engine.removeChangeListener (this);
    stopTimer();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::window);
    g.setColour (theme::toolbar);
    g.fillRect (0, 0, getWidth(), 46);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (46).reduced (8);

    auto place = [&toolbar] (juce::Component& c, int width, int gapAfter = 4) {
        c.setBounds (toolbar.removeFromLeft (width));
        toolbar.removeFromLeft (gapAfter);
    };

    place (newButton, 56);
    place (openButton, 56);
    place (saveButton, 60, 16);
    place (playButton, 60);
    place (stopButton, 60, 8);
    place (metronomeButton, 70);
    place (bpmSlider, 150, 12);
    place (patternBox, 170);
    place (newPatternButton, 30);

    settingsButton.setBounds (toolbar.removeFromRight (80));
    cpuLabel.setBounds (toolbar.removeFromRight (90));
    positionLabel.setBounds (toolbar);

    hintLabel.setBounds (area.removeFromBottom (20).reduced (8, 0));
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

    if (cmd && key.getKeyCode() == 'S')
    {
        save (key.getModifiers().isShiftDown());
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
                engine.sendLiveNote (engine.getSelectedChannel(), note, 0.8f, true);
            return true;
        }
    }

    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    // O JUCE não tem "keyReleased" por tecla; conferimos quais teclas presas foram soltas.
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (*it))
        {
            engine.sendLiveNote (engine.getSelectedChannel(), keyCodeToNote (*it), 0.0f, false);
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
}

void MainComponent::refreshToolbar()
{
    const auto& song = engine.getSong();
    patternBox.clear (juce::dontSendNotification);

    for (std::size_t i = 0; i < song.patterns.size(); ++i)
        patternBox.addItem (juce::String::fromUTF8 (song.patterns[i].name.c_str()), (int) i + 1);

    patternBox.setSelectedItemIndex (song.currentPattern, juce::dontSendNotification);
    bpmSlider.setValue (song.bpm, juce::dontSendNotification);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto& song = engine.getSong();
    bool namesChanged = patternBox.getNumItems() != (int) song.patterns.size();

    for (int i = 0; ! namesChanged && i < patternBox.getNumItems(); ++i)
        namesChanged = patternBox.getItemText (i) != juce::String::fromUTF8 (song.patterns[(std::size_t) i].name.c_str());

    if (namesChanged || patternBox.getSelectedItemIndex() != song.currentPattern || std::abs (bpmSlider.getValue() - song.bpm) > 1e-9)
        refreshToolbar();
}

void MainComponent::timerCallback()
{
    auto& transport = engine.getTransport();
    const auto beats = transport.positionInBeats();
    const auto beatsPerBar = juce::jmax (1, transport.beatsPerBar.load());
    const auto bar = (int) (beats / beatsPerBar) + 1;
    const auto beat = (int) std::fmod (beats, (double) beatsPerBar) + 1;
    const auto step = (int) (std::fmod (beats, 1.0) * stepsPerBeat) + 1;

    positionLabel.setText (juce::String::formatted ("%d : %d : %d", bar, beat, step), juce::dontSendNotification);
    playButton.setButtonText (transport.playing.load() ? "Pause" : "Play");
    cpuLabel.setText ("CPU " + juce::String (engine.getDeviceManager().getCpuUsage() * 100.0, 1) + "%",
                      juce::dontSendNotification);

    // Se um diálogo roubou o foco, devolve para o teclado do PC continuar tocando.
    if (! hasKeyboardFocus (true) && isShowing() && juce::Process::isForegroundProcess()
        && juce::Component::getCurrentlyModalComponent() == nullptr)
        grabKeyboardFocus();
}

void MainComponent::updateTitle()
{
    if (auto* window = getTopLevelComponent())
        window->setName (currentFile.exists() ? "Daw - " + currentFile.getFileNameWithoutExtension() : juce::String ("Daw"));
}

void MainComponent::newProject()
{
    engine.stop();
    engine.rewind();
    engine.setSong (Song::makeDefault());
    currentFile = juce::File();
    updateTitle();
}

void MainComponent::chooseFileToOpen()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Abrir projeto", currentFile.getParentDirectory(),
                                                       juce::String ("*") + ProjectFile::extension + ";*.flp");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser) {
                                  if (chooser.getResult().existsAsFile())
                                      openFile (chooser.getResult());
                              });
}

void MainComponent::openFile (const juce::File& file)
{
    juce::String error;

    if (file.hasFileExtension ("flp"))
    {
        if (! engine.importFlpFile (file, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Não foi possível abrir"), error);
            return;
        }

        currentFile = juce::File(); // importado: "Salvar" pergunta onde gravar o .dawproj
        if (auto* window = getTopLevelComponent())
            window->setName ("Daw - " + file.getFileNameWithoutExtension() + " (importado do FL)");
        return;
    }

    auto song = ProjectFile::load (file, error);

    if (! song)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Não foi possível abrir"), error);
        return;
    }

    engine.stop();
    engine.rewind();
    engine.setSong (std::move (*song));
    currentFile = file;
    updateTitle();
}

void MainComponent::save (bool askForFile)
{
    auto write = [this] (juce::File file) {
        if (! file.hasFileExtension (ProjectFile::extension))
            file = file.withFileExtension (ProjectFile::extension);

        juce::String error;

        if (ProjectFile::save (engine.getSong(), file, error))
        {
            currentFile = file;
            updateTitle();
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Não foi possível salvar"), error);
    };

    if (! askForFile && currentFile != juce::File())
    {
        write (currentFile);
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser> ("Salvar projeto", currentFile.getParentDirectory(),
                                                       juce::String ("*") + ProjectFile::extension);

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [write] (const juce::FileChooser& chooser) {
                                  if (chooser.getResult() != juce::File())
                                      write (chooser.getResult());
                              });
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(), 0, 0, 2, 2, false, false, true, false);
    selector->setSize (500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = utf8 ("Configurações de áudio");
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}
} // namespace daw
