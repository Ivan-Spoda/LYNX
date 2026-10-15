#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// File browser (like FL's Browser): FL and user sample folders.
//  - click an audio file: preview; double-click: new sampler channel with it
//  - drag onto the Channel Rack or the Playlist (like dragging from Explorer)
//  - double-click a .lynx/.flp/.mid: opens it (onOpenProject)
//  - search box (Ctrl+F): searches the current folder and below (core/SearchQuery syntax); Esc clears
//  - keys while the Browser has focus: Up/Down step and preview, Shift+Up/Down step and load each file
//    into the selected channel, Enter loads the selected file into the selected channel
//  - the folder list has "Add folder..." for user folders (kept in the user settings)
class BrowserComponent final : public juce::Component,
                               private juce::FileBrowserListener,
                               private juce::TextEditor::Listener,
                               private juce::Timer
{
public:
    explicit BrowserComponent (AudioEngine& engine);
    ~BrowserComponent() override;

    std::function<void (const juce::File&)> onOpenProject;
    // Enter / Shift+Up/Down: put this file into the selected channel; returns false if it can't take it.
    std::function<bool (const juce::File&)> onLoadIntoChannel;

    void focusSearch();
    void setSearchForSnapshot (const juce::String& text);
    // Up/Down (load = false) or Shift+Up/Down (load = true); Enter = loadSelected()
    void step (int delta, bool load);
    void loadSelected();
    juce::File getSelectedFile() const;

    // Drags from the Browser (tree or search results): is it one, and which file it carries.
    static bool isBrowserDrag (const juce::DragAndDropTarget::SourceDetails& details);
    static juce::File draggedFile (const juce::DragAndDropTarget::SourceDetails& details);

    void resized() override;

    // --snapshot: waits (up to timeoutMs) for the folder listing so the tree is drawn filled
    void waitForListing (int timeoutMs);
    void paint (juce::Graphics& g) override;

    static constexpr const char* dragDescription = "lynx-arquivo";
    static constexpr int addFolderId = 1000;

private:
    void selectionChanged() override;
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File& file) override;
    void browserRootChanged (const juce::File&) override {}
    void setRoot (int index);
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void timerCallback() override;
    void runSearch();
    void addUserFolder();
    void rebuildRoots();
    static juce::File userFoldersFile();
    void openFile (const juce::File& file); // what a double-click does

    class Results;
    std::unique_ptr<Results> results;
    class SearchJob;
    std::unique_ptr<SearchJob> job;
    juce::TextEditor searchBox;
    std::unique_ptr<juce::FileChooser> chooser;
    int currentRoot = 0;

    AudioEngine& engine;
    juce::ComboBox rootBox;
    juce::Array<juce::File> roots;
    juce::TimeSliceThread thread { "LYNX browser" };
    juce::WildcardFileFilter filter { "*.wav;*.flac;*.ogg;*.mp3;*.aif;*.aiff;*.mid;*.midi;*.lynx;*.flp;*.fst", "*", "files" };
    juce::DirectoryContentsList list { &filter, thread };
    juce::FileTreeComponent tree { list };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserComponent)
};
} // namespace lynx
