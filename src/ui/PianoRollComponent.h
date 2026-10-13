#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Piano Roll for the selected channel in the current pattern.
//  - left click on empty space creates a note (with the last used length) and starts dragging it
//  - dragging the body moves; dragging the right edge changes the length (selected notes together)
//  - Ctrl+drag selects with a rectangle; Ctrl+click adds/removes a note from the selection;
//    Shift+drag on a note drags a copy (as in FL)
//  - right click deletes (dragging deletes several)
//  - velocity lane at the bottom: click/drag draws velocity (only on selected notes, if any)
//  - Alt disables snap; mouse wheel scrolls, Shift+wheel scrolls horizontally, Ctrl+wheel zooms
//  - keyboard on the left plays the note
//  - the bottom lane edits velocity, pan or fine pitch (combo in its corner)
//  - keys (via handleKey): Delete, Ctrl+A/C/X/V/B/Q, Ctrl+M (mute), Ctrl+P (porta), Esc, arrows (Shift = octave/bar)
// Notes from other channels appear dimmed in the background, like "ghost notes" in FL.
class PianoRollComponent final : public juce::Component,
                                 private juce::ChangeListener,
                                 private juce::Timer,
                                 private juce::ScrollBar::Listener
{
public:
    explicit PianoRollComponent (AudioEngine& engine);
    ~PianoRollComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Editing shortcuts; the main window forwards keys when the Piano Roll is visible.
    bool handleKey (const juce::KeyPress& key);

    // Centers the view on the channel's notes (called on open).
    void scrollToNotes();

    // For tests/snapshots: selects all of the channel's notes.
    void selectAll();

    static constexpr int keyboardWidth = 64;
    static constexpr int toolbarHeight = 28;
    static constexpr int timelineHeight = 22;
    static constexpr int headerHeight = toolbarHeight + timelineHeight;
    static constexpr int velocityHeight = 72;
    static constexpr int rowHeight = 14;
    static constexpr int scrollBarSize = 12;

private:
    enum class Drag { none, move, resize, erase, keyboard, select, velocity };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::Rectangle<int> gridArea() const;
    juce::Rectangle<int> velocityArea() const;
    double tickToX (double tick) const;
    double xToTick (int x) const;
    int keyToY (int key) const;
    int yToKey (int y) const;
    std::int64_t gridTicks() const;
    std::int64_t snap (double tick, bool enabled) const;
    std::int64_t viewLengthTicks() const;
    int noteAt (int x, int y, bool& onRightEdge) const;
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
    juce::ComboBox snapBox;
    juce::TextButton midiButton { "MIDI" };
    juce::ComboBox laneBox; // what the bottom lane edits: velocity, pan or fine pitch
    juce::ComboBox chordBox; // chord created on each click (like FL's "stamp")
    juce::TextButton slideButton { "Slide" }; // new notes slide (like FL's slide tool)
    juce::ComboBox scaleBox; // scale highlighted on the keyboard rows
    std::vector<int> chordIntervals() const;
    bool inScale (int key) const;
    std::unique_ptr<juce::FileChooser> chooser;
    void showMidiMenu();
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
    juce::Rectangle<int> rubberBand;
    int lastVelocityX = 0;
    int knownPattern = -1, knownChannel = -1;
    std::size_t knownNoteCount = 0;

    static inline std::vector<Note> clipboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
} // namespace lynx
