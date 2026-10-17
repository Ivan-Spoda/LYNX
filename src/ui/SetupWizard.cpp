#include "ui/SetupWizard.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include "engine/AudioEngine.h"
#include "engine/FlStudio.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
juce::File setupFile()
{
    return PluginHost::settingsFolder().getChildFile ("setup-done.txt");
}

juce::File scanAtStartFile()
{
    return PluginHost::settingsFolder().getChildFile ("scan-at-start.txt");
}

class SectionLabel final : public juce::Label
{
public:
    explicit SectionLabel (const juce::String& text)
    {
        setText (text, juce::dontSendNotification);
        setFont (theme::font (theme::type::title, true));
        setColour (juce::Label::textColourId, theme::colour::textPrimary);
    }
};

class SetupWizard final : public juce::Component,
                          private juce::ListBoxModel,
                          private juce::Timer
{
public:
    SetupWizard (AudioEngine& e, std::function<void (const juce::String&)> language, std::function<void()> close)
        : engine (e),
          onLanguage (std::move (language)),
          onClose (std::move (close)),
          devices (e.getDeviceManager(), 0, 0, 2, 2, false, false, true, true)
    {
        auto& host = engine.getPluginHost();

        title.setText (tr ("Welcome to LYNX"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::display, true));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        intro.setText (tr ("A few things to get started. Everything here can be changed later (Options > Setup wizard)."),
                       juce::dontSendNotification);
        intro.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        for (auto* c : std::initializer_list<juce::Component*> { &title, &intro, &languageLabel, &languageBox, &audioLabel, &devices, &flLabel,
                                                                 &flStatus, &foldersLabel, &folders, &addFolder, &removeFolder, &resetFolders,
                                                                 &scanNow, &progressBar, &scanStatus, &scanAtStart, &skip, &finish })
            addAndMakeVisible (c);

        // language
        int id = 1;
        for (const auto& l : i18n::languages())
        {
            languageBox.addItem (juce::String::fromUTF8 (l.nativeName), id);
            if (i18n::saved() == l.id)
                languageBox.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }

        // FL Studio
        const auto fl = findFlStudio();
        if (fl.found())
        {
            const auto& db = host.flPluginDatabase();
            if (db.empty())
                flStatus.setText (tr ("{0} found (its plugin database lists no third-party plugins yet). .flp projects find their "
                                      "samples in FL Studio's folders; their plugins are found by scanning.", { fl.name() }),
                                  juce::dontSendNotification);
            else
                flStatus.setText (tr ("{0} found. Its plugin database lists {1} plugin file(s); .flp projects use the same plugin files FL Studio uses, "
                                      "and their samples are looked for in FL Studio's folders.",
                                      { fl.name(), juce::String ((int) db.entries().size()) }),
                                  juce::dontSendNotification);
        }
        else
            flStatus.setText (tr ("FL Studio was not found on this PC. .flp projects still open; their plugins are found by scanning."),
                              juce::dontSendNotification);
        flStatus.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        flStatus.setMinimumHorizontalScale (1.0f);
        flStatus.setJustificationType (juce::Justification::topLeft);

        // plugin folders, pre-filled with the standard ones and FL Studio's
        const auto current = host.pluginFolders();
        for (int i = 0; i < current.getNumPaths(); ++i)
            folderList.add (current[i].getFullPathName());
        folders.setModel (this);
        folders.setRowHeight (22);
        folders.setColour (juce::ListBox::backgroundColourId, theme::colour::bg0);
        addFolder.onClick = [this] { chooseFolder(); };
        removeFolder.onClick = [this] {
            if (const auto row = folders.getSelectedRow(); row >= 0)
            {
                folderList.remove (row);
                folders.updateContent();
                folders.repaint();
            }
        };
        resetFolders.onClick = [this] {
            const auto suggested = engine.getPluginHost().suggestedFolders();
            folderList.clear();
            for (int i = 0; i < suggested.getNumPaths(); ++i)
                folderList.add (suggested[i].getFullPathName());
            folders.updateContent();
            folders.repaint();
        };

        // scan
        scanNow.onClick = [this] {
            saveFolders();
            engine.getPluginHost().startBackgroundScan();
            scanStarted = true;
        };
        scanAtStart.setToggleState (backgroundScanAtStart(), juce::dontSendNotification);
        scanStatus.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        progressBar.setColour (juce::ProgressBar::foregroundColourId, theme::colour::accent);
        progressBar.setPercentageDisplay (false);

        skip.onClick = [this] {
            markSetupWizardDone();
            if (onClose)
                onClose();
        };
        finish.onClick = [this] { finishSetup(); };
        finish.setColour (juce::TextButton::buttonColourId, theme::colour::accent);

        updateScanStatus();
        startTimerHz (10);
        setSize (680, 660);
    }

    ~SetupWizard() override { folders.setModel (nullptr); }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 16);
        title.setBounds (r.removeFromTop (30));
        intro.setBounds (r.removeFromTop (22));
        r.removeFromTop (8);

        auto row = r.removeFromTop (26);
        languageLabel.setBounds (row.removeFromLeft (150));
        languageBox.setBounds (row.removeFromLeft (220));
        r.removeFromTop (10);

        audioLabel.setBounds (r.removeFromTop (24));
        devices.setBounds (r.removeFromTop (110));
        r.removeFromTop (6);

        flLabel.setBounds (r.removeFromTop (24));
        flStatus.setBounds (r.removeFromTop (44));
        r.removeFromTop (6);

        foldersLabel.setBounds (r.removeFromTop (24));
        auto list = r.removeFromTop (118);
        auto buttons = list.removeFromRight (150);
        folders.setBounds (list.withTrimmedRight (8));
        addFolder.setBounds (buttons.removeFromTop (26));
        buttons.removeFromTop (6);
        removeFolder.setBounds (buttons.removeFromTop (26));
        buttons.removeFromTop (6);
        resetFolders.setBounds (buttons.removeFromTop (26));
        r.removeFromTop (10);

        row = r.removeFromTop (26);
        scanNow.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (10);
        progressBar.setBounds (row);
        scanStatus.setBounds (r.removeFromTop (22));
        scanAtStart.setBounds (r.removeFromTop (24));

        auto bottom = getLocalBounds().reduced (20, 14).removeFromBottom (30);
        finish.setBounds (bottom.removeFromRight (120));
        bottom.removeFromRight (10);
        skip.setBounds (bottom.removeFromRight (120));
    }

private:
    int getNumRows() override { return folderList.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (selected)
            g.fillAll (theme::colour::accentMuted);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::body));
        g.drawText (folderList[row], 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    }

    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> (tr ("Plugin folder"), juce::File(), "");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& c) {
            if (const auto folder = c.getResult(); folder.isDirectory())
            {
                folderList.addIfNotAlreadyThere (folder.getFullPathName(), true);
                folders.updateContent();
                folders.repaint();
            }
        });
    }

    void saveFolders()
    {
        juce::FileSearchPath paths;
        for (const auto& f : folderList)
            paths.add (juce::File (f));
        engine.getPluginHost().setPluginFolders (paths);
        setBackgroundScanAtStart (scanAtStart.getToggleState());
    }

    void finishSetup()
    {
        saveFolders();
        markSetupWizardDone();
        if (! scanStarted)
            engine.getPluginHost().startBackgroundScan(); // keeps going after the window closes
        const auto chosen = languageBox.getSelectedId() - 1;
        const auto& languages = i18n::languages();
        const auto language = chosen >= 0 && chosen < (int) languages.size() ? juce::String (languages[(std::size_t) chosen].id) : i18n::saved();
        auto languageCallback = onLanguage;
        auto close = onClose;
        if (close)
            close(); // deletes this
        if (language != i18n::saved() && languageCallback)
            languageCallback (language);
    }

    void updateScanStatus()
    {
        const auto s = engine.getPluginHost().backgroundScan();
        progress = s.running ? s.progress : (s.finished > 0 ? 1.0 : 0.0);
        const auto count = engine.getPluginHost().getKnownPlugins().getNumTypes();
        if (s.running)
            scanStatus.setText (tr ("Scanning in the background: {0}", { s.file }), juce::dontSendNotification);
        else if (s.finished > 0)
            scanStatus.setText (tr ("Scan finished: {0} new, {1} plugins known.", { juce::String (s.found), juce::String (count) }), juce::dontSendNotification);
        else
            scanStatus.setText (tr ("{0} plugins known. Scanning runs in the background; you can close this window.", { juce::String (count) }),
                                juce::dontSendNotification);
        scanNow.setEnabled (! s.running);
        progressBar.setVisible (s.running || s.finished > 0);
    }

    void timerCallback() override { updateScanStatus(); }

    AudioEngine& engine;
    std::function<void (const juce::String&)> onLanguage;
    std::function<void()> onClose;

    juce::Label title, intro, flStatus, scanStatus;
    SectionLabel languageLabel { tr ("Language") }, audioLabel { tr ("Audio output") }, flLabel { "FL Studio" },
        foldersLabel { tr ("Plugin folders (VST3 and CLAP)") };
    juce::ComboBox languageBox;
    juce::AudioDeviceSelectorComponent devices;
    juce::StringArray folderList;
    juce::ListBox folders;
    juce::TextButton addFolder { tr ("Add folder...") }, removeFolder { tr ("Remove") }, resetFolders { tr ("Suggested folders") };
    juce::TextButton scanNow { tr ("Scan now") }, skip { tr ("Skip") }, finish { tr ("Finish") };
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };
    juce::ToggleButton scanAtStart { tr ("Look for new plugins in the background at every start") };
    std::unique_ptr<juce::FileChooser> chooser;
    bool scanStarted = false;
};

class WizardWindow final : public juce::DialogWindow
{
public:
    WizardWindow (AudioEngine& engine, std::function<void (const juce::String&)> onLanguage)
        : juce::DialogWindow (tr ("LYNX setup"), theme::colour::bg2, true, true)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new SetupWizard (engine, std::move (onLanguage), [this] { closeButtonPressed(); }), true);
        centreWithSize (getWidth(), getHeight());
        setResizable (false, false);
    }

    void closeButtonPressed() override
    {
        // closing the window counts as "skip": the wizard doesn't come back by itself
        markSetupWizardDone();
        juce::MessageManager::callAsync ([w = juce::Component::SafePointer<juce::Component> (this)] { delete w.getComponent(); });
        setVisible (false);
    }
};
} // namespace

std::unique_ptr<juce::Component> makeSetupWizardContent (AudioEngine& engine, std::function<void (const juce::String&)> onLanguage,
                                                         std::function<void()> onClose)
{
    return std::make_unique<SetupWizard> (engine, std::move (onLanguage), std::move (onClose));
}

void showSetupWizard (AudioEngine& engine, std::function<void (const juce::String&)> onLanguage)
{
    auto* window = new WizardWindow (engine, std::move (onLanguage)); // deletes itself when closed
    window->setVisible (true);
    keepAboveMainWindow (*window);
}

bool setupWizardDone()
{
    return setupFile().existsAsFile();
}

void markSetupWizardDone()
{
    if (! setupFile().existsAsFile())
        setupFile().replaceWithText (juce::Time::getCurrentTime().toISO8601 (true));
}

bool backgroundScanAtStart()
{
    return scanAtStartFile().loadFileAsString().trim() != "0";
}

void setBackgroundScanAtStart (bool on)
{
    scanAtStartFile().replaceWithText (on ? "1" : "0");
}
} // namespace lynx
