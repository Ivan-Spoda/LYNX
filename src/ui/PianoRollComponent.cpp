#include "ui/PianoRollComponent.h"

#include "core/NoteEdit.h"
#include "core/NoteTools.h"
#include "engine/MidiFileIo.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
bool isBlackKey (int key)
{
    const auto n = key % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

// FL-style name: key 60 = C5.
juce::String keyName (int key)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[key % 12]) + juce::String (key / 12);
}

const Pattern& currentPattern (const Song& s)
{
    return s.patterns[(std::size_t) s.currentPattern];
}

Pattern& currentPattern (Song& s)
{
    return s.patterns[(std::size_t) s.currentPattern];
}

struct NamedIntervals
{
    const char* name;
    std::vector<int> intervals;
};

const std::vector<NamedIntervals>& chords()
{
    static const std::vector<NamedIntervals> list {
        { TR_NOOP ("Note"), { 0 } }, { TR_NOOP ("Major"), { 0, 4, 7 } }, { TR_NOOP ("Minor"), { 0, 3, 7 } },     { "Dim", { 0, 3, 6 } },
        { "Aug", { 0, 4, 8 } },    { "Sus2", { 0, 2, 7 } },      { "Sus4", { 0, 5, 7 } },      { "7", { 0, 4, 7, 10 } },
        { "Maj7", { 0, 4, 7, 11 } }, { "m7", { 0, 3, 7, 10 } }, { "m9", { 0, 3, 7, 10, 14 } }, { TR_NOOP ("Octave"), { 0, 12 } },
    };
    return list;
}

const std::vector<notetools::ScaleInfo>& scales()
{
    return notetools::scales();
}

} // namespace


PianoRollComponent::PianoRollComponent (AudioEngine& e)
    : engine (e)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);

    for (auto* bar : { &hBar, &vBar })
    {
        bar->addListener (this);
        bar->setAutoHide (false);
        addAndMakeVisible (bar);
    }

    grid::fillEditorBox (snapBox);
    snapBox.setTooltip (tr ("Piano Roll grid: follows the global grid in the toolbar, or its own; hold Alt while dragging to ignore it"));
    snapBox.setWantsKeyboardFocus (false);
    snapBox.setMouseClickGrabsKeyboardFocus (false);
    snapBox.onChange = [this] { repaint(); };
    addAndMakeVisible (snapBox);

    tools.onChange = [this] (EditTool) {
        stopListening();
        setMouseCursor (juce::MouseCursor::NormalCursor);
    };
    addAndMakeVisible (tools);

    channelBox.setTooltip (tr ("Channel being edited (marked: has notes in this pattern). Wheel: channels with notes; Ctrl+wheel: all"));
    channelBox.setWantsKeyboardFocus (false);
    channelBox.setMouseClickGrabsKeyboardFocus (false);
    channelBox.onChange = [this] {
        const auto channel = channelBox.getSelectedId() - 1;
        if (channel >= 0 && channel < (int) engine.getSong().channels.size() && channel != engine.getSelectedChannel())
            engine.setSelectedChannel (channel);
    };
    channelBox.onStep = [this] (int direction, bool all) { stepChannel (direction, ! all); };
    addAndMakeVisible (channelBox);

    ghostButton.setClickingTogglesState (true);
    ghostButton.setToggleState (showGhosts, juce::dontSendNotification);
    ghostButton.setOnColour (theme::colour::bg6);
    ghostButton.setIconColourWhenOn (theme::colour::textPrimary);
    ghostButton.setFlat (true);
    ghostButton.setTooltip (tr ("Ghost notes: show the other channels' notes (Alt+V); double right-click one to edit its channel"));
    ghostButton.onClick = [this] {
        showGhosts = ghostButton.getToggleState();
        repaint();
    };

    moreButton.setFlat (true);
    moreButton.setTooltip (tr ("Note operations, left-edge resizing and MIDI import/export"));
    moreButton.onClick = [this] { showMoreMenu(); };

    for (auto* b : { &ghostButton, &moreButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    for (int i = 0; i < (int) chords().size(); ++i)
        chordBox.addItem (tr (chords()[(std::size_t) i].name), i + 1);
    chordBox.setSelectedId (1, juce::dontSendNotification);
    chordBox.setTooltip (tr ("Clicking creates this chord (the clicked note is the root)"));

    // scale: root x type
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int s = 0; s < (int) scales().size(); ++s)
    {
        if (s == 0)
        {
            scaleBox.addItem (tr (scales()[0].name), 1);
            continue;
        }

        for (int root = 0; root < 12; ++root)
            scaleBox.addItem (tr ("{0} {1}", { juce::String (noteNames[root]), tr (scales()[(std::size_t) s].name) }), s * 100 + root + 1);
    }
    scaleBox.setSelectedId (1, juce::dontSendNotification);
    scaleBox.setTooltip (tr ("Highlights the notes of a scale"));

    slideButton.setClickingTogglesState (true);
    slideButton.setTooltip (tr ("New notes are \"slide\" notes: the currently sounding note glides to them (808 glide)"));
    slideButton.setWantsKeyboardFocus (false);
    slideButton.setMouseClickGrabsKeyboardFocus (false);
    slideButton.setButtonText (tr ("Slide"));
    slideButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
    addAndMakeVisible (slideButton);

    for (auto* box : { &chordBox, &scaleBox })
    {
        box->setWantsKeyboardFocus (false);
        box->setMouseClickGrabsKeyboardFocus (false);
        box->onChange = [this] {
            publishScale();
            repaint();
        };
        addAndMakeVisible (box);
    }

    laneBox.addItem (tr ("Velocity"), 1);
    laneBox.addItem (tr ("Pan"), 2);
    laneBox.addItem (tr ("Fine pitch"), 3);
    laneBox.setTooltip (tr ("What the bottom lane edits"));
    laneBox.setSelectedId (1, juce::dontSendNotification);
    laneBox.setWantsKeyboardFocus (false);
    laneBox.setMouseClickGrabsKeyboardFocus (false);
    laneBox.onChange = [this] { repaint(); };
    addAndMakeVisible (laneBox);

    hintLabel.setFont (theme::font (theme::type::caption));
    hintLabel.setColour (juce::Label::textColourId, theme::colour::textDisabled.brighter (0.2f));
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setMinimumHorizontalScale (1.0f);
    hintLabel.setText (tr ("Double-click a note: inspector · Ctrl+U chop · Ctrl+G join · Ctrl+L legato · Shift+D shrink · "
                           "Shift+Q / Ctrl+Q quantise · Ctrl+Shift+arrows rotate · Backspace grid on/off · Alt+V ghosts"),
                       juce::dontSendNotification);
    hintLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (hintLabel);

    engine.addChangeListener (this);
    EditorGlobals::get().addChangeListener (this);
    refreshChannelBox();
    startTimerHz (30);
}

PianoRollComponent::~PianoRollComponent()
{
    stopListening();
    EditorGlobals::get().removeChangeListener (this);
    engine.removeChangeListener (this);
}

juce::Rectangle<int> PianoRollComponent::gridArea() const
{
    return getLocalBounds().withTrimmedLeft (keyboardWidth).withTrimmedTop (headerHeight)
        .withTrimmedRight (scrollBarSize).withTrimmedBottom (scrollBarSize + velocityHeight);
}

juce::Rectangle<int> PianoRollComponent::velocityArea() const
{
    const auto grid = gridArea();
    return { grid.getX(), grid.getBottom(), grid.getWidth(), velocityHeight };
}

double PianoRollComponent::tickToX (double tick) const
{
    return keyboardWidth + (tick - scrollTicks) * pixelsPerTick;
}

double PianoRollComponent::xToTick (int x) const
{
    return (x - keyboardWidth) / pixelsPerTick + scrollTicks;
}

int PianoRollComponent::keyToY (int key) const
{
    return headerHeight + (127 - key) * rowHeight - scrollY;
}

int PianoRollComponent::yToKey (int y) const
{
    return juce::jlimit (0, 127, 127 - (y - headerHeight + scrollY) / rowHeight);
}

std::int64_t PianoRollComponent::gridTicks() const
{
    const auto bar = (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    const auto& option = grid::resolve (snapBox);

    switch (option.kind)
    {
        case grid::Kind::none: return 1;
        case grid::Kind::ticks: return option.ticks;
        case grid::Kind::bar: return bar;
        case grid::Kind::screen:
        case grid::Kind::nearest: break;
    }

    return noteedit::screenStep (pixelsPerTick, bar);
}

std::vector<std::int64_t> PianoRollComponent::snapCandidates() const
{
    // edges of this channel's notes (except the ones being dragged) and of the visible ghosts
    std::vector<std::int64_t> out;
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto channel = engine.getSelectedChannel();

    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(std::size_t) i];

        if ((n.channel != channel && ! showGhosts) || std::find (dragIndices.begin(), dragIndices.end(), i) != dragIndices.end())
            continue;

        out.push_back (n.start);
        out.push_back (n.start + n.length);
    }

    return out;
}

std::int64_t PianoRollComponent::snap (double tick, bool enabled) const
{
    if (! enabled)
        return (std::int64_t) std::floor (tick);

    if (grid::resolve (snapBox).kind == grid::Kind::nearest)
        return noteedit::snapNearest (tick, snapCandidates(), 10.0 / pixelsPerTick, gridTicks());

    return noteedit::snapDown (tick, gridTicks());
}

std::int64_t PianoRollComponent::newNoteLength() const
{
    const auto fixed = EditorGlobals::get().getDefaultNoteLength();
    return fixed > 0 ? fixed : lastLength;
}

std::int64_t PianoRollComponent::viewLengthTicks() const
{
    const auto& song = engine.getSong();
    const auto bar = (std::int64_t) ppq * song.beatsPerBar;
    // pattern + 4 bars of headroom to grow
    return currentPattern (song).effectiveLength (song.beatsPerBar) + 4 * bar;
}

void PianoRollComponent::updateScrollBars()
{
    const auto grid = gridArea();
    const auto total = (double) viewLengthTicks();
    hBar.setRangeLimits (0.0, total, juce::dontSendNotification);
    hBar.setCurrentRange (scrollTicks, grid.getWidth() / pixelsPerTick, juce::dontSendNotification);
    vBar.setRangeLimits (0.0, 128.0 * rowHeight, juce::dontSendNotification);
    vBar.setCurrentRange (scrollY, grid.getHeight(), juce::dontSendNotification);
    scrollTicks = hBar.getCurrentRangeStart();
    scrollY = (int) vBar.getCurrentRangeStart();
}

void PianoRollComponent::resized()
{
    const auto h = toolbarHeight - 6;
    auto bar = juce::Rectangle<int> (4, 3, getWidth() - 8, h);

    // groups that don't fit in a narrow window are hidden (the tools and the grid always show)
    auto place = [&bar] (juce::Component& c, int width, bool optional = false) {
        const auto fits = ! optional || width <= bar.getWidth();
        c.setVisible (fits);
        c.setBounds (fits ? bar.removeFromLeft (juce::jmin (width, bar.getWidth())) : juce::Rectangle<int>());
        bar.removeFromLeft (juce::jmin (6, bar.getWidth()));
    };

    place (channelBox, 150);
    place (tools, tools.getIdealWidth (h));
    place (snapBox, 128);
    place (ghostButton, h);
    place (slideButton, 54, true);
    place (chordBox, 86, true);
    place (scaleBox, 150, true);
    place (moreButton, h);
    // the shortcut hint only shows when there is room for it
    const auto hintArea = juce::Rectangle<int> (bar.getX() + 4, 0, bar.getWidth() - 4, toolbarHeight);
    hintLabel.setBounds (hintArea);
    hintLabel.setVisible (hintArea.getWidth() >= 320);
    laneBox.setBounds (2, velocityArea().getY() + 4, keyboardWidth - 4, 20);
    hBar.setBounds (keyboardWidth, getHeight() - scrollBarSize, getWidth() - keyboardWidth - scrollBarSize, scrollBarSize);
    vBar.setBounds (getWidth() - scrollBarSize, headerHeight, scrollBarSize,
                    getHeight() - headerHeight - scrollBarSize - velocityHeight);
    updateScrollBars();
}

void PianoRollComponent::scrollToNotes()
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();
    int low = 128, high = -1;

    for (const auto& n : currentPattern (song).notes)
        if (n.channel == channel)
        {
            low = juce::jmin (low, n.key);
            high = juce::jmax (high, n.key);
        }

    refreshChannelBox();
    const auto centreKey = high >= 0 ? (low + high) / 2 : 66;
    scrollY = (127 - centreKey) * rowHeight - gridArea().getHeight() / 2;
    scrollTicks = 0.0;
    updateScrollBars();
    repaint();
}

// --- selection ---------------------------------------------------------------

std::vector<int> PianoRollComponent::chordIntervals() const
{
    const auto index = juce::jlimit (0, (int) chords().size() - 1, chordBox.getSelectedId() - 1);
    return chords()[(std::size_t) index].intervals;
}

int PianoRollComponent::scaleIndex() const
{
    const auto id = scaleBox.getSelectedId();
    return id <= 1 ? 0 : id / 100;
}

int PianoRollComponent::scaleRoot() const
{
    const auto id = scaleBox.getSelectedId();
    return id <= 1 ? 0 : (id % 100) - 1;
}

bool PianoRollComponent::inScale (int key) const
{
    return scaleIndex() > 0 && notetools::inScale (key, scaleRoot(), scaleIndex());
}

int PianoRollComponent::snapKey (int key) const
{
    return scaleSnap ? notetools::snapToScale (key, scaleRoot(), scaleIndex()) : key;
}

void PianoRollComponent::setScaleSnap (bool on)
{
    scaleSnap = on;
    publishScale();
    repaint();
}

void PianoRollComponent::publishScale()
{
    engine.setInputScale (scaleSnap ? scaleIndex() : 0, scaleRoot());
}

bool PianoRollComponent::isSelected (int index) const
{
    return std::binary_search (selection.begin(), selection.end(), index);
}

std::vector<int> PianoRollComponent::channelNotes() const
{
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto channel = engine.getSelectedChannel();
    std::vector<int> out;

    for (int i = 0; i < (int) notes.size(); ++i)
        if (notes[(std::size_t) i].channel == channel)
            out.push_back (i);

    return out;
}

std::vector<int> PianoRollComponent::selectionOrAll() const
{
    return selection.empty() ? channelNotes() : selection;
}

void PianoRollComponent::setSelection (std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    selection = std::move (indices);
    repaint();
}

void PianoRollComponent::selectAll()
{
    setSelection (channelNotes());
}

void PianoRollComponent::editNotes (const std::function<void (Pattern&)>& edit)
{
    engine.editSong ([&edit] (Song& s) { edit (currentPattern (s)); });
    knownNoteCount = currentPattern (engine.getSong()).notes.size();
}

void PianoRollComponent::sortNotes()
{
    auto tracked = selection;
    tracked.push_back (dragNote);
    // only reorders: no undo step of its own needed
    engine.editSong ([&tracked] (Song& s) { noteedit::sortTracking (currentPattern (s), tracked); }, false);
    dragNote = tracked.back();
    tracked.pop_back();
    setSelection (tracked);
}

// --- painting ------------------------------------------------------------------

void PianoRollComponent::paint (juce::Graphics& g)
{
    const auto& song = engine.getSong();
    const auto& pattern = currentPattern (song);
    const auto channel = engine.getSelectedChannel();
    const auto grid = gridArea();
    const auto patternLength = pattern.effectiveLength (song.beatsPerBar);
    const auto bar = (std::int64_t) ppq * song.beatsPerBar;
    const auto snapGrid = gridTicks();

    using namespace theme::colour;
    g.fillAll (bg1);
    g.setColour (bg3);
    g.fillRect (0, 0, getWidth(), toolbarHeight);
    g.setColour (border);
    g.fillRect (0, toolbarHeight - 1, getWidth(), 1);

    auto drawVerticalGrid = [&] (juce::Rectangle<int> area) {
        // grid lines: bar > beat > snap (unless too dense)
        const auto step = snapGrid * pixelsPerTick >= 6.0 ? snapGrid : (std::int64_t) ppq;
        const auto first = (std::int64_t) std::floor (scrollTicks / (double) step);
        const auto last = (std::int64_t) std::ceil (xToTick (area.getRight()) / (double) step);

        for (auto i = first; i <= last; ++i)
        {
            const auto tick = i * step;
            const auto x = (float) tickToX ((double) tick);
            g.setColour (tick % bar == 0 ? gridBar : tick % ppq == 0 ? gridBeat : gridStep);
            g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
        }
    };

    // --- grid ------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (grid);

        for (int key = 0; key < 128; ++key)
        {
            const auto y = keyToY (key);

            if (y > grid.getBottom() || y + rowHeight < grid.getY())
                continue;

            auto row = isBlackKey (key) ? rowBlack : rowWhite;
            if (inScale (key))
                row = row.interpolatedWith (info, 0.10f);
            g.setColour (row);
            g.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);

            if (key % 12 == 0)
            {
                g.setColour (gridBeat);
                g.drawHorizontalLine (y + rowHeight - 1, (float) grid.getX(), (float) grid.getRight());
            }
        }

        drawVerticalGrid (grid);

        // outside the pattern is darkened
        const auto endX = (float) tickToX ((double) patternLength);
        g.setColour (bg0.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (endX, (float) grid.getY(), (float) grid.getRight() - endX, (float) grid.getHeight()));

        // ghost notes: the other channels of this pattern, solid grey
        for (const auto& n : pattern.notes)
        {
            if (n.channel == channel || ! showGhosts)
                continue;

            const auto r = juce::Rectangle<float> ((float) tickToX ((double) n.start), (float) keyToY (n.key) + 1,
                                                   (float) (n.length * pixelsPerTick), (float) rowHeight - 2);
            g.setColour (textPrimary.withAlpha (0.09f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (textPrimary.withAlpha (0.12f));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
        }

        for (int i = 0; i < (int) pattern.notes.size(); ++i)
        {
            const auto& n = pattern.notes[(std::size_t) i];

            if (n.channel != channel)
                continue;

            const auto selected = isSelected (i);
            const auto r = juce::Rectangle<float> ((float) tickToX ((double) n.start), (float) keyToY (n.key) + 1,
                                                   juce::jmax (4.0f, (float) (n.length * pixelsPerTick) - 1.0f), (float) rowHeight - 2);
            g.setColour (selected ? accentHover.brighter (0.15f) : n.muted ? noteMuted : theme::noteColour (n.velocity));
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (selected ? textPrimary : bg0.withAlpha (0.6f));
            g.drawRoundedRectangle (r, 2.5f, 1.0f);

            if (n.porta) // porta: slanted stroke at the start, like the glide
            {
                g.setColour (bg0.withAlpha (0.65f));
                g.drawLine (r.getX() + 2.0f, r.getBottom() - 2.0f, r.getX() + 9.0f, r.getY() + 2.0f, 2.0f);
            }

            if (n.slide) // slide arrow at the start of the note
            {
                juce::Path arrow;
                arrow.addTriangle (r.getX() + 2.0f, r.getBottom() - 2.0f, r.getX() + 2.0f, r.getY() + 2.0f, r.getX() + 9.0f, r.getCentreY());
                g.setColour (bg0.withAlpha (0.65f));
                g.fillPath (arrow);
            }

            if (r.getWidth() > 30)
            {
                g.setColour (bg0.withAlpha (0.8f));
                g.setFont (theme::font (10.0f));
                g.drawText (keyName (n.key), r.reduced (n.slide ? 11 : 3, 0), juce::Justification::centredLeft, false);
            }
        }

        if (! rubberBand.isEmpty())
        {
            g.setColour (selectionFill);
            g.fillRect (rubberBand);
            g.setColour (theme::colour::selection);
            g.drawRect (rubberBand);
        }

        if (drag == Drag::knife)
        {
            juce::Path cut;
            cut.startNewSubPath (knifeFrom.toFloat());
            cut.lineTo (knifeTo.toFloat());
            juce::Path dashed;
            const float dash[] = { 5.0f, 3.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, cut, dash, 2);
            g.setColour (textPrimary);
            g.fillPath (dashed);
        }

        if (playheadTick >= 0.0)
        {
            g.setColour (playhead);
            g.drawVerticalLine ((int) tickToX (playheadTick), (float) grid.getY(), (float) grid.getBottom());
        }
    }

    // --- velocity lane -----------------------------------------------------
    {
        const auto lane = velocityArea();
        g.setColour (bg0);
        g.fillRect (lane);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (lane);
        drawVerticalGrid (lane);
        g.setColour (border);
        g.drawHorizontalLine (lane.getY(), (float) lane.getX(), (float) lane.getRight());

        const auto usable = (float) lane.getHeight() - 8.0f;
        const auto bipolar = laneIsBipolar();
        const auto base = bipolar ? (float) lane.getBottom() - 2.0f - usable * 0.5f : (float) lane.getBottom();

        if (bipolar)
        {
            g.setColour (textPrimary.withAlpha (0.15f));
            g.drawHorizontalLine ((int) base, (float) lane.getX(), (float) lane.getRight());
        }

        for (int i = 0; i < (int) pattern.notes.size(); ++i)
        {
            const auto& n = pattern.notes[(std::size_t) i];

            if (n.channel != channel)
                continue;

            const auto x = (float) tickToX ((double) n.start);
            const auto top = (float) lane.getBottom() - 2.0f - usable * laneValue (n);
            auto colour = isSelected (i) ? accentHover.brighter (0.15f) : theme::noteColour (n.velocity);
            if (n.muted)
                colour = noteMuted;
            g.setColour (colour.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x, juce::jmin (top, base), 2.0f, std::abs (base - top)));
            g.fillEllipse (x - 3.0f, top - 3.0f, 7.0f, 7.0f);
        }
    }

    // --- time ruler --------------------------------------------------------
    {
        const auto ruler = juce::Rectangle<int> (keyboardWidth, toolbarHeight, grid.getWidth(), timelineHeight);
        g.setColour (bg2);
        g.fillRect (ruler);
        g.setColour (border);
        g.fillRect (ruler.withTop (ruler.getBottom() - 1));
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (ruler);
        g.setFont (theme::font (theme::type::caption));

        const auto firstBar = (std::int64_t) std::floor (scrollTicks / (double) bar);
        const auto lastBar = (std::int64_t) std::ceil (xToTick (grid.getRight()) / (double) bar);

        for (auto b = firstBar; b <= lastBar; ++b)
        {
            const auto x = (int) tickToX ((double) (b * bar));
            g.setColour (textSecondary);
            g.drawVerticalLine (x, (float) toolbarHeight + 10.0f, (float) headerHeight);
            g.drawText (juce::String (b + 1), x + 3, toolbarHeight + 2, 40, timelineHeight - 4, juce::Justification::centredLeft);
        }

        if (playheadTick >= 0.0)
        {
            const auto x = (float) tickToX (playheadTick);
            juce::Path tri;
            tri.addTriangle (x - 5, (float) toolbarHeight, x + 5, (float) toolbarHeight, x, (float) toolbarHeight + 8);
            g.setColour (playhead);
            g.fillPath (tri);
        }
    }

    // --- keyboard ------------------------------------------------------------
    {
        const auto keys = juce::Rectangle<int> (0, headerHeight, keyboardWidth, grid.getHeight());
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (keys);
        g.setFont (theme::font (10.0f));
        g.setColour (bg0);
        g.fillRect (keys);

        for (int key = 0; key < 128; ++key)
        {
            const auto y = keyToY (key);

            if (y > keys.getBottom() || y + rowHeight < keys.getY())
                continue;

            const auto black = isBlackKey (key);
            const auto pressed = key == previewKey;
            g.setColour (pressed ? accent : black ? keyBlack : keyWhite);
            g.fillRect (0, y, black ? keyboardWidth * 2 / 3 : keyboardWidth - 1, rowHeight);
            g.setColour (black ? bg0 : keyWhite.darker (0.35f));
            g.drawHorizontalLine (y + rowHeight - 1, 0.0f, (float) (black ? keyboardWidth * 2 / 3 : keyboardWidth - 1));

            if (key % 12 == 0)
            {
                g.setColour (pressed ? onAccent : bg1);
                g.drawText (keyName (key), keyboardWidth - 30, y, 28, rowHeight, juce::Justification::centredRight);
            }
        }
    }

    // velocity lane label
    {
        const auto lane = velocityArea();
        g.setColour (bg2);
        g.fillRect (0, lane.getY(), keyboardWidth, lane.getHeight());
        juce::ignoreUnused (lane); // the property combo sits here
    }

    // top-left corner: channel name
    g.setColour (bg3);
    g.fillRect (0, 0, keyboardWidth, toolbarHeight);
    g.setColour (bg2);
    g.fillRect (0, toolbarHeight, keyboardWidth, timelineHeight);
    g.setColour (border);
    g.fillRect (0, headerHeight - 1, keyboardWidth, 1);
    g.fillRect (keyboardWidth - 1, 0, 1, headerHeight);

    if (channel < (int) song.channels.size())
    {
        const auto& ch = song.channels[(std::size_t) channel];
        g.setColour (ch.colour != 0 ? theme::tame (juce::Colour (ch.colour)) : accent);
        g.fillRoundedRectangle (4.0f, (float) toolbarHeight + 5.0f, 3.0f, (float) timelineHeight - 10.0f, 1.0f);
        g.setColour (textPrimary);
        g.setFont (theme::font (theme::type::caption, true));
        g.drawFittedText (juce::String::fromUTF8 (ch.name.c_str()), 11, toolbarHeight, keyboardWidth - 14, timelineHeight,
                          juce::Justification::centredLeft, 1, 0.8f);
    }
}

// --- mouse -------------------------------------------------------------------

int PianoRollComponent::noteAt (int x, int y, bool& onRightEdge) const
{
    bool left = false;
    return noteAt (x, y, onRightEdge, left);
}

int PianoRollComponent::noteAt (int x, int y, bool& onRightEdge, bool& onLeftEdge) const
{
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto channel = engine.getSelectedChannel();
    const auto key = yToKey (y);
    onRightEdge = onLeftEdge = false;

    // back to front: the note drawn on top wins
    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& n = notes[(std::size_t) i];

        if (n.channel != channel || n.key != key)
            continue;

        const auto x0 = tickToX ((double) n.start);
        const auto x1 = tickToX ((double) (n.start + n.length));

        if (x >= x0 && x < juce::jmax (x1, x0 + 4.0))
        {
            onRightEdge = x >= x1 - 6.0;
            // the left edge only when enabled, and never on notes too short to grab both edges
            onLeftEdge = resizeFromLeft && ! onRightEdge && x <= x0 + 5.0 && x1 - x0 >= 16.0;
            return i;
        }
    }

    return -1;
}

int PianoRollComponent::ghostAt (int x, int y) const
{
    if (! showGhosts)
        return -1;

    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto channel = engine.getSelectedChannel();
    const auto key = yToKey (y);

    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& n = notes[(std::size_t) i];

        if (n.channel != channel && n.key == key && x >= tickToX ((double) n.start) && x < tickToX ((double) (n.start + n.length)))
            return i;
    }

    return -1;
}

void PianoRollComponent::previewOn (int key, float velocity)
{
    previewOff();
    previewKey = key;
    engine.sendLiveNote (engine.getSelectedChannel(), key, velocity, true);
    repaint (0, headerHeight, keyboardWidth, getHeight());
}

void PianoRollComponent::previewOff()
{
    if (previewKey >= 0)
    {
        engine.sendLiveNote (engine.getSelectedChannel(), previewKey, 0.0f, false);
        previewKey = -1;
        repaint (0, headerHeight, keyboardWidth, getHeight());
    }
}

float PianoRollComponent::laneValue (const Note& n) const
{
    switch (laneBox.getSelectedId())
    {
        case 2: return (n.pan + 1.0f) * 0.5f;
        case 3: return (n.finePitch + 120.0f) / 240.0f;
        default: return n.velocity;
    }
}

void PianoRollComponent::setLaneValue (Note& n, float v) const
{
    v = juce::jlimit (0.0f, 1.0f, v);

    switch (laneBox.getSelectedId())
    {
        case 2: n.pan = std::abs (v - 0.5f) < 0.02f ? 0.0f : v * 2.0f - 1.0f; break;   // snaps to center
        case 3: n.finePitch = std::round (v * 240.0f - 120.0f); break;
        default: n.velocity = v; break;
    }
}

bool PianoRollComponent::laneIsBipolar() const
{
    return laneBox.getSelectedId() != 1;
}

void PianoRollComponent::applyVelocity (int fromX, int toX, int y)
{
    const auto lane = velocityArea();
    const auto value = juce::jlimit (0.0f, 1.0f, (float) (lane.getBottom() - 2 - y) / (float) (lane.getHeight() - 8));
    const auto lo = juce::jmin (fromX, toX) - 3, hi = juce::jmax (fromX, toX) + 3;
    const auto& notes = currentPattern (engine.getSong()).notes;
    std::vector<int> hits;

    for (auto i : selectionOrAll())
    {
        const auto x = (int) tickToX ((double) notes[(std::size_t) i].start);

        if (x >= lo && x <= hi)
            hits.push_back (i);
    }

    if (! hits.empty())
        editNotes ([this, &hits, value] (Pattern& p) {
            for (auto i : hits)
                setLaneValue (p.notes[(std::size_t) i], value);
        });
}

void PianoRollComponent::togglePorta()
{
    const auto target = selectionOrAll();

    if (target.empty())
        return;

    // like mute: if any note lacks porta, enable it on all; otherwise disable
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto porta = std::any_of (target.begin(), target.end(), [&notes] (int i) { return ! notes[(std::size_t) i].porta; });
    editNotes ([&target, porta] (Pattern& p) {
        for (auto i : target)
            p.notes[(std::size_t) i].porta = porta;
    });
}

void PianoRollComponent::toggleMute()
{
    const auto target = selectionOrAll();

    if (target.empty())
        return;

    // if any is playing, mute all; otherwise unmute
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto mute = std::any_of (target.begin(), target.end(), [&notes] (int i) { return ! notes[(std::size_t) i].muted; });
    editNotes ([&target, mute] (Pattern& p) {
        for (auto i : target)
            p.notes[(std::size_t) i].muted = mute;
    });
}

void PianoRollComponent::paintNoteAt (std::int64_t start, int key, bool monophonic)
{
    // a brush stroke lays single notes (or the chosen chord) at the default length
    Note n;
    n.start = juce::jmax<std::int64_t> (0, start);
    n.length = paintLength;
    n.key = key;
    n.channel = engine.getSelectedChannel();
    n.slide = slideButton.getToggleState();

    std::vector<Note> stamp;
    for (auto interval : chordIntervals())
        if (n.key + interval <= 127)
        {
            auto chordNote = n;
            chordNote.key = snapKey (n.key + interval); // with scale snap the chord follows the scale
            if (std::none_of (stamp.begin(), stamp.end(), [&chordNote] (const Note& o) { return o.key == chordNote.key; }))
                stamp.push_back (chordNote);
        }

    editNotes ([&] (Pattern& p) {
        if (monophonic) // Shift: the new note replaces whatever this channel plays at that time
        {
            std::vector<int> overlapping;
            for (int i = 0; i < (int) p.notes.size(); ++i)
            {
                const auto& o = p.notes[(std::size_t) i];
                if (o.channel == n.channel && o.start < n.start + n.length && o.start + o.length > n.start)
                    overlapping.push_back (i);
            }
            noteedit::remove (p, overlapping);
        }

        noteedit::paste (p, stamp, 0);
    });
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& e)
{
    if (engine.getSong().channels.empty())
        return;

    downMods = e.mods;
    startedOnEmpty = erasedSomething = holdExpired = false;

    if (e.mods.isMiddleButtonDown()) // middle-drag pans both ways, in every tool
    {
        drag = Drag::pan;
        panStartTicks = scrollTicks;
        panStartY = scrollY;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        return;
    }

    if (e.x < keyboardWidth && e.y >= headerHeight && e.y < gridArea().getBottom())
    {
        drag = Drag::keyboard;
        previewOn (yToKey (e.y), 0.8f);
        return;
    }

    if (velocityArea().contains (e.getPosition()))
    {
        drag = Drag::velocity;
        lastVelocityX = e.x;
        applyVelocity (e.x, e.x, e.y);
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    bool rightEdge = false, leftEdge = false;
    const auto hit = noteAt (e.x, e.y, rightEdge, leftEdge);
    const auto tool = tools.getTool();

    // every tool: right click deletes (double right-click on a ghost note switches to its channel)
    if (e.mods.isRightButtonDown())
    {
        if (hit < 0 && e.getNumberOfClicks() >= 2)
            if (const auto ghost = ghostAt (e.x, e.y); ghost >= 0)
            {
                engine.setSelectedChannel (currentPattern (engine.getSong()).notes[(std::size_t) ghost].channel);
                return;
            }

        drag = Drag::erase;
        startedOnEmpty = hit < 0;
        setSelection ({}); // indices change while deleting

        if (hit >= 0)
        {
            erasedSomething = true;
            editNotes ([hit] (Pattern& p) { noteedit::remove (p, { hit }); });
        }
        return;
    }

    // every tool: Ctrl+click on a note adds/removes it from the selection; Ctrl+drag selects (Shift adds)
    if (hit >= 0 && e.mods.isCtrlDown())
    {
        auto s = selection;

        if (isSelected (hit))
            s.erase (std::find (s.begin(), s.end(), hit));
        else
            s.push_back (hit);

        setSelection (s);
        return;
    }

    if ((hit < 0 && e.mods.isCtrlDown()) || tool == EditTool::lasso
        || (hit < 0 && e.mods.isShiftDown() && tool != EditTool::brush))
    {
        drag = Drag::select;
        dragNote = hit; // a click without dragging selects this note (Lasso)
        selectionBefore = e.mods.isShiftDown() ? selection : std::vector<int> {};
        rubberBand = {};
        setSelection (selectionBefore);
        return;
    }

    const auto snapOn = ! e.mods.isAltDown();

    switch (tool)
    {
        case EditTool::eraser:
            drag = Drag::erase;
            setSelection ({});
            if (hit >= 0)
                editNotes ([hit] (Pattern& p) { noteedit::remove (p, { hit }); });
            return;

        case EditTool::mute:
            drag = Drag::muting;
            touchedNotes.clear();
            if (hit >= 0)
            {
                muteTarget = ! currentPattern (engine.getSong()).notes[(std::size_t) hit].muted;
                touchedNotes.push_back (hit);
                editNotes ([hit, m = muteTarget] (Pattern& p) { p.notes[(std::size_t) hit].muted = m; });
            }
            else
                muteTarget = true;
            return;

        case EditTool::knife:
            drag = Drag::knife;
            knifeFrom = knifeTo = e.getPosition();
            repaint();
            return;

        case EditTool::zoom:
            drag = Drag::zoomRange;
            rubberBand = {};
            return;

        case EditTool::listen:
            drag = Drag::listen;
            listenAt (e.x);
            return;

        case EditTool::pencil:
        case EditTool::brush:
        case EditTool::lasso:
        case EditTool::slip:
            break;
    }

    // Pencil (and Brush on an existing note): Shift+drag on a note drags a copy
    if (hit >= 0 && e.mods.isShiftDown() && ! rightEdge && ! leftEdge)
    {
        const auto sources = isSelected (hit) ? selection : std::vector<int> { hit };
        const auto grabbedAt = (std::size_t) (std::find (sources.begin(), sources.end(), hit) - sources.begin());
        std::vector<int> added;

        // the copy comes out in the same order as the originals (copy sorts by start)
        std::vector<Note> ordered;
        for (auto i : sources)
            ordered.push_back (currentPattern (engine.getSong()).notes[(std::size_t) i]);

        editNotes ([&] (Pattern& p) { added = noteedit::paste (p, ordered, 0); });
        setSelection (added);
        dragIndices = added;
        dragOriginals = ordered;
        dragNote = added[grabbedAt];
        drag = Drag::move;
        grabOffset = ordered[grabbedAt].start - snap (xToTick (e.x), snapOn);
        previewOn (ordered[grabbedAt].key, ordered[grabbedAt].velocity);
        return;
    }

    if (hit >= 0)
    {
        if (! isSelected (hit))
            setSelection ({});

        const auto& notes = currentPattern (engine.getSong()).notes;
        const auto& n = notes[(std::size_t) hit];
        dragNote = hit;
        dragIndices = selection.empty() ? std::vector<int> { hit } : selection;
        dragOriginals.clear();

        for (auto i : dragIndices)
            dragOriginals.push_back (notes[(std::size_t) i]);

        drag = rightEdge ? Drag::resize : leftEdge ? Drag::resizeLeft : Drag::move;
        grabOffset = n.start - snap (xToTick (e.x), snapOn);
        lastLength = n.length;
        previewOn (n.key, n.velocity);
        return;
    }

    // new note (or the chosen chord, with the clicked note as the root)
    setSelection ({});
    createdAt = juce::Time::getMillisecondCounter();

    if (tool == EditTool::brush)
    {
        drag = Drag::paint;
        paintLength = juce::jmax<std::int64_t> (1, newNoteLength());
        const auto start = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
        paintNoteAt (start, snapKey (yToKey (e.y)), e.mods.isShiftDown());
        nextPaintTick = start + paintLength;
        previewOn (snapKey (yToKey (e.y)), 0.78f);
        return;
    }

    Note n;
    n.start = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
    n.length = newNoteLength();
    n.key = snapKey (yToKey (e.y));
    n.channel = engine.getSelectedChannel();
    n.slide = slideButton.getToggleState();

    std::vector<Note> stamp;
    for (auto interval : chordIntervals())
        if (n.key + interval <= 127)
        {
            auto chordNote = n;
            chordNote.key = snapKey (n.key + interval); // with scale snap the chord follows the scale
            if (std::none_of (stamp.begin(), stamp.end(), [&chordNote] (const Note& o) { return o.key == chordNote.key; }))
                stamp.push_back (chordNote);
        }

    std::vector<int> added;
    editNotes ([&] (Pattern& p) { added = noteedit::paste (p, stamp, 0); });
    dragNote = added.front(); // the root leads the drag
    dragIndices = added;
    dragOriginals = stamp;

    if (added.size() > 1)
        setSelection (added);
    drag = Drag::move;
    grabOffset = 0;
    previewOn (n.key, n.velocity);
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& e)
{
    const auto snapOn = ! e.mods.isAltDown();

    switch (drag)
    {
        case Drag::none:
            return;

        case Drag::pan:
            scrollTicks = juce::jmax (0.0, panStartTicks - e.getDistanceFromDragStartX() / pixelsPerTick);
            scrollY = juce::jlimit (0, juce::jmax (0, 128 * rowHeight - gridArea().getHeight()), panStartY - e.getDistanceFromDragStartY());
            updateScrollBars();
            repaint();
            return;

        case Drag::keyboard:
        {
            const auto key = yToKey (e.y);
            if (key != previewKey)
                previewOn (key, 0.8f);
            return;
        }

        case Drag::velocity:
            applyVelocity (lastVelocityX, e.x, e.y);
            lastVelocityX = e.x;
            return;

        case Drag::select:
        case Drag::zoomRange:
        {
            if (e.getDistanceFromDragStart() < 3 && rubberBand.isEmpty())
                return;

            const auto area = gridArea();
            rubberBand = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition()).getIntersection (area);

            if (drag == Drag::zoomRange)
            {
                rubberBand = rubberBand.withY (area.getY()).withHeight (area.getHeight());
                repaint();
                return;
            }

            const auto t0 = xToTick (rubberBand.getX()), t1 = xToTick (rubberBand.getRight());
            const auto kTop = yToKey (rubberBand.getY()), kBottom = yToKey (rubberBand.getBottom());
            const auto& notes = currentPattern (engine.getSong()).notes;
            auto s = selectionBefore;

            for (auto i : channelNotes())
            {
                const auto& n = notes[(std::size_t) i];

                if ((double) (n.start + n.length) > t0 && (double) n.start < t1 && n.key <= kTop && n.key >= kBottom)
                    s.push_back (i);
            }

            setSelection (s);
            return;
        }

        case Drag::erase:
        {
            // right press-and-hold on empty grid, then drag: a temporary Knife
            if (e.mods.isRightButtonDown() && startedOnEmpty && ! erasedSomething && ! holdExpired)
            {
                if (e.getLengthOfMousePress() >= 350)
                {
                    drag = Drag::knife;
                    knifeFrom = e.getMouseDownPosition();
                    knifeTo = e.getPosition();
                    repaint();
                    return;
                }

                if (e.getDistanceFromDragStart() > 3)
                    holdExpired = true; // moved right away: it is an erase stroke
            }

            bool edge = false;
            const auto hit = noteAt (e.x, e.y, edge);

            if (hit >= 0)
            {
                erasedSomething = true;
                editNotes ([hit] (Pattern& p) { noteedit::remove (p, { hit }); });
            }
            return;
        }

        case Drag::muting:
        {
            bool edge = false;
            const auto hit = noteAt (e.x, e.y, edge);

            if (hit >= 0 && std::find (touchedNotes.begin(), touchedNotes.end(), hit) == touchedNotes.end())
            {
                touchedNotes.push_back (hit);
                editNotes ([hit, m = muteTarget] (Pattern& p) { p.notes[(std::size_t) hit].muted = m; });
            }
            return;
        }

        case Drag::knife:
            knifeTo = e.getPosition();
            repaint();
            return;

        case Drag::listen:
            listenAt (e.x);
            return;

        case Drag::paint:
        {
            // lays notes to the right, one per default length, at the pitch under the mouse
            const auto tick = snap (xToTick (e.x), snapOn);
            for (int guard = 0; tick >= nextPaintTick && guard < 64; ++guard)
            {
                paintNoteAt (nextPaintTick, snapKey (yToKey (e.y)), e.mods.isShiftDown());
                nextPaintTick += paintLength;
            }

            if (snapKey (yToKey (e.y)) != previewKey)
                previewOn (snapKey (yToKey (e.y)), 0.78f);
            return;
        }

        case Drag::move:
        {
            const auto& grabbed = dragOriginals[(std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragNote) - dragIndices.begin())];
            auto deltaTicks = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) + grabOffset) - grabbed.start;
            auto deltaKeys = snapKey (yToKey (e.y)) - grabbed.key;

            // locks: Shift pressed after the click keeps the pitch, Ctrl keeps the time
            if (e.mods.isShiftDown() && ! downMods.isShiftDown())
                deltaKeys = 0;
            if (e.mods.isCtrlDown() && ! downMods.isCtrlDown())
                deltaTicks = 0;

            const auto& current = currentPattern (engine.getSong()).notes[(std::size_t) dragNote];

            if (current.start == grabbed.start + deltaTicks && current.key == grabbed.key + deltaKeys)
                return;

            const auto oldKey = current.key;

            // from the original positions: the group keeps its shape even when hitting the limits
            editNotes ([this, &deltaTicks, &deltaKeys] (Pattern& p) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                    p.notes[(std::size_t) dragIndices[j]] = dragOriginals[j];

                noteedit::move (p, dragIndices, deltaTicks, deltaKeys);
            });

            const auto& moved = currentPattern (engine.getSong()).notes[(std::size_t) dragNote];

            if (moved.key != oldKey)
                previewOn (moved.key, moved.velocity);
            return;
        }

        case Drag::resize:
        {
            const auto& grabbed = dragOriginals[(std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragNote) - dragIndices.begin())];
            const auto minimum = snapOn ? gridTicks() : (std::int64_t) 10;
            const auto end = snapOn ? snap (xToTick (e.x) + (double) gridTicks() / 2.0, true) : (std::int64_t) xToTick (e.x);
            const auto delta = juce::jmax (minimum, end - grabbed.start) - grabbed.length;

            if (currentPattern (engine.getSong()).notes[(std::size_t) dragNote].length == grabbed.length + delta)
                return;

            lastLength = grabbed.length + delta;
            editNotes ([this, delta, minimum] (Pattern& p) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                    p.notes[(std::size_t) dragIndices[j]].length = juce::jmax (minimum, dragOriginals[j].length + delta);
            });
            return;
        }

        case Drag::resizeLeft:
        {
            // the start moves, the end stays (selected notes together)
            const auto& grabbed = dragOriginals[(std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragNote) - dragIndices.begin())];
            const auto minimum = snapOn ? gridTicks() : (std::int64_t) 10;
            auto delta = juce::jmax<std::int64_t> (0, snap (xToTick (e.x) + (snapOn ? (double) gridTicks() / 2.0 : 0.0), snapOn)) - grabbed.start;

            for (const auto& o : dragOriginals)
                delta = juce::jlimit (-o.start, o.length - juce::jmin (minimum, o.length), delta);

            if (currentPattern (engine.getSong()).notes[(std::size_t) dragNote].start == grabbed.start + delta)
                return;

            editNotes ([this, delta] (Pattern& p) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto& n = p.notes[(std::size_t) dragIndices[j]];
                    n.start = dragOriginals[j].start + delta;
                    n.length = dragOriginals[j].length - delta;
                }
            });
            lastLength = grabbed.length - delta;
            return;
        }
    }
}

void PianoRollComponent::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::knife)
        cutWithKnife (knifeFrom, knifeTo, ! e.mods.isAltDown());

    if (drag == Drag::zoomRange)
    {
        if (rubberBand.getWidth() > 4)
            zoomToTicks (xToTick (rubberBand.getX()), xToTick (rubberBand.getRight()));
        else
            zoomOut (e.x);
    }

    if (drag == Drag::select && rubberBand.isEmpty() && dragNote >= 0)
    {
        // Lasso click on a note: selects it (Shift: adds / removes)
        auto s = selectionBefore;
        if (auto it = std::find (s.begin(), s.end(), dragNote); it != s.end())
            s.erase (it);
        else
            s.push_back (dragNote);
        setSelection (s);
    }

    if (drag == Drag::listen)
        stopListening();

    if (drag == Drag::pan)
        setMouseCursor (juce::MouseCursor::NormalCursor);

    if (drag == Drag::move || drag == Drag::resize || drag == Drag::resizeLeft || drag == Drag::paint)
        sortNotes();

    previewOff();
    drag = Drag::none;
    dragNote = -1;
    dragIndices.clear();
    dragOriginals.clear();
    touchedNotes.clear();
    rubberBand = {};
    repaint();
}

void PianoRollComponent::mouseMove (const juce::MouseEvent& e)
{
    bool rightEdge = false, leftEdge = false;
    const auto inGrid = gridArea().contains (e.getPosition());
    const auto hit = inGrid ? noteAt (e.x, e.y, rightEdge, leftEdge) : -1;
    const auto tool = tools.getTool();

    if (inGrid && ! e.mods.isCtrlDown() && tool != EditTool::pencil && tool != EditTool::brush)
    {
        setMouseCursor (tool == EditTool::knife || tool == EditTool::zoom ? juce::MouseCursor::CrosshairCursor
                        : tool == EditTool::lasso                          ? juce::MouseCursor::NormalCursor
                                                                           : juce::MouseCursor::PointingHandCursor);
        return;
    }

    setMouseCursor (hit >= 0 ? (rightEdge || leftEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                             : velocityArea().contains (e.getPosition()) ? juce::MouseCursor::CrosshairCursor
                                                                         : juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! gridArea().contains (e.getPosition()) || e.mods.isRightButtonDown() || e.mods.isCtrlDown())
        return;

    // the first click of this double click created the note: that was drawing, not opening it
    if (juce::Time::getMillisecondCounter() - createdAt < 800)
        return;

    bool edge = false;
    const auto hit = noteAt (e.x, e.y, edge);

    if (hit < 0)
        return;

    const auto indices = isSelected (hit) ? selection : std::vector<int> { hit };
    const auto& n = currentPattern (engine.getSong()).notes[(std::size_t) hit];
    const auto area = juce::Rectangle<int> ((int) tickToX ((double) n.start), keyToY (n.key), juce::jmax (8, (int) (n.length * pixelsPerTick)), rowHeight);
    showNoteInspector (engine, indices, hit, localAreaToGlobal (area));
}

std::unique_ptr<juce::Component> PianoRollComponent::makeNoteInspectorForSnapshot (bool singleNote)
{
    if (selection.empty())
        selectAll();

    if (selection.empty())
        return nullptr;

    return makeNoteInspector (engine, singleNote ? std::vector<int> { selection.front() } : selection, selection.front());
}

// --- tools ---------------------------------------------------------------------------

void PianoRollComponent::cutWithKnife (juce::Point<int> from, juce::Point<int> to, bool snapOn)
{
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto channel = engine.getSelectedChannel();
    std::vector<std::pair<int, std::int64_t>> cuts;

    if (from.getDistanceFrom (to) < 3)
    {
        // a click: cuts the note under the cursor
        bool edge = false;
        if (const auto hit = noteAt (from.x, from.y, edge); hit >= 0)
            cuts.push_back ({ hit, snap (xToTick (from.x) + (snapOn ? (double) gridTicks() / 2.0 : 0.0), snapOn) });
    }
    else
    {
        // a line: every note row it crosses is cut where the line passes
        const auto top = juce::jmin (from.y, to.y), bottom = juce::jmax (from.y, to.y);

        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(std::size_t) i];

            if (n.channel != channel)
                continue;

            const auto rowY = keyToY (n.key) + rowHeight / 2;

            if (rowY < top - rowHeight / 2 || rowY > bottom + rowHeight / 2)
                continue;

            const auto y = juce::jlimit (top, bottom, rowY);
            const auto x = from.y == to.y ? (double) from.x : from.x + (double) (y - from.y) * (to.x - from.x) / (double) (to.y - from.y);
            const auto tick = snap (xToTick ((int) std::round (x)) + (snapOn ? (double) gridTicks() / 2.0 : 0.0), snapOn);

            if (tick > n.start && tick < n.start + n.length)
                cuts.push_back ({ i, tick });
        }
    }

    if (cuts.empty())
        return;

    setSelection ({});
    editNotes ([&cuts] (Pattern& p) {
        for (auto [index, tick] : cuts)
            noteedit::split (p, index, tick);
    });
    sortNotes();
}

void PianoRollComponent::zoomToTicks (double from, double to)
{
    if (to - from < 1.0)
        return;

    pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, gridArea().getWidth() / (to - from));
    scrollTicks = juce::jmax (0.0, from);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::zoomOut (int anchorX)
{
    const auto anchor = xToTick (anchorX);
    pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, pixelsPerTick / 2.0);
    scrollTicks = juce::jmax (0.0, anchor - (anchorX - keyboardWidth) / pixelsPerTick);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::listenAt (int x)
{
    // plays the channel's notes that sound at the tick under the mouse
    const auto tick = (std::int64_t) xToTick (x);
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto channel = engine.getSelectedChannel();
    std::vector<int> keys;
    std::vector<float> velocities;

    for (const auto& n : notes)
        if (n.channel == channel && ! n.muted && n.start <= tick && n.start + n.length > tick
            && std::find (keys.begin(), keys.end(), n.key) == keys.end())
        {
            keys.push_back (n.key);
            velocities.push_back (n.velocity);
        }

    for (auto k : listenKeys)
        if (std::find (keys.begin(), keys.end(), k) == keys.end())
            engine.sendLiveNote (channel, k, 0.0f, false);

    for (std::size_t i = 0; i < keys.size(); ++i)
        if (std::find (listenKeys.begin(), listenKeys.end(), keys[i]) == listenKeys.end())
            engine.sendLiveNote (channel, keys[i], velocities[i], true);

    listenKeys = keys;
}

void PianoRollComponent::stopListening()
{
    for (auto k : listenKeys)
        engine.sendLiveNote (engine.getSelectedChannel(), k, 0.0f, false);

    listenKeys.clear();
}

void PianoRollComponent::refreshChannelBox()
{
    const auto& song = engine.getSong();
    std::vector<bool> hasNotes (song.channels.size(), false);

    for (const auto& n : currentPattern (song).notes)
        if (n.channel >= 0 && n.channel < (int) hasNotes.size())
            hasNotes[(std::size_t) n.channel] = true;

    juce::StringArray names;
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        names.add ((hasNotes[c] ? juce::String::fromUTF8 ("\xe2\x97\x8f ") : juce::String ("    ")) + juce::String::fromUTF8 (song.channels[c].name.c_str()));

    // only rebuilds when something changed (rebuilding closes an open menu)
    bool same = channelBox.getNumItems() == names.size();
    for (int i = 0; same && i < names.size(); ++i)
        same = channelBox.getItemText (i) == names[i];

    if (! same)
    {
        channelBox.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            channelBox.addItem (names[i], i + 1);
    }

    channelBox.setSelectedId (engine.getSelectedChannel() + 1, juce::dontSendNotification);
}

void PianoRollComponent::stepChannel (int direction, bool withNotesOnly)
{
    const auto& song = engine.getSong();
    const auto count = (int) song.channels.size();

    if (count == 0)
        return;

    std::vector<bool> hasNotes ((std::size_t) count, false);
    for (const auto& n : currentPattern (song).notes)
        if (n.channel >= 0 && n.channel < count)
            hasNotes[(std::size_t) n.channel] = true;

    auto channel = engine.getSelectedChannel();

    for (int step = 0; step < count; ++step)
    {
        channel += direction;

        if (channel < 0 || channel >= count)
            return; // stops at the ends

        if (! withNotesOnly || hasNotes[(std::size_t) channel])
        {
            engine.setSelectedChannel (channel);
            return;
        }
    }
}

void PianoRollComponent::runOperation (const std::function<std::vector<int> (Pattern&, const std::vector<int>&)>& op)
{
    const auto target = selectionOrAll();

    if (target.empty())
        return;

    const auto hadSelection = ! selection.empty();
    std::vector<int> result;
    editNotes ([&] (Pattern& p) { result = op (p, target); });
    setSelection (hadSelection ? result : std::vector<int> {});
    sortNotes();
}

void PianoRollComponent::showMoreMenu()
{
    const auto grid = gridTicks();
    const auto bar = (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Note operations (selection, or all notes)"));
    menu.addItem (tr ("Chop to grid") + "\tCtrl+U", [this, grid] { runOperation ([grid] (Pattern& p, const std::vector<int>& t) { return noteedit::chop (p, t, grid); }); });
    menu.addItem (tr ("Join touching notes") + "\tCtrl+G", [this] { runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::join (p, t); return std::vector<int> {}; }); });
    menu.addItem (tr ("Make legato") + "\tCtrl+L", [this] { runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::legato (p, t); return t; }); });
    menu.addItem (tr ("Shrink to grid") + "\tShift+D", [this, grid] { runOperation ([grid] (Pattern& p, const std::vector<int>& t) { noteedit::shrinkToGrid (p, t, grid); return t; }); });
    menu.addItem (tr ("Quantise starts") + "\tShift+Q", [this, grid] { runOperation ([grid] (Pattern& p, const std::vector<int>& t) { noteedit::quantize (p, t, grid); return t; }); });
    menu.addItem (tr ("Quantise starts and lengths") + "\tCtrl+Q", [this, grid] { runOperation ([grid] (Pattern& p, const std::vector<int>& t) { noteedit::quantizeFull (p, t, grid); return t; }); });
    menu.addItem (tr ("Rotate left within the bar") + "\tCtrl+Shift+Left", [this, grid, bar] { runOperation ([grid, bar] (Pattern& p, const std::vector<int>& t) { noteedit::rotate (p, t, grid, bar, -1); return t; }); });
    menu.addItem (tr ("Rotate right within the bar") + "\tCtrl+Shift+Right", [this, grid, bar] { runOperation ([grid, bar] (Pattern& p, const std::vector<int>& t) { noteedit::rotate (p, t, grid, bar, 1); return t; }); });
    menu.addSeparator();
    menu.addSubMenu (tr ("Tools"), toolsMenu());
    {
        juce::PopupMenu zoom;
        zoom.addItem (tr ("Zoom in") + "\tPageUp", [this] { runAction ("pianoRoll.zoomIn"); });
        zoom.addItem (tr ("Zoom out") + "\tPageDown", [this] { runAction ("pianoRoll.zoomOut"); });
        zoom.addItem (tr ("Zoom: one bar") + "\tShift+1", [this] { runAction ("pianoRoll.zoom1"); });
        zoom.addItem (tr ("Zoom: four bars") + "\tShift+2", [this] { runAction ("pianoRoll.zoom2"); });
        zoom.addItem (tr ("Zoom: sixteen bars") + "\tShift+3", [this] { runAction ("pianoRoll.zoom3"); });
        zoom.addItem (tr ("Zoom to show everything") + "\tShift+4", [this] { runAction ("pianoRoll.zoomAll"); });
        zoom.addItem (tr ("Zoom to the selection") + "\tShift+5", [this] { runAction ("pianoRoll.zoomSelection"); });
        zoom.addItem (tr ("Centre the view on the playhead") + "\tShift+0", [this] { runAction ("pianoRoll.centrePlayhead"); });
        zoom.addSeparator();
        zoom.addItem (tr ("Taller keys") + "\tCtrl+Alt+Wheel", [this] { rowHeight = juce::jmin (32, rowHeight + 2); updateScrollBars(); repaint(); });
        zoom.addItem (tr ("Shorter keys"), [this] { rowHeight = juce::jmax (6, rowHeight - 2); updateScrollBars(); repaint(); });
        menu.addSubMenu (tr ("Zoom"), zoom);
    }
    menu.addSeparator();
    menu.addItem (tr ("Snap notes to the scale"), scaleIndex() > 0, scaleSnap, [this] { setScaleSnap (! scaleSnap); });
    menu.addItem (tr ("Move the notes onto the scale"), scaleIndex() > 0, false, [this] {
        const auto root = scaleRoot(), scale = scaleIndex();
        runOperation ([root, scale] (Pattern& p, const std::vector<int>& t) { notetools::snapNotesToScale (p, t, root, scale); return t; });
    });
    menu.addSeparator();
    menu.addItem (tr ("Resize notes from the left edge too") + "\tCtrl+Alt+Home", true, resizeFromLeft, [this] { resizeFromLeft = ! resizeFromLeft; });
    menu.addSeparator();
    menu.addSubMenu ("MIDI", midiMenu());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton));
}

juce::PopupMenu PianoRollComponent::toolsMenu()
{
    juce::PopupMenu menu;
    const std::pair<NoteTool, const char*> items[] = { { NoteTool::arpeggiate, "Alt+A" }, { NoteTool::strum, "Alt+S" },
                                                       { NoteTool::flip, "Alt+Y" },       { NoteTool::randomise, "Alt+R" },
                                                       { NoteTool::scaleLevels, "Alt+X" }, { NoteTool::limit, "Alt+K" } };

    for (const auto& [tool, key] : items)
        menu.addItem (tr (noteToolTitle (tool)) + "...\t" + key, [this, tool = tool] { openNoteTool (tool); });

    return menu;
}

void PianoRollComponent::openNoteTool (NoteTool tool)
{
    const auto target = selectionOrAll();

    if (target.empty())
        return;

    showNoteTool (engine, tool, target, gridTicks(), [safe = juce::Component::SafePointer<PianoRollComponent> (this)] {
        if (safe != nullptr)
            safe->setSelection ({});
    });
}

void PianoRollComponent::zoomAroundCentre (double factor)
{
    const auto grid = gridArea();
    const auto centre = xToTick (grid.getCentreX());
    pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, pixelsPerTick * factor);
    scrollTicks = juce::jmax (0.0, centre - (grid.getCentreX() - keyboardWidth) / pixelsPerTick);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::zoomToBars (int bars)
{
    const auto bar = (double) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    const auto from = std::floor (scrollTicks / bar) * bar;
    zoomToTicks (from, from + bars * bar);
}

void PianoRollComponent::zoomAll()
{
    const auto& song = engine.getSong();
    zoomToTicks (0.0, (double) currentPattern (song).effectiveLength (song.beatsPerBar));
}

void PianoRollComponent::zoomToSelection()
{
    const auto target = selectionOrAll();

    if (target.empty())
        return zoomAll();

    const auto& notes = currentPattern (engine.getSong()).notes;
    std::int64_t from = INT64_MAX, to = 0;
    int low = 127, high = 0;

    for (auto i : target)
    {
        const auto& n = notes[(std::size_t) i];
        from = juce::jmin (from, n.start);
        to = juce::jmax (to, n.start + n.length);
        low = juce::jmin (low, n.key);
        high = juce::jmax (high, n.key);
    }

    const auto margin = (double) ticksPerStep;
    zoomToTicks (juce::jmax (0.0, (double) from - margin), (double) to + margin);
    scrollY = juce::jmax (0, (127 - (low + high) / 2) * rowHeight - gridArea().getHeight() / 2);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::centreOnPlayhead()
{
    const auto& song = engine.getSong();
    const auto length = (double) juce::jmax<std::int64_t> (1, currentPattern (song).effectiveLength (song.beatsPerBar));
    const auto tick = std::fmod (engine.getTransport().positionTicks.load(), length);
    scrollTicks = juce::jmax (0.0, tick - gridArea().getWidth() / pixelsPerTick / 2.0);
    updateScrollBars();
    repaint();
}

bool PianoRollComponent::runAction (const std::string& id)
{
    auto is = [&id] (const char* name) { return id == name; };

    if (is ("pianoRoll.arpeggiate")) openNoteTool (NoteTool::arpeggiate);
    else if (is ("pianoRoll.strum")) openNoteTool (NoteTool::strum);
    else if (is ("pianoRoll.flip")) openNoteTool (NoteTool::flip);
    else if (is ("pianoRoll.randomise")) openNoteTool (NoteTool::randomise);
    else if (is ("pianoRoll.scaleLevels")) openNoteTool (NoteTool::scaleLevels);
    else if (is ("pianoRoll.limit")) openNoteTool (NoteTool::limit);
    else if (is ("pianoRoll.zoomIn")) zoomAroundCentre (1.25);
    else if (is ("pianoRoll.zoomOut")) zoomAroundCentre (1.0 / 1.25);
    else if (is ("pianoRoll.zoom1")) zoomToBars (1);
    else if (is ("pianoRoll.zoom2")) zoomToBars (4);
    else if (is ("pianoRoll.zoom3")) zoomToBars (16);
    else if (is ("pianoRoll.zoomAll")) zoomAll();
    else if (is ("pianoRoll.zoomSelection")) zoomToSelection();
    else if (is ("pianoRoll.centrePlayhead")) centreOnPlayhead();
    else
        return false;

    return true;
}

void PianoRollComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() && e.mods.isAltDown())
    {
        // vertical zoom keeping the key under the mouse in place
        const auto key = yToKey (e.y);
        const auto offset = e.y - keyToY (key);
        rowHeight = juce::jlimit (6, 32, rowHeight + (wheel.deltaY > 0 ? 2 : -2));
        scrollY = juce::jmax (0, (127 - key) * rowHeight + offset - (e.y - headerHeight));
    }
    else if (e.mods.isCtrlDown())
    {
        // zoom keeping the tick under the mouse in place
        const auto anchor = xToTick (e.x);
        pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, pixelsPerTick * (wheel.deltaY > 0 ? 1.2 : 1.0 / 1.2));
        scrollTicks = juce::jmax (0.0, anchor - (e.x - keyboardWidth) / pixelsPerTick);
    }
    else if (e.mods.isShiftDown())
        scrollTicks = juce::jmax (0.0, scrollTicks - wheel.deltaY * 400.0 / pixelsPerTick * 0.25);
    else
        scrollY = juce::jlimit (0, 128 * rowHeight, scrollY - (int) (wheel.deltaY * 200.0f));

    updateScrollBars();
    repaint();
}

// --- keyboard ----------------------------------------------------------------

void PianoRollComponent::deleteSelection()
{
    if (selection.empty())
        return;

    const auto doomed = selection;
    setSelection ({});
    editNotes ([&doomed] (Pattern& p) { noteedit::remove (p, doomed); });
}

void PianoRollComponent::copySelection()
{
    clipboard = noteedit::copy (currentPattern (engine.getSong()), selectionOrAll());
}

void PianoRollComponent::paste()
{
    if (clipboard.empty())
        return;

    // as in FL: pastes at the same position, already selected for dragging
    std::vector<int> added;
    const auto channel = engine.getSelectedChannel();
    editNotes ([&added, channel] (Pattern& p) { added = noteedit::paste (p, clipboard, 0, channel); });
    setSelection (added);
    sortNotes();
}

void PianoRollComponent::duplicateSelection()
{
    const auto source = selectionOrAll();

    if (source.empty())
        return;

    std::vector<int> added;
    const auto grid = gridTicks();
    editNotes ([&added, &source, grid] (Pattern& p) { added = noteedit::duplicate (p, source, grid); });
    setSelection (added);
    sortNotes();
}

void PianoRollComponent::quantizeSelection()
{
    const auto target = selectionOrAll();
    const auto grid = gridTicks();

    if (! target.empty())
    {
        editNotes ([&target, grid] (Pattern& p) { noteedit::quantize (p, target, grid); });
        sortNotes();
    }
}

void PianoRollComponent::nudge (std::int64_t ticks, int keys)
{
    if (selection.empty())
        return;

    editNotes ([this, &ticks, &keys] (Pattern& p) { noteedit::move (p, selection, ticks, keys); });

    if (keys != 0 && selection.size() == 1)
    {
        const auto& n = currentPattern (engine.getSong()).notes[(std::size_t) selection[0]];
        previewOn (n.key, n.velocity);
        juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<PianoRollComponent> (this)] {
            if (safe != nullptr)
                safe->previewOff();
        });
    }

    sortNotes();
}

bool PianoRollComponent::handleKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto cmd = mods.isCommandDown();
    const auto alt = mods.isAltDown();
    const auto shift = mods.isShiftDown();
    const auto code = key.getKeyCode();
    const auto letter = (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);
    const auto grid = gridTicks();
    const auto bar = (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);

    if (code == juce::KeyPress::backspaceKey)
    {
        EditorGlobals::get().toggleGrid();
        return true;
    }

    if (code == juce::KeyPress::deleteKey)
    {
        deleteSelection();
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        setSelection ({});
        return true;
    }

    if (alt && ! cmd && letter == 'V')
    {
        ghostButton.setToggleState (! ghostButton.getToggleState(), juce::sendNotificationSync);
        return true;
    }

    if (cmd && alt && code == juce::KeyPress::homeKey)
    {
        resizeFromLeft = ! resizeFromLeft;
        return true;
    }

    if (cmd && letter == 'A')
    {
        selectAll();
        return true;
    }

    if (cmd && letter == 'C')
    {
        copySelection();
        return true;
    }

    if (cmd && letter == 'X')
    {
        copySelection();
        deleteSelection();
        return true;
    }

    if (cmd && letter == 'V')
    {
        paste();
        return true;
    }

    if (cmd && letter == 'B')
    {
        duplicateSelection();
        return true;
    }

    // quick note operations: one undo step each, on the selection (or all notes)
    if (cmd && letter == 'Q')
    {
        runOperation ([grid] (Pattern& p, const std::vector<int>& t) { noteedit::quantizeFull (p, t, grid); return t; });
        return true;
    }

    if (shift && ! cmd && ! alt && letter == 'Q')
    {
        quantizeSelection();
        return true;
    }

    if (cmd && letter == 'U')
    {
        runOperation ([grid] (Pattern& p, const std::vector<int>& t) { return noteedit::chop (p, t, grid); });
        return true;
    }

    if (cmd && letter == 'G')
    {
        runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::join (p, t); return std::vector<int> {}; });
        return true;
    }

    if (cmd && letter == 'L')
    {
        runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::legato (p, t); return t; });
        return true;
    }

    if (shift && ! cmd && ! alt && letter == 'D')
    {
        runOperation ([grid] (Pattern& p, const std::vector<int>& t) { noteedit::shrinkToGrid (p, t, grid); return t; });
        return true;
    }

    if (cmd && shift && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        const auto steps = code == juce::KeyPress::rightKey ? 1 : -1;
        runOperation ([grid, bar, steps] (Pattern& p, const std::vector<int>& t) { noteedit::rotate (p, t, grid, bar, steps); return t; });
        return true;
    }

    if (cmd && letter == 'M')
    {
        toggleMute();
        return true;
    }

    if (cmd && letter == 'P')
    {
        togglePorta();
        return true;
    }

    if (! cmd && ! alt && ! selection.empty())
    {
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            nudge (0, (code == juce::KeyPress::upKey ? 1 : -1) * (shift ? 12 : 1));
            return true;
        }

        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            const auto amount = shift ? bar : grid;
            nudge ((code == juce::KeyPress::rightKey ? 1 : -1) * amount, 0);
            return true;
        }
    }

    // single letters: only while the typing piano is off (otherwise they play notes)
    if (! cmd && ! alt && ! shift && ! EditorGlobals::get().typingPiano)
    {
        for (const auto& t : editTools())
            if (t.tool != EditTool::slip && letter == t.key)
            {
                tools.setTool (t.tool);
                return true;
            }

        switch (letter)
        {
            case 'S': slideButton.setToggleState (! slideButton.getToggleState(), juce::dontSendNotification); return true;
            case 'G': stepChannel (-1, false); return true; // previous channel
            case 'K': stepChannel (1, false); return true;  // next channel
            case 'H': stepChannel (-1, true); return true;  // previous channel with notes
            case 'J': stepChannel (1, true); return true;   // next channel with notes
            default: break;
        }
    }

    return false;
}

juce::PopupMenu PianoRollComponent::midiMenu()
{
    juce::PopupMenu menu;
    menu.addItem (tr ("Import .mid into this channel..."), [this] {
        chooser = std::make_unique<juce::FileChooser> (tr ("Import MIDI"), juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            if (! fc.getResult().existsAsFile())
                return;

            juce::String error;
            auto notes = MidiFileIo::loadNotes (fc.getResult(), engine.getSelectedChannel(), error);

            if (! notes)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI", error);
                return;
            }

            std::vector<int> added;
            editNotes ([&] (Pattern& p) { added = noteedit::paste (p, *notes, 0); });
            setSelection (added);
            sortNotes();
            scrollToNotes();
        });
    });
    menu.addItem (tr ("Export pattern as .mid..."), [this] {
        const auto& song = engine.getSong();
        const auto name = juce::String::fromUTF8 (song.patterns[(std::size_t) song.currentPattern].name.c_str());
        chooser = std::make_unique<juce::FileChooser> (tr ("Export MIDI"), juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (name + ".mid"),
                                                       "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) {
            auto file = fc.getResult();

            if (file == juce::File())
                return;

            if (! file.hasFileExtension ("mid;midi"))
                file = file.withFileExtension (".mid");

            juce::String error;

            if (! MidiFileIo::savePattern (engine.getSong(), engine.getSong().currentPattern, file, error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI", error);
        });
    });
    return menu;
}

// --- other -------------------------------------------------------------------

void PianoRollComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hBar)
        scrollTicks = start;
    else
        scrollY = (int) start;

    repaint();
}

void PianoRollComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // pattern/channel switched or the pattern changed externally (undo, another window): the selection no longer applies
    const auto& song = engine.getSong();
    const auto count = currentPattern (song).notes.size();

    if (song.currentPattern != knownPattern || engine.getSelectedChannel() != knownChannel || count != knownNoteCount)
    {
        if (engine.getSelectedChannel() != knownChannel)
            stopListening();

        knownPattern = song.currentPattern;
        knownChannel = engine.getSelectedChannel();
        knownNoteCount = count;

        if (drag == Drag::none)
            selection.clear();
    }

    refreshChannelBox();
    updateScrollBars();
    repaint();
}

void PianoRollComponent::timerCallback()
{
    if (channelBox.getSelectedId() != engine.getSelectedChannel() + 1)
        refreshChannelBox();

    auto& transport = engine.getTransport();
    auto tick = -1.0;

    if (transport.playing.load() && isShowing() && engine.getSong().mode == PlayMode::pattern)
    {
        const auto& song = engine.getSong();
        tick = std::fmod (transport.positionTicks.load(), (double) currentPattern (song).effectiveLength (song.beatsPerBar));
    }

    if (tick != playheadTick)
    {
        auto dirty = [this] (double t) {
            if (t >= 0.0)
                repaint ((int) tickToX (t) - 7, 0, 14, getHeight());
        };

        dirty (playheadTick);
        playheadTick = tick;
        dirty (tick);
    }
}
} // namespace lynx
