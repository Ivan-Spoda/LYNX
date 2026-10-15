#include "ui/PlaylistComponent.h"

#include "core/RackOps.h"

#include "core/NoteEdit.h"
#include "engine/Text.h"
#include "ui/BrowserComponent.h"
#include "ui/AutomationUi.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WaveformView.h"

namespace lynx
{
namespace
{
constexpr float clipTitleHeight = 14.0f;

// Text colour readable on a clip's title band.
juce::Colour clipTitleText (juce::Colour band)
{
    return band.getPerceivedBrightness() > 0.62f ? theme::colour::bg0 : theme::colour::textPrimary;
}

// Clip body (dark tint of the clip colour) with a solid title band; returns the body below the band.
juce::Rectangle<float> paintClipFrame (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, const juce::String& name)
{
    using namespace theme;
    g.setColour (base.interpolatedWith (colour::bg1, 0.62f));
    g.fillRoundedRectangle (r, radius::sm);

    const auto title = r.withHeight (juce::jmin (clipTitleHeight, r.getHeight()));
    juce::Path band;
    band.addRoundedRectangle (title.getX(), title.getY(), title.getWidth(), title.getHeight(), radius::sm, radius::sm, true, true, false, false);
    g.setColour (base);
    g.fillPath (band);

    if (r.getWidth() > 24.0f)
    {
        g.setColour (clipTitleText (base));
        g.setFont (font (type::caption, true));
        g.drawText (name, title.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
    }

    return r.withTrimmedTop (title.getHeight());
}

void paintClipOutline (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, bool selected, bool current)
{
    using namespace theme;
    if (selected)
    {
        g.setColour (colour::selection);
        g.drawRoundedRectangle (r.reduced (0.5f), radius::sm, 2.0f);
    }
    else
    {
        g.setColour (current ? colour::textPrimary.withAlpha (0.55f) : base.darker (0.3f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius::sm, current ? 1.2f : 1.0f);
    }
}

} // namespace

PlaylistComponent::PlaylistComponent (AudioEngine& e)
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
    snapBox.setTooltip (tr ("Playlist grid: follows the global grid in the toolbar, or its own; hold Alt while dragging to ignore it"));
    snapBox.setWantsKeyboardFocus (false);
    snapBox.setMouseClickGrabsKeyboardFocus (false);
    snapBox.onChange = [this] { repaint(); };
    addAndMakeVisible (snapBox);
    tools.onChange = [this] (EditTool) { updateCursor (juce::ModifierKeys::currentModifiers); };
    setOpaque (true); // paints every pixel: nothing behind it is redrawn while scrolling
    addAndMakeVisible (tools);

    arrangementBox.setTooltip (tr ("Arrangement (as in FL, a project can have several)"));
    arrangementBox.setWantsKeyboardFocus (false);
    arrangementBox.setMouseClickGrabsKeyboardFocus (false);
    arrangementBox.onChange = [this] { arrangementChosen(); };
    addAndMakeVisible (arrangementBox);
    refreshArrangementBox();

    engine.addChangeListener (this);
    EditorGlobals::get().addChangeListener (this);
    ui::WaveformCache::getInstance()->addChangeListener (this); // a waveform became ready, or the settings changed
    startTimerHz (30);
}

namespace
{
enum ArrangementAction
{
    newArrangement = 1001,
    cloneArrangement,
    renameArrangementAction,
    removeArrangementAction
};
} // namespace

void PlaylistComponent::refreshArrangementBox()
{
    const auto& song = engine.getSong();
    juce::StringArray names;

    for (const auto& a : song.arrangements)
        names.add (juce::String::fromUTF8 (a.name.c_str()));

    // only rebuilds when something changed (rebuilding closes the open menu)
    bool same = arrangementBox.getNumItems() == names.size() + 4;
    for (int i = 0; same && i < names.size(); ++i)
        same = arrangementBox.getItemText (i) == names[i];

    if (! same)
    {
        arrangementBox.clear (juce::dontSendNotification);

        for (int i = 0; i < names.size(); ++i)
            arrangementBox.addItem (names[i], i + 1);

        arrangementBox.addSeparator();
        arrangementBox.addItem (tr ("New arrangement"), newArrangement);
        arrangementBox.addItem (tr ("Clone this one"), cloneArrangement);
        arrangementBox.addItem (tr ("Rename..."), renameArrangementAction);
        arrangementBox.addItem (tr ("Remove this one"), removeArrangementAction);
        arrangementBox.setItemEnabled (removeArrangementAction, names.size() > 1);
    }

    arrangementBox.setSelectedId (song.currentArrangement + 1, juce::dontSendNotification);
}

void PlaylistComponent::arrangementChosen()
{
    const auto id = arrangementBox.getSelectedId();
    const auto& song = engine.getSong();
    setSelection ({});

    if (id >= 1 && id <= (int) song.arrangements.size())
    {
        if (id - 1 != song.currentArrangement)
            engine.editSong ([index = id - 1] (Song& s) { s.selectArrangement (index); }, false);
        return;
    }

    switch (id)
    {
        case newArrangement:
            engine.editSong ([] (Song& s) { s.addArrangement ("Arrangement " + std::to_string (s.arrangements.size() + 1)); });
            break;
        case cloneArrangement:
            engine.editSong ([] (Song& s) {
                s.addArrangement (s.arrangements[(std::size_t) s.currentArrangement].name + " (copy)", true);
            });
            break;
        case renameArrangementAction:
            renameArrangement();
            break;
        case removeArrangementAction:
            engine.editSong ([] (Song& s) { s.removeArrangement (s.currentArrangement); });
            break;
        default:
            break;
    }

    refreshArrangementBox();
}

void PlaylistComponent::renameArrangement()
{
    const auto index = engine.getSong().currentArrangement;
    auto* window = new juce::AlertWindow (tr ("Rename arrangement"), {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String::fromUTF8 (engine.getSong().arrangements[(std::size_t) index].name.c_str()));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, index, window] (int result) {
                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();

                                 if (result == 1 && ! name.empty() && index < (int) engine.getSong().arrangements.size())
                                     engine.editSong ([index, name] (Song& s) { s.arrangements[(std::size_t) index].name = name; });
                             }),
                             true);
}

PlaylistComponent::~PlaylistComponent()
{
    EditorGlobals::get().removeChangeListener (this);
    engine.removeChangeListener (this);
    if (auto* cache = ui::WaveformCache::getInstanceWithoutCreating())
        cache->removeChangeListener (this);
}

juce::Colour PlaylistComponent::patternColour (const Song& song, int pattern)
{
    if (pattern >= 0 && pattern < (int) song.patterns.size() && song.patterns[(std::size_t) pattern].colour != 0)
        return juce::Colour (song.patterns[(std::size_t) pattern].colour);

    // hues spread by the golden ratio: neighboring patterns get clearly different colors
    const auto hue = std::fmod (0.08f + (float) pattern * 0.618034f, 1.0f);
    return juce::Colour::fromHSV (hue, 0.45f, 0.75f, 1.0f);
}

juce::Rectangle<int> PlaylistComponent::gridArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedTop (headerHeight)
        .withTrimmedRight (scrollBarSize).withTrimmedBottom (scrollBarSize);
}

double PlaylistComponent::tickToX (double tick) const { return headerWidth + (tick - scrollTicks) * pixelsPerTick; }
double PlaylistComponent::xToTick (int x) const { return (x - headerWidth) / pixelsPerTick + scrollTicks; }
int PlaylistComponent::trackToY (int track) const { return headerHeight + track * trackHeight - scrollY; }
int PlaylistComponent::yToTrack (int y) const { return juce::jlimit (0, numTracks() - 1, (y - headerHeight + scrollY) / trackHeight); }

int PlaylistComponent::numTracks() const
{
    int highest = 0;

    for (const auto& c : engine.getSong().playlist)
        highest = juce::jmax (highest, c.track);

    return juce::jlimit (16, 500, highest + 8); // always a few free tracks at the bottom
}

std::int64_t PlaylistComponent::gridTicks() const
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

std::vector<std::int64_t> PlaylistComponent::snapCandidates() const
{
    // edges of the other clips ("Nearest note" in the Playlist snaps to clip edges)
    std::vector<std::int64_t> out;
    const auto& playlist = engine.getSong().playlist;

    for (int i = 0; i < (int) playlist.size(); ++i)
        if (std::find (dragIndices.begin(), dragIndices.end(), i) == dragIndices.end())
        {
            out.push_back (playlist[(std::size_t) i].start);
            out.push_back (playlist[(std::size_t) i].end());
        }

    return out;
}

std::int64_t PlaylistComponent::snap (double tick, bool enabled) const
{
    if (! enabled)
        return (std::int64_t) std::floor (tick);

    const auto& option = grid::resolve (snapBox);
    const auto step = gridTicks();

    if (option.kind == grid::Kind::nearest)
        return noteedit::snapNearest (tick, snapCandidates(), 10.0 / pixelsPerTick, step);

    // whole bars respect time signature changes
    if (step == (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar))
        return engine.getSong().barAt ((std::int64_t) std::max (0.0, std::floor (tick))).tick;

    return noteedit::snapDown (tick, step);
}

void PlaylistComponent::updateScrollBars()
{
    const auto& song = engine.getSong();
    const auto grid = gridArea();
    const auto bar = (double) ppq * song.beatsPerBar;
    const auto total = juce::jmax ((double) song.songLength() + 16 * bar, grid.getWidth() / pixelsPerTick);

    hBar.setRangeLimits (0.0, total, juce::dontSendNotification);
    hBar.setCurrentRange (scrollTicks, grid.getWidth() / pixelsPerTick, juce::dontSendNotification);
    vBar.setRangeLimits (0.0, (double) numTracks() * trackHeight, juce::dontSendNotification);
    vBar.setCurrentRange (scrollY, grid.getHeight(), juce::dontSendNotification);
    scrollTicks = hBar.getCurrentRangeStart();
    scrollY = (int) vBar.getCurrentRangeStart();
}

void PlaylistComponent::setTitleBarInsets (int left, int right)
{
    if (left != titleInsetLeft || right != titleInsetRight)
    {
        titleInsetLeft = left;
        titleInsetRight = right;
        resized();
    }
}

void PlaylistComponent::resized()
{
    const auto h = toolbarHeight - 6;
    arrangementBox.setBounds (4 + titleInsetLeft, 3, juce::jmin (150, getWidth() / 5), h);
    tools.setBounds (arrangementBox.getRight() + 8, 3, tools.getIdealWidth (h), h);
    snapBox.setBounds (tools.getRight() + 8, 3, 128, h);
    hBar.setBounds (headerWidth, getHeight() - scrollBarSize, getWidth() - headerWidth - scrollBarSize, scrollBarSize);
    vBar.setBounds (getWidth() - scrollBarSize, headerHeight, scrollBarSize, getHeight() - headerHeight - scrollBarSize);
    updateScrollBars();
}

void PlaylistComponent::paint (juce::Graphics& g)
{
    const auto& song = engine.getSong();
    const auto grid = gridArea();
    const auto bar = (std::int64_t) ppq * song.beatsPerBar;
    const auto tracks = numTracks();

    using namespace theme;
    g.fillAll (colour::bg1);

    // --- tracks and grid ---------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (grid);

        for (int t = 0; t < tracks; ++t)
        {
            const auto y = trackToY (t);

            if (y > grid.getBottom() || y + trackHeight < grid.getY())
                continue;

            auto lane = t % 2 == 0 ? colour::bg2 : colour::bg2.interpolatedWith (colour::bg1, 0.5f);
            if (t == hover.track && drag == Drag::none)
                lane = lane.interpolatedWith (colour::textPrimary, 0.025f); // the track under the mouse
            g.setColour (lane);
            g.fillRect (grid.getX(), y, grid.getWidth(), trackHeight);
            g.setColour (colour::bg0);
            g.drawHorizontalLine (y + trackHeight - 1, (float) grid.getX(), (float) grid.getRight());
        }

        const auto beatPixels = ppq * pixelsPerTick;

        for (const auto& b : song.barsBetween ((std::int64_t) std::max (0.0, scrollTicks), (std::int64_t) xToTick (grid.getRight()) + 1))
        {
            g.setColour (colour::gridBar);
            g.drawVerticalLine ((int) tickToX ((double) b.tick), (float) grid.getY(), (float) grid.getBottom());

            if (beatPixels < 8.0)
                continue; // zoomed far out: bar lines only

            g.setColour (colour::gridBeat);
            for (auto tick = b.tick + ppq; tick < b.tick + b.length; tick += ppq)
                g.drawVerticalLine ((int) tickToX ((double) tick), (float) grid.getY(), (float) grid.getBottom());
        }

        // loop region
        if (song.hasLoop())
        {
            const auto lx0 = (float) tickToX ((double) song.loopStart), lx1 = (float) tickToX ((double) song.loopEnd);
            g.setColour (colour::accent.withAlpha (0.06f));
            g.fillRect (juce::Rectangle<float> (lx0, (float) grid.getY(), lx1 - lx0, (float) grid.getHeight()));
            g.setColour (colour::accent.withAlpha (0.45f));
            g.drawVerticalLine ((int) lx0, (float) grid.getY(), (float) grid.getBottom());
            g.drawVerticalLine ((int) lx1, (float) grid.getY(), (float) grid.getBottom());
        }

        // end of song
        if (! song.playlist.empty())
        {
            const auto endX = (float) tickToX ((double) song.songLength());
            g.setColour (colour::bg0.withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (endX, (float) grid.getY(), (float) grid.getRight() - endX, (float) grid.getHeight()));
        }

        g.setFont (font (type::caption));

        // muted clips (the Mute tool) are veiled
        auto veilIfMuted = [&g] (const Clip& clip, juce::Rectangle<float> area) {
            if (clip.muted)
            {
                g.setColour (colour::bg1.withAlpha (0.62f));
                g.fillRoundedRectangle (area, radius::sm);
                g.setColour (colour::textDisabled);
                g.drawLine (area.getX() + 2.0f, area.getBottom() - 2.0f, area.getRight() - 2.0f, area.getY() + 2.0f, 1.0f);
            }
        };

        for (int i = 0; i < (int) song.playlist.size(); ++i)
        {
            const auto& c = song.playlist[(std::size_t) i];
            const auto x0 = (float) tickToX ((double) c.start);
            const auto x1 = (float) tickToX ((double) c.end());
            const auto y = (float) trackToY (c.track);

            if (x1 < grid.getX() || x0 > grid.getRight() || y > grid.getBottom() || y + trackHeight < grid.getY())
                continue;

            const auto r = juce::Rectangle<float> (x0, y + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), (float) trackHeight - 3.0f);

            if (! g.getClipBounds().toFloat().intersects (r.expanded (1.0f)))
                continue; // outside the area being repainted

            if (c.kind == ClipKind::automation)
            {
                paintAutomationClip (g, c, r);
                veilIfMuted (c, r);

                if (isSelected (i))
                    paintClipOutline (g, r, colour::automation, true, false);
                continue;
            }

            if (c.kind == ClipKind::audio)
            {
                paintAudioClip (g, c, r);
                veilIfMuted (c, r);

                if (isSelected (i))
                    paintClipOutline (g, r, colour::audioClip, true, false);
                continue;
            }

            const auto base = tame (patternColour (song, c.pattern));
            const auto current = c.pattern == song.currentPattern;
            const auto selected = isSelected (i);

            // body + name strip
            const auto clipBody = paintClipFrame (g, r, base, juce::String::fromUTF8 (song.patterns[(std::size_t) c.pattern].name.c_str()));

            // note thumbnail
            const auto& notes = song.patterns[(std::size_t) c.pattern].notes;

            if (! notes.empty() && r.getWidth() > 6)
            {
                int low = 127, high = 0;
                for (const auto& n : notes)
                {
                    low = juce::jmin (low, n.key);
                    high = juce::jmax (high, n.key);
                }

                const auto body = clipBody.reduced (2.0f, 3.0f);
                const auto keySpan = (float) juce::jmax (1, high - low);
                g.setColour (base.brighter (0.25f));

                for (const auto& n : notes)
                {
                    if (n.start < c.offset || n.start >= c.offset + c.length)
                        continue;

                    const auto nx = (float) tickToX ((double) (c.start + n.start - c.offset));
                    const auto nw = juce::jmax (1.5f, (float) (juce::jmin (n.length, c.end() - (c.start + n.start - c.offset)) * pixelsPerTick));
                    const auto ny = body.getBottom() - (n.key - low) / keySpan * (body.getHeight() - 2.0f) - 2.0f;
                    g.fillRect (nx, ny, nw, 2.0f);
                }
            }

            veilIfMuted (c, r);
            paintClipOutline (g, r, base, selected, current);
        }

        // the clip under the mouse: outline, the grip of the edge it is on, its menu button
        if (hover.clip >= 0 && hover.clip < (int) song.playlist.size() && drag == Drag::none)
        {
            const auto r = clipBounds (hover.clip).toFloat();
            g.setColour (colour::textPrimary.withAlpha (isSelected (hover.clip) ? 0.0f : 0.55f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius::sm, 1.2f);

            if (hover.target == gesture::Target::leftEdge || hover.target == gesture::Target::rightEdge)
            {
                const auto x = hover.target == gesture::Target::rightEdge ? r.getRight() - 4.0f : r.getX() + 1.0f;
                g.setColour (colour::textPrimary);
                g.fillRoundedRectangle (x, r.getY() + clipTitleHeight + 3.0f, 3.0f, juce::jmax (4.0f, r.getHeight() - clipTitleHeight - 6.0f), 1.5f);
            }

            if (const auto button = clipMenuButton (hover.clip); ! button.isEmpty())
            {
                const auto b = button.toFloat();
                g.setColour (hover.target == gesture::Target::clipMenu ? colour::bg0.withAlpha (0.75f) : colour::bg0.withAlpha (0.45f));
                g.fillRoundedRectangle (b, 2.0f);
                icons::draw (g, Icon::chevronDown, b.reduced (1.0f), colour::textPrimary);
            }
        }

        if (! rubberBand.isEmpty())
        {
            g.setColour (colour::selectionFill);
            g.fillRect (rubberBand);
            g.setColour (colour::selection);
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
            g.setColour (colour::textPrimary);
            g.fillPath (dashed);
        }

        if (playheadTick >= 0.0)
        {
            g.setColour (colour::playhead);
            g.fillRect (juce::Rectangle<float> ((float) tickToX (playheadTick) - 0.5f, (float) grid.getY(), 1.5f, (float) grid.getHeight()));
        }
    }

    // --- ruler (drawn in its own coordinates, below the toolbar) -----------------
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setOrigin (0, toolbarHeight);
        const auto ruler = juce::Rectangle<int> (headerWidth, 0, grid.getWidth(), timelineHeight);
        g.setColour (colour::bg2);
        g.fillRect (ruler);
        g.setColour (colour::border);
        g.fillRect (ruler.withTop (ruler.getBottom() - 1));
        g.reduceClipRegion (ruler);
        g.setFont (font (type::small));

        if (song.hasLoop()) // the time selection
        {
            const auto lx0 = (float) tickToX ((double) song.loopStart), lx1 = (float) tickToX ((double) song.loopEnd);
            g.setColour (colour::accent.withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float> (lx0, 0.0f, lx1 - lx0, (float) timelineHeight));
            g.setColour (colour::accent.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (lx0, 0.0f, lx1 - lx0, 4.0f));
        }

        const auto barPixels = bar * pixelsPerTick;
        const auto every = barPixels < 30.0 ? (barPixels < 10.0 ? 16 : 4) : 1; // numbers spaced further apart when zoomed out

        for (const auto& b : song.barsBetween ((std::int64_t) std::max (0.0, scrollTicks), (std::int64_t) xToTick (grid.getRight()) + 1))
        {
            if ((b.number - 1) % every != 0)
                continue;

            const auto x = (int) tickToX ((double) b.tick);
            g.setColour (colour::textDisabled);
            g.drawVerticalLine (x, 12.0f, (float) timelineHeight);
            g.setColour (colour::textSecondary);
            g.drawText (juce::String (b.number), x + 3, 2, 40, timelineHeight - 4, juce::Justification::centredLeft);
        }

        // markers: small flag with the name; time signature changes show the fraction
        g.setFont (font (type::caption, true));

        for (const auto& m : song.markers())
        {
            const auto x = (float) tickToX ((double) m.tick);

            if (x < (float) ruler.getX() - 200.0f || x > (float) ruler.getRight())
                continue;

            auto label = juce::String::fromUTF8 (m.name.c_str());
            if (const auto fraction = juce::String (m.numerator) + "/" + juce::String (m.denominator); m.isSignature() && label != fraction)
                label = fraction + (label.isEmpty() ? "" : " " + label);
            if (label.isEmpty())
                label = "*";

            const auto width = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 8.0f;
            const auto tag = juce::Rectangle<float> (x, (float) timelineHeight - 11.0f, width, 11.0f);
            g.setColour (m.isSignature() ? colour::info : colour::warning);
            g.fillRoundedRectangle (tag, 2.0f);
            g.drawVerticalLine ((int) x, 0.0f, (float) timelineHeight);
            g.setColour (colour::bg0);
            g.drawText (label, tag.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
        }

        if (playheadTick >= 0.0)
        {
            const auto x = (float) tickToX (playheadTick);
            juce::Path tri;
            tri.addTriangle (x - 6, 0, x + 6, 0, x, 9);
            g.setColour (colour::playhead);
            g.fillPath (tri);
        }
    }

    // --- track headers -----------------------------------------------------
    {
        const auto headers = juce::Rectangle<int> (0, headerHeight, headerWidth, grid.getHeight());
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (headers);
        g.setColour (colour::bg2);
        g.fillRect (headers);
        g.setFont (font (type::small));

        for (int t = 0; t < tracks; ++t)
        {
            const auto y = trackToY (t);

            if (y > headers.getBottom() || y + trackHeight < headers.getY())
                continue;

            const auto muted = song.isTrackMuted (t);
            const auto custom = t < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) t].name.empty();
            const auto hovered = hover.header && hover.track == t;
            auto plate = muted ? colour::bg2 : colour::bg3;
            if (hovered)
                plate = plate.interpolatedWith (colour::bg5, 0.6f);
            g.setColour (plate);
            g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, (float) y + 1.0f, (float) headerWidth - 4.0f, (float) trackHeight - 3.0f), radius::sm);
            g.setColour (muteLed (muted));
            g.fillEllipse (7.0f, (float) y + trackHeight / 2.0f - 4.0f, 8.0f, 8.0f);
            if (hovered && hover.led) // the LED is a button: ring it
            {
                g.setColour (colour::textPrimary.withAlpha (0.7f));
                g.drawEllipse (5.0f, (float) y + trackHeight / 2.0f - 6.0f, 12.0f, 12.0f, 1.0f);
            }
            g.setColour (muted ? colour::textDisabled : (custom ? colour::textPrimary : colour::textSecondary));
            g.drawFittedText (custom ? juce::String::fromUTF8 (song.playlistTracks[(std::size_t) t].name.c_str())
                                     : tr ("Track {0}", { juce::String (t + 1) }),
                              20, y, headerWidth - 24, trackHeight, juce::Justification::centredLeft, 2, 0.8f);
        }
    }

    // toolbar band, and the corner left of the ruler
    g.setColour (colour::bg3);
    g.fillRect (0, 0, getWidth(), toolbarHeight);
    g.setColour (colour::bg2);
    g.fillRect (0, toolbarHeight, headerWidth, timelineHeight);
    g.setColour (colour::border);
    g.fillRect (0, toolbarHeight - 1, getWidth(), 1);
    g.fillRect (0, headerHeight - 1, headerWidth, 1);
    g.fillRect (headerWidth - 1, toolbarHeight, 1, getHeight() - toolbarHeight);
}

void PlaylistComponent::paintOverChildren (juce::Graphics& g)
{
    if (! ripple)
        return;

    // ripple editing is on: a small tag in the grid's top-right corner
    const auto grid = gridArea();
    const auto tag = juce::Rectangle<float> ((float) grid.getRight() - 70.0f, (float) grid.getY() + 4.0f, 64.0f, 18.0f);
    g.setColour (theme::colour::accentMuted);
    g.fillRoundedRectangle (tag, theme::radius::sm);
    g.setColour (theme::colour::accent);
    g.drawRoundedRectangle (tag.reduced (0.5f), theme::radius::sm, 1.0f);
    g.setFont (theme::font (theme::type::caption, true));
    g.drawText (tr ("Ripple"), tag.toNearestInt(), juce::Justification::centred);
}

void PlaylistComponent::paintAudioClip (juce::Graphics& g, const Clip& c, juce::Rectangle<float> r)
{
    const auto& song = engine.getSong();

    if (c.channel < 0 || c.channel >= (int) song.channels.size())
        return;

    const auto& channel = song.channels[(std::size_t) c.channel];
    const auto colour = theme::colour::audioClip;
    const auto name = juce::String::fromUTF8 (channel.name.c_str());

    // waveform (ui/WaveformView): analysed in the background, drawn at any zoom with its band colours; it uses
    // the clip's whole height (tracks are short) and the title band is laid over it, translucent
    const auto path = juce::String::fromUTF8 (channel.samplePath.c_str());
    const auto summary = ui::WaveformCache::getInstance()->find (path, [this, path] { return engine.getSampleLibrary().load (path); });

    if (summary == nullptr)
        paintClipFrame (g, r, colour, name);
    else
    {
        g.setColour (colour.interpolatedWith (theme::colour::bg1, 0.62f));
        g.fillRoundedRectangle (r, theme::radius::sm);
        const auto body = r.reduced (1.0f, 1.0f);
        const auto framesPerTick = 60.0 / (song.bpm * ppq) * summary->sampleRate;
        // r's left edge is the clip start, i.e. tick `offset` of the audio
        const auto startFrame = ((double) c.offset + (double) (body.getX() - r.getX()) / pixelsPerTick) * framesPerTick;
        ui::paintWaveform (g, body, *summary, startFrame, framesPerTick / pixelsPerTick,
                           ui::currentWaveStyle (colour.brighter (0.35f), ui::normalisingGain (*summary)));

        const auto title = r.withHeight (juce::jmin (clipTitleHeight, r.getHeight()));
        juce::Path band;
        band.addRoundedRectangle (title.getX(), title.getY(), title.getWidth(), title.getHeight(), theme::radius::sm, theme::radius::sm, true, true, false, false);
        g.setColour (colour.withAlpha (0.55f));
        g.fillPath (band);
        if (r.getWidth() > 24.0f)
        {
            g.setColour (theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::caption, true));
            g.drawText (name, title.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, true);
        }
    }
    paintClipOutline (g, r, colour, false, false);

    // fades: ramps and handles at the top corners
    {
        const auto inX = r.getX() + (float) (c.fadeIn * pixelsPerTick);
        const auto outX = r.getRight() - (float) (c.fadeOut * pixelsPerTick);
        g.setColour (theme::colour::textPrimary.withAlpha (0.8f));

        if (c.fadeIn > 0)
            g.drawLine (r.getX(), r.getBottom(), inX, r.getY() + 14.0f, 1.2f);
        if (c.fadeOut > 0)
            g.drawLine (outX, r.getY() + 14.0f, r.getRight(), r.getBottom(), 1.2f);

        if (r.getWidth() > 20.0f)
        {
            g.fillRect (juce::Rectangle<float> (inX, r.getY() + 13.0f, 5.0f, 5.0f));
            g.fillRect (juce::Rectangle<float> (outX - 5.0f, r.getY() + 13.0f, 5.0f, 5.0f));
        }
    }
}

void PlaylistComponent::paintAutomationClip (juce::Graphics& g, const Clip& c, juce::Rectangle<float> r)
{
    const auto& song = engine.getSong();

    if (c.automation < 0 || c.automation >= (int) song.automations.size())
        return;

    const auto& automation = song.automations[(std::size_t) c.automation];
    const auto colour = automation.target.kind == AutomationTargetKind::none ? theme::colour::textDisabled
                                                                              : theme::colour::automation.withMultipliedSaturation (0.75f).withMultipliedBrightness (0.8f);
    paintClipFrame (g, r, colour, juce::String::fromUTF8 (automation.name.c_str()));
    paintClipOutline (g, r, colour, false, false);

    // automation curve over the clip's range
    const auto body = r.withTrimmedTop (14.0f).reduced (1.0f, 2.0f);
    const auto clip = g.getClipBounds().toFloat();
    juce::Path curve;
    bool started = false;

    for (auto x = std::max (body.getX(), clip.getX()); x <= std::min (body.getRight(), clip.getRight()); x += 2.0f)
    {
        const auto value = automation.valueAt ((double) c.offset + xToTick ((int) x) - (double) c.start);
        const auto y = body.getBottom() - value * body.getHeight();

        if (! started)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);

        started = true;
    }

    g.setColour (automation.target.kind == AutomationTargetKind::none ? theme::colour::textSecondary : theme::colour::automation.brighter (0.3f));
    g.strokePath (curve, juce::PathStrokeType (1.5f));

    // the points inside the clip, as handles (edited right here)
    if (r.getWidth() > 24.0f)
        for (const auto& p : automation.points)
        {
            const auto songTick = (double) (c.start + p.tick - c.offset);
            if (songTick < (double) c.start || songTick > (double) c.end())
                continue;
            const auto x = (float) tickToX (songTick);
            const auto y = body.getBottom() - p.value * body.getHeight();
            g.setColour (theme::colour::bg0);
            g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            g.setColour (theme::colour::automation.brighter (0.5f));
            g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        }
}

juce::Rectangle<float> PlaylistComponent::automationBody (const Clip& c) const
{
    const auto x0 = (float) tickToX ((double) c.start), x1 = (float) tickToX ((double) c.end());
    const auto y = (float) trackToY (c.track);
    const auto r = juce::Rectangle<float> (x0, y + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), (float) trackHeight - 3.0f);
    return r.withTrimmedTop (14.0f).reduced (1.0f, 2.0f);
}

int PlaylistComponent::automationPointAt (int clip, juce::Point<int> pos) const
{
    const auto& song = engine.getSong();

    if (clip < 0 || clip >= (int) song.playlist.size() || song.playlist[(std::size_t) clip].kind != ClipKind::automation)
        return -1;

    const auto& c = song.playlist[(std::size_t) clip];
    if (c.automation < 0 || c.automation >= (int) song.automations.size())
        return -1;

    const auto body = automationBody (c);
    const auto& points = song.automations[(std::size_t) c.automation].points;

    for (int i = (int) points.size() - 1; i >= 0; --i)
    {
        const auto songTick = (double) (c.start + points[(std::size_t) i].tick - c.offset);
        if (songTick < (double) c.start || songTick > (double) c.end())
            continue;
        const juce::Point<float> at ((float) tickToX (songTick), body.getBottom() - points[(std::size_t) i].value * body.getHeight());
        if (at.getDistanceFrom (pos.toFloat()) <= 6.0f)
            return i;
    }

    return -1;
}

bool PlaylistComponent::isOnAutomationCurve (int clip, juce::Point<int> pos) const
{
    const auto& song = engine.getSong();

    if (clip < 0 || clip >= (int) song.playlist.size() || song.playlist[(std::size_t) clip].kind != ClipKind::automation)
        return false;

    const auto& c = song.playlist[(std::size_t) clip];
    if (c.automation < 0 || c.automation >= (int) song.automations.size())
        return false;

    const auto body = automationBody (c);
    if (! body.expanded (0.0f, 4.0f).contains (pos.toFloat()))
        return false;

    const auto value = song.automations[(std::size_t) c.automation].valueAt ((double) c.offset + xToTick (pos.x) - (double) c.start);
    return std::abs (body.getBottom() - value * body.getHeight() - (float) pos.y) <= 5.0f;
}

int PlaylistComponent::fadeHandleAt (int index, juce::Point<int> p) const
{
    const auto& c = engine.getSong().playlist[(std::size_t) index];

    if (c.kind != ClipKind::audio)
        return 0;

    const auto y = trackToY (c.track) + 1 + 15;
    const auto inX = tickToX ((double) c.start) + c.fadeIn * pixelsPerTick;
    const auto outX = tickToX ((double) c.end()) - c.fadeOut * pixelsPerTick;

    if (std::abs (p.y - y) > 6)
        return 0;
    if (std::abs (p.x - (inX + 2.0)) <= 6.0)
        return 1;
    if (std::abs (p.x - (outX - 2.0)) <= 6.0)
        return 2;
    return 0;
}

int PlaylistComponent::clipAt (int x, int y, bool& onRightEdge) const
{
    bool left = false;
    return clipAt (x, y, onRightEdge, left);
}

int PlaylistComponent::clipAt (int x, int y, bool& onRightEdge, bool& onLeftEdge) const
{
    const auto& playlist = engine.getSong().playlist;
    const auto track = yToTrack (y);
    onRightEdge = onLeftEdge = false;

    for (int i = (int) playlist.size() - 1; i >= 0; --i)
    {
        const auto& c = playlist[(std::size_t) i];

        if (c.track != track)
            continue;

        // the left edge only on clips wide enough to still be grabbed in the middle
        gesture::EdgeRules rules;
        rules.minimumForLeft = 24.0;

        if (const auto zone = gesture::zoneAt (x, tickToX ((double) c.start), tickToX ((double) c.end()), rules); zone != gesture::Zone::outside)
        {
            onRightEdge = zone == gesture::Zone::rightEdge;
            onLeftEdge = zone == gesture::Zone::leftEdge;
            return i;
        }
    }

    return -1;
}

void PlaylistComponent::seekTo (int x)
{
    const auto tick = juce::jmax (0.0, (double) snap (xToTick (x), true));

    if (engine.getSong().mode != PlayMode::song)
        engine.editSong ([] (Song& s) { s.mode = PlayMode::song; }, false);

    engine.seek (tick);
    playheadTick = tick;
    repaint();
}

bool PlaylistComponent::isSelected (int index) const
{
    return std::binary_search (selection.begin(), selection.end(), index);
}

void PlaylistComponent::setSelection (std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    selection = std::move (indices);
    knownClipCount = engine.getSong().playlist.size();
    repaint();
}

void PlaylistComponent::sortClips()
{
    auto tracked = selection;
    engine.editSong ([&tracked] (Song& s) { noteedit::sortByStartTracking (s.playlist, tracked); }, false);
    setSelection (tracked);
}

Clip PlaylistComponent::makeBrushClip (int track, std::int64_t start) const
{
    // what is chosen in the Picker: the current pattern, an audio clip or an automation clip
    const auto& song = engine.getSong();
    Clip c;
    c.pattern = song.currentPattern;
    c.track = track;
    c.start = juce::jmax<std::int64_t> (0, start);
    c.length = song.patterns[(std::size_t) song.currentPattern].effectiveLength (song.beatsPerBar);

    if (brush.kind == ClipKind::audio && brush.index >= 0 && brush.index < (int) song.channels.size()
        && song.channels[(std::size_t) brush.index].instrument == InstrumentKind::audioClip)
    {
        c.kind = ClipKind::audio;
        c.channel = brush.index;
        c.pattern = 0;

        // audio length in ticks at the current BPM
        if (auto data = engine.getSampleLibrary().load (juce::String::fromUTF8 (song.channels[(std::size_t) brush.index].samplePath.c_str())))
            c.length = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) ((double) data->frames() / data->sampleRate * song.bpm / 60.0 * ppq));
    }
    else if (brush.kind == ClipKind::automation && brush.index >= 0 && brush.index < (int) song.automations.size())
    {
        c.kind = ClipKind::automation;
        c.automation = brush.index;
        c.pattern = 0;
        c.length = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar) * 4; // 4 bars
    }

    return c;
}

void PlaylistComponent::setTimeSelection (std::int64_t from, std::int64_t to)
{
    engine.editSong ([from, to] (Song& s) {
        s.loopStart = from;
        s.loopEnd = to;
        s.mode = PlayMode::song;
    }, false);

    setSelection (to > from ? noteedit::clipsInTime (engine.getSong().playlist, from, to) : std::vector<int> {});
}

void PlaylistComponent::setTimeSelectionForSnapshot (int fromBar, int toBar)
{
    const auto bar = (std::int64_t) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    setTimeSelection (fromBar * bar, toBar * bar);
}

void PlaylistComponent::setViewForSnapshot (double pixelsPerBar, int firstBar, int firstTrack)
{
    const auto bar = (double) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    pixelsPerTick = pixelsPerBar / bar;
    scrollTicks = firstBar * bar;
    scrollY = juce::jmax (0, firstTrack) * trackHeight;
    updateScrollBars();
    repaint();
}

ui::Hint PlaylistComponent::hoverForSnapshot()
{
    const auto& playlist = engine.getSong().playlist;
    const auto grid = gridArea();

    // the last clip fully in view (the floating Channel Rack tends to cover the first ones)
    for (int i = (int) playlist.size() - 1; i >= 0; --i)
        if (const auto r = clipBounds (i); grid.contains (r) && r.getWidth() >= 40 && playlist[(std::size_t) i].kind == ClipKind::pattern)
        {
            const auto at = juce::Point<int> (r.getRight() - 2, r.getCentreY() + 4);
            updateHover (at, {});
            return hintAt (at);
        }

    return {};
}

void PlaylistComponent::mouseDown (const juce::MouseEvent& e)
{
    downMods = e.mods;
    startedOnEmpty = erasedSomething = holdExpired = false;
    hover = {};
    gliding = false;

    if (e.mods.isMiddleButtonDown() && e.x >= headerWidth) // middle-drag pans, in every tool
    {
        drag = Drag::pan;
        panStartTicks = scrollTicks;
        panStartY = scrollY;
        setMouseCursor (editorCursor (gesture::Cursor::pan));
        return;
    }

    // track header: the LED mutes (Ctrl+click or right-click on it: solo); right-click elsewhere: track menu
    if (e.x < headerWidth && e.y >= headerHeight && e.y < gridArea().getBottom())
    {
        const auto track = yToTrack (e.y);
        const auto onLed = e.x < 20;

        if (onLed && (e.mods.isPopupMenu() || e.mods.isCtrlDown()))
            engine.editSong ([track, count = numTracks()] (Song& s) { noteedit::soloTrack (s, track, count); });
        else if (e.mods.isPopupMenu())
            showTrackMenu (track);
        else if (onLed)
            engine.editSong ([track] (Song& s) {
                if ((int) s.playlistTracks.size() <= track)
                    s.playlistTracks.resize ((std::size_t) track + 1);
                s.playlistTracks[(std::size_t) track].muted = ! s.playlistTracks[(std::size_t) track].muted;
            });
        return;
    }

    // ruler: click = position; Ctrl+drag = time selection (Ctrl+click or double click clears it)
    if (e.y >= toolbarHeight && e.y < headerHeight && e.x >= headerWidth)
    {
        if (e.mods.isPopupMenu())
        {
            showRulerMenu (e.x);
            return;
        }

        if (e.getNumberOfClicks() >= 2)
        {
            setTimeSelection (0, 0);
            return;
        }

        loopAnchor = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), ! e.mods.isAltDown()));

        // Shift+drag inside the time selection moves it with what it holds (Ctrl+Shift: a copy)
        if (const auto& song = engine.getSong(); e.mods.isShiftDown() && song.hasLoop() && xToTick (e.x) >= (double) song.loopStart
                                                    && xToTick (e.x) < (double) song.loopEnd)
        {
            drag = Drag::timeMove;
            timeMoveOriginals = song.playlist;
            timeMoveFrom = song.loopStart;
            timeMoveTo = song.loopEnd;
            timeMoveAnchor = loopAnchor;
            timeMoveCopy = e.mods.isCtrlDown();
            return;
        }

        if (e.mods.isCtrlDown())
        {
            drag = Drag::loop;
            return;
        }

        drag = Drag::seek;
        seekTo (e.x);
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    bool rightEdge = false, leftEdge = false;
    const auto hit = clipAt (e.x, e.y, rightEdge, leftEdge);
    const auto tool = tools.getTool();

    // Ctrl+right-click on empty space: zoom to show everything; again: back to the previous view
    if (e.mods.isRightButtonDown() && e.mods.isCtrlDown() && hit < 0)
    {
        if (zoomBeforeAll > 0.0)
        {
            pixelsPerTick = zoomBeforeAll;
            scrollTicks = scrollBeforeAll;
            zoomBeforeAll = 0.0;
            updateScrollBars();
            repaint();
        }
        else
        {
            zoomBeforeAll = pixelsPerTick;
            scrollBeforeAll = scrollTicks;
            zoomAll();
        }
        return;
    }

    // the menu button in a clip's title strip
    if (hit >= 0 && e.mods.isLeftButtonDown() && ! e.mods.isCtrlDown() && clipMenuButton (hit).contains (e.getPosition()))
    {
        showClipMenu (hit);
        return;
    }

    if (e.mods.isRightButtonDown())
    {
        // on an automation point: delete that point
        if (const auto point = automationPointAt (hit, e.getPosition()); point >= 0)
        {
            const auto automation = engine.getSong().playlist[(std::size_t) hit].automation;
            engine.editSong ([automation, point] (Song& s) {
                auto& points = s.automations[(std::size_t) automation].points;
                if (points.size() > 1 && point < (int) points.size())
                    points.erase (points.begin() + point);
            });
            return;
        }

        // on the name strip: clip menu; on the body: delete (press and hold on empty space: Knife)
        if (hit >= 0 && e.y < trackToY (engine.getSong().playlist[(std::size_t) hit].track) + 15)
        {
            showClipMenu (hit);
            return;
        }

        // double right-click on a clip: the first click deleted it; bring it back muted (or unmuted)
        if (e.getNumberOfClicks() >= 2 && lastClipEraseTime != 0 && lastClipErasePos.getDistanceFrom (e.getPosition()) < 6
            && juce::Time::getMillisecondCounter() - lastClipEraseTime < 1000)
        {
            lastClipEraseTime = 0;
            engine.undo();
            bool r = false, l = false;
            if (const auto clip = clipAt (e.x, e.y, r, l); clip >= 0)
                engine.editSong ([clip] (Song& s) { s.playlist[(std::size_t) clip].muted = ! s.playlist[(std::size_t) clip].muted; });
            return;
        }

        drag = Drag::erase;
        startedOnEmpty = hit < 0;
        setSelection ({}); // indices change while deleting
        lastClipEraseTime = 0;

        if (hit >= 0)
        {
            erasedSomething = true;
            lastClipEraseTime = juce::Time::getMillisecondCounter();
            lastClipErasePos = e.getPosition();
            engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
        }
        return;
    }

    // every tool: Ctrl+click on a clip adds/removes it from the selection; Ctrl+drag selects (Shift adds)
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
        dragClip = hit; // a Lasso click without dragging selects this clip
        selectionBefore = e.mods.isShiftDown() ? selection : std::vector<int> {};
        rubberBand = {};
        setSelection (selectionBefore);
        return;
    }

    const auto snapOn = ! e.mods.isAltDown();

    auto grabClips = [&] (Drag kind) {
        if (! isSelected (hit))
            setSelection ({});

        const auto& playlist = engine.getSong().playlist;
        dragClip = hit;
        dragIndices = selection.empty() ? std::vector<int> { hit } : selection;
        dragOriginals.clear();

        for (auto i : dragIndices)
            dragOriginals.push_back (playlist[(std::size_t) i]);

        drag = kind;
        grabOffset = playlist[(std::size_t) hit].start - snap (xToTick (e.x), snapOn);
    };

    switch (tool)
    {
        case EditTool::eraser:
            drag = Drag::erase;
            setSelection ({});
            if (hit >= 0)
                engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
            return;

        case EditTool::mute:
            drag = Drag::muting;
            touchedClips.clear();
            muteTarget = true;
            if (hit >= 0)
            {
                muteTarget = ! engine.getSong().playlist[(std::size_t) hit].muted;
                touchedClips.push_back (hit);
                engine.editSong ([hit, m = muteTarget] (Song& s) { s.playlist[(std::size_t) hit].muted = m; });
            }
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
            seekTo (e.x);
            engine.play();
            return;

        case EditTool::slip:
            if (hit >= 0)
                grabClips (Drag::slip);
            return;

        case EditTool::pencil:
        case EditTool::brush:
        case EditTool::lasso:
            break;
    }

    // automation clip: drag a point, or click on the curve to add one (Pencil and Brush)
    if (hit >= 0 && ! e.mods.isShiftDown() && (tool == EditTool::pencil || tool == EditTool::brush))
    {
        auto point = automationPointAt (hit, e.getPosition());

        if (point < 0 && isOnAutomationCurve (hit, e.getPosition()))
        {
            const auto& c = engine.getSong().playlist[(std::size_t) hit];
            AutomationPoint p;
            p.tick = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) - c.start + c.offset);
            const auto body = automationBody (c);
            p.value = juce::jlimit (0.0f, 1.0f, (body.getBottom() - (float) e.y) / juce::jmax (1.0f, body.getHeight()));
            engine.editSong ([a = c.automation, p] (Song& s) {
                auto& points = s.automations[(std::size_t) a].points;
                points.push_back (p);
                std::stable_sort (points.begin(), points.end(), [] (auto& l, auto& r) { return l.tick < r.tick; });
            });
            point = automationPointAt (hit, e.getPosition());
        }

        if (point >= 0)
        {
            setSelection ({});
            drag = Drag::autoPoint;
            autoClip = hit;
            autoPoint = point;
            return;
        }
    }

    // audio clip fade handles (drawn at the top corners of the body)
    if (hit >= 0)
        if (const auto fade = fadeHandleAt (hit, e.getPosition()); fade != 0)
        {
            setSelection ({});
            dragClip = hit;
            dragIndices = { hit };
            drag = fade == 1 ? Drag::fadeIn : Drag::fadeOut;
            return;
        }

    // Alt+Shift+drag inside a clip: slip its content
    if (hit >= 0 && e.mods.isAltDown() && e.mods.isShiftDown() && ! rightEdge && ! leftEdge)
    {
        grabClips (Drag::slip);
        return;
    }

    // Shift+drag on a clip body drags a copy (on an edge, Shift trims like a plain edge drag)
    if (hit >= 0 && e.mods.isShiftDown() && ! rightEdge && ! leftEdge)
    {
        const auto sources = isSelected (hit) ? selection : std::vector<int> { hit };
        const auto grabbedAt = (std::size_t) (std::find (sources.begin(), sources.end(), hit) - sources.begin());
        std::vector<Clip> copies;
        std::vector<int> added;

        for (auto i : sources)
            copies.push_back (engine.getSong().playlist[(std::size_t) i]);

        engine.editSong ([&] (Song& s) {
            for (const auto& c : copies)
            {
                added.push_back ((int) s.playlist.size());
                s.playlist.push_back (c);
            }
        });
        setSelection (added);
        dragIndices = added;
        dragOriginals = copies;
        dragClip = added[grabbedAt];
        drag = Drag::move;
        grabOffset = copies[grabbedAt].start - snap (xToTick (e.x), snapOn);
        return;
    }

    if (hit >= 0)
    {
        grabClips (rightEdge ? Drag::resize : leftEdge ? Drag::trimLeft : Drag::move);

        if (drag == Drag::resize && ripple)
        {
            setSelection ({}); // a ripple resize works on the grabbed clip only
            dragIndices = { hit };
            dragOriginals = { engine.getSong().playlist[(std::size_t) hit] };
            rippleOriginals = engine.getSong().playlist;
        }

        const auto& c = engine.getSong().playlist[(std::size_t) hit];

        if (c.kind == ClipKind::pattern && c.pattern != engine.getSong().currentPattern)
            engine.editSong ([p = c.pattern] (Song& s) { s.currentPattern = p; }, false);
        return;
    }

    // paints what is chosen in the Picker (the current pattern, an audio clip or an automation clip)
    setSelection ({});
    const auto c = makeBrushClip (yToTrack (e.y), snap (xToTick (e.x), snapOn));

    if (c.kind == ClipKind::pattern)
        brush = {};

    engine.editSong ([c] (Song& s) {
        s.playlist.push_back (c);
        s.mode = PlayMode::song; // someone arranging wants to hear the song
    });
    dragClip = (int) engine.getSong().playlist.size() - 1;
    dragIndices = { dragClip };
    dragOriginals = { c };
    grabOffset = 0;

    if (tool == EditTool::brush)
    {
        drag = Drag::paint;
        paintClip = c;
        nextPaintTick = c.end();
        return;
    }

    drag = Drag::move;
}

void PlaylistComponent::mouseDrag (const juce::MouseEvent& e)
{
    const auto snapOn = ! e.mods.isAltDown();
    // after the step below, the hint bar says what the gesture is doing
    const struct PostAfter
    {
        PlaylistComponent& list;
        ~PostAfter() { list.postDragHint(); }
    } postAfter { *this };

    switch (drag)
    {
        case Drag::none:
            return;

        case Drag::pan:
            scrollTicks = juce::jmax (0.0, panStartTicks - e.getDistanceFromDragStartX() / pixelsPerTick);
            scrollY = juce::jmax (0, panStartY - e.getDistanceFromDragStartY());
            updateScrollBars();
            repaint();
            return;

        case Drag::autoPoint:
        {
            const auto& song = engine.getSong();
            if (autoClip < 0 || autoClip >= (int) song.playlist.size())
                return;
            const auto& c = song.playlist[(std::size_t) autoClip];
            if (c.automation < 0 || c.automation >= (int) song.automations.size())
                return;
            const auto& points = song.automations[(std::size_t) c.automation].points;
            if (autoPoint < 0 || autoPoint >= (int) points.size())
                return;

            // the point stays between its neighbours; Shift pressed after the click keeps the time, Ctrl the value
            auto tick = snap (xToTick (e.x), snapOn) - c.start + c.offset;
            const auto lo = autoPoint > 0 ? points[(std::size_t) autoPoint - 1].tick : 0;
            const auto hi = autoPoint + 1 < (int) points.size() ? points[(std::size_t) autoPoint + 1].tick : std::numeric_limits<std::int64_t>::max();
            tick = juce::jlimit (lo, hi, tick);
            const auto body = automationBody (c);
            auto value = juce::jlimit (0.0f, 1.0f, (body.getBottom() - (float) e.y) / juce::jmax (1.0f, body.getHeight()));

            if (e.mods.isShiftDown() && ! downMods.isShiftDown())
                tick = points[(std::size_t) autoPoint].tick;
            if (e.mods.isCtrlDown() && ! downMods.isCtrlDown())
                value = points[(std::size_t) autoPoint].value;

            engine.editSong ([a = c.automation, i = autoPoint, tick, value] (Song& s) {
                auto& p = s.automations[(std::size_t) a].points[(std::size_t) i];
                p.tick = tick;
                p.value = value;
            });
            return;
        }

        case Drag::timeMove:
        {
            const auto here = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
            const auto delta = juce::jmax (-timeMoveFrom, here - timeMoveAnchor);
            std::vector<int> moved;
            engine.editSong ([&] (Song& s) {
                s.playlist = timeMoveOriginals;
                moved = noteedit::moveTimeRange (s.playlist, timeMoveFrom, timeMoveTo, delta, timeMoveCopy);
                s.loopStart = timeMoveFrom + delta;
                s.loopEnd = timeMoveTo + delta;
            });
            setSelection (moved);
            return;
        }

        case Drag::seek:
        case Drag::listen:
            seekTo (e.x);
            return;

        case Drag::loop:
        {
            const auto here = juce::jmax<std::int64_t> (0, snap (xToTick (e.x) + (snapOn ? gridTicks() / 2.0 : 0.0), snapOn));
            setTimeSelection (juce::jmin (loopAnchor, here), juce::jmax (loopAnchor, here));
            return;
        }

        case Drag::fadeIn:
        case Drag::fadeOut:
        {
            const auto& clip = engine.getSong().playlist[(std::size_t) dragClip];
            const auto tick = snapOn ? snap (xToTick (e.x) + gridTicks() / 2.0, true) : (std::int64_t) xToTick (e.x);
            auto length = drag == Drag::fadeIn ? tick - clip.start : clip.end() - tick;
            length = juce::jlimit<std::int64_t> (0, clip.length, length);
            const auto in = drag == Drag::fadeIn;

            if ((in ? clip.fadeIn : clip.fadeOut) == std::min (length, clip.length - (in ? clip.fadeOut : clip.fadeIn)))
                return;

            engine.editSong ([i = dragClip, length, in] (Song& s) {
                auto& c = s.playlist[(std::size_t) i];
                (in ? c.fadeIn : c.fadeOut) = std::min (length, c.length - (in ? c.fadeOut : c.fadeIn));
            });
            return;
        }

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
            const auto tr0 = yToTrack (rubberBand.getY()), tr1 = yToTrack (rubberBand.getBottom());
            const auto& playlist = engine.getSong().playlist;
            auto s = selectionBefore;

            for (int i = 0; i < (int) playlist.size(); ++i)
            {
                const auto& c = playlist[(std::size_t) i];

                if ((double) c.end() > t0 && (double) c.start < t1 && c.track >= tr0 && c.track <= tr1)
                    s.push_back (i);
            }

            setSelection (s);
            return;
        }

        case Drag::erase:
        {
            // right press-and-hold on empty space, then drag: a temporary Knife
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
                    holdExpired = true;
            }

            bool edge = false;
            const auto hit = clipAt (e.x, e.y, edge);

            if (hit >= 0)
            {
                erasedSomething = true;
                engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
            }
            return;
        }

        case Drag::muting:
        {
            bool edge = false;
            const auto hit = clipAt (e.x, e.y, edge);

            if (hit >= 0 && std::find (touchedClips.begin(), touchedClips.end(), hit) == touchedClips.end())
            {
                touchedClips.push_back (hit);
                engine.editSong ([hit, m = muteTarget] (Song& s) { s.playlist[(std::size_t) hit].muted = m; });
            }
            return;
        }

        case Drag::knife:
            knifeTo = e.getPosition();
            repaint();
            return;

        case Drag::paint:
        {
            // copies of the painted clip, end to end, on the track under the mouse
            const auto tick = snap (xToTick (e.x), snapOn);
            const auto length = juce::jmax<std::int64_t> (1, paintClip.length);

            for (int guard = 0; tick >= nextPaintTick && guard < 64; ++guard)
            {
                auto c = paintClip;
                c.start = nextPaintTick;
                c.track = yToTrack (e.y);
                engine.editSong ([c] (Song& s) { s.playlist.push_back (c); });
                nextPaintTick += length;
            }
            return;
        }

        case Drag::slip:
        {
            // the content moves by the (snapped) mouse movement; the clips stay put
            const auto delta = snap (xToTick (e.x), snapOn) - snap (xToTick (e.getMouseDownX()), snapOn);
            const auto grabbedAt = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragClip) - dragIndices.begin());
            auto probe = dragOriginals[grabbedAt];
            noteedit::slip (probe, delta);

            if (engine.getSong().playlist[(std::size_t) dragClip].offset == probe.offset)
                return;

            engine.editSong ([this, delta] (Song& s) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto c = dragOriginals[j];
                    noteedit::slip (c, delta);
                    s.playlist[(std::size_t) dragIndices[j]].offset = c.offset;
                }
            });
            return;
        }

        case Drag::move:
        {
            const auto grabbedAt = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragClip) - dragIndices.begin());
            const auto& grabbed = dragOriginals[grabbedAt];
            auto deltaTicks = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) + grabOffset) - grabbed.start;
            auto deltaTracks = yToTrack (e.y) - grabbed.track;

            // the group keeps its shape: clamp by the leftmost / topmost clip
            for (const auto& c : dragOriginals)
            {
                deltaTicks = juce::jmax (deltaTicks, -c.start);
                deltaTracks = juce::jlimit (-c.track, 499 - c.track, deltaTracks);
            }

            const auto& current = engine.getSong().playlist[(std::size_t) dragClip];

            if (current.start == grabbed.start + deltaTicks && current.track == grabbed.track + deltaTracks)
                return;

            engine.editSong ([this, deltaTicks, deltaTracks] (Song& s) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto& c = s.playlist[(std::size_t) dragIndices[j]];
                    c.start = dragOriginals[j].start + deltaTicks;
                    c.track = dragOriginals[j].track + deltaTracks;
                }
            });
            return;
        }

        case Drag::resize:
        {
            const auto grabbedAt = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragClip) - dragIndices.begin());
            const auto& grabbed = dragOriginals[grabbedAt];
            const auto minimum = snapOn ? juce::jmax<std::int64_t> (10, gridTicks()) : (std::int64_t) 10;
            const auto end = snapOn ? snap (xToTick (e.x) + gridTicks() / 2.0, true) : (std::int64_t) xToTick (e.x);
            const auto delta = juce::jmax (minimum, end - grabbed.start) - grabbed.length;

            if (engine.getSong().playlist[(std::size_t) dragClip].length == grabbed.length + delta)
                return;

            if (ripple && ! rippleOriginals.empty()) // the later clips on the track follow the end
            {
                engine.editSong ([this, length = juce::jmax (minimum, grabbed.length + delta)] (Song& s) {
                    s.playlist = rippleOriginals;
                    noteedit::rippleResize (s.playlist, dragClip, length);
                });
                return;
            }

            engine.editSong ([this, delta, minimum] (Song& s) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto& c = s.playlist[(std::size_t) dragIndices[j]];
                    c.length = juce::jmax (minimum, dragOriginals[j].length + delta);
                    c.fadeIn = juce::jmin (c.fadeIn, c.length);
                    c.fadeOut = juce::jmin (c.fadeOut, c.length - c.fadeIn);
                }
            });
            return;
        }

        case Drag::trimLeft:
        {
            // the start moves, the content stays where it is (the offset follows)
            const auto grabbedAt = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragClip) - dragIndices.begin());
            const auto& grabbed = dragOriginals[grabbedAt];
            const auto minimum = snapOn ? juce::jmax<std::int64_t> (10, gridTicks()) : (std::int64_t) 10;
            const auto start = snapOn ? snap (xToTick (e.x) + gridTicks() / 2.0, true) : (std::int64_t) xToTick (e.x);
            const auto delta = start - grabbed.start;
            auto probe = grabbed;
            noteedit::trimStart (probe, grabbed.start + delta, minimum);

            if (engine.getSong().playlist[(std::size_t) dragClip].start == probe.start)
                return;

            engine.editSong ([this, delta, minimum] (Song& s) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto c = dragOriginals[j];
                    noteedit::trimStart (c, c.start + delta, minimum);
                    s.playlist[(std::size_t) dragIndices[j]] = c;
                }
            });
            return;
        }
    }
}

void PlaylistComponent::postDragHint()
{
    const auto& song = engine.getSong();
    const auto count = juce::String ((int) juce::jmax<std::size_t> (1, dragIndices.size()));
    auto position = [&song] (std::int64_t tick) {
        const auto bar = song.barAt (juce::jmax<std::int64_t> (0, tick));
        const auto beat = (tick - bar.tick) / ppq + 1;
        return juce::String (bar.number) + ":" + juce::String (beat);
    };
    const auto clip = dragClip >= 0 && dragClip < (int) song.playlist.size() ? &song.playlist[(std::size_t) dragClip] : nullptr;
    ui::Hint hint;

    switch (drag)
    {
        case Drag::move:
            if (clip == nullptr)
                return;
            hint.text = tr ("Moving {0} clip(s)  [Alt] no snap", { count });
            hint.value = position (clip->start) + "  " + tr ("Track {0}", { juce::String (clip->track + 1) });
            break;
        case Drag::resize:
        case Drag::trimLeft:
            if (clip == nullptr)
                return;
            hint.text = tr ("Resizing {0} clip(s)  [Alt] no snap", { count });
            hint.value = position (drag == Drag::resize ? clip->end() : clip->start);
            break;
        case Drag::slip: hint.text = tr ("Sliding the content inside the clip  [Alt] no snap"); break;
        case Drag::fadeIn:
        case Drag::fadeOut: hint.text = tr ("Setting the fade  [Alt] no snap"); break;
        case Drag::autoPoint: hint.text = tr ("Moving the point: then [Shift] keeps time  [Ctrl] keeps value"); break;
        case Drag::select:
            hint.text = tr ("Selecting  [Shift] adds to the selection");
            hint.value = tr ("{0} selected", { juce::String ((int) selection.size()) });
            break;
        case Drag::loop:
            hint.text = tr ("Time selection");
            hint.value = position (song.loopStart) + " - " + position (song.loopEnd);
            break;
        case Drag::erase: hint.text = tr ("Deleting what the mouse crosses"); break;
        case Drag::muting: hint.text = tr ("Muting / unmuting what the mouse crosses"); break;
        case Drag::knife: hint.text = tr ("Cutting along the line  [Alt] no snap"); break;
        case Drag::paint: hint.text = tr ("Painting clips  [Alt] no snap"); break;
        case Drag::timeMove: hint.text = tr ("Moving the time selection with its clips"); break;
        case Drag::seek:
        case Drag::listen:
        case Drag::pan:
        case Drag::zoomRange:
        case Drag::none: return;
    }

    ui::postHint (hint);
}

void PlaylistComponent::mouseUp (const juce::MouseEvent& e)
{
    ui::clearPostedHint();
    if (drag == Drag::knife)
        cutWithKnife (knifeFrom, knifeTo, ! e.mods.isAltDown());

    if (drag == Drag::zoomRange)
    {
        if (rubberBand.getWidth() > 4)
            zoomToTicks (xToTick (rubberBand.getX()), xToTick (rubberBand.getRight()));
        else
            zoomOut (e.x);
    }

    if (drag == Drag::select && rubberBand.isEmpty() && dragClip >= 0)
    {
        auto s = selectionBefore;
        if (auto it = std::find (s.begin(), s.end(), dragClip); it != s.end())
            s.erase (it);
        else
            s.push_back (dragClip);
        setSelection (s);
    }

    // Ctrl+click on the ruler without dragging clears the time selection
    if (drag == Drag::loop && e.getDistanceFromDragStart() < 3)
        setTimeSelection (0, 0);

    if (drag == Drag::listen)
        engine.stop();

    if (drag == Drag::move || drag == Drag::resize || drag == Drag::trimLeft || drag == Drag::paint || drag == Drag::timeMove)
        sortClips();

    rippleOriginals.clear();
    timeMoveOriginals.clear();
    autoClip = autoPoint = -1;

    drag = Drag::none;
    dragClip = -1;
    dragIndices.clear();
    dragOriginals.clear();
    touchedClips.clear();
    rubberBand = {};
    knownClipCount = engine.getSong().playlist.size();
    hover = {};
    updateHover (e.getPosition(), e.mods); // the cursor of what is under the mouse now
    repaint();
}

void PlaylistComponent::cutWithKnife (juce::Point<int> from, juce::Point<int> to, bool snapOn)
{
    const auto& playlist = engine.getSong().playlist;
    std::vector<std::pair<int, std::int64_t>> cuts;
    const auto round = snapOn ? gridTicks() / 2.0 : 0.0;

    if (from.getDistanceFrom (to) < 3)
    {
        bool edge = false;
        if (const auto hit = clipAt (from.x, from.y, edge); hit >= 0)
            cuts.push_back ({ hit, snap (xToTick (from.x) + round, snapOn) });
    }
    else
    {
        // every track the line crosses is cut where the line passes its middle
        const auto top = juce::jmin (from.y, to.y), bottom = juce::jmax (from.y, to.y);

        for (int i = 0; i < (int) playlist.size(); ++i)
        {
            const auto& c = playlist[(std::size_t) i];
            const auto rowY = trackToY (c.track) + trackHeight / 2;

            if (rowY < top - trackHeight / 2 || rowY > bottom + trackHeight / 2)
                continue;

            const auto y = juce::jlimit (top, bottom, rowY);
            const auto x = from.y == to.y ? (double) from.x : from.x + (double) (y - from.y) * (to.x - from.x) / (double) (to.y - from.y);
            const auto tick = snap (xToTick ((int) std::round (x)) + round, snapOn);

            if (tick > c.start && tick < c.end())
                cuts.push_back ({ i, tick });
        }
    }

    if (cuts.empty())
        return;

    setSelection ({});
    engine.editSong ([&cuts] (Song& s) {
        for (auto [index, tick] : cuts)
        {
            auto rest = splitClip (s.playlist[(std::size_t) index], tick);
            s.playlist.push_back (rest);
        }
    });
    sortClips();
}

void PlaylistComponent::zoomToTicks (double from, double to)
{
    if (to - from < 1.0)
        return;

    pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), gridArea().getWidth() / (to - from));
    scrollTicks = juce::jmax (0.0, from);
    updateScrollBars();
    repaint();
}

void PlaylistComponent::zoomOut (int anchorX)
{
    const auto anchor = xToTick (anchorX);
    pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick / 2.0);
    scrollTicks = juce::jmax (0.0, anchor - (anchorX - headerWidth) / pixelsPerTick);
    updateScrollBars();
    repaint();
}

void PlaylistComponent::insertTimeSelection()
{
    const auto& song = engine.getSong();

    if (! song.hasLoop())
        return;

    setSelection ({});
    engine.editSong ([from = song.loopStart, to = song.loopEnd] (Song& s) { noteedit::insertTime (s.playlist, s.markers(), from, to - from); });
    sortClips();
}

void PlaylistComponent::deleteTimeSelection()
{
    const auto& song = engine.getSong();

    if (! song.hasLoop())
        return;

    setSelection ({});
    engine.editSong ([from = song.loopStart, to = song.loopEnd] (Song& s) { noteedit::deleteTime (s.playlist, s.markers(), from, to); });
    sortClips();
}

void PlaylistComponent::showTrackMenu (int track)
{
    const auto& song = engine.getSong();
    const auto muted = song.isTrackMuted (track);
    const auto count = numTracks();
    juce::PopupMenu menu;
    menu.addSectionHeader (track < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) track].name.empty()
                               ? juce::String::fromUTF8 (song.playlistTracks[(std::size_t) track].name.c_str())
                               : tr ("Track {0}", { juce::String (track + 1) }));
    menu.addItem (tr ("Rename..."), [this, track] { renameTrack (track); });
    menu.addItem (muted ? tr ("Unmute") : tr ("Mute"), [this, track] {
        engine.editSong ([track] (Song& s) {
            if ((int) s.playlistTracks.size() <= track)
                s.playlistTracks.resize ((std::size_t) track + 1);
            s.playlistTracks[(std::size_t) track].muted = ! s.playlistTracks[(std::size_t) track].muted;
        });
    });
    menu.addItem (tr ("Solo (mute the other tracks)"), [this, track, count] { engine.editSong ([track, count] (Song& s) { noteedit::soloTrack (s, track, count); }); });
    menu.addSeparator();
    menu.addItem (tr ("Insert a track above"), [this, track] {
        setSelection ({});
        engine.editSong ([track] (Song& s) { noteedit::insertTrack (s, track); });
    });
    menu.addItem (tr ("Insert a track below"), [this, track] {
        setSelection ({});
        engine.editSong ([track] (Song& s) { noteedit::insertTrack (s, track + 1); });
    });
    menu.addItem (tr ("Move up"), track > 0, false, [this, track] { engine.editSong ([track] (Song& s) { noteedit::swapTracks (s, track, track - 1); }); });
    menu.addItem (tr ("Move down"), [this, track] { engine.editSong ([track] (Song& s) { noteedit::swapTracks (s, track, track + 1); }); });
    menu.addSeparator();
    menu.addItem (tr ("Delete the track and its clips"), [this, track] {
        setSelection ({});
        engine.editSong ([track] (Song& s) { noteedit::removeTrack (s, track); });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PlaylistComponent::renameTrack (int track)
{
    const auto& tracks = engine.getSong().playlistTracks;
    const auto current = track < (int) tracks.size() ? juce::String::fromUTF8 (tracks[(std::size_t) track].name.c_str()) : juce::String();
    auto* window = new juce::AlertWindow (tr ("Rename track"), {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", current.isNotEmpty() ? current : tr ("Track {0}", { juce::String (track + 1) }));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, track, window] (int result) {
                                 if (result != 1)
                                     return;

                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();
                                 engine.editSong ([track, name] (Song& s) {
                                     if ((int) s.playlistTracks.size() <= track)
                                         s.playlistTracks.resize ((std::size_t) track + 1);
                                     s.playlistTracks[(std::size_t) track].name = name;
                                 });
                             }),
                             true);
}

void PlaylistComponent::showRulerMenu (int x)
{
    const auto& song = engine.getSong();
    const auto tick = juce::jmax<std::int64_t> (0, snap (xToTick (x), true));

    // marker near the click (up to 6 px)
    int near = -1;
    for (int i = 0; i < (int) song.markers().size(); ++i)
        if (std::abs (tickToX ((double) song.markers()[(std::size_t) i].tick) - x) <= 6.0)
            near = i;

    juce::PopupMenu menu;
    menu.addItem (tr ("Add marker here..."), [this, tick] { editMarkerName (tick, -1); });

    juce::PopupMenu signatures;
    for (auto [n, d] : { std::pair { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 4 }, { 7, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } })
        signatures.addItem (juce::String (n) + "/" + juce::String (d), [this, tick, n = n, d = d] {
            engine.editSong ([tick, n, d] (Song& s) { s.addMarker ({ tick, {}, n, d }); });
        });
    menu.addSubMenu (tr ("Time signature change here"), signatures);

    if (near >= 0)
    {
        menu.addSeparator();
        menu.addItem (tr ("Rename marker..."), [this, near] { editMarkerName (0, near); });
        menu.addItem (tr ("Remove marker"), [this, near] {
            engine.editSong ([near] (Song& s) {
                if (near < (int) s.markers().size())
                    s.markers().erase (s.markers().begin() + near);
            });
        });
    }

    if (song.hasLoop())
    {
        menu.addSeparator();
        menu.addItem (tr ("Remove loop"), [this] { engine.editSong ([] (Song& s) { s.loopStart = s.loopEnd = 0; }, false); });
    }

    menu.addSeparator();
    menu.addItem (tr ("Ripple editing (resizing and deleting move the later clips)") + "\tShift+S", true, ripple, [this] { setRipple (! ripple); });
    {
        juce::PopupMenu zoom;
        zoom.addItem (tr ("Zoom in") + "\tPageUp", [this] { runAction ("playlist.zoomIn"); });
        zoom.addItem (tr ("Zoom out") + "\tPageDown", [this] { runAction ("playlist.zoomOut"); });
        zoom.addItem (tr ("Zoom: one bar") + "\tShift+1", [this] { runAction ("playlist.zoom1"); });
        zoom.addItem (tr ("Zoom: four bars") + "\tShift+2", [this] { runAction ("playlist.zoom2"); });
        zoom.addItem (tr ("Zoom: sixteen bars") + "\tShift+3", [this] { runAction ("playlist.zoom3"); });
        zoom.addItem (tr ("Zoom to show everything") + "\tShift+4", [this] { runAction ("playlist.zoomAll"); });
        zoom.addItem (tr ("Zoom to the selection") + "\tShift+5", [this] { runAction ("playlist.zoomSelection"); });
        zoom.addItem (tr ("Centre the view on the playhead") + "\tShift+0", [this] { runAction ("playlist.centrePlayhead"); });
        menu.addSubMenu (tr ("Zoom"), zoom);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PlaylistComponent::editMarkerName (std::int64_t tick, int existing)
{
    const auto& markers = engine.getSong().markers();
    const auto current = existing >= 0 && existing < (int) markers.size() ? juce::String::fromUTF8 (markers[(std::size_t) existing].name.c_str())
                                                                          : tr ("Marker");
    auto* window = new juce::AlertWindow (existing >= 0 ? tr ("Rename marker") : tr ("New marker"), {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", current);
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, tick, existing, window] (int result) {
                                 if (result != 1)
                                     return;

                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();
                                 engine.editSong ([tick, existing, name] (Song& s) {
                                     if (existing < 0)
                                         s.addMarker ({ tick, name, 0, 4 });
                                     else if (existing < (int) s.markers().size())
                                         s.markers()[(std::size_t) existing].name = name;
                                 });
                             }),
                             true);
}

void PlaylistComponent::showClipMenu (int index)
{
    const auto& clip = engine.getSong().playlist[(std::size_t) index];
    const auto targets = isSelected (index) ? selection : std::vector<int> { index };
    juce::PopupMenu menu;

    // split at the clicked point (on a 1-beat grid)
    const auto splitAt = snap (xToTick (getMouseXYRelative().x) + ppq / 2.0, true);

    if (splitAt > clip.start && splitAt < clip.end())
        menu.addItem (tr ("Split here"), [this, index, splitAt] {
            setSelection ({});
            engine.editSong ([index, splitAt] (Song& s) {
                auto rest = splitClip (s.playlist[(std::size_t) index], splitAt);
                s.playlist.push_back (rest);
            });
            sortClips();
        });

    menu.addItem (clip.muted ? tr ("Unmute") : tr ("Mute"), [this, targets, m = ! clip.muted] {
        engine.editSong ([targets, m] (Song& s) {
            for (auto i : targets)
                if (i < (int) s.playlist.size())
                    s.playlist[(std::size_t) i].muted = m;
        });
    });
    menu.addItem (tr ("Chop to grid"), [this, targets, grid = gridTicks()] {
        setSelection ({});
        engine.editSong ([targets, grid] (Song& s) {
            for (auto i : targets)
                noteedit::chopClip (s.playlist, i, grid);
        });
        sortClips();
    });

    if (clip.kind == ClipKind::pattern)
    {
        menu.addSectionHeader (juce::String::fromUTF8 (engine.getSong().patterns[(std::size_t) clip.pattern].name.c_str()));
        // like FL's "Make unique": the chosen clips switch to using a copy of the pattern
        menu.addItem (tr ("Make unique"), [this, targets] {
            engine.editSong ([targets] (Song& s) {
                std::map<int, int> copies; // original pattern -> copy

                for (auto i : targets)
                {
                    auto& c = s.playlist[(std::size_t) i];

                    if (c.kind != ClipKind::pattern)
                        continue;

                    if (! copies.count (c.pattern))
                    {
                        auto copy = s.patterns[(std::size_t) c.pattern];
                        copy.name += " #2";
                        s.patterns.push_back (std::move (copy));
                        copies[c.pattern] = (int) s.patterns.size() - 1;
                    }

                    c.pattern = copies[c.pattern];
                }

                s.currentPattern = copies.empty() ? s.currentPattern : copies.begin()->second;
            });
        });
        menu.addItem (tr ("Select all of this pattern"), [this, p = clip.pattern] {
            std::vector<int> same;
            const auto& playlist = engine.getSong().playlist;

            for (int i = 0; i < (int) playlist.size(); ++i)
                if (playlist[(std::size_t) i].kind == ClipKind::pattern && playlist[(std::size_t) i].pattern == p)
                    same.push_back (i);

            setSelection (same);
        });
        menu.addItem (tr ("Open in Channel Rack"), [this] {
            if (onOpenPattern)
                onOpenPattern();
        });
        menu.addItem (tr ("Open in Piano Roll"), [this] {
            if (onOpenPianoRoll)
                onOpenPianoRoll();
        });
    }

    if (clip.kind == ClipKind::audio)
    {
        juce::PopupMenu gains;

        for (auto db : { 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f })
        {
            const auto g = juce::Decibels::decibelsToGain (db);
            gains.addItem ((db > 0 ? "+" : "") + juce::String (db, 0) + " dB", true, std::abs (clip.gain - g) < 0.01f, [this, targets, g] {
                engine.editSong ([targets, g] (Song& s) {
                    for (auto i : targets)
                        if (s.playlist[(std::size_t) i].kind == ClipKind::audio)
                            s.playlist[(std::size_t) i].gain = g;
                });
            });
        }

        menu.addSubMenu (tr ("Clip volume"), gains);

        menu.addItem (tr ("Normalize"), [this, targets] {
            // clip gain so that the peak of the played segment reaches 0 dB (at most +24 dB)
            const auto& song = engine.getSong();
            const auto secondsPerTick = 60.0 / (juce::jmax (1.0, song.bpm) * ppq);
            std::vector<std::pair<int, float>> gainsFor;

            for (auto i : targets)
            {
                const auto& c = song.playlist[(std::size_t) i];

                if (c.kind != ClipKind::audio || c.channel < 0 || c.channel >= (int) song.channels.size())
                    continue;

                const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (song.channels[(std::size_t) c.channel].samplePath.c_str()));

                if (sample == nullptr || sample->frames() == 0)
                    continue;

                const auto from = (std::size_t) juce::jlimit (0.0, (double) sample->frames(), (double) c.offset * secondsPerTick * sample->sampleRate);
                const auto to = (std::size_t) juce::jlimit ((double) from, (double) sample->frames(),
                                                            (double) (c.offset + c.length) * secondsPerTick * sample->sampleRate);
                float peak = 0.0f;

                for (auto* data : { sample->leftData(), sample->rightData() })
                    if (data != nullptr)
                        for (auto f = from; f < to; ++f)
                            peak = juce::jmax (peak, std::abs (data[f]));

                if (peak > 1.0e-6f)
                    gainsFor.push_back ({ i, juce::jmin (1.0f / peak, juce::Decibels::decibelsToGain (24.0f)) });
            }

            if (! gainsFor.empty())
                engine.editSong ([gainsFor] (Song& s) {
                    for (auto [i, g] : gainsFor)
                        if (i < (int) s.playlist.size())
                            s.playlist[(std::size_t) i].gain = g;
                });
        });

        if (clip.fadeIn > 0 || clip.fadeOut > 0)
            menu.addItem (tr ("Remove fades"), [this, targets] {
                engine.editSong ([targets] (Song& s) {
                    for (auto i : targets)
                        s.playlist[(std::size_t) i].fadeIn = s.playlist[(std::size_t) i].fadeOut = 0;
                });
            });
    }

    if (clip.kind == ClipKind::automation && clip.automation >= 0 && clip.automation < (int) engine.getSong().automations.size())
    {
        menu.addSectionHeader (juce::String::fromUTF8 (engine.getSong().automations[(std::size_t) clip.automation].name.c_str()));
        menu.addItem (tr ("Edit points..."), [this, a = clip.automation] { openAutomationEditor (engine, a); });
        menu.addItem (tr ("Make unique"), [this, index] {
            engine.editSong ([index] (Song& s) {
                auto& c = s.playlist[(std::size_t) index];
                auto copy = s.automations[(std::size_t) c.automation];
                copy.name += " #2";
                s.automations.push_back (std::move (copy));
                c.automation = (int) s.automations.size() - 1;
            });
        });
        menu.addItem (tr ("Delete automation (all of its clips)"), [this, a = clip.automation] {
            setSelection ({});
            engine.editSong ([a] (Song& s) { s.removeAutomation (a); });
        });
    }

    menu.addItem (targets.size() > 1 ? tr ("Delete selected") : tr ("Delete"), [this, targets] {
        setSelection ({});
        engine.editSong ([targets] (Song& s) {
            auto sorted = targets;
            std::sort (sorted.rbegin(), sorted.rend());

            for (auto i : sorted)
                s.playlist.erase (s.playlist.begin() + i);
        });
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void PlaylistComponent::setRipple (bool on)
{
    ripple = on;
    repaint();
}

void PlaylistComponent::rippleDeleteSelection()
{
    if (selection.empty())
        return;

    auto doomed = selection;
    setSelection ({});
    engine.editSong ([&doomed] (Song& s) { noteedit::rippleDelete (s.playlist, doomed); });
}

void PlaylistComponent::zoomAroundCentre (double factor)
{
    const auto grid = gridArea();
    const auto centre = xToTick (grid.getCentreX());
    pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick * factor);
    scrollTicks = juce::jmax (0.0, centre - (grid.getCentreX() - headerWidth) / pixelsPerTick);
    updateScrollBars();
    repaint();
}

void PlaylistComponent::zoomToBars (int bars)
{
    const auto bar = (double) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    const auto from = std::floor (scrollTicks / bar) * bar;
    zoomToTicks (from, from + bars * bar);
}

void PlaylistComponent::zoomAll()
{
    zoomToTicks (0.0, (double) engine.getSong().songLength());
}

void PlaylistComponent::zoomToSelection()
{
    const auto& song = engine.getSong();

    if (song.hasLoop() && selection.empty())
        return zoomToTicks ((double) song.loopStart, (double) song.loopEnd);

    if (selection.empty())
        return zoomAll();

    std::int64_t from = INT64_MAX, to = 0;
    int top = 1 << 20;
    for (auto i : selection)
        if (i >= 0 && i < (int) song.playlist.size())
        {
            from = juce::jmin (from, song.playlist[(std::size_t) i].start);
            to = juce::jmax (to, song.playlist[(std::size_t) i].end());
            top = juce::jmin (top, song.playlist[(std::size_t) i].track);
        }

    zoomToTicks ((double) from, (double) to);
    scrollY = juce::jmax (0, top * trackHeight);
    updateScrollBars();
    repaint();
}

void PlaylistComponent::centreOnPlayhead()
{
    const auto& song = engine.getSong();
    const auto tick = std::fmod (engine.getTransport().positionTicks.load(), (double) juce::jmax<std::int64_t> (1, song.songLength()));
    scrollTicks = juce::jmax (0.0, tick - gridArea().getWidth() / pixelsPerTick / 2.0);
    updateScrollBars();
    repaint();
}

bool PlaylistComponent::runAction (const std::string& id)
{
    auto is = [&id] (const char* name) { return id == name; };

    if (is ("playlist.zoomIn")) zoomAroundCentre (1.25);
    else if (is ("playlist.zoomOut")) zoomAroundCentre (1.0 / 1.25);
    else if (is ("playlist.zoom1")) zoomToBars (1);
    else if (is ("playlist.zoom2")) zoomToBars (4);
    else if (is ("playlist.zoom3")) zoomToBars (16);
    else if (is ("playlist.zoomAll")) zoomAll();
    else if (is ("playlist.zoomSelection")) zoomToSelection();
    else if (is ("playlist.centrePlayhead")) centreOnPlayhead();
    else if (is ("playlist.rippleDelete")) rippleDeleteSelection();
    else if (is ("playlist.ripple")) setRipple (! ripple);
    else
        return false;

    return true;
}

void PlaylistComponent::deleteSelection()
{
    if (selection.empty())
        return;

    auto doomed = selection;
    setSelection ({});
    engine.editSong ([&doomed] (Song& s) {
        std::sort (doomed.rbegin(), doomed.rend());

        for (auto i : doomed)
            if (i >= 0 && i < (int) s.playlist.size())
                s.playlist.erase (s.playlist.begin() + i);
    });
}

void PlaylistComponent::copySelection()
{
    clipboard.clear();

    for (auto i : selection)
        clipboard.push_back (engine.getSong().playlist[(std::size_t) i]);
}

void PlaylistComponent::pasteClips()
{
    if (clipboard.empty())
        return;

    // pastes at the playback cursor (or where the first clip was), keeping tracks and spacing
    std::int64_t first = clipboard.front().start;
    for (const auto& c : clipboard)
        first = juce::jmin (first, c.start);

    const auto target = playheadTick >= 0.0 ? snap (playheadTick, true) : first;
    std::vector<int> added;

    engine.editSong ([&added, target, first] (Song& s) {
        for (auto c : clipboard)
        {
            if (c.kind == ClipKind::pattern && c.pattern >= (int) s.patterns.size())
                continue;
            if (c.kind == ClipKind::audio && c.channel >= (int) s.channels.size())
                continue;
            if (c.kind == ClipKind::automation && c.automation >= (int) s.automations.size())
                continue;

            c.start = c.start - first + target;
            added.push_back ((int) s.playlist.size());
            s.playlist.push_back (c);
        }
    });
    setSelection (added);
    sortClips();
}

void PlaylistComponent::duplicateSelection()
{
    const auto& song = engine.getSong();

    // with a time selection: repeats it right after itself (the selected clips that start inside it, or
    // all the clips that do), and the time selection moves on so the next Ctrl+B continues
    if (song.hasLoop())
    {
        const auto from = song.loopStart, to = song.loopEnd;
        std::vector<int> sources;

        for (auto i : selection)
            if (song.playlist[(std::size_t) i].start >= from && song.playlist[(std::size_t) i].start < to)
                sources.push_back (i);

        std::vector<int> added;
        engine.editSong ([&] (Song& s) {
            added = noteedit::repeatTime (s.playlist, from, to, sources);
            s.loopStart = to;
            s.loopEnd = to + (to - from);
        });
        setSelection (added);
        sortClips();
        return;
    }

    if (selection.empty())
        return;

    const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);
    std::int64_t from = INT64_MAX, to = 0;

    for (auto i : selection)
    {
        from = juce::jmin (from, song.playlist[(std::size_t) i].start);
        to = juce::jmax (to, song.playlist[(std::size_t) i].end());
    }

    const auto offset = juce::jmax (bar, (to - from + bar - 1) / bar * bar); // like FL's Ctrl+B
    const auto source = selection;
    std::vector<int> added;

    engine.editSong ([&added, &source, offset] (Song& s) {
        for (auto i : source)
        {
            auto c = s.playlist[(std::size_t) i];
            c.start += offset;
            added.push_back ((int) s.playlist.size());
            s.playlist.push_back (c);
        }
    });
    setSelection (added);
    sortClips();
}

bool PlaylistComponent::handleKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto cmd = mods.isCommandDown();
    const auto code = key.getKeyCode();
    const auto letter = (int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code);

    if (code == juce::KeyPress::backspaceKey)
    {
        EditorGlobals::get().toggleGrid();
        return true;
    }

    // Ctrl+Ins / Ctrl+Del: insert / remove the time selection's length across all tracks
    if (cmd && code == juce::KeyPress::insertKey)
    {
        insertTimeSelection();
        return true;
    }

    if (cmd && code == juce::KeyPress::deleteKey)
    {
        deleteTimeSelection();
        return true;
    }

    if (code == juce::KeyPress::deleteKey && ! mods.isShiftDown())
    {
        if (ripple)
            rippleDeleteSelection();
        else
            deleteSelection();
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        setSelection ({});
        return true;
    }

    if (cmd && letter == 'A')
    {
        std::vector<int> all ((std::size_t) engine.getSong().playlist.size());
        for (std::size_t i = 0; i < all.size(); ++i)
            all[i] = (int) i;
        setSelection (all);
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
        pasteClips();
        return true;
    }

    if (cmd && letter == 'B')
    {
        duplicateSelection();
        return true;
    }

    // single letters: only while the typing piano is off (otherwise they play notes)
    if (! cmd && ! mods.isAltDown() && ! mods.isShiftDown() && ! EditorGlobals::get().typingPiano)
        for (const auto& t : editTools())
            if (letter == t.key)
            {
                tools.setTool (t.tool);
                return true;
            }

    return false;
}

void PlaylistComponent::mouseMove (const juce::MouseEvent& e)
{
    updateHover (e.getPosition(), e.mods);
}

void PlaylistComponent::mouseExit (const juce::MouseEvent&)
{
    lastMouse = { -1, -1 };

    if (! (hover == Hover {}))
    {
        hover = {};
        repaint();
    }
}

void PlaylistComponent::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    if (drag == Drag::none && lastMouse.x >= 0)
        updateCursor (mods);
}

juce::Rectangle<int> PlaylistComponent::clipBounds (int index) const
{
    const auto& c = engine.getSong().playlist[(std::size_t) index];
    const auto x0 = (float) tickToX ((double) c.start), x1 = (float) tickToX ((double) c.end());
    return juce::Rectangle<float> (x0, (float) trackToY (c.track) + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), (float) trackHeight - 3.0f)
        .getSmallestIntegerContainer();
}

juce::Rectangle<int> PlaylistComponent::clipMenuButton (int index) const
{
    const auto r = clipBounds (index);

    if (r.getWidth() < 40)
        return {};

    return { r.getX() + 2, r.getY() + 1, 13, (int) clipTitleHeight - 2 };
}

PlaylistComponent::Hover PlaylistComponent::hoverAt (juce::Point<int> p) const
{
    Hover h;

    if (p.x < headerWidth && p.y >= headerHeight && p.y < gridArea().getBottom())
    {
        h.header = true;
        h.track = yToTrack (p.y);
        h.led = p.x < 20;
        return h;
    }

    if (! gridArea().contains (p))
        return h;

    h.track = yToTrack (p.y);
    bool rightEdge = false, leftEdge = false;
    h.clip = clipAt (p.x, p.y, rightEdge, leftEdge);

    if (h.clip < 0)
        return h;

    const auto& c = engine.getSong().playlist[(std::size_t) h.clip];
    h.target = rightEdge ? gesture::Target::rightEdge : leftEdge ? gesture::Target::leftEdge : gesture::Target::item;

    if (clipMenuButton (h.clip).contains (p))
        h.target = gesture::Target::clipMenu;
    else if (fadeHandleAt (h.clip, p) != 0)
        h.target = gesture::Target::fadeHandle;
    else if (c.kind == ClipKind::automation && automationPointAt (h.clip, p) >= 0)
        h.target = gesture::Target::autoPoint;
    else if (c.kind == ClipKind::automation && isOnAutomationCurve (h.clip, p))
        h.target = gesture::Target::autoCurve;

    return h;
}

void PlaylistComponent::updateHover (juce::Point<int> p, juce::ModifierKeys mods)
{
    lastMouse = p;
    const auto h = hoverAt (p);

    if (! (h == hover))
    {
        // repaint the old and new track rows (header and lane)
        for (auto t : { hover.track, h.track })
            if (t >= 0)
                repaint (0, trackToY (t) - 1, getWidth(), trackHeight + 2);
        hover = h;
    }

    if (drag == Drag::none)
        updateCursor (mods);
}

void PlaylistComponent::updateCursor (juce::ModifierKeys mods)
{
    const auto p = lastMouse;
    auto cursor = gesture::Cursor::normal;

    if (hover.header)
        cursor = hover.led ? gesture::Cursor::point : gesture::Cursor::normal;
    else if (gridArea().contains (p))
    {
        const gesture::Mods m { mods.isShiftDown(), mods.isCtrlDown(), mods.isAltDown() };
        const auto tool = tools.getTool();
        cursor = gesture::cursorFor (gesture::leftAction (tool, hover.target, m, true), tool);
    }
    else if (p.y >= toolbarHeight && p.y < headerHeight && p.x >= headerWidth)
        cursor = mods.isCtrlDown() ? gesture::Cursor::select : gesture::Cursor::point;

    setMouseCursor (editorCursor (cursor));
}

ui::Hint PlaylistComponent::hintAt (juce::Point<int> p)
{
    ui::Hint hint;
    const auto mods = juce::ModifierKeys::currentModifiers;
    const auto& song = engine.getSong();
    const auto h = hoverAt (p);
    auto trackName = [&song] (int t) {
        return t < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) t].name.empty()
                   ? juce::String::fromUTF8 (song.playlistTracks[(std::size_t) t].name.c_str())
                   : tr ("Track {0}", { juce::String (t + 1) });
    };

    if (h.header)
    {
        hint.text = h.led ? tr ("Track: [LMB] mute / unmute  [Ctrl][LMB] or [RMB] solo")
                          : tr ("Track: [RMB] menu  [2xLMB] rename  [Shift][Wheel] move up / down");
        hint.value = trackName (h.track);
        hint.hasMenu = ! h.led;
        return hint;
    }

    if (p.y >= toolbarHeight && p.y < headerHeight && p.x >= headerWidth)
    {
        hint.text = tr ("Ruler: [LMB] song position  [Ctrl][LMB] drag: time selection (loop)  [Shift][LMB] drag in it: move  [2xLMB] clear  [RMB] markers");
        hint.hasMenu = true;
        return hint;
    }

    if (! gridArea().contains (p))
        return hint;

    const gesture::Mods m { mods.isShiftDown(), mods.isCtrlDown(), mods.isAltDown() };
    hint.text = actionHint (gesture::leftAction (tools.getTool(), h.target, m, true), true);

    if (h.clip >= 0)
    {
        const auto& c = song.playlist[(std::size_t) h.clip];
        hint.hasMenu = true;
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
            hint.value = juce::String::fromUTF8 (song.patterns[(std::size_t) c.pattern].name.c_str());
        else if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
            hint.value = juce::String::fromUTF8 (song.channels[(std::size_t) c.channel].name.c_str());
        else if (c.kind == ClipKind::automation && c.automation >= 0 && c.automation < (int) song.automations.size())
            hint.value = juce::String::fromUTF8 (song.automations[(std::size_t) c.automation].name.c_str());
    }
    else
        hint.value = trackName (h.track);

    return hint;
}

void PlaylistComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-click a track name: rename it
    if (e.x >= 20 && e.x < headerWidth && e.y >= headerHeight && e.y < gridArea().getBottom() && e.mods.isLeftButtonDown())
    {
        renameTrack (yToTrack (e.y));
        return;
    }

    bool edge = false;
    const auto hit = gridArea().contains (e.getPosition()) ? clipAt (e.x, e.y, edge) : -1;

    if (hit < 0)
        return;

    const auto& clip = engine.getSong().playlist[(std::size_t) hit];

    if (clip.kind == ClipKind::automation)
        openAutomationEditor (engine, clip.automation);
    else if (clip.kind == ClipKind::pattern)
    {
        auto pianoRoll = editPatternsIn == 2
                         || (editPatternsIn == 0 && clip.pattern < (int) engine.getSong().patterns.size()
                             && hasPianoRollNotes (engine.getSong().patterns[(std::size_t) clip.pattern]));
        if (e.mods.isAltDown())
            pianoRoll = ! pianoRoll;

        if (pianoRoll && onOpenPianoRoll)
            onOpenPianoRoll();
        else if (! pianoRoll && onOpenPattern)
            onOpenPattern();
    }
}

bool PlaylistComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;

    return false;
}

void PlaylistComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    auto start = juce::jmax<std::int64_t> (0, snap (xToTick (x), true));
    auto track = yToTrack (y);

    for (const auto& f : files)
    {
        const juce::File file (f);

        if (! file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            continue;

        const auto data = engine.getSampleLibrary().load (file.getFullPathName());

        if (data == nullptr)
            continue;

        // audio length converted to ticks at the current BPM
        const auto seconds = (double) data->frames() / data->sampleRate;
        const auto length = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) (seconds * engine.getSong().bpm / 60.0 * ppq));
        const auto path = file.getFullPathName().toStdString();
        const auto name = file.getFileNameWithoutExtension().toStdString();

        engine.editSong ([=] (Song& s) {
            ChannelDef c;
            c.name = name;
            c.instrument = InstrumentKind::audioClip;
            c.samplePath = path;
            c.mixerTrack = s.nextFreeInsert();
            s.channels.push_back (c);

            Clip clip;
            clip.kind = ClipKind::audio;
            clip.channel = (int) s.channels.size() - 1;
            clip.track = track;
            clip.start = start;
            clip.length = length;
            s.playlist.push_back (clip);
            s.mode = PlayMode::song;
        });

        ++track; // multiple files: one below the other
    }
}

void PlaylistComponent::glideBy (double ticks, double pixels)
{
    if (! gliding)
    {
        glideX.jumpTo (scrollTicks);
        glideY.jumpTo (scrollY);
        gliding = true;
    }

    const auto grid = gridArea();
    glideX.target = juce::jlimit (0.0, juce::jmax (0.0, hBar.getMaximumRangeLimit() - grid.getWidth() / pixelsPerTick), glideX.target + ticks);
    glideY.target = juce::jlimit (0.0, juce::jmax (0.0, (double) numTracks() * trackHeight - grid.getHeight()), glideY.target + pixels);
    glideFrame();
}

void PlaylistComponent::glideFrame()
{
    if (! gliding)
        return;

    // someone else moved the view (scroll bar, zoom, pan, playback follow): the glide gives way
    if (std::abs (scrollTicks - glideX.current) > 1e-6 || std::abs ((double) scrollY - std::round (glideY.current)) > 0.5)
    {
        gliding = false;
        return;
    }

    scrollTicks = glideX.step (0.45, 0.5 / pixelsPerTick);
    scrollY = (int) std::round (glideY.step (0.45, 1.0));
    glideY.current = scrollY;
    updateScrollBars();
    glideX.current = scrollTicks;
    gliding = glideX.isMoving() || glideY.isMoving();
    repaint();
}

void PlaylistComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto ctrl = e.mods.isCtrlDown(), alt = e.mods.isAltDown(), shift = e.mods.isShiftDown();

    // Shift+wheel over a track header: moves the track up / down
    if (shift && ! ctrl && e.x < headerWidth && e.y >= headerHeight && wheel.deltaY != 0.0f)
    {
        const auto track = yToTrack (e.y);
        const auto other = track + (wheel.deltaY > 0 ? -1 : 1);

        if (other >= 0 && other < numTracks())
        {
            setSelection ({});
            engine.editSong ([track, other] (Song& s) { noteedit::swapTracks (s, track, other); });
            // the mouse follows the track, so the next wheel step moves it further
            juce::Desktop::setMousePosition (localPointToGlobal (juce::Point<int> (e.x, e.y + (other - track) * trackHeight)));
        }
        return;
    }

    if (shift && alt) // nudge the selected clips by the grid
    {
        if (wheel.deltaY != 0.0f && ! selection.empty())
        {
            const auto now = juce::Time::getMillisecondCounter();
            const auto newBurst = now - lastWheelEdit > 700; // one undo step per burst
            lastWheelEdit = now;
            const auto ticks = (wheel.deltaY > 0 ? 1 : -1) * gridTicks();
            engine.editSong ([this, ticks] (Song& s) {
                auto delta = ticks;
                for (auto i : selection)
                    delta = juce::jmax (delta, -s.playlist[(std::size_t) i].start);
                for (auto i : selection)
                    s.playlist[(std::size_t) i].start += delta;
            }, newBurst);
            sortClips();
        }
        return;
    }

    if (ctrl)
    {
        gliding = false;
        const auto anchorPixels = (double) juce::jmax (0, e.x - headerWidth);
        const auto zoomed = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick * gesture::wheelZoomFactor (wheel.deltaY, wheel.isSmooth));
        scrollTicks = gesture::zoomAnchored (scrollTicks, pixelsPerTick, zoomed, anchorPixels);
        pixelsPerTick = zoomed;
        zoomBeforeAll = 0.0;
        updateScrollBars();
        repaint();
        return;
    }

    // plain wheel: up/down (Shift: left/right); a horizontal wheel or trackpad swipe scrolls sideways
    if (shift)
        glideBy (gesture::wheelScrollPixels (wheel.deltaY, wheel.isSmooth, 120.0) / pixelsPerTick, 0.0);
    else
        glideBy (gesture::wheelScrollPixels (wheel.deltaX, wheel.isSmooth, 80.0) / pixelsPerTick,
                 gesture::wheelScrollPixels (wheel.deltaY, wheel.isSmooth, 2.0 * trackHeight));
}

void PlaylistComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hBar)
        scrollTicks = start;
    else
        scrollY = (int) start;

    repaint();
}

void PlaylistComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == ui::WaveformCache::getInstanceWithoutCreating())
    {
        repaint();
        return;
    }

    // the clip list changed externally (undo, pattern removal...): the selection no longer applies
    const auto count = engine.getSong().playlist.size();

    if (count != knownClipCount && drag == Drag::none)
    {
        knownClipCount = count;
        selection.clear();
    }

    // switching pattern (top bar, Channel Rack) sets the pencil back to the pattern
    if (engine.getSong().currentPattern != knownCurrentPattern)
    {
        knownCurrentPattern = engine.getSong().currentPattern;
        brush = {};
    }

    refreshArrangementBox();
    updateScrollBars();
    repaint();
}

void PlaylistComponent::timerCallback()
{
    auto& transport = engine.getTransport();
    const auto& song = engine.getSong();
    auto tick = -1.0;

    if (song.mode == PlayMode::song && (transport.playing.load() || transport.positionTicks.load() > 0.0))
        tick = std::fmod (transport.positionTicks.load(), (double) song.songLength());

    if (tick != playheadTick && isShowing())
    {
        // only the cursor column (old and new) is repainted: large projects stay cheap
        auto dirty = [this] (double t) {
            if (t >= 0.0)
                repaint ((int) tickToX (t) - 8, 0, 16, getHeight());
        };

        dirty (playheadTick);
        playheadTick = tick;

        // follows playback: if the cursor leaves the screen, scrolls one page
        const auto grid = gridArea();
        const auto x = tickToX (tick);

        if (tick >= 0.0 && transport.playing.load() && (x > grid.getRight() - 20 || x < grid.getX()))
        {
            scrollTicks = juce::jmax (0.0, tick - 20.0 / pixelsPerTick);
            updateScrollBars();
            repaint();
        }
        else
            dirty (tick);
    }
}

bool PlaylistComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return BrowserComponent::isBrowserDrag (details);
}

void PlaylistComponent::itemDropped (const SourceDetails& details)
{
    if (const auto file = BrowserComponent::draggedFile (details); file.existsAsFile())
        filesDropped ({ file.getFullPathName() }, details.localPosition.x, details.localPosition.y);
}
// ---------------------------------------------------------------------------

PatternPicker::PatternPicker (AudioEngine& e, PlaylistComponent& p)
    : engine (e),
      playlist (p)
{
    engine.addChangeListener (this);
}

PatternPicker::~PatternPicker()
{
    engine.removeChangeListener (this);
}

void PatternPicker::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

std::vector<PatternPicker::Row> PatternPicker::rows() const
{
    const auto& song = engine.getSong();
    std::vector<Row> list;
    list.push_back ({ ClipKind::pattern, -1, tr ("Patterns"), {} });

    for (int i = 0; i < (int) song.patterns.size(); ++i)
        list.push_back ({ ClipKind::pattern, i, juce::String::fromUTF8 (song.patterns[(std::size_t) i].name.c_str()),
                          PlaylistComponent::patternColour (song, i) });

    bool audioHeader = false;
    for (int c = 0; c < (int) song.channels.size(); ++c)
        if (song.channels[(std::size_t) c].instrument == InstrumentKind::audioClip)
        {
            if (! audioHeader)
                list.push_back ({ ClipKind::audio, -1, tr ("Audio"), {} });
            audioHeader = true;
            list.push_back ({ ClipKind::audio, c, juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str()), theme::colour::audioClip });
        }

    if (! song.automations.empty())
        list.push_back ({ ClipKind::automation, -1, tr ("Automation"), {} });
    for (int a = 0; a < (int) song.automations.size(); ++a)
        list.push_back ({ ClipKind::automation, a, juce::String::fromUTF8 (song.automations[(std::size_t) a].name.c_str()), theme::colour::automation });

    return list;
}

int PatternPicker::rowAt (int y) const
{
    if (y < headerHeight)
        return -1;

    const auto index = (y - headerHeight) / rowHeight + scroll;
    return index < (int) rows().size() ? index : -1;
}

void PatternPicker::paint (juce::Graphics& g)
{
    const auto& song = engine.getSong();
    const auto list = rows();
    using namespace theme;
    g.fillAll (colour::bg2);
    g.setColour (colour::bg3);
    g.fillRect (0, 0, getWidth(), headerHeight);
    g.setColour (colour::border);
    g.fillRect (0, headerHeight - 1, getWidth(), 1);
    g.setColour (colour::textSecondary);
    g.setFont (font (type::small, true));
    g.drawText (theme::caps (tr ("Picker")), 10, 0, getWidth() - 20, headerHeight, juce::Justification::centredLeft);

    for (int i = scroll; i < (int) list.size(); ++i)
    {
        const auto y = headerHeight + (i - scroll) * rowHeight;

        if (y > getHeight())
            break;

        const auto& row = list[(std::size_t) i];

        if (row.index < 0) // section title
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::caption, true));
            g.drawText (theme::caps (row.name), 10, y, getWidth() - 20, rowHeight - 3, juce::Justification::bottomLeft);
            continue;
        }

        const auto r = juce::Rectangle<int> (4, y + 2, getWidth() - 8, rowHeight - 4);
        const auto current = row.kind == ClipKind::pattern ? (playlist.brush.kind == ClipKind::pattern && row.index == song.currentPattern)
                                                           : (playlist.brush.kind == row.kind && playlist.brush.index == row.index);
        const auto rowColour = tame (row.colour);
        const auto box = r.toFloat();
        g.setColour (current ? rowColour.interpolatedWith (colour::bg3, 0.35f) : colour::bg3);
        g.fillRoundedRectangle (box, radius::sm);

        // colour swatch on the left; the current row gets the accent strip instead
        g.setColour (current ? colour::accent : rowColour);
        g.fillRoundedRectangle (box.withWidth (3.0f), 1.5f);

        g.setColour (current ? clipTitleText (rowColour.interpolatedWith (colour::bg3, 0.35f)) : colour::textPrimary.withAlpha (0.85f));
        g.setFont (font (type::small, current));
        g.drawText (row.name, r.withTrimmedLeft (10).withTrimmedRight (4), juce::Justification::centredLeft, true);
    }

    g.setColour (colour::border);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void PatternPicker::mouseDown (const juce::MouseEvent& e)
{
    const auto list = rows();
    const auto i = rowAt (e.y);

    if (i < 0 || list[(std::size_t) i].index < 0)
        return;

    const auto& row = list[(std::size_t) i];
    playlist.brush = { row.kind, row.index };

    if (row.kind == ClipKind::pattern && row.index != engine.getSong().currentPattern)
        engine.editSong ([p = row.index] (Song& s) { s.currentPattern = p; }, false);

    repaint();
}

void PatternPicker::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto list = rows();
    const auto i = rowAt (e.y);

    if (i >= 0 && list[(std::size_t) i].kind == ClipKind::pattern && list[(std::size_t) i].index >= 0 && onOpenPattern)
        onOpenPattern();
}

void PatternPicker::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto rows = (int) this->rows().size();
    const auto visible = juce::jmax (1, (getHeight() - headerHeight) / rowHeight);
    scroll = juce::jlimit (0, juce::jmax (0, rows - visible), scroll - (int) std::round (wheel.deltaY * 6.0f));
    repaint();
}
} // namespace lynx
