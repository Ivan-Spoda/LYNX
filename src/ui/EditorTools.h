#pragma once

// Shared pieces of the Piano Roll and Playlist editors: the edit tools, the global grid (with the
// per-editor "Follow global" choice and the default note length) and the Note inspector.

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/Icons.h"

namespace lynx
{
// --- shared title bar ---------------------------------------------------------------------
// The Playlist and Piano Roll carry their toolbar in their window's title bar, as FL Studio's editors do
// (docs/WORKFLOW_FL.md, "Layout"): the workspace frame lays the editor over its title row and keeps
// `left` px for its menu button and title and `right` px for its window buttons; the editor lays out
// its toolbar between them.
class SharesTitleBar
{
public:
    virtual ~SharesTitleBar() = default;
    virtual void setTitleBarInsets (int left, int right) = 0;
};

// --- edit tools ------------------------------------------------------------------------
// Rules shared by every tool: right-click deletes, Alt bypasses the grid, Ctrl selects
// (Ctrl+drag draws a selection box). Right-press-and-hold on empty grid gives a temporary Knife.
enum class EditTool
{
    pencil, // add, move and resize (the default gestures)
    brush,  // drag lays a run of notes / clips (Shift: one note at a time in the Piano Roll)
    eraser, // click or drag deletes
    mute,   // click or drag mutes / unmutes
    knife,  // drag a line to cut through notes / clips; a click cuts at the cursor
    lasso,  // click selects, drag draws a selection box (Shift adds)
    zoom,   // drag a range to zoom into it; a click zooms back out
    listen, // click or drag to hear notes (Piano Roll) or play from there (Playlist)
    slip    // Playlist only: drag inside a clip to move its content under it
};

struct EditToolInfo
{
    EditTool tool;
    Icon icon;
    const char* name;        // English, translated with tr()
    const char* description; // English, translated with tr()
    char key;                // single-key shortcut (only while the typing piano is off)
};

const std::vector<EditToolInfo>& editTools();
const EditToolInfo& editToolInfo (EditTool tool);

// A row of radio icon buttons, one per tool.
class ToolStrip final : public juce::Component
{
public:
    explicit ToolStrip (bool includeSlip);

    EditTool getTool() const noexcept { return tool; }
    void setTool (EditTool newTool);
    int getIdealWidth (int height) const;

    std::function<void (EditTool)> onChange;

    void resized() override;

private:
    EditTool tool = EditTool::pencil;
    std::vector<std::unique_ptr<IconButton>> buttons;
    std::vector<EditTool> tools;
};

// --- grid --------------------------------------------------------------------------------
namespace grid
{
enum class Kind
{
    none,    // free (whole ticks)
    screen,  // follows the lines the editor is drawing at its zoom
    nearest, // the nearest note / clip edge (and ghost notes); otherwise the screen grid
    ticks,   // a fixed fraction of a step or beat
    bar      // whole bars
};

struct Option
{
    Kind kind;
    int ticks; // for Kind::ticks
    const char* name;
};

const std::vector<Option>& options();
constexpr int noneOption = 0;
constexpr int screenOption = 1;

// Editor combo: id 1 = "Follow global", id i + 2 = options()[i]. Global combo: id i + 1.
constexpr int followId = 1;
void fillEditorBox (juce::ComboBox& box);
void fillGlobalBox (juce::ComboBox& box);

// The option an editor combo stands for (resolving "Follow global").
const Option& resolve (const juce::ComboBox& editorBox);
} // namespace grid

// State the editors share with the main window's toolbar. Listeners hear about every change.
class EditorGlobals final : public juce::ChangeBroadcaster
{
public:
    static EditorGlobals& get();

    int getGridOption() const noexcept { return gridOption; }
    void setGridOption (int option);
    void toggleGrid(); // Backspace: between the last grid and "No snap"

    // Length of new notes in ticks; 0 = the length of the last note clicked or resized.
    std::int64_t getDefaultNoteLength() const noexcept { return defaultNoteLength; }
    void setDefaultNoteLength (std::int64_t ticks);

    // While the typing piano owns the letter keys, the editors' single-key shortcuts (tools, channel
    // switching) are off. The main window's typing-piano toggle (P1 #14) sets this.
    bool typingPiano = true;

private:
    EditorGlobals() = default;
    int gridOption = grid::screenOption, lastGridOption = grid::screenOption;
    std::int64_t defaultNoteLength = 0;
};

// The global grid selector of the main toolbar: left-click picks the grid, right-click sets the
// default note length.
class GlobalGridBox final : public juce::ComboBox,
                            private juce::ChangeListener
{
public:
    GlobalGridBox();
    ~GlobalGridBox() override;

    void mouseDown (const juce::MouseEvent& e) override;
    static void showDefaultLengthMenu (juce::Component* target);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

// --- Note inspector ------------------------------------------------------------------------
// Edits velocity, pan, release, fine pitch, mod X/Y, slide and porta of `indices` (notes of the current
// pattern) as one undo step on Apply; Revert puts the controls back. Time and length are offered when a
// single note is inspected. `focus` is the note whose values are shown.
std::unique_ptr<juce::Component> makeNoteInspector (AudioEngine& engine, std::vector<int> indices, int focus);

// Opens the inspector in a call-out pointing at `screenArea`.
void showNoteInspector (AudioEngine& engine, std::vector<int> indices, int focus, juce::Rectangle<int> screenArea);
} // namespace lynx
