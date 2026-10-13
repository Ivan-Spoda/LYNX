#include "ui/ExportDialog.h"
#include "ui/WindowOwner.h"

#include "engine/Exporter.h"
#include "engine/Text.h"

namespace lynx
{
namespace
{
class ExportThread final : public juce::ThreadWithProgressWindow
{
public:
    // onDone (na thread da UI): se vazio, mostra o resultado numa mensagem
    ExportThread (std::unique_ptr<ExportJob> j, juce::File f, std::function<void (bool)> done = {},
                  const juce::String& title = utf8 ("Exportando áudio..."))
        : juce::ThreadWithProgressWindow (title, true, true),
          job (std::move (j)),
          file (std::move (f)),
          onDone (std::move (done))
    {
    }

    void run() override
    {
        ok = job->run ([this] (double p) {
            setProgress (p);
            return ! threadShouldExit();
        }, error);
    }

    void threadComplete (bool userPressedCancel) override
    {
        if (onDone)
        {
            if (! ok && ! userPressedCancel)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Não foi possível renderizar"), error);

            onDone (ok);
        }
        else if (ok)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, utf8 ("Exportação concluída"),
                                                    file.getFullPathName() + "\n\n"
                                                        + utf8 ("Pico: ") + juce::String (juce::Decibels::gainToDecibels (job->getPeak()), 1) + " dBFS");
        else if (! userPressedCancel)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Não foi possível exportar"), error);

        // o engine da exportação é desmontado na thread da UI (plugins pedem isso)
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    std::unique_ptr<ExportJob> job;
    juce::File file;
    std::function<void (bool)> onDone;
    juce::String error;
    bool ok = false;
};

// Stems: um arquivo por insert com som (como o "split mixer tracks" do FL). Cada render cria
// o seu engine; os plugins nascem e morrem na thread da UI, então a thread de fundo pede isso a ela.
class StemsThread final : public juce::ThreadWithProgressWindow
{
public:
    StemsThread (const AudioEngine& e, ExportSettings s, juce::File base)
        : juce::ThreadWithProgressWindow (utf8 ("Exportando stems..."), true, true),
          settings (s),
          baseFile (std::move (base)),
          song (e.getSong())
    {
        // inserts que recebem canal ou envio
        for (int t = 1; t < (int) song.mixer.size(); ++t)
        {
            auto used = std::any_of (song.channels.begin(), song.channels.end(), [t] (const ChannelDef& c) { return c.mixerTrack == t; });

            for (int src = 1; src < (int) song.mixer.size() && ! used; ++src)
                used = song.routes (src, t) && src != t;

            if (used)
                inserts.push_back (t);
        }
    }

    void run() override
    {
        for (std::size_t i = 0; i < inserts.size() && ! threadShouldExit(); ++i)
        {
            const auto t = inserts[i];
            const auto name = juce::File::createLegalFileName (juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str()));
            const auto file = baseFile.getSiblingFile (baseFile.getFileNameWithoutExtension() + " - " + juce::String (t) + " " + name
                                                       + exportExtension (settings.format));
            setStatusMessage (name);

            std::unique_ptr<ExportJob> job;
            juce::WaitableEvent ready;
            juce::MessageManager::callAsync ([&] {
                job = std::make_unique<ExportJob> (makeBounceSong (song, t), settings, file);
                ready.signal();
            });
            ready.wait();

            juce::String error;
            const auto base = (double) i / (double) inserts.size();
            const auto ok = job->run ([this, base] (double p) {
                setProgress (base + p / (double) inserts.size());
                return ! threadShouldExit();
            }, error);

            if (ok)
                ++written;

            juce::WaitableEvent gone;
            juce::MessageManager::callAsync ([&] {
                job.reset();
                gone.signal();
            });
            gone.wait();
        }
    }

    void threadComplete (bool cancelled) override
    {
        if (! cancelled)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, utf8 ("Stems exportados"),
                                                    juce::String (written) + utf8 (" arquivo(s) em ") + baseFile.getParentDirectory().getFullPathName());

        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    ExportSettings settings;
    juce::File baseFile;
    Song song;
    std::vector<int> inserts;
    int written = 0;
};

class ExportPanel final : public juce::Component
{
public:
    ExportPanel (AudioEngine& e, juce::File folder, juce::String name)
        : engine (e),
          suggestedFolder (std::move (folder)),
          suggestedName (std::move (name))
    {
        formatBox.addItemList ({ "WAV 16 bits", "WAV 24 bits", "WAV 32 bits (float)", "FLAC 24 bits", "OGG Vorbis" }, 1);
        formatBox.setSelectedItemIndex (lastFormat, juce::dontSendNotification);
        rateBox.addItemList ({ "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz" }, 1);
        rateBox.setSelectedItemIndex (lastRate, juce::dontSendNotification);
        whatBox.addItem (utf8 ("Música inteira (Playlist)"), 1);
        whatBox.addItem (utf8 ("Pattern atual"), 2);
        whatBox.setSelectedItemIndex (engine.getSong().mode == PlayMode::song && ! engine.getSong().playlist.empty() ? 0 : 1,
                                      juce::dontSendNotification);
        tailBox.addItemList ({ utf8 ("Sem cauda"), utf8 ("Cauda de até 3 s"), utf8 ("Cauda de até 6 s"), utf8 ("Cauda de até 12 s") }, 1);
        tailBox.setSelectedItemIndex (lastTail, juce::dontSendNotification);

        for (auto* l : { &formatLabel, &rateLabel, &whatLabel, &tailLabel })
            addAndMakeVisible (l);

        formatLabel.setText ("Formato", juce::dontSendNotification);
        rateLabel.setText ("Taxa", juce::dontSendNotification);
        whatLabel.setText ("Exportar", juce::dontSendNotification);
        tailLabel.setText (utf8 ("Final"), juce::dontSendNotification);

        for (auto* c : { &formatBox, &rateBox, &whatBox, &tailBox })
            addAndMakeVisible (c);

        stemsToggle.setButtonText (utf8 ("Um arquivo por insert do mixer (stems)"));
        addAndMakeVisible (stemsToggle);
        addAndMakeVisible (exportButton);
        addAndMakeVisible (cancelButton);
        exportButton.onClick = [this] { chooseFile(); };
        cancelButton.onClick = [this] { close(); };
        setSize (380, 252);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14);
        auto row = [&area] (juce::Label& l, juce::ComboBox& c) {
            auto r = area.removeFromTop (28);
            l.setBounds (r.removeFromLeft (80));
            c.setBounds (r);
            area.removeFromTop (8);
        };

        row (whatLabel, whatBox);
        row (formatLabel, formatBox);
        row (rateLabel, rateBox);
        row (tailLabel, tailBox);
        stemsToggle.setBounds (area.removeFromTop (24));

        auto buttons = area.removeFromBottom (30);
        cancelButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        exportButton.setBounds (buttons.removeFromRight (120));
    }

private:
    void close()
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    }

    ExportSettings settings() const
    {
        static const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
        static const double tails[] = { 0.0, 3.0, 6.0, 12.0 };
        ExportSettings s;
        s.format = (ExportSettings::Format) juce::jlimit (0, 4, formatBox.getSelectedItemIndex());
        s.sampleRate = rates[juce::jlimit (0, 3, rateBox.getSelectedItemIndex())];
        s.wholeSong = whatBox.getSelectedItemIndex() == 0;
        s.tailSeconds = tails[juce::jlimit (0, 3, tailBox.getSelectedItemIndex())];
        return s;
    }

    void chooseFile()
    {
        lastFormat = formatBox.getSelectedItemIndex();
        lastRate = rateBox.getSelectedItemIndex();
        lastTail = tailBox.getSelectedItemIndex();

        const auto s = settings();
        const auto extension = exportExtension (s.format);
        auto folder = lastFolder.isDirectory() ? lastFolder
                      : suggestedFolder.isDirectory() ? suggestedFolder
                                                      : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        chooser = std::make_unique<juce::FileChooser> (utf8 ("Exportar áudio"), folder.getChildFile (suggestedName + extension),
                                                       "*" + extension);
        auto& engineRef = engine;

        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, s, extension, &engineRef] (const juce::FileChooser& fc) {
                                  auto file = fc.getResult();

                                  if (file == juce::File())
                                      return;

                                  if (! file.hasFileExtension (extension))
                                      file = file.withFileExtension (extension);

                                  lastFolder = file.getParentDirectory();
                                  engineRef.capturePluginStates(); // a cópia leva o estado atual dos plugins

                                  if (stemsToggle.getToggleState())
                                  {
                                      auto stemSettings = s;
                                      stemSettings.wholeSong = true;
                                      close();
                                      (new StemsThread (engineRef, stemSettings, file))->launchThread();
                                      return;
                                  }

                                  auto job = std::make_unique<ExportJob> (engineRef.getSong(), s, file);
                                  close(); // apaga este painel: nada de `this` depois daqui
                                  (new ExportThread (std::move (job), file))->launchThread();
                              });
    }

    AudioEngine& engine;
    juce::File suggestedFolder;
    juce::String suggestedName;
    juce::Label formatLabel, rateLabel, whatLabel, tailLabel;
    juce::ComboBox formatBox, rateBox, whatBox, tailBox;
    juce::TextButton exportButton { "Exportar..." }, cancelButton { "Cancelar" };
    juce::ToggleButton stemsToggle;
    std::unique_ptr<juce::FileChooser> chooser;

    // lembra a última escolha enquanto o app está aberto
    static inline int lastFormat = 1, lastRate = 0, lastTail = 2;
    static inline juce::File lastFolder;
};
} // namespace

void bounceInsertToAudio (AudioEngine& engine, int track)
{
    const auto& song = engine.getSong();

    if (track <= 0 || track >= (int) song.mixer.size())
        return;

    engine.capturePluginStates();
    const auto name = juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str());
    const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Renders");
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (name), ".wav");

    ExportSettings settings;
    settings.format = ExportSettings::Format::wav32Float;
    settings.sampleRate = engine.getTransport().sampleRate.load();
    settings.tailSeconds = 4.0;

    // comprimento do clip: a música + a cauda, no andamento do fim
    std::int64_t musicTicks = 0;
    makeExportSong (song, true, 0, musicTicks);
    const auto length = musicTicks + (std::int64_t) (settings.tailSeconds * song.tempoAt ((double) musicTicks) * ppq / 60.0);

    auto job = std::make_unique<ExportJob> (makeBounceSong (song, track), settings, file);
    auto* engineRef = &engine;
    (new ExportThread (std::move (job), file, [engineRef, track, file, length] (bool ok) {
        if (! ok)
            return;

        engineRef->editSong ([&] (Song& s) { applyBounce (s, track, file.getFullPathName().toStdString(), length); });
    }, utf8 ("Renderizando o insert para áudio...")))->launchThread();
}

std::unique_ptr<juce::Component> makeExportPanel (AudioEngine& engine)
{
    return std::make_unique<ExportPanel> (engine, juce::File(), "LYNX");
}

void showExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new ExportPanel (engine, suggestedFolder, suggestedName.isNotEmpty() ? suggestedName : "LYNX"));
    options.dialogTitle = utf8 ("Exportar áudio");
    options.dialogBackgroundColour = juce::Colour (0xff2b3036);
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}
} // namespace lynx
