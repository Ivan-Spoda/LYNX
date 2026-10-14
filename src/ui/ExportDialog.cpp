#include "ui/ExportDialog.h"
#include "ui/WindowOwner.h"

#include "engine/Exporter.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
class ExportThread final : public juce::ThreadWithProgressWindow
{
public:
    // onDone (on the UI thread): if empty, shows the result in a message box
    ExportThread (std::unique_ptr<ExportJob> j, juce::File f, std::function<void (bool)> done = {},
                  const juce::String& title = tr ("Exporting audio..."))
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
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not render"), error);

            onDone (ok);
        }
        else if (ok)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Export complete"),
                                                    file.getFullPathName() + "\n\n"
                                                        + tr ("Peak: {0} dBFS", { juce::String (juce::Decibels::gainToDecibels (job->getPeak()), 1) }));
        else if (! userPressedCancel)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not export"), error);

        // the export engine is torn down on the UI thread (plugins require this)
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    std::unique_ptr<ExportJob> job;
    juce::File file;
    std::function<void (bool)> onDone;
    juce::String error;
    bool ok = false;
};

// Stems: one file per insert that produces sound (like FL's "split mixer tracks"). Each render creates
// its own engine; plugins are created and destroyed on the UI thread, so the background thread asks it to do that.
class StemsThread final : public juce::ThreadWithProgressWindow
{
public:
    StemsThread (const AudioEngine& e, ExportSettings s, juce::File base)
        : juce::ThreadWithProgressWindow (tr ("Exporting stems..."), true, true),
          settings (s),
          baseFile (std::move (base)),
          song (e.getSong())
    {
        // inserts that receive a channel or a send
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
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Stems exported"),
                                                    tr ("{0} file(s) in {1}", { juce::String (written), baseFile.getParentDirectory().getFullPathName() }));

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
        formatBox.addItemList ({ tr ("WAV 16 bits"), tr ("WAV 24 bits"), tr ("WAV 32 bits (float)"), tr ("FLAC 24 bits"), "OGG Vorbis" }, 1);
        formatBox.setSelectedItemIndex (lastFormat, juce::dontSendNotification);
        rateBox.addItemList ({ "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz" }, 1);
        rateBox.setSelectedItemIndex (lastRate, juce::dontSendNotification);
        whatBox.addItem (tr ("Full song (Playlist)"), 1);
        whatBox.addItem (tr ("Current pattern"), 2);
        whatBox.setSelectedItemIndex (engine.getSong().mode == PlayMode::song && ! engine.getSong().playlist.empty() ? 0 : 1,
                                      juce::dontSendNotification);
        tailBox.addItemList ({ tr ("No tail"), tr ("Tail up to {0} s", { "3" }), tr ("Tail up to {0} s", { "6" }), tr ("Tail up to {0} s", { "12" }) }, 1);
        tailBox.setSelectedItemIndex (lastTail, juce::dontSendNotification);

        for (auto* l : { &formatLabel, &rateLabel, &whatLabel, &tailLabel })
            addAndMakeVisible (l);

        formatLabel.setText (tr ("Format"), juce::dontSendNotification);
        rateLabel.setText (tr ("Sample rate"), juce::dontSendNotification);
        whatLabel.setText (tr ("Export"), juce::dontSendNotification);
        tailLabel.setText (tr ("Tail"), juce::dontSendNotification);

        for (auto* c : { &formatBox, &rateBox, &whatBox, &tailBox })
            addAndMakeVisible (c);

        stemsToggle.setButtonText (tr ("One file per mixer insert (stems)"));
        for (auto* l : { &formatLabel, &rateLabel, &whatLabel, &tailLabel })
            l->setColour (juce::Label::textColourId, theme::colour::textSecondary);
        exportButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        exportButton.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
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
            l.setBounds (r.removeFromLeft (96));
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

        chooser = std::make_unique<juce::FileChooser> (tr ("Export audio"), folder.getChildFile (suggestedName + extension),
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
                                  engineRef.capturePluginStates(); // the copy carries the current plugin state

                                  if (stemsToggle.getToggleState())
                                  {
                                      auto stemSettings = s;
                                      stemSettings.wholeSong = true;
                                      close();
                                      (new StemsThread (engineRef, stemSettings, file))->launchThread();
                                      return;
                                  }

                                  auto job = std::make_unique<ExportJob> (engineRef.getSong(), s, file);
                                  close(); // deletes this panel: no `this` after this point
                                  (new ExportThread (std::move (job), file))->launchThread();
                              });
    }

    AudioEngine& engine;
    juce::File suggestedFolder;
    juce::String suggestedName;
    juce::Label formatLabel, rateLabel, whatLabel, tailLabel;
    juce::ComboBox formatBox, rateBox, whatBox, tailBox;
    juce::TextButton exportButton { tr ("Export...") }, cancelButton { tr ("Cancel") };
    juce::ToggleButton stemsToggle;
    std::unique_ptr<juce::FileChooser> chooser;

    // remembers the last choice while the app is open
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

    // clip length: the song + the tail, at the ending tempo
    std::int64_t musicTicks = 0;
    makeExportSong (song, true, 0, musicTicks);
    const auto length = musicTicks + (std::int64_t) (settings.tailSeconds * song.tempoAt ((double) musicTicks) * ppq / 60.0);

    auto job = std::make_unique<ExportJob> (makeBounceSong (song, track), settings, file);
    auto* engineRef = &engine;
    (new ExportThread (std::move (job), file, [engineRef, track, file, length] (bool ok) {
        if (! ok)
            return;

        engineRef->editSong ([&] (Song& s) { applyBounce (s, track, file.getFullPathName().toStdString(), length); });
    }, tr ("Rendering insert to audio...")))->launchThread();
}

std::unique_ptr<juce::Component> makeExportPanel (AudioEngine& engine)
{
    return std::make_unique<ExportPanel> (engine, juce::File(), "LYNX");
}

void showExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& suggestedName)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new ExportPanel (engine, suggestedFolder, suggestedName.isNotEmpty() ? suggestedName : "LYNX"));
    options.dialogTitle = tr ("Export audio");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}
} // namespace lynx
