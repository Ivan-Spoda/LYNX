#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "ui/EditorTools.h"

namespace lynx
{
// FL-style Playlist (arrangement): tracks vertically, time horizontally.
//  - a tool strip (Pencil, Brush, Eraser, Mute, Knife, Lasso, Zoom, Listen, Slip); with the Pencil:
//    left click on empty space "paints" the current source (and starts dragging it), dragging a clip moves
//    it (time and track), its edges trim it without moving the content (Shift+edge too), and the fade
//    handles of audio clips set the fades
//  - every tool: right click deletes (dragging deletes several; press and hold on empty space = Knife),
//    Ctrl+drag selects (Ctrl+click adds), Alt ignores the grid; Shift+drag on a clip drags a copy;
//    Alt+Shift+drag inside a clip slips its content
//  - clicking a clip makes its pattern the current one; double click opens it in the Channel Rack
//  - ruler: click sets the playback position (Song mode); Ctrl+drag makes a time selection (it loops and
//    selects the clips it touches); a double click clears it
//  - dragging an audio file into the Playlist creates an audio clip
//  - wheel scrolls, Shift+wheel horizontal, Ctrl+wheel zoom
//  - right click on a clip's name strip opens its menu (split, make unique, etc.)
//  - track header: LED mutes the track; right click renames
//  - keys (via handleKey): Delete, Backspace (global grid on/off), Ctrl+A/C/X/V, Ctrl+B (repeats the
//    time selection when there is one), Ctrl+Ins / Ctrl+Del insert / remove the time selection, Esc
class PlaylistComponent final : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                public juce::DragAndDropTarget,
                                private juce::ChangeListener,
                                private juce::Timer,
                                private juce::ScrollBar::Listener
{
public:
    // What the pencil paints (chosen in the Picker): the current pattern, an audio clip or an automation clip.
    struct Brush
    {
        ClipKind kind = ClipKind::pattern;
        int index = -1; // audio or automation channel; for patterns the current pattern is used
    };
    Brush brush;
    int knownCurrentPattern = -1;

    explicit PlaylistComponent (AudioEngine& engine);
    ~PlaylistComponent() override;

    std::function<void()> onOpenPattern;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Editing shortcuts; the main window forwards keys when the Playlist is visible.
    bool handleKey (const juce::KeyPress& key);

    // The key map's playlist.* actions (zoom presets, ripple editing). False for unknown ids.
    bool runAction (const std::string& id);

    // Ripple editing: resizing a clip's end moves the later clips on its track, deleting closes the gap.
    bool isRippleOn() const noexcept { return ripple; }
    void setRipple (bool on);
    void rippleDeleteSelection();

    // dragging audio files creates audio clips (and a channel for each)
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // files dragged from the LYNX browser
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    // For snapshots: the tool in use and a time selection over [fromBar, toBar).
    void setToolForSnapshot (EditTool tool) { tools.setTool (tool); }
    void setTimeSelectionForSnapshot (int fromBar, int toBar);

    static constexpr int headerWidth = 96;
    static constexpr int toolbarHeight = 28;
    static constexpr int timelineHeight = 24;
    static constexpr int headerHeight = toolbarHeight + timelineHeight;
    static constexpr int trackHeight = 38;
    static constexpr int scrollBarSize = 12;

    static juce::Colour patternColour (const Song& song, int pattern);

private:
    enum class Drag { none, move, resize, trimLeft, slip, erase, seek, loop, select, fadeIn, fadeOut, paint, muting, knife, zoomRange, listen, pan, timeMove, autoPoint };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;

    juce::Rectangle<int> gridArea() const;
    double tickToX (double tick) const;
    double xToTick (int x) const;
    int trackToY (int track) const;
    int yToTrack (int y) const;
    int numTracks() const;
    std::int64_t gridTicks() const; // the grid step in use (resolving Screen / Nearest note)
    std::int64_t snap (double tick, bool enabled) const;
    std::vector<std::int64_t> snapCandidates() const;
    int clipAt (int x, int y, bool& onRightEdge) const;
    int clipAt (int x, int y, bool& onRightEdge, bool& onLeftEdge) const;
    Clip makeBrushClip (int track, std::int64_t start) const; // what a click on empty space places
    void cutWithKnife (juce::Point<int> from, juce::Point<int> to, bool snapOn);
    void zoomToTicks (double from, double to);
    void zoomOut (int anchorX);
    void setTimeSelection (std::int64_t from, std::int64_t to); // also selects the clips it touches
    void insertTimeSelection();
    void deleteTimeSelection();
    // fade handle of an audio clip under the mouse: 1 = fade-in, 2 = fade-out, 0 = none
    int fadeHandleAt (int clip, juce::Point<int> p) const;
    void updateScrollBars();
    bool isSelected (int index) const;
    void setSelection (std::vector<int> indices);
    void sortClips(); // sorts while keeping the selection (no undo step)
    void showClipMenu (int clip);
    void showRulerMenu (int x);                           // markers, time signature and loop
    void editMarkerName (std::int64_t tick, int existing); // existing = index, or -1 to create
    void deleteSelection();
    void copySelection();
    void pasteClips();
    void duplicateSelection();
    void seekTo (int x);
    void paintAudioClip (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> bounds);
    void paintAutomationClip (juce::Graphics& g, const Clip& clip, juce::Rectangle<float> bounds);
    const std::vector<float>* waveformFor (const std::string& path);

    static constexpr std::size_t waveformBucket = 256; // frames per waveform point
    std::map<std::string, std::vector<float>> waveforms;
    std::map<std::string, double> waveformRates;

    void refreshArrangementBox();
    void arrangementChosen();
    void renameArrangement();
    void renameTrack (int track);

    AudioEngine& engine;
    juce::ScrollBar hBar { false }, vBar { true };
    juce::ComboBox arrangementBox;
    juce::ComboBox snapBox; // "Follow global grid" or this editor's own grid
    ToolStrip tools { true };

    double pixelsPerTick = 96.0 / (4 * ppq); // 96 px per 4-beat bar
    double scrollTicks = 0.0;
    int scrollY = 0;

    Drag drag = Drag::none;
    int dragClip = -1;
    std::vector<int> dragIndices;
    std::vector<Clip> dragOriginals;
    std::vector<int> selection, selectionBefore;
    juce::Rectangle<int> rubberBand;
    std::int64_t loopAnchor = 0;
    juce::ModifierKeys downMods;
    bool startedOnEmpty = false, erasedSomething = false, holdExpired = false, muteTarget = false;
    std::vector<int> touchedClips;
    std::int64_t nextPaintTick = 0;
    Clip paintClip;
    juce::Point<int> knifeFrom, knifeTo;
    std::size_t knownClipCount = 0;
    static inline std::vector<Clip> clipboard;
    std::int64_t grabOffset = 0;
    double playheadTick = -1.0;

    // workflow P2
    bool ripple = false;
    std::vector<Clip> rippleOriginals;   // the whole playlist when a ripple resize starts
    double panStartTicks = 0.0;
    int panStartY = 0;
    std::vector<Clip> timeMoveOriginals; // Shift+drag on the ruler: the playlist when it started
    std::int64_t timeMoveFrom = 0, timeMoveTo = 0, timeMoveAnchor = 0;
    bool timeMoveCopy = false;
    juce::uint32 lastClipEraseTime = 0;  // double right-click on a clip: mute instead of delete
    juce::Point<int> lastClipErasePos;
    // inline automation editing: points of automation clips are dragged, added on the curve, deleted by right-click
    int autoClip = -1, autoPoint = -1;
    juce::Rectangle<float> automationBody (const Clip& clip) const;
    int automationPointAt (int clip, juce::Point<int> p) const;
    bool isOnAutomationCurve (int clip, juce::Point<int> p) const;
    void zoomAroundCentre (double factor);
    void zoomToBars (int bars);
    void zoomToSelection();
    void zoomAll();
    void centreOnPlayhead();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaylistComponent)
};
// Playlist "Picker" (as in FL): list of patterns with their colors; clicking chooses the
// pattern the Playlist pencil paints; double click opens it in the Channel Rack.
class PatternPicker final : public juce::Component,
                            private juce::ChangeListener
{
public:
    PatternPicker (AudioEngine& engine, PlaylistComponent& playlist);
    ~PatternPicker() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    std::function<void()> onOpenPattern;
    static constexpr int width = 150;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    struct Row
    {
        ClipKind kind;
        int index;            // pattern, audio or automation channel; -1 = section title
        juce::String name;
        juce::Colour colour;
    };
    std::vector<Row> rows() const;
    int rowAt (int y) const;

    AudioEngine& engine;
    PlaylistComponent& playlist;
    int scroll = 0; // in rows
    static constexpr int rowHeight = 22, headerHeight = 24;
};

// The Playlist tab: the Picker on the left and the Playlist (does not own them).
class PlaylistArea final : public juce::Component
{
public:
    PlaylistArea (PatternPicker& p, PlaylistComponent& l) : picker (p), playlist (l)
    {
        addAndMakeVisible (picker);
        addAndMakeVisible (playlist);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        picker.setBounds (area.removeFromLeft (PatternPicker::width));
        playlist.setBounds (area);
    }

private:
    PatternPicker& picker;
    PlaylistComponent& playlist;
};
} // namespace lynx
