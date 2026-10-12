#include "ui/PlaylistComponent.h"

#include "ui/Theme.h"

namespace lynx
{
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

    engine.addChangeListener (this);
    startTimerHz (30);
}

PlaylistComponent::~PlaylistComponent()
{
    engine.removeChangeListener (this);
}

juce::Colour PlaylistComponent::patternColour (int pattern)
{
    // matizes espalhados pela razão áurea: patterns vizinhas ficam com cores bem diferentes
    const auto hue = std::fmod (0.08f + (float) pattern * 0.618034f, 1.0f);
    return juce::Colour::fromHSV (hue, 0.45f, 0.75f, 1.0f);
}

juce::Rectangle<int> PlaylistComponent::gridArea() const
{
    return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedTop (timelineHeight)
        .withTrimmedRight (scrollBarSize).withTrimmedBottom (scrollBarSize);
}

double PlaylistComponent::tickToX (double tick) const { return headerWidth + (tick - scrollTicks) * pixelsPerTick; }
double PlaylistComponent::xToTick (int x) const { return (x - headerWidth) / pixelsPerTick + scrollTicks; }
int PlaylistComponent::trackToY (int track) const { return timelineHeight + track * trackHeight - scrollY; }
int PlaylistComponent::yToTrack (int y) const { return juce::jlimit (0, numTracks() - 1, (y - timelineHeight + scrollY) / trackHeight); }

int PlaylistComponent::numTracks() const
{
    int highest = 0;

    for (const auto& c : engine.getSong().playlist)
        highest = juce::jmax (highest, c.track);

    return juce::jlimit (16, 500, highest + 8); // sempre algumas trilhas livres embaixo
}

std::int64_t PlaylistComponent::snap (double tick, bool enabled) const
{
    if (! enabled)
        return (std::int64_t) std::floor (tick);

    return (std::int64_t) std::floor (tick / ppq) * ppq;
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

void PlaylistComponent::resized()
{
    hBar.setBounds (headerWidth, getHeight() - scrollBarSize, getWidth() - headerWidth - scrollBarSize, scrollBarSize);
    vBar.setBounds (getWidth() - scrollBarSize, timelineHeight, scrollBarSize, getHeight() - timelineHeight - scrollBarSize);
    updateScrollBars();
}

void PlaylistComponent::paint (juce::Graphics& g)
{
    const auto& song = engine.getSong();
    const auto grid = gridArea();
    const auto bar = (std::int64_t) ppq * song.beatsPerBar;
    const auto tracks = numTracks();

    g.fillAll (theme::window);

    // --- trilhas e grade ---------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (grid);

        for (int t = 0; t < tracks; ++t)
        {
            const auto y = trackToY (t);

            if (y > grid.getBottom() || y + trackHeight < grid.getY())
                continue;

            g.setColour (t % 2 == 0 ? juce::Colour (0xff2c3238) : juce::Colour (0xff292e34));
            g.fillRect (grid.getX(), y, grid.getWidth(), trackHeight);
            g.setColour (juce::Colour (0xff1d2125));
            g.drawHorizontalLine (y + trackHeight - 1, (float) grid.getX(), (float) grid.getRight());
        }

        const auto firstBeat = (std::int64_t) std::floor (scrollTicks / ppq);
        const auto lastBeat = (std::int64_t) std::ceil (xToTick (grid.getRight()) / ppq);
        const auto beatPixels = ppq * pixelsPerTick;

        for (auto beat = firstBeat; beat <= lastBeat; ++beat)
        {
            const auto tick = beat * ppq;
            const auto isBar = tick % bar == 0;

            if (! isBar && beatPixels < 8.0)
                continue; // muito afastado: só linhas de compasso

            g.setColour (isBar ? juce::Colour (0xff181b1f) : juce::Colour (0xff24292e));
            g.drawVerticalLine ((int) tickToX ((double) tick), (float) grid.getY(), (float) grid.getBottom());
        }

        // fim da música
        if (! song.playlist.empty())
        {
            const auto endX = (float) tickToX ((double) song.songLength());
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (endX, (float) grid.getY(), (float) grid.getRight() - endX, (float) grid.getHeight()));
        }

        g.setFont (juce::FontOptions (11.0f));

        for (int i = 0; i < (int) song.playlist.size(); ++i)
        {
            const auto& c = song.playlist[(std::size_t) i];
            const auto x0 = (float) tickToX ((double) c.start);
            const auto x1 = (float) tickToX ((double) c.end());
            const auto y = (float) trackToY (c.track);

            if (x1 < grid.getX() || x0 > grid.getRight() || y > grid.getBottom() || y + trackHeight < grid.getY())
                continue;

            const auto r = juce::Rectangle<float> (x0, y + 1.0f, juce::jmax (3.0f, x1 - x0 - 1.0f), (float) trackHeight - 3.0f);
            const auto colour = patternColour (c.pattern);
            const auto selected = c.pattern == song.currentPattern;

            g.setColour (colour.withAlpha (0.85f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (selected ? juce::Colours::white : colour.darker (0.6f));
            g.drawRoundedRectangle (r, 3.0f, selected ? 1.5f : 1.0f);

            // faixa do nome
            const auto title = r.withHeight (13.0f);
            g.setColour (colour.darker (0.4f));
            g.fillRoundedRectangle (title, 3.0f);

            if (r.getWidth() > 24)
            {
                g.setColour (juce::Colours::white);
                g.drawText (juce::String::fromUTF8 (song.patterns[(std::size_t) c.pattern].name.c_str()),
                            title.reduced (3, 0).toNearestInt(), juce::Justification::centredLeft, true);
            }

            // miniatura das notas
            const auto& notes = song.patterns[(std::size_t) c.pattern].notes;

            if (! notes.empty() && r.getWidth() > 6)
            {
                int low = 127, high = 0;
                for (const auto& n : notes)
                {
                    low = juce::jmin (low, n.key);
                    high = juce::jmax (high, n.key);
                }

                const auto body = r.withTrimmedTop (15.0f).reduced (2.0f, 2.0f);
                const auto keySpan = (float) juce::jmax (1, high - low);
                g.setColour (colour.darker (0.75f));

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
        }

        if (playheadTick >= 0.0)
        {
            g.setColour (theme::playhead);
            g.drawVerticalLine ((int) tickToX (playheadTick), (float) grid.getY(), (float) grid.getBottom());
        }
    }

    // --- régua ---------------------------------------------------------------
    {
        const auto ruler = juce::Rectangle<int> (headerWidth, 0, grid.getWidth(), timelineHeight);
        g.setColour (theme::toolbar);
        g.fillRect (ruler);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (ruler);
        g.setFont (juce::FontOptions (12.0f));

        const auto barPixels = bar * pixelsPerTick;
        const auto every = barPixels < 30.0 ? (barPixels < 10.0 ? 16 : 4) : 1; // números mais espaçados quando afastado
        const auto firstBar = (std::int64_t) std::floor (scrollTicks / (double) bar);
        const auto lastBar = (std::int64_t) std::ceil (xToTick (grid.getRight()) / (double) bar);

        for (auto b = firstBar; b <= lastBar; ++b)
        {
            if (b % every != 0)
                continue;

            const auto x = (int) tickToX ((double) (b * bar));
            g.setColour (theme::text.withAlpha (0.7f));
            g.drawVerticalLine (x, 12.0f, (float) timelineHeight);
            g.drawText (juce::String (b + 1), x + 3, 2, 40, timelineHeight - 4, juce::Justification::centredLeft);
        }

        if (playheadTick >= 0.0)
        {
            const auto x = (float) tickToX (playheadTick);
            juce::Path tri;
            tri.addTriangle (x - 6, 0, x + 6, 0, x, 9);
            g.setColour (theme::playhead);
            g.fillPath (tri);
        }
    }

    // --- cabeçalhos das trilhas --------------------------------------------
    {
        const auto headers = juce::Rectangle<int> (0, timelineHeight, headerWidth, grid.getHeight());
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (headers);
        g.setFont (juce::FontOptions (12.0f));

        for (int t = 0; t < tracks; ++t)
        {
            const auto y = trackToY (t);

            if (y > headers.getBottom() || y + trackHeight < headers.getY())
                continue;

            g.setColour (theme::channelButton);
            g.fillRect (2, y + 1, headerWidth - 4, trackHeight - 3);
            g.setColour (theme::text.withAlpha (0.8f));
            g.drawText ("Track " + juce::String (t + 1), 8, y, headerWidth - 12, trackHeight, juce::Justification::centredLeft);
        }
    }

    g.setColour (theme::toolbar);
    g.fillRect (0, 0, headerWidth, timelineHeight);
    g.setColour (song.mode == PlayMode::song ? theme::accentGreen : theme::text.withAlpha (0.5f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (song.mode == PlayMode::song ? "SONG" : "PAT (L troca)", 6, 0, headerWidth - 8, timelineHeight,
                juce::Justification::centredLeft);
}

int PlaylistComponent::clipAt (int x, int y, bool& onRightEdge) const
{
    const auto& playlist = engine.getSong().playlist;
    const auto track = yToTrack (y);

    for (int i = (int) playlist.size() - 1; i >= 0; --i)
    {
        const auto& c = playlist[(std::size_t) i];

        if (c.track != track)
            continue;

        const auto x0 = tickToX ((double) c.start);
        const auto x1 = tickToX ((double) c.end());

        if (x >= x0 && x < juce::jmax (x1, x0 + 4.0))
        {
            onRightEdge = x >= x1 - 6.0;
            return i;
        }
    }

    return -1;
}

void PlaylistComponent::seekTo (int x)
{
    const auto tick = juce::jmax (0.0, (double) snap (xToTick (x), true));

    if (engine.getSong().mode != PlayMode::song)
        engine.editSong ([] (Song& s) { s.mode = PlayMode::song; });

    engine.seek (tick);
    playheadTick = tick;
    repaint();
}

void PlaylistComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < timelineHeight && e.x >= headerWidth)
    {
        drag = Drag::seek;
        seekTo (e.x);
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    bool rightEdge = false;
    const auto hit = clipAt (e.x, e.y, rightEdge);

    if (e.mods.isRightButtonDown())
    {
        drag = Drag::erase;

        if (hit >= 0)
            engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
        return;
    }

    const auto snapOn = ! e.mods.isAltDown();

    if (hit >= 0)
    {
        const auto& c = engine.getSong().playlist[(std::size_t) hit];
        dragClip = hit;
        drag = rightEdge ? Drag::resize : Drag::move;
        grabOffset = c.start - snap (xToTick (e.x), snapOn);

        if (c.pattern != engine.getSong().currentPattern)
            engine.editSong ([p = c.pattern] (Song& s) { s.currentPattern = p; });
        return;
    }

    // pinta a pattern atual
    const auto& song = engine.getSong();
    Clip c;
    c.pattern = song.currentPattern;
    c.track = yToTrack (e.y);
    c.start = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
    c.length = song.patterns[(std::size_t) song.currentPattern].effectiveLength (song.beatsPerBar);

    engine.editSong ([c] (Song& s) {
        s.playlist.push_back (c);
        s.mode = PlayMode::song; // quem está arranjando quer ouvir a música
    });
    dragClip = (int) engine.getSong().playlist.size() - 1;
    drag = Drag::move;
    grabOffset = 0;
}

void PlaylistComponent::mouseDrag (const juce::MouseEvent& e)
{
    const auto snapOn = ! e.mods.isAltDown();

    switch (drag)
    {
        case Drag::none:
            return;

        case Drag::seek:
            seekTo (e.x);
            return;

        case Drag::erase:
        {
            bool edge = false;
            const auto hit = clipAt (e.x, e.y, edge);

            if (hit >= 0)
                engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
            return;
        }

        case Drag::move:
        {
            const auto start = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) + grabOffset);
            const auto track = yToTrack (e.y);
            const auto& c = engine.getSong().playlist[(std::size_t) dragClip];

            if (c.start != start || c.track != track)
                engine.editSong ([i = dragClip, start, track] (Song& s) {
                    s.playlist[(std::size_t) i].start = start;
                    s.playlist[(std::size_t) i].track = track;
                });
            return;
        }

        case Drag::resize:
        {
            const auto& c = engine.getSong().playlist[(std::size_t) dragClip];
            const auto end = snapOn ? snap (xToTick (e.x) + ppq / 2.0, true) : (std::int64_t) xToTick (e.x);
            const auto length = juce::jmax<std::int64_t> (snapOn ? ppq : 10, end - c.start);

            if (length != c.length)
                engine.editSong ([i = dragClip, length] (Song& s) { s.playlist[(std::size_t) i].length = length; });
            return;
        }
    }
}

void PlaylistComponent::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::move || drag == Drag::resize)
        engine.editSong ([] (Song& s) {
            std::stable_sort (s.playlist.begin(), s.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
        });

    drag = Drag::none;
    dragClip = -1;
}

void PlaylistComponent::mouseMove (const juce::MouseEvent& e)
{
    bool rightEdge = false;
    const auto hit = gridArea().contains (e.getPosition()) ? clipAt (e.x, e.y, rightEdge) : -1;
    setMouseCursor (hit >= 0 ? (rightEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                             : juce::MouseCursor::NormalCursor);
}

void PlaylistComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    bool edge = false;

    if (gridArea().contains (e.getPosition()) && clipAt (e.x, e.y, edge) >= 0 && onOpenPattern)
        onOpenPattern();
}

void PlaylistComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        const auto anchor = xToTick (e.x);
        pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 800.0 / (4 * ppq), pixelsPerTick * (wheel.deltaY > 0 ? 1.2 : 1.0 / 1.2));
        scrollTicks = juce::jmax (0.0, anchor - (e.x - headerWidth) / pixelsPerTick);
    }
    else if (e.mods.isShiftDown())
        scrollTicks = juce::jmax (0.0, scrollTicks - wheel.deltaY * 300.0 / pixelsPerTick);
    else
        scrollY = juce::jmax (0, scrollY - (int) (wheel.deltaY * 200.0f));

    updateScrollBars();
    repaint();
}

void PlaylistComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hBar)
        scrollTicks = start;
    else
        scrollY = (int) start;

    repaint();
}

void PlaylistComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
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
        playheadTick = tick;

        // acompanha a reprodução: se o cursor sair da tela, rola uma página
        const auto grid = gridArea();
        const auto x = tickToX (tick);

        if (tick >= 0.0 && transport.playing.load() && (x > grid.getRight() - 20 || x < grid.getX()))
        {
            scrollTicks = juce::jmax (0.0, tick - 20.0 / pixelsPerTick);
            updateScrollBars();
        }

        repaint();
    }
}
} // namespace lynx
