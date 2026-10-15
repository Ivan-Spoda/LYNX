#include "ui/BrowserComponent.h"

#include "core/FlpImport.h"
#include "core/SearchQuery.h"
#include "engine/PluginHost.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
bool isAudio (const juce::File& f)
{
    return f.hasFileExtension ("wav;flac;ogg;mp3;aif;aiff;wv");
}

bool isListed (const juce::File& f)
{
    return f.hasFileExtension ("wav;flac;ogg;mp3;aif;aiff;mid;midi;lynx;flp;fst");
}

constexpr int maxResults = 500;

juce::Array<juce::File> searchFiles (const juce::File& root, const juce::String& query, bool& truncated, const std::function<bool()>& shouldStop)
{
    const auto parsed = search::parse (query.toStdString());
    juce::Array<juce::File> found;
    truncated = false;

    for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*", juce::File::findFiles))
    {
        if (shouldStop())
            break;

        const auto file = entry.getFile();

        if (! isListed (file) || ! search::matches (parsed, file.getFileName().toStdString()))
            continue;

        found.add (file);

        if (found.size() >= maxResults)
        {
            truncated = true;
            break;
        }
    }

    return found;
}
} // namespace

// --- search: a background walk of the current folder ------------------------------------------------

class BrowserComponent::SearchJob final : public juce::Thread
{
public:
    SearchJob (juce::File r, juce::String q, std::function<void (juce::Array<juce::File>, bool)> d)
        : juce::Thread ("LYNX browser search"), root (std::move (r)), query (std::move (q)), done (std::move (d))
    {
    }

    ~SearchJob() override { stopThread (3000); }

    void run() override
    {
        bool truncated = false;
        const auto found = searchFiles (root, query, truncated, [this] { return threadShouldExit(); });

        if (threadShouldExit())
            return;

        juce::MessageManager::callAsync ([done = done, found, truncated] { done (found, truncated); });
    }

private:
    juce::File root;
    juce::String query;
    std::function<void (juce::Array<juce::File>, bool)> done;
};

class BrowserComponent::Results final : public juce::ListBox,
                                        private juce::ListBoxModel
{
public:
    explicit Results (BrowserComponent& b) : browser (b)
    {
        setModel (this);
        setRowHeight (22);
        setColour (juce::ListBox::backgroundColourId, theme::colour::bg2);
        setOutlineThickness (0);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    juce::Array<juce::File> files;
    juce::File root;
    juce::String status;

    juce::File selected() const
    {
        const auto row = getSelectedRow();
        return juce::isPositiveAndBelow (row, files.size()) ? files[row] : juce::File();
    }

    int getNumRows() override { return files.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected) override
    {
        if (! juce::isPositiveAndBelow (row, files.size()))
            return;

        if (isSelected)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
        }

        const auto& f = files[row];
        g.setFont (theme::font (theme::type::body));
        g.setColour (theme::colour::textPrimary);
        g.drawText (f.getFileName(), 6, 0, width - 12, height, juce::Justification::centredLeft, true);
        g.setFont (theme::font (theme::type::caption));
        g.setColour (theme::colour::textDisabled);
        g.drawText (f.getParentDirectory().getRelativePathFrom (root), 6, 0, width - 12, height, juce::Justification::centredRight, true);
    }

    void paint (juce::Graphics& g) override
    {
        juce::ListBox::paint (g);

        if (files.isEmpty())
        {
            g.setColour (theme::colour::textDisabled);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (status, getLocalBounds().reduced (10), juce::Justification::centredTop, 3);
        }
    }

    void selectedRowsChanged (int row) override
    {
        if (juce::isPositiveAndBelow (row, files.size()) && isAudio (files[row]))
            browser.engine.previewFile (files[row]);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow (row, files.size()))
            browser.openFile (files[row]);
    }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        if (rows.isEmpty() || ! juce::isPositiveAndBelow (rows[0], files.size()))
            return {};
        return juce::String (dragDescription) + "|" + files[rows[0]].getFullPathName();
    }

private:
    BrowserComponent& browser;
};

BrowserComponent::BrowserComponent (AudioEngine& e)
    : engine (e)
{
    rebuildRoots();

    rootBox.onChange = [this] {
        const auto id = rootBox.getSelectedId();
        if (id == addFolderId)
        {
            rootBox.setSelectedId (currentRoot + 1, juce::dontSendNotification);
            addUserFolder();
            return;
        }
        setRoot (id - 1);
        if (searchBox.getText().isNotEmpty())
            runSearch();
    };
    rootBox.setWantsKeyboardFocus (false);
    rootBox.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (rootBox);

    searchBox.setTextToShowWhenEmpty (tr ("Search (Ctrl+F): kick -loop, snare OR clap, hat*.wav"), theme::colour::textDisabled);
    searchBox.addListener (this);
    searchBox.setEscapeAndReturnKeysConsumed (true);
    addAndMakeVisible (searchBox);

    results = std::make_unique<Results> (*this);
    addChildComponent (*results);

    tree.setItemHeight (20);
    tree.setDragAndDropDescription (dragDescription);
    tree.addListener (this);
    tree.setColour (juce::TreeView::backgroundColourId, theme::colour::bg2);
    tree.setWantsKeyboardFocus (false);
    addAndMakeVisible (tree);

    thread.startThread (juce::Thread::Priority::low);

    if (! roots.isEmpty())
    {
        rootBox.setSelectedId (1, juce::dontSendNotification);
        setRoot (0);
    }
}

juce::File BrowserComponent::userFoldersFile()
{
    return PluginHost::settingsFolder().getChildFile ("browser-folders.txt");
}

void BrowserComponent::rebuildRoots()
{
    roots.clear();
    rootBox.clear (juce::dontSendNotification);

    // FL folders (if installed) and user folders
    const auto programs = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory);
    juce::File fl;

    for (const auto& dir : programs.getChildFile ("Image-Line").findChildFiles (juce::File::findDirectories, false, "FL Studio*"))
        if (dir.getChildFile ("Data").isDirectory() && dir.getFileName() > fl.getFileName()) // "FL Studio ASIO" is the driver
            fl = dir;

    auto add = [this] (const juce::String& name, const juce::File& folder) {
        if (folder.isDirectory())
        {
            roots.add (folder);
            rootBox.addItem (name, roots.size());
        }
    };

    const auto data = fl.getChildFile ("Data");
    add (tr ("FL samples (Packs)"), data.getChildFile ("Patches").getChildFile ("Packs"));
    add (tr ("FL demo projects"), data.getChildFile ("Demo projects"));
    add (tr ("FL MIDI"), data.getChildFile ("Patches").getChildFile ("Scores"));
    add (tr ("3x Osc presets (FL)"), data.getChildFile ("Patches").getChildFile ("Channel presets").getChildFile ("3x Osc"));
    add (tr ("Synth presets (FL)"), data.getChildFile ("Patches").getChildFile ("Plugin presets").getChildFile ("Generators"));
    add (tr ("Documents"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    add (tr ("Music"), juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    add (tr ("Desktop"), juce::File::getSpecialLocation (juce::File::userDesktopDirectory));
    add (tr ("Downloads"), juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"));

    for (const auto& line : juce::StringArray::fromLines (userFoldersFile().loadFileAsString()))
        if (line.trim().isNotEmpty())
            add (juce::File (line.trim()).getFileName(), juce::File (line.trim()));

    rootBox.addSeparator();
    rootBox.addItem (tr ("Add folder..."), addFolderId);
}

void BrowserComponent::addUserFolder()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Add a folder to the Browser"), juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
        const auto folder = fc.getResult();
        if (! folder.isDirectory())
            return;

        auto lines = juce::StringArray::fromLines (userFoldersFile().loadFileAsString());
        lines.removeEmptyStrings();
        lines.addIfNotAlreadyThere (folder.getFullPathName());
        userFoldersFile().replaceWithText (lines.joinIntoString ("\n"));
        rebuildRoots();

        for (int i = 0; i < roots.size(); ++i)
            if (roots[i] == folder)
            {
                rootBox.setSelectedId (i + 1, juce::dontSendNotification);
                setRoot (i);
            }
    });
}

BrowserComponent::~BrowserComponent()
{
    job.reset();
    tree.removeListener (this);
    engine.previewFile ({});
    thread.stopThread (2000);
}

void BrowserComponent::setRoot (int index)
{
    if (juce::isPositiveAndBelow (index, roots.size()))
    {
        currentRoot = index;
        list.setDirectory (roots[index], true, true);
    }
}

void BrowserComponent::waitForListing (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (list.isStillLoading() && juce::Time::getMillisecondCounter() < end)
        juce::Thread::sleep (20);
    list.dispatchPendingMessages();
    // the tree lays new items out asynchronously; re-setting the root item does it now
    if (auto* root = tree.getRootItem())
    {
        tree.setRootItem (nullptr);
        tree.setRootItem (root);
    }
}

void BrowserComponent::resized()
{
    auto area = getLocalBounds().reduced (4);
    rootBox.setBounds (area.removeFromTop (26));
    area.removeFromTop (4);
    searchBox.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    tree.setBounds (area);
    results->setBounds (area);
}

void BrowserComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour::bg2);
}

void BrowserComponent::selectionChanged()
{
    const auto file = tree.getSelectedFile();

    if (isAudio (file))
        engine.previewFile (file);
}

void BrowserComponent::fileDoubleClicked (const juce::File& file)
{
    openFile (file);
}

// --- search ------------------------------------------------------------------------------------------

void BrowserComponent::focusSearch()
{
    searchBox.grabKeyboardFocus();
    searchBox.selectAll();
}

void BrowserComponent::setSearchForSnapshot (const juce::String& text)
{
    // synchronous: the snapshot is drawn before any message is dispatched
    searchBox.setText (text, juce::dontSendNotification);
    job.reset();
    results->setVisible (text.isNotEmpty());
    tree.setVisible (text.isEmpty());

    if (text.isEmpty() || ! juce::isPositiveAndBelow (currentRoot, roots.size()))
        return;

    bool truncated = false;
    results->root = roots[currentRoot];
    results->files = searchFiles (roots[currentRoot], text, truncated, [] { return false; });
    results->status = results->files.isEmpty() ? tr ("Nothing found for \"{0}\".", { text }) : juce::String();
    results->updateContent();
    results->selectRow (results->files.isEmpty() ? -1 : 0, true, true);
}

void BrowserComponent::textEditorTextChanged (juce::TextEditor&)
{
    startTimer (250); // search once typing pauses
}

void BrowserComponent::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    searchBox.clear();
    runSearch();
}

void BrowserComponent::textEditorReturnKeyPressed (juce::TextEditor&)
{
    stopTimer();
    runSearch();
}

void BrowserComponent::timerCallback()
{
    stopTimer();
    runSearch();
}

void BrowserComponent::runSearch()
{
    job.reset();
    const auto query = searchBox.getText().trim();
    results->setVisible (query.isNotEmpty());
    tree.setVisible (query.isEmpty());

    if (query.isEmpty() || ! juce::isPositiveAndBelow (currentRoot, roots.size()))
        return;

    results->files.clear();
    results->root = roots[currentRoot];
    results->status = tr ("Searching {0}...", { roots[currentRoot].getFullPathName() });
    results->updateContent();
    results->repaint();

    job = std::make_unique<SearchJob> (roots[currentRoot], query, [safe = juce::Component::SafePointer<BrowserComponent> (this), query] (juce::Array<juce::File> found, bool truncated) {
        if (safe == nullptr || safe->searchBox.getText().trim() != query)
            return;

        auto& r = *safe->results;
        r.files = found;
        r.status = found.isEmpty() ? tr ("Nothing found for \"{0}\".", { query }) : juce::String();
        r.updateContent();
        r.selectRow (-1);
        r.repaint();

        if (truncated)
            r.status = tr ("Showing the first {0} results.", { juce::String (maxResults) });
    });
    job->startThread (juce::Thread::Priority::low);
}

// --- keys --------------------------------------------------------------------------------------------

juce::File BrowserComponent::getSelectedFile() const
{
    return results->isVisible() ? results->selected() : tree.getSelectedFile();
}

void BrowserComponent::step (int delta, bool load)
{
    juce::File file;

    if (results->isVisible())
    {
        const auto count = results->files.size();
        if (count == 0)
            return;
        const auto row = juce::jlimit (0, count - 1, results->getSelectedRow() < 0 ? 0 : results->getSelectedRow() + delta);
        results->selectRow (row);
        results->scrollToEnsureRowIsOnscreen (row);
        file = results->selected();
    }
    else
    {
        // walk the visible rows of the tree, skipping folders
        auto* selected = tree.getSelectedItem (0);
        auto row = selected != nullptr ? selected->getRowNumberInTree() : -1;

        for (int guard = 0; guard < tree.getNumRowsInTree(); ++guard)
        {
            row += delta;
            auto* item = tree.getItemOnRow (row);
            if (item == nullptr)
                return;
            item->setSelected (true, true);
            tree.scrollToKeepItemVisible (item);
            file = tree.getSelectedFile();
            if (! file.isDirectory())
                break;
        }
    }

    if (load && file.existsAsFile() && onLoadIntoChannel)
        onLoadIntoChannel (file);
}

void BrowserComponent::loadSelected()
{
    const auto file = getSelectedFile();

    if (! file.existsAsFile())
        return;

    if (! onLoadIntoChannel || ! onLoadIntoChannel (file))
        openFile (file);
}

bool BrowserComponent::isBrowserDrag (const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto text = details.description.toString();
    return text == dragDescription || text.startsWith (juce::String (dragDescription) + "|");
}

juce::File BrowserComponent::draggedFile (const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto text = details.description.toString();

    if (text.startsWith (juce::String (dragDescription) + "|"))
        return juce::File (text.fromFirstOccurrenceOf ("|", false, false));

    if (auto* tree = dynamic_cast<juce::FileTreeComponent*> (details.sourceComponent.get()))
        return tree->getSelectedFile();

    return {};
}

void BrowserComponent::openFile (const juce::File& file)
{
    if (isAudio (file))
    {
        const auto path = file.getFullPathName().toStdString();
        const auto name = file.getFileNameWithoutExtension().toStdString();
        engine.editSong ([&] (Song& s) {
            ChannelDef c;
            c.name = name;
            c.instrument = InstrumentKind::sampler;
            c.samplePath = path;
            c.mixerTrack = s.nextFreeInsert();
            s.channels.push_back (c);
        });
        engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
        return;
    }

    // preset of an FL synth that LYNX converts (3x Osc, Sytrus, GMS...): new channel with the built-in synth
    if (file.hasFileExtension ("fst;gmsynth"))
    {
        juce::MemoryBlock data;

        if (! file.loadFileAsData (data))
            return;

        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());

        if (auto synth = synthFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize())))
        {
            const auto name = file.getFileNameWithoutExtension().toStdString();
            engine.editSong ([&] (Song& s) {
                ChannelDef c;
                c.name = name;
                c.instrument = InstrumentKind::synth;
                c.synth = *synth;
                c.mixerTrack = s.nextFreeInsert();
                s.channels.push_back (c);
            });
            engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Preset"),
                                                    tr ("LYNX reads presets from 3x Osc, Sytrus, Harmor, GMS, Harmless, MiniSynth, DX10, Fruit Kick, Ogun and Sakura."));
        return;
    }

    if (file.hasFileExtension ("lynx;flp;mid;midi") && onOpenProject)
        onOpenProject (file);
}
} // namespace lynx
