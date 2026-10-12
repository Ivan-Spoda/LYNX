#include "ui/MainComponent.h"

#include "core/FlpImport.h"
#include "flp/Flp.h"

namespace daw
{
MainComponent::MainComponent (AudioEngine& e)
    : engine (e),
      rack (e)
{
    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this]
    {
        engine.stop();
        engine.rewind();
    };
    metronomeButton.onClick = [this] { engine.getTransport().metronome.store (metronomeButton.getToggleState()); };
    settingsButton.onClick = [this] { showAudioSettings(); };
    openButton.onClick = [this] { openFlp(); };

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

    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (true, true);

    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &stopButton, &metronomeButton, &bpmSlider,
                                                             &patternBox, &newPatternButton, &openButton,
                                                             &settingsButton, &positionLabel, &cpuLabel, &rackViewport })
        addAndMakeVisible (c);

    engine.addChangeListener (this);
    refreshPatternList();

    setWantsKeyboardFocus (true);
    setSize (1000, 420);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    engine.removeChangeListener (this);
    stopTimer();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff23272c));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (8);
    auto toolbar = area.removeFromTop (30);

    playButton.setBounds (toolbar.removeFromLeft (64));
    toolbar.removeFromLeft (4);
    stopButton.setBounds (toolbar.removeFromLeft (64));
    toolbar.removeFromLeft (8);
    metronomeButton.setBounds (toolbar.removeFromLeft (70));
    bpmSlider.setBounds (toolbar.removeFromLeft (150));
    toolbar.removeFromLeft (8);
    patternBox.setBounds (toolbar.removeFromLeft (160));
    toolbar.removeFromLeft (4);
    newPatternButton.setBounds (toolbar.removeFromLeft (30));
    toolbar.removeFromLeft (8);
    openButton.setBounds (toolbar.removeFromLeft (90));

    settingsButton.setBounds (toolbar.removeFromRight (80));
    cpuLabel.setBounds (toolbar.removeFromRight (90));
    positionLabel.setBounds (toolbar);

    area.removeFromTop (8);
    rackViewport.setBounds (area);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        togglePlay();
        return true;
    }

    return false;
}

void MainComponent::togglePlay()
{
    engine.getTransport().playing.load() ? engine.stop() : engine.play();
}

void MainComponent::refreshPatternList()
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

    if (patternBox.getNumItems() != (int) song.patterns.size() || patternBox.getSelectedItemIndex() != song.currentPattern
        || std::abs (bpmSlider.getValue() - song.bpm) > 1e-9)
        refreshPatternList();
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
}

void MainComponent::openFlp()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Abrir projeto do FL Studio", juce::File(), "*.flp");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  if (! file.existsAsFile())
                                      return;

                                  juce::MemoryBlock data;

                                  if (! file.loadFileAsData (data))
                                      return;

                                  try
                                  {
                                      const auto project = flp::parseProject (
                                          { static_cast<const std::uint8_t*> (data.getData()), data.getSize() });

                                      engine.stop();
                                      engine.rewind();
                                      engine.setSong (importFlp (project));
                                      getTopLevelComponent()->setName ("Daw - " + file.getFileNameWithoutExtension());
                                  }
                                  catch (const flp::Error& e)
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                              "Não foi possível abrir",
                                                                              juce::String::fromUTF8 (e.what()));
                                  }
                              });
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(), 0, 0, 2, 2, false, false, true, false);
    selector->setSize (500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Configurações de áudio";
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}
} // namespace daw
