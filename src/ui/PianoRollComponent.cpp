#include "ui/PianoRollComponent.h"

#include "core/NoteEdit.h"
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

struct SnapOption
{
    const char* name;
    int ticks; // 0 = one bar
};

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

const std::vector<NamedIntervals>& scales()
{
    static const std::vector<NamedIntervals> list {
        { TR_NOOP ("No scale"), {} },
        { TR_NOOP ("Major"), { 0, 2, 4, 5, 7, 9, 11 } },
        { TR_NOOP ("Natural minor"), { 0, 2, 3, 5, 7, 8, 10 } },
        { TR_NOOP ("Harmonic minor"), { 0, 2, 3, 5, 7, 8, 11 } },
        { TR_NOOP ("Major pentatonic"), { 0, 2, 4, 7, 9 } },
        { TR_NOOP ("Minor pentatonic"), { 0, 3, 5, 7, 10 } },
        { "Blues", { 0, 3, 5, 6, 7, 10 } },
        { TR_NOOP ("Dorian"), { 0, 2, 3, 5, 7, 9, 10 } },
        { TR_NOOP ("Phrygian"), { 0, 1, 3, 5, 7, 8, 10 } },
    };
    return list;
}

const SnapOption snapOptions[] = {
    { TR_NOOP ("No snap"), 1 }, { TR_NOOP ("1/6 step"), ticksPerStep / 6 }, { TR_NOOP ("1/4 step"), ticksPerStep / 4 },
    { TR_NOOP ("1/3 step"), ticksPerStep / 3 }, { TR_NOOP ("1/2 step"), ticksPerStep / 2 }, { TR_NOOP ("Step"), ticksPerStep },
    { TR_NOOP ("1/3 beat"), ppq / 3 }, { TR_NOOP ("1/2 beat"), ppq / 2 }, { TR_NOOP ("Beat"), ppq }, { TR_NOOP ("Bar"), 0 },
};
constexpr int defaultSnap = 5;
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

    for (int i = 0; i < (int) std::size (snapOptions); ++i)
        snapBox.addItem (tr (snapOptions[i].name), i + 1);

    snapBox.setSelectedItemIndex (defaultSnap, juce::dontSendNotification);
    snapBox.setTooltip (tr ("Piano Roll snap (grid); hold Alt while dragging to disable"));
    snapBox.setWantsKeyboardFocus (false);
    snapBox.setMouseClickGrabsKeyboardFocus (false);
    snapBox.onChange = [this] { repaint(); };
    addAndMakeVisible (snapBox);

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
        box->onChange = [this] { repaint(); };
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

    midiButton.setTooltip (tr ("Import a .mid into this channel or export the pattern"));
    midiButton.setWantsKeyboardFocus (false);
    midiButton.setMouseClickGrabsKeyboardFocus (false);
    midiButton.onClick = [this] { showMidiMenu(); };
    addAndMakeVisible (midiButton);

    hintLabel.setFont (theme::font (theme::type::caption));
    hintLabel.setColour (juce::Label::textColourId, theme::colour::textDisabled.brighter (0.2f));
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setMinimumHorizontalScale (1.0f);
    hintLabel.setText (tr ("Ctrl+drag selects · Ctrl+click adds · Shift+drag copies · Del deletes · Ctrl+C/X/V copy · "
                           "Ctrl+B duplicate · Ctrl+Q quantize · Ctrl+M mute · Ctrl+P porta · arrows move (Shift = octave)"),
                       juce::dontSendNotification);
    hintLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (hintLabel);

    engine.addChangeListener (this);
    startTimerHz (30);
}

PianoRollComponent::~PianoRollComponent()
{
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
    const auto index = juce::jlimit (0, (int) std::size (snapOptions) - 1, snapBox.getSelectedItemIndex());
    const auto ticks = snapOptions[index].ticks;
    return ticks > 0 ? ticks : (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
}

std::int64_t PianoRollComponent::snap (double tick, bool enabled) const
{
    const auto grid = enabled ? gridTicks() : 1;
    return (std::int64_t) std::floor (tick / (double) grid) * grid;
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
    snapBox.setBounds (keyboardWidth + 4, 3, 120, h);
    midiButton.setBounds (snapBox.getRight() + 4, 3, 50, h);
    slideButton.setBounds (midiButton.getRight() + 4, 3, 56, h);
    chordBox.setBounds (slideButton.getRight() + 4, 3, 96, h);
    scaleBox.setBounds (chordBox.getRight() + 4, 3, 180, h);
    // the shortcut hint only shows when there is room for it (two small lines)
    const auto hintArea = juce::Rectangle<int> (scaleBox.getRight() + 10, 0, getWidth() - scaleBox.getRight() - 14, toolbarHeight);
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

bool PianoRollComponent::inScale (int key) const
{
    const auto id = scaleBox.getSelectedId();

    if (id <= 1)
        return false;

    const auto& intervals = scales()[(std::size_t) (id / 100)].intervals;
    const auto root = (id % 100) - 1;
    return std::find (intervals.begin(), intervals.end(), ((key - root) % 12 + 12) % 12) != intervals.end();
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

        // ghost notes
        for (const auto& n : pattern.notes)
        {
            if (n.channel == channel)
                continue;

            const auto r = juce::Rectangle<float> ((float) tickToX ((double) n.start), (float) keyToY (n.key) + 1,
                                                   (float) (n.length * pixelsPerTick), (float) rowHeight - 2);
            g.setColour (textPrimary.withAlpha (0.07f));
            g.fillRoundedRectangle (r, 2.0f);
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
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto channel = engine.getSelectedChannel();
    const auto key = yToKey (y);

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
            return i;
        }
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

void PianoRollComponent::mouseDown (const juce::MouseEvent& e)
{
    if (engine.getSong().channels.empty())
        return;

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

    bool rightEdge = false;
    const auto hit = noteAt (e.x, e.y, rightEdge);

    if (e.mods.isRightButtonDown())
    {
        drag = Drag::erase;
        setSelection ({}); // indices change while deleting

        if (hit >= 0)
        {
            editNotes ([hit] (Pattern& p) { noteedit::remove (p, { hit }); });
        }
        return;
    }

    // as in FL: Ctrl+click on a note adds/removes it from the selection; Ctrl+drag on empty space selects
    // (with Shift, adding); Shift+drag on a note drags a copy
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

    if (hit < 0 && (e.mods.isCtrlDown() || e.mods.isShiftDown()))
    {
        drag = Drag::select;
        selectionBefore = e.mods.isShiftDown() ? selection : std::vector<int> {};
        rubberBand = { e.x, e.y, 0, 0 };
        setSelection (selectionBefore);
        return;
    }

    const auto snapOn = ! e.mods.isAltDown();

    if (hit >= 0 && e.mods.isShiftDown() && ! rightEdge)
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

        drag = rightEdge ? Drag::resize : Drag::move;
        grabOffset = n.start - snap (xToTick (e.x), snapOn);
        lastLength = n.length;
        previewOn (n.key, n.velocity);
        return;
    }

    // new note (or the chosen chord, with the clicked note as the root)
    setSelection ({});
    Note n;
    n.start = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
    n.length = lastLength;
    n.key = yToKey (e.y);
    n.channel = engine.getSelectedChannel();
    n.slide = slideButton.getToggleState();

    std::vector<Note> stamp;
    for (auto interval : chordIntervals())
        if (n.key + interval <= 127)
        {
            auto chordNote = n;
            chordNote.key = n.key + interval;
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
        {
            rubberBand = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition()).getIntersection (gridArea());
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
            bool edge = false;
            const auto hit = noteAt (e.x, e.y, edge);

            if (hit >= 0)
                editNotes ([hit] (Pattern& p) { noteedit::remove (p, { hit }); });
            return;
        }

        case Drag::move:
        {
            const auto& grabbed = dragOriginals[(std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragNote) - dragIndices.begin())];
            auto deltaTicks = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) + grabOffset) - grabbed.start;
            auto deltaKeys = yToKey (e.y) - grabbed.key;
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
    }
}

void PianoRollComponent::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::move || drag == Drag::resize)
        sortNotes();

    previewOff();
    drag = Drag::none;
    dragNote = -1;
    dragIndices.clear();
    dragOriginals.clear();
    rubberBand = {};
    repaint();
}

void PianoRollComponent::mouseMove (const juce::MouseEvent& e)
{
    bool rightEdge = false;
    const auto hit = gridArea().contains (e.getPosition()) ? noteAt (e.x, e.y, rightEdge) : -1;
    setMouseCursor (hit >= 0 ? (rightEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                             : velocityArea().contains (e.getPosition()) ? juce::MouseCursor::CrosshairCursor
                                                                         : juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
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
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        setSelection ({});
        return true;
    }

    if (cmd && code == 'A')
    {
        selectAll();
        return true;
    }

    if (cmd && code == 'C')
    {
        copySelection();
        return true;
    }

    if (cmd && code == 'X')
    {
        copySelection();
        deleteSelection();
        return true;
    }

    if (cmd && code == 'V')
    {
        paste();
        return true;
    }

    if (cmd && code == 'B')
    {
        duplicateSelection();
        return true;
    }

    if (cmd && code == 'Q')
    {
        quantizeSelection();
        return true;
    }

    if (cmd && code == 'M')
    {
        toggleMute();
        return true;
    }

    if (cmd && code == 'P')
    {
        togglePorta();
        return true;
    }

    if (! cmd && ! selection.empty())
    {
        const auto bar = (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);

        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            nudge (0, (code == juce::KeyPress::upKey ? 1 : -1) * (mods.isShiftDown() ? 12 : 1));
            return true;
        }

        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            const auto amount = mods.isShiftDown() ? bar : gridTicks();
            nudge ((code == juce::KeyPress::rightKey ? 1 : -1) * amount, 0);
            return true;
        }
    }

    return false;
}

void PianoRollComponent::showMidiMenu()
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
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&midiButton));
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
        knownPattern = song.currentPattern;
        knownChannel = engine.getSelectedChannel();
        knownNoteCount = count;

        if (drag == Drag::none)
            selection.clear();
    }

    updateScrollBars();
    repaint();
}

void PianoRollComponent::timerCallback()
{
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
