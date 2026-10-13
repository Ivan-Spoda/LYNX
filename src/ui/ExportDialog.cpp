#include "ui/ExportDialog.h"

#include "engine/Exporter.h"
#include "engine/Text.h"

namespace lynx
{
namespace
{
class ExportThread final : public juce::ThreadWithProgressWindow
{
public:
    explicit ExportThread (std::unique_ptr<ExportJob> j, juce::File f)
        : juce::ThreadWithProgressWindow (utf8 ("Exportando áudio..."), true, true),
          job (std::move (j)),
          file (std::move (f))
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
        if (ok)
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
    juce::String error;
    bool ok = false;
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
        tailBox.addItemList ({ "Sem cauda", "Cauda de até 3 s", "Cauda de até 6 s", "Cauda de até 12 s" }, 1);
        tailBox.setSelectedItemIndex (lastTail, juce::dontSendNotification);

        for (auto* l : { &formatLabel, &rateLabel, &whatLabel, &tailLabel })
            addAndMakeVisible (l);

        formatLabel.setText ("Formato", juce::dontSendNotification);
        rateLabel.setText ("Taxa", juce::dontSendNotification);
        whatLabel.setText ("Exportar", juce::dontSendNotification);
        tailLabel.setText (utf8 ("Final"), juce::dontSendNotification);

        for (auto* c : { &formatBox, &rateBox, &whatBox, &tailBox })
            addAndMakeVisible (c);

        addAndMakeVisible (exportButton);
        addAndMakeVisible (cancelButton);
        exportButton.onClick = [this] { chooseFile(); };
        cancelButton.onClick = [this] { close(); };
        setSize (380, 220);
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
    std::unique_ptr<juce::FileChooser> chooser;

    // lembra a última escolha enquanto o app está aberto
    static inline int lastFormat = 1, lastRate = 0, lastTail = 2;
    static inline juce::File lastFolder;
};
} // namespace

void showExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new ExportPanel (engine, suggestedFolder, suggestedName.isNotEmpty() ? suggestedName : "LYNX"));
    options.dialogTitle = utf8 ("Exportar áudio");
    options.dialogBackgroundColour = juce::Colour (0xff2b3036);
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    options.launchAsync();
}
} // namespace lynx
