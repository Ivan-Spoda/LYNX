#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// File browser (like FL's Browser): FL and user sample folders, drawn by LYNX itself so it can show
// what the mouse is over and explain it in the hint bar.
//  - a click on a folder opens / closes it; a click on an audio file previews it (the strip at the bottom
//    shows its waveform and the playback position); double-click: new sampler channel with it
//  - drag a file onto the Channel Rack (new channel, or onto a row: replaces its sample), the Playlist (audio
//    clip where it is dropped) or a mixer insert (effect presets)
//  - double-click a .lynx/.flp/.mid: opens it (onOpenProject)
//  - search box (Ctrl+F): searches the current folder and below (core/SearchQuery syntax); Esc clears
//  - keys while the Browser has focus: Up/Down step and preview, Shift+Up/Down step and load each file
//    into the selected channel, Enter loads the selected file into the selected channel, Left/Right
//    close / open folders
//  - right-click: folder menu (collapse all, open in the file manager, add folder)
//  - the folder list has "Add folder..." for user folders (kept in the user settings)
class BrowserComponent final : public juce::Component,
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
    void addUserFolder(); // asks for a folder and adds it to the folder list
    void reloadUserFolders() { rebuildRoots(); } // after the list changed elsewhere (Settings > Files)
    void setSearchForSnapshot (const juce::String& text);
    // For snapshots: selects (and "previews" without sound) the first audio file under the first folder,
    // and puts the mouse over the row after it.
    void selectFirstSampleForSnapshot();
    // --snapshot: shows this audio file in the preview strip (without playing it)
    void showPreviewForSnapshot (const juce::File& file);
    // Up/Down (load = false) or Shift+Up/Down (load = true); Enter = loadSelected()
    void step (int delta, bool load);
    void loadSelected();
    juce::File getSelectedFile() const;
    // Left / Right: close / open the selected folder (Left on a file goes to its folder)
    void openFolder (bool open);

    // Drags from the Browser (tree or search results): is it one, and which file it carries.
    static bool isBrowserDrag (const juce::DragAndDropTarget::SourceDetails& details);
    static juce::File draggedFile (const juce::DragAndDropTarget::SourceDetails& details);
    static bool isAudioFile (const juce::File& file);

    void resized() override;

    // --snapshot: the tree is listed synchronously; kept for the snapshot code
    void waitForListing (int timeoutMs);
    void paint (juce::Graphics& g) override;

    static constexpr const char* dragDescription = "lynx-arquivo";
    static constexpr int addFolderId = 1000;

    class FileTree;    // the folder tree / search results (implementation detail)
    class PreviewStrip; // waveform of the selected sample

private:
    void setRoot (int index);
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void timerCallback() override;
    void runSearch();
    void rebuildRoots();
    static juce::File userFoldersFile();
    void openFile (const juce::File& file); // what a double-click does
    void fileSelected (const juce::File& file, bool preview);
    void showFolderMenu (const juce::File& folder);
    FileTree& activeList() const;
    juce::File previewedFile; // the sample last sent to the preview player
    bool isPreviewPlaying() const;

    std::unique_ptr<FileTree> tree, results;
    std::unique_ptr<juce::Viewport> treeView, resultsView;
    std::unique_ptr<PreviewStrip> strip;
    class SearchJob;
    std::unique_ptr<SearchJob> job;
    juce::TextEditor searchBox;
    std::unique_ptr<juce::FileChooser> chooser;
    int currentRoot = 0;

    AudioEngine& engine;
    juce::ComboBox rootBox;
    juce::Array<juce::File> roots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserComponent)
};
} // namespace lynx
