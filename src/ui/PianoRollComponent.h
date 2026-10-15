#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/EditorTools.h"

namespace lynx
{
// Piano Roll for the selected channel in the current pattern.
//  - a tool strip (Pencil, Brush, Eraser, Mute, Knife, Lasso, Zoom, Listen); with the Pencil:
//    left click on empty space creates a note (default length, or the last used one) and drags it,
//    dragging the body moves, the right edge resizes (the left edge too, when enabled)
//  - every tool: right click deletes (dragging deletes several; press and hold on empty grid = Knife),
//    Ctrl+drag selects (Ctrl+click adds/removes), Alt ignores the grid; Shift+drag on a note drags a copy
//  - while moving: Shift pressed after the click keeps the pitch, Ctrl keeps the time
//  - double click on a note opens the Note inspector (applies to the selection)
//  - channel selector, previous/next channel keys and ghost notes of the other channels (Alt+V);
//    double right-click a ghost note to switch to its channel
//  - the bottom lane edits velocity, pan or fine pitch (combo in its corner)
//  - keys (via handleKey): Delete, Backspace (global grid on/off), Ctrl+A/C/X/V/B, Ctrl+Q quantise,
//    Shift+Q quantise starts, Ctrl+U chop, Ctrl+G join, Ctrl+L legato, Shift+D shrink to grid,
//    Ctrl+Shift+Left/Right rotate, Ctrl+M mute, Ctrl+P porta, Esc, arrows (Shift = octave/bar)
class PianoRollComponent final : public juce::Component,
                                 public SharesTitleBar,
                                 private juce::ChangeListener,
                                 private juce::Timer,
                                 private juce::ScrollBar::Listener
{
public:
    explicit PianoRollComponent (AudioEngine& engine);
    ~PianoRollComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void setTitleBarInsets (int left, int right) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Editing shortcuts; the main window forwards keys when the Piano Roll is visible.
    bool handleKey (const juce::KeyPress& key);

    // Centers the view on the channel's notes (called on open).
    void scrollToNotes();

    // For tests/snapshots: selects all of the channel's notes.
    void selectAll();

    // For snapshots: the tool in use and the Note inspector of the selection.
    void setToolForSnapshot (EditTool tool) { tools.setTool (tool); }
    std::unique_ptr<juce::Component> makeNoteInspectorForSnapshot (bool singleNote = false);

    static constexpr int keyboardWidth = 64;
    static constexpr int toolbarHeight = 28;
    static constexpr int timelineHeight = 22;
    static constexpr int headerHeight = toolbarHeight + timelineHeight;
    static constexpr int velocityHeight = 72;
    static constexpr int rowHeight = 14;
    static constexpr int scrollBarSize = 12;

private:
    enum class Drag { none, move, resize, resizeLeft, erase, keyboard, select, velocity, paint, muting, knife, zoomRange, listen };

    // Channel selector: the wheel steps through the channels with notes (Ctrl+wheel: all channels).
    class ChannelBox final : public juce::ComboBox
    {
    public:
        std::function<void (int direction, bool all)> onStep;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (onStep && wheel.deltaY != 0.0f)
                onStep (wheel.deltaY < 0 ? 1 : -1, e.mods.isCtrlDown());
        }
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> velocityArea() const;
    double tickToX (double tick) const;
    double xToTick (int x) const;
    int keyToY (int key) const;
    int yToKey (int y) const;
    std::int64_t gridTicks() const;        // the grid step in use (resolving Screen / Nearest note)
    std::int64_t snap (double tick, bool enabled) const;
    std::int64_t viewLengthTicks() const;
    std::int64_t newNoteLength() const;
    int noteAt (int x, int y, bool& onRightEdge) const;
    int noteAt (int x, int y, bool& onRightEdge, bool& onLeftEdge) const;
    int ghostAt (int x, int y) const;      // index of a ghost note (another channel) under the point
    void refreshChannelBox();
    void stepChannel (int direction, bool withNotesOnly);
    void zoomToTicks (double from, double to);
    void zoomOut (int anchorX);
    void listenAt (int x);
    void stopListening();
    void cutWithKnife (juce::Point<int> from, juce::Point<int> to, bool snapOn);
    void paintNoteAt (std::int64_t start, int key, bool monophonic);
    void runOperation (const std::function<std::vector<int> (Pattern&, const std::vector<int>&)>& op);
    void showMoreMenu();
    void updateScrollBars();
    void previewOn (int key, float velocity);
    void previewOff();

    bool isSelected (int index) const;
    std::vector<int> channelNotes() const;           // indices of the channel's notes
    std::vector<int> selectionOrAll() const;         // selection, or all of the channel's notes if empty
    void setSelection (std::vector<int> indices);
    void sortNotes();                                // sorts while keeping the selection (no undo step)
    void editNotes (const std::function<void (Pattern&)>& edit);
    void applyVelocity (int fromX, int toX, int y);
    float laneValue (const Note& n) const;          // 0..1 of the chosen property
    void setLaneValue (Note& n, float value) const; // inverse
    bool laneIsBipolar() const;
    void toggleMute();
    void togglePorta(); // porta notes glide from the previous note (toggles the channel's porta)

    void deleteSelection();
    void copySelection();
    void paste();
    void duplicateSelection();
    void quantizeSelection();
    void nudge (std::int64_t ticks, int keys);

    AudioEngine& engine;
    juce::ScrollBar hBar { false }, vBar { true };
    ToolStrip tools { false };
    int titleInsetLeft = 0, titleInsetRight = 0; // the window's title and buttons share the toolbar row
    ChannelBox channelBox;
    juce::ComboBox snapBox;           // "Follow global grid" or this editor's own grid
    IconButton ghostButton { "ghosts", Icon::ghost };
    IconButton moreButton { "more", Icon::more }; // note operations, left-edge resize, MIDI import/export
    juce::ComboBox laneBox; // what the bottom lane edits: velocity, pan or fine pitch
    juce::ComboBox chordBox; // chord created on each click (like FL's "stamp")
    juce::TextButton slideButton { "Slide" }; // new notes slide (like FL's slide tool)
    juce::ComboBox scaleBox; // scale highlighted on the keyboard rows
    std::vector<int> chordIntervals() const;
    bool inScale (int key) const;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::PopupMenu midiMenu();
    juce::Label hintLabel;

    double pixelsPerTick = 24.0 / ticksPerStep; // 24 px per step
    double scrollTicks = 0.0;
    int scrollY = (127 - 84) * rowHeight;       // starts showing roughly C6..C3

    Drag drag = Drag::none;
    int dragNote = -1;              // grabbed note (reference for snap and preview)
    std::vector<int> dragIndices;   // notes that follow the drag
    std::vector<Note> dragOriginals;
    std::int64_t grabOffset = 0;    // note tick - mouse tick at drag start
    std::int64_t lastLength = ticksPerStep * 2;
    int previewKey = -1;
    double playheadTick = -1.0;

    std::vector<int> selection;     // indices into Pattern::notes, sorted
    std::vector<int> selectionBefore; // selection at the start of a Shift+rectangle
    juce::ModifierKeys downMods;      // modifiers at the click (locks start when Shift/Ctrl are pressed later)
    bool showGhosts = true;
    bool resizeFromLeft = false;      // opt-in: the left edge of a note resizes it
    bool startedOnEmpty = false, erasedSomething = false, holdExpired = false;
    juce::uint32 createdAt = 0;       // when the last click created a note (a double click there is not an edit)
    std::int64_t nextPaintTick = 0, paintLength = 0;
    bool muteTarget = false;
    std::vector<int> touchedNotes;    // notes already muted/unmuted during this drag
    juce::Point<int> knifeFrom, knifeTo;
    std::vector<int> listenKeys;
    std::vector<std::int64_t> snapCandidates() const;
    juce::Rectangle<int> rubberBand;
    int lastVelocityX = 0;
    int knownPattern = -1, knownChannel = -1;
    std::size_t knownNoteCount = 0;

    static inline std::vector<Note> clipboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
} // namespace lynx
