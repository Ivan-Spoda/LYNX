#include "ui/BrowserComponent.h"

#include "core/FlpImport.h"
#include "core/SearchQuery.h"
#include "engine/PluginHost.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

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

Icon iconFor (const juce::File& f, bool folder)
{
    if (folder)
        return Icon::folder;
    if (isAudio (f))
        return Icon::audioFile;
    if (f.hasFileExtension ("mid;midi"))
        return Icon::pianoRoll;
    if (f.hasFileExtension ("lynx;flp"))
        return Icon::project;
    if (f.hasFileExtension ("fst"))
        return Icon::preset;
    return Icon::file;
}

// The hint for a file or folder row: what the mouse can do with it.
juce::String rowHint (const juce::File& f, bool folder, bool expanded)
{
    if (folder)
        return (expanded ? tr ("Folder: [LMB] close") : tr ("Folder: [LMB] open")) + "  " + tr ("[RMB] menu (collapse all, show in the file manager)");
    if (isAudio (f))
        return tr ("Sample: [LMB] preview  [2xLMB] new channel  drag to the Channel Rack, Playlist or Piano Roll  [Enter] into the selected channel  "
                   "[Shift][Up] / [Shift][Down] swap through the files");
    if (f.hasFileExtension ("mid;midi"))
        return tr ("MIDI file: [2xLMB] open  drag onto the Piano Roll: its notes go into the channel being edited");
    if (f.hasFileExtension ("lynx;flp"))
        return tr ("Project: [2xLMB] open it");
    if (f.hasFileExtension ("fst"))
        return tr ("Preset: [2xLMB] new channel with it (FL synth presets)  drag onto a mixer insert: effect preset");
    return {};
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

// --- the tree (and, flat, the search results) ---------------------------------------------------------

class BrowserComponent::FileTree final : public juce::Component,
                                         public ui::HintSource
{
public:
    static constexpr int rowHeight = 22;

    FileTree (BrowserComponent& b, bool isFlat) : browser (b), flat (isFlat)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    struct Node
    {
        juce::File file;
        bool folder = false, expanded = false, loaded = false;
        int depth = 0;
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    // tree mode: the contents of `folder`
    void setRoot (const juce::File& folder)
    {
        root = std::make_unique<Node>();
        root->file = folder;
        root->folder = root->expanded = true;
        root->depth = -1;
        load (*root);
        selected = hovered = -1;
        rebuild();
    }

    // flat mode: a list of files with their path relative to `base`
    void setFiles (const juce::Array<juce::File>& files, const juce::File& newBase, const juce::String& newStatus)
    {
        base = newBase;
        status = newStatus;
        flatNodes.clear();
        for (const auto& f : files)
        {
            auto n = std::make_unique<Node>();
            n->file = f;
            flatNodes.push_back (std::move (n));
        }
        selected = hovered = -1;
        rebuild();
    }

    void setStatus (const juce::String& text)
    {
        status = text;
        repaint();
    }

    juce::File selectedFile() const
    {
        return juce::isPositiveAndBelow (selected, (int) rows.size()) ? rows[(std::size_t) selected]->file : juce::File();
    }

    bool selectedIsFolder() const { return juce::isPositiveAndBelow (selected, (int) rows.size()) && rows[(std::size_t) selected]->folder; }
    int numRows() const { return (int) rows.size(); }
    int getSelectedRow() const { return selected; }

    void select (int row, bool preview)
    {
        row = rows.empty() ? -1 : juce::jlimit (0, (int) rows.size() - 1, row);
        if (row != selected)
        {
            repaintRow (selected);
            selected = row;
            repaintRow (selected);
        }
        scrollToRow (row);
        if (row >= 0)
            browser.fileSelected (rows[(std::size_t) row]->file, preview && ! rows[(std::size_t) row]->folder);
    }

    // Left / Right: close the selected folder (or go to its parent) / open it
    void openSelected (bool open)
    {
        if (! juce::isPositiveAndBelow (selected, (int) rows.size()))
            return;
        auto* n = rows[(std::size_t) selected];

        if (n->folder && n->expanded != open)
        {
            toggle (*n);
            return;
        }

        if (! open && n->parent != nullptr && n->parent != root.get())
            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(std::size_t) i] == n->parent)
                {
                    select (i, false);
                    return;
                }
    }

    void collapseAll()
    {
        if (root == nullptr)
            return;
        std::function<void (Node&)> close = [&close] (Node& n) {
            for (auto& c : n.children)
            {
                c->expanded = false;
                close (*c);
            }
        };
        close (*root);
        selected = hovered = -1;
        rebuild();
    }

    void rebuild()
    {
        rows.clear();

        if (flat)
            for (auto& n : flatNodes)
                rows.push_back (n.get());
        else if (root != nullptr)
            addRows (*root);

        updateSize();
        repaint();
    }

    void updateSize()
    {
        if (auto* parent = getParentComponent())
        {
            const auto width = parent->getWidth();
            const auto viewHeight = parent->getParentComponent() != nullptr ? parent->getParentComponent()->getHeight() : parent->getHeight();
            setSize (width, juce::jmax (viewHeight, (int) rows.size() * rowHeight));
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        g.fillAll (bg2);
        const auto clip = g.getClipBounds();
        const auto first = juce::jmax (0, clip.getY() / rowHeight), last = juce::jmin ((int) rows.size() - 1, clip.getBottom() / rowHeight);
        const auto previewing = browser.isPreviewPlaying() ? browser.previewedFile : juce::File();

        for (int i = first; i <= last; ++i)
        {
            const auto& n = *rows[(std::size_t) i];
            auto r = juce::Rectangle<int> (0, i * rowHeight, getWidth(), rowHeight);

            if (i == selected)
            {
                g.setColour (accentMuted);
                g.fillRect (r);
                g.setColour (accent);
                g.fillRect (r.withWidth (2));
            }
            else if (i == hovered)
            {
                g.setColour (bg4);
                g.fillRect (r);
            }

            const auto indent = flat ? 6 : 6 + n.depth * 14;
            auto x = (float) indent;

            if (n.folder) // a chevron: right when closed, down when open
            {
                juce::Path p;
                const auto c = juce::Point<float> (x + 5.0f, (float) r.getCentreY());
                if (n.expanded)
                    p.addTriangle (c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
                else
                    p.addTriangle (c.x - 2.0f, c.y - 3.5f, c.x - 2.0f, c.y + 3.5f, c.x + 2.5f, c.y);
                g.setColour (i == hovered || i == selected ? textPrimary : textSecondary);
                g.fillPath (p);
            }
            x += 12.0f;

            const auto playing = ! n.folder && n.file == previewing;
            icons::draw (g, iconFor (n.file, n.folder), juce::Rectangle<float> (x, (float) r.getY() + 3.0f, 16.0f, 16.0f),
                         playing ? accent : n.folder ? warning.withAlpha (0.85f) : textSecondary);
            x += 21.0f;

            auto text = r.withLeft ((int) x).withTrimmedRight (6);

            if (flat) // the folder it lives in, on the right
            {
                const auto where = n.file.getParentDirectory().getRelativePathFrom (base);
                g.setFont (theme::font (theme::type::caption));
                g.setColour (textDisabled);
                const auto w = juce::jmin (text.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), where) + 4);
                g.drawText (where, text.removeFromRight (w), juce::Justification::centredRight, true);
            }

            g.setFont (theme::font (theme::type::body, n.folder && n.expanded));
            g.setColour (playing ? accentHover : n.folder || i == hovered || i == selected ? textPrimary : textPrimary.withAlpha (0.86f));
            g.drawText (n.folder || ! isAudio (n.file) ? n.file.getFileName() : n.file.getFileNameWithoutExtension(), text,
                        juce::Justification::centredLeft, true);
        }

        if (rows.empty() && status.isNotEmpty())
        {
            g.setColour (textDisabled);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (status, getLocalBounds().reduced (10).withHeight (60), juce::Justification::centredTop, 3);
        }
    }

    int rowAt (int y) const
    {
        const auto row = y / rowHeight;
        return y >= 0 && row < (int) rows.size() ? row : -1;
    }

    void mouseMove (const juce::MouseEvent& e) override { setHovered (rowAt (e.y)); }
    void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        const auto row = rowAt (e.y);
        pressedRow = row;

        if (row < 0)
            return;

        auto& n = *rows[(std::size_t) row];

        if (e.mods.isPopupMenu())
        {
            select (row, false);
            browser.showFolderMenu (n.folder ? n.file : n.file.getParentDirectory());
            return;
        }

        if (n.folder)
        {
            select (row, false);
            if (e.getNumberOfClicks() == 1) // a click opens / closes it (the second click of a double click does not undo it)
                toggle (n);
            return;
        }

        // a click previews, even when the file is already selected (click again: hear it again)
        select (row, true);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStarted || e.getDistanceFromDragStart() < 5 || ! juce::isPositiveAndBelow (pressedRow, (int) rows.size()))
            return;

        const auto& n = *rows[(std::size_t) pressedRow];

        if (n.folder)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragStarted = true;
            container->startDragging (juce::String (dragDescription) + "|" + n.file.getFullPathName(), this, juce::ScaledImage (dragImage (n.file)),
                                      true);
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto row = rowAt (e.y);

        if (row >= 0 && ! rows[(std::size_t) row]->folder && e.mods.isLeftButtonDown())
            browser.openFile (rows[(std::size_t) row]->file);
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint h;
        const auto row = rowAt (p.y);

        if (row < 0)
        {
            h.text = flat ? tr ("Search results: files in the current folder and below that match") : tr ("Browser: [LMB] open a folder or preview a sample");
            return h;
        }

        const auto& n = *rows[(std::size_t) row];
        h.text = rowHint (n.file, n.folder, n.expanded);
        h.value = n.folder ? juce::String() : n.file.getFileExtension().substring (1).toUpperCase();
        h.hasMenu = true;
        return h;
    }

    // for snapshots: the first audio file under the first folders (opening them)
    int firstAudioRow()
    {
        for (int pass = 0; pass < 3; ++pass)
        {
            for (int i = 0; i < (int) rows.size(); ++i)
                if (! rows[(std::size_t) i]->folder && isAudio (rows[(std::size_t) i]->file))
                    return i;

            for (auto* n : rows)
                if (n->folder && ! n->expanded)
                {
                    toggle (*n);
                    break;
                }
        }
        return -1;
    }

    void setHovered (int row)
    {
        if (row != hovered)
        {
            repaintRow (hovered);
            hovered = row;
            repaintRow (hovered);
        }
    }

private:
    void load (Node& n)
    {
        if (n.loaded)
            return;
        n.loaded = true;

        auto folders = n.file.findChildFiles (juce::File::findDirectories, false, "*", juce::File::FollowSymlinks::noCycles);
        auto files = n.file.findChildFiles (juce::File::findFiles, false, "*", juce::File::FollowSymlinks::noCycles);
        auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
        std::sort (folders.begin(), folders.end(), byName);
        std::sort (files.begin(), files.end(), byName);

        for (const auto& f : folders)
        {
            if (f.isHidden() || f.getFileName().startsWithChar ('.'))
                continue;
            auto c = std::make_unique<Node>();
            c->file = f;
            c->folder = true;
            c->depth = n.depth + 1;
            c->parent = &n;
            n.children.push_back (std::move (c));
        }

        for (const auto& f : files)
            if (isListed (f))
            {
                auto c = std::make_unique<Node>();
                c->file = f;
                c->depth = n.depth + 1;
                c->parent = &n;
                n.children.push_back (std::move (c));
            }
    }

    void addRows (Node& n)
    {
        for (auto& c : n.children)
        {
            rows.push_back (c.get());
            if (c->folder && c->expanded)
                addRows (*c);
        }
    }

    void toggle (Node& n)
    {
        const auto keep = selectedFile();
        n.expanded = ! n.expanded;
        if (n.expanded)
            load (n);
        rebuild();

        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(std::size_t) i]->file == keep)
                selected = i;

        if (n.expanded) // show what opened
            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(std::size_t) i] == &n)
                    scrollToRow (juce::jmin ((int) rows.size() - 1, i + juce::jmin ((int) n.children.size(), 6)));
    }

    void scrollToRow (int row)
    {
        if (row < 0)
            return;
        if (auto* view = findParentComponentOfClass<juce::Viewport>())
        {
            const auto top = row * rowHeight, bottom = top + rowHeight;
            const auto y = view->getViewPositionY(), h = view->getViewHeight();
            if (top < y)
                view->setViewPosition (0, top);
            else if (bottom > y + h)
                view->setViewPosition (0, bottom - h);
        }
    }

    void repaintRow (int row)
    {
        if (row >= 0)
            repaint (0, row * rowHeight, getWidth(), rowHeight);
    }

    juce::Image dragImage (const juce::File& f) const
    {
        const auto name = f.getFileNameWithoutExtension();
        const auto font = juce::Font (theme::font (theme::type::body));
        const auto width = juce::jlimit (80, 260, juce::GlyphArrangement::getStringWidthInt (font, name) + 36);
        juce::Image image (juce::Image::ARGB, width, 24, true);
        juce::Graphics g (image);
        g.setColour (theme::colour::bg4.withAlpha (0.92f));
        g.fillRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, 23.0f, 5.0f);
        g.setColour (theme::colour::accent);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, 23.0f, 5.0f, 1.0f);
        icons::draw (g, iconFor (f, false), { 6.0f, 4.0f, 16.0f, 16.0f }, theme::colour::accent);
        g.setColour (theme::colour::textPrimary);
        g.setFont (font);
        g.drawText (name, 28, 0, width - 32, 24, juce::Justification::centredLeft, true);
        return image;
    }

    BrowserComponent& browser;
    const bool flat;
    std::unique_ptr<Node> root;
    std::vector<std::unique_ptr<Node>> flatNodes;
    std::vector<Node*> rows;
    juce::File base;
    juce::String status;
    int selected = -1, hovered = -1, pressedRow = -1;
    bool dragStarted = false;
};

// --- the preview strip: waveform of the selected sample ----------------------------------------------

class BrowserComponent::PreviewStrip final : public juce::Component,
                                             public ui::HintSource,
                                             private juce::Timer
{
public:
    explicit PreviewStrip (BrowserComponent& b) : browser (b) {}

    void show (const juce::File& f, bool playing)
    {
        if (f != file)
        {
            file = f;
            peaks.clear();
            seconds = 0.0;
            info.clear();

            if (isAudio (f))
                if (const auto sample = browser.engine.getSampleLibrary().load (f.getFullPathName()); sample != nullptr && sample->frames() > 0)
                {
                    // 256 peaks over the whole file
                    const auto frames = sample->frames();
                    constexpr std::size_t count = 256;
                    peaks.assign (count, 0.0f);
                    const auto* l = sample->leftData();
                    const auto* r = sample->rightData();
                    const auto stride = juce::jmax<std::size_t> (1, frames / (count * 64)); // look at up to 64 points per peak

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const auto from = frames * i / count, to = juce::jmax (from + 1, frames * (i + 1) / count);
                        float m = 0.0f;
                        for (auto k = from; k < to && k < frames; k += stride)
                            m = juce::jmax (m, std::abs (l[k]), std::abs (r[k]));
                        peaks[i] = m;
                    }

                    const auto top = *std::max_element (peaks.begin(), peaks.end());
                    if (top > 1.0e-6f) // normalised: quiet files are still readable
                        for (auto& p : peaks)
                            p /= top;

                    seconds = (double) frames / juce::jmax (1.0, sample->sampleRate);
                    info = juce::String (seconds, seconds < 10.0 ? 2 : 1) + " s  " + juce::String (juce::roundToInt (sample->sampleRate / 100.0) / 10.0, 1)
                           + " kHz  " + (sample->isStereo() ? tr ("stereo") : tr ("mono"));
                }
        }

        if (playing && seconds > 0.0)
        {
            startedAt = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (30);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        g.fillAll (bg1);
        g.setColour (border);
        g.fillRect (0, 0, getWidth(), 1);
        auto area = getLocalBounds().reduced (6, 4).withTrimmedTop (1);

        if (file == juce::File() || ! isAudio (file))
        {
            g.setColour (textDisabled);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (tr ("Click a sample to hear it; its waveform shows here"), area, juce::Justification::centred, 2);
            return;
        }

        auto top = area.removeFromTop (14);
        g.setFont (theme::font (theme::type::caption, true));
        g.setColour (textPrimary);
        g.drawText (file.getFileNameWithoutExtension(), top, juce::Justification::centredLeft, true);
        g.setFont (theme::font (theme::type::caption));
        g.setColour (textSecondary);
        g.drawText (info, top, juce::Justification::centredRight, true);
        area.removeFromTop (2);

        g.setColour (bg0);
        g.fillRoundedRectangle (area.toFloat(), theme::radius::sm);

        if (peaks.empty())
            return;

        const auto wave = area.toFloat().reduced (2.0f, 2.0f);
        const auto mid = wave.getCentreY();
        const auto progress = position();
        const auto playedX = wave.getX() + wave.getWidth() * (float) progress;

        for (int x = 0; x < (int) wave.getWidth(); ++x)
        {
            const auto p = peaks[(std::size_t) (x * (int) peaks.size() / juce::jmax (1, (int) wave.getWidth()))];
            const auto h = juce::jmax (0.5f, p * wave.getHeight() * 0.5f);
            const auto px = wave.getX() + (float) x;
            g.setColour (progress > 0.0 && px <= playedX ? accent : textSecondary.withAlpha (0.75f));
            g.fillRect (px, mid - h, 1.0f, h * 2.0f);
        }

        if (progress > 0.0)
        {
            g.setColour (textPrimary);
            g.fillRect (playedX, wave.getY(), 1.0f, wave.getHeight());
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (isAudio (file) && e.mods.isLeftButtonDown())
            browser.fileSelected (file, true); // hear it again
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging && isAudio (file) && e.getDistanceFromDragStart() > 5)
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            {
                dragging = true;
                container->startDragging (juce::String (dragDescription) + "|" + file.getFullPathName(), this, juce::ScaledImage(), true);
            }
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    ui::Hint hintAt (juce::Point<int>) override
    {
        ui::Hint h;
        h.text = isAudio (file) ? tr ("Sample preview: [LMB] hear it again  drag it like a file in the list") : tr ("Sample preview");
        h.value = info;
        return h;
    }

    bool isPlaying() const { return position() > 0.0 && position() < 1.0; }

    void stop()
    {
        stopTimer();
        startedAt = 0.0;
        repaint();
    }

private:
    double position() const
    {
        if (startedAt <= 0.0 || seconds <= 0.0)
            return 0.0;
        return juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - startedAt) / 1000.0 / seconds);
    }

    void timerCallback() override
    {
        if (position() >= 1.0)
        {
            stop();
            browser.tree->repaint(); // the "playing" colour goes
            browser.results->repaint();
        }
        repaint();
    }

    BrowserComponent& browser;
    juce::File file;
    std::vector<float> peaks;
    double seconds = 0.0, startedAt = 0.0;
    juce::String info;
    bool dragging = false;
};

// --- the browser -------------------------------------------------------------------------------------

bool BrowserComponent::isPreviewPlaying() const
{
    return strip != nullptr && strip->isPlaying();
}

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
    rootBox.setScrollWheelEnabled (true);
    ui::setHint (rootBox, tr ("Folder shown in the Browser: FL Studio's folders, your Documents, Music, Desktop, Downloads and the folders you add  "
                              "[LMB] choose  [Wheel] step"),
                 true);
    addAndMakeVisible (rootBox);

    searchBox.setTextToShowWhenEmpty (tr ("Search (Ctrl+F): kick -loop, snare OR clap, hat*.wav"), theme::colour::textDisabled);
    searchBox.addListener (this);
    searchBox.setEscapeAndReturnKeysConsumed (true);
    ui::setHint (searchBox, tr ("Search this folder and below: words must all match; OR, -word (not), \"exact phrase\", * and ? wildcards  [Esc] clear"));
    addAndMakeVisible (searchBox);

    tree = std::make_unique<FileTree> (*this, false);
    results = std::make_unique<FileTree> (*this, true);

    for (auto [view, list] : { std::pair { &treeView, tree.get() }, std::pair { &resultsView, results.get() } })
    {
        *view = std::make_unique<juce::Viewport>();
        (*view)->setViewedComponent (list, false);
        (*view)->setScrollBarsShown (true, false);
        (*view)->setScrollBarThickness (10);
        (*view)->setWantsKeyboardFocus (false);
    }

    addAndMakeVisible (*treeView);
    addChildComponent (*resultsView);

    strip = std::make_unique<PreviewStrip> (*this);
    addAndMakeVisible (*strip);

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
    engine.previewFile ({});
}

void BrowserComponent::setRoot (int index)
{
    if (juce::isPositiveAndBelow (index, roots.size()))
    {
        currentRoot = index;
        tree->setRoot (roots[index]);
        treeView->setViewPosition (0, 0);
    }
}

void BrowserComponent::waitForListing (int)
{
    // the tree lists folders synchronously when they open
}

void BrowserComponent::resized()
{
    auto area = getLocalBounds().reduced (4);
    rootBox.setBounds (area.removeFromTop (26));
    area.removeFromTop (4);
    searchBox.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    strip->setBounds (getLocalBounds().removeFromBottom (juce::jmin (64, getHeight() / 4)));
    area.setBottom (strip->getY() - 2);
    treeView->setBounds (area);
    resultsView->setBounds (area);

    for (auto* list : { tree.get(), results.get() })
    {
        list->setSize (area.getWidth() - 10, list->getHeight());
        list->updateSize();
    }
}

void BrowserComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour::bg2);
}

void BrowserComponent::fileSelected (const juce::File& file, bool preview)
{
    if (! isAudio (file))
        return;

    if (preview)
    {
        engine.previewFile (file);
        previewedFile = file;
    }

    strip->show (file, preview);
    tree->repaint();
    results->repaint();
}

void BrowserComponent::showFolderMenu (const juce::File& folder)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (folder.getFileName());
    menu.addItem (tr ("Collapse all folders"), [this] { tree->collapseAll(); });
    menu.addItem (tr ("Show in the file manager"), [folder] { folder.revealToUser(); });
    menu.addSeparator();
    menu.addItem (tr ("Add folder..."), [this] { addUserFolder(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

BrowserComponent::FileTree& BrowserComponent::activeList() const
{
    return resultsView->isVisible() ? *results : *tree;
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
    resultsView->setVisible (text.isNotEmpty());
    treeView->setVisible (text.isEmpty());

    if (text.isEmpty() || ! juce::isPositiveAndBelow (currentRoot, roots.size()))
        return;

    bool truncated = false;
    const auto found = searchFiles (roots[currentRoot], text, truncated, [] { return false; });
    results->setFiles (found, roots[currentRoot], found.isEmpty() ? tr ("Nothing found for \"{0}\".", { text }) : juce::String());
    if (! found.isEmpty())
        results->select (0, false);
}

void BrowserComponent::selectFirstSampleForSnapshot()
{
    if (const auto row = tree->firstAudioRow(); row >= 0)
    {
        tree->select (row, false);
        strip->show (tree->selectedFile(), false);
        tree->setHovered (row + 1 < tree->numRows() ? row + 1 : -1);
    }
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
    resultsView->setVisible (query.isNotEmpty());
    treeView->setVisible (query.isEmpty());

    if (query.isEmpty() || ! juce::isPositiveAndBelow (currentRoot, roots.size()))
        return;

    results->setFiles ({}, roots[currentRoot], tr ("Searching {0}...", { roots[currentRoot].getFullPathName() }));
    resultsView->setViewPosition (0, 0);

    job = std::make_unique<SearchJob> (roots[currentRoot], query, [safe = juce::Component::SafePointer<BrowserComponent> (this), query,
                                                                   root = roots[currentRoot]] (juce::Array<juce::File> found, bool truncated) {
        if (safe == nullptr || safe->searchBox.getText().trim() != query)
            return;

        safe->results->setFiles (found, root, found.isEmpty() ? tr ("Nothing found for \"{0}\".", { query }) : juce::String());

        if (truncated)
            safe->results->setStatus (tr ("Showing the first {0} results.", { juce::String (maxResults) }));
    });
    job->startThread (juce::Thread::Priority::low);
}

// --- keys --------------------------------------------------------------------------------------------

juce::File BrowserComponent::getSelectedFile() const
{
    return activeList().selectedFile();
}

void BrowserComponent::step (int delta, bool load)
{
    auto& list = activeList();
    const auto count = list.numRows();

    if (count == 0)
        return;

    // the next row in that direction (search results: every row; the tree: skipping folders)
    auto row = list.getSelectedRow();
    for (int guard = 0; guard < count; ++guard)
    {
        row = row < 0 ? (delta > 0 ? 0 : count - 1) : row + delta;
        if (row < 0 || row >= count)
            return;
        list.select (row, false);
        if (! list.selectedIsFolder())
            break;
    }

    const auto file = list.selectedFile();

    if (file.existsAsFile())
        fileSelected (file, true);

    if (load && file.existsAsFile() && onLoadIntoChannel)
        onLoadIntoChannel (file);
}

void BrowserComponent::openFolder (bool open)
{
    if (! resultsView->isVisible())
        tree->openSelected (open);
}

void BrowserComponent::loadSelected()
{
    const auto file = getSelectedFile();

    if (! file.existsAsFile())
        return;

    if (! onLoadIntoChannel || ! onLoadIntoChannel (file))
        openFile (file);
}

bool BrowserComponent::isAudioFile (const juce::File& file)
{
    return isAudio (file);
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
