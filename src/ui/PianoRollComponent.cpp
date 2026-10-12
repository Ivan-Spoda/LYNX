#include "ui/PianoRollComponent.h"

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

// Nome no padrão do FL: tecla 60 = C5.
juce::String keyName (int key)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[key % 12]) + juce::String (key / 12);
}

const Pattern& currentPattern (const Song& s)
{
    return s.patterns[(std::size_t) s.currentPattern];
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

    engine.addChangeListener (this);
    startTimerHz (30);
}

PianoRollComponent::~PianoRollComponent()
{
    engine.removeChangeListener (this);
}

juce::Rectangle<int> PianoRollComponent::gridArea() const
{
    return getLocalBounds().withTrimmedLeft (keyboardWidth).withTrimmedTop (timelineHeight)
        .withTrimmedRight (scrollBarSize).withTrimmedBottom (scrollBarSize);
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
    return timelineHeight + (127 - key) * rowHeight - scrollY;
}

int PianoRollComponent::yToKey (int y) const
{
    return juce::jlimit (0, 127, 127 - (y - timelineHeight + scrollY) / rowHeight);
}

std::int64_t PianoRollComponent::snap (double tick, bool enabled) const
{
    if (! enabled)
        return (std::int64_t) std::floor (tick);

    return (std::int64_t) std::floor (tick / ticksPerStep) * ticksPerStep;
}

std::int64_t PianoRollComponent::viewLengthTicks() const
{
    const auto& song = engine.getSong();
    const auto bar = (std::int64_t) ppq * song.beatsPerBar;
    // pattern + 4 compassos de folga para crescer
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
    hBar.setBounds (keyboardWidth, getHeight() - scrollBarSize, getWidth() - keyboardWidth - scrollBarSize, scrollBarSize);
    vBar.setBounds (getWidth() - scrollBarSize, timelineHeight, scrollBarSize, getHeight() - timelineHeight - scrollBarSize);
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

void PianoRollComponent::paint (juce::Graphics& g)
{
    const auto& song = engine.getSong();
    const auto& pattern = currentPattern (song);
    const auto channel = engine.getSelectedChannel();
    const auto grid = gridArea();
    const auto patternLength = pattern.effectiveLength (song.beatsPerBar);
    const auto bar = (std::int64_t) ppq * song.beatsPerBar;

    g.fillAll (theme::window);

    // --- grade -----------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (grid);

        for (int key = 0; key < 128; ++key)
        {
            const auto y = keyToY (key);

            if (y > grid.getBottom() || y + rowHeight < grid.getY())
                continue;

            g.setColour (isBlackKey (key) ? juce::Colour (0xff262b30) : juce::Colour (0xff30363d));
            g.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);

            if (key % 12 == 0)
            {
                g.setColour (juce::Colour (0xff1a1d21));
                g.drawHorizontalLine (y + rowHeight - 1, (float) grid.getX(), (float) grid.getRight());
            }
        }

        const auto firstStep = (std::int64_t) std::floor (scrollTicks / ticksPerStep);
        const auto lastStep = (std::int64_t) std::ceil (xToTick (grid.getRight()) / ticksPerStep);

        for (auto step = firstStep; step <= lastStep; ++step)
        {
            const auto tick = step * ticksPerStep;
            const auto x = (float) tickToX ((double) tick);
            g.setColour (tick % bar == 0 ? juce::Colour (0xff15181b) : tick % ppq == 0 ? juce::Colour (0xff22272c) : juce::Colour (0xff2a2f35));
            g.drawVerticalLine ((int) x, (float) grid.getY(), (float) grid.getBottom());
        }

        // fora da pattern fica escurecido
        const auto endX = (float) tickToX ((double) patternLength);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (endX, (float) grid.getY(), (float) grid.getRight() - endX, (float) grid.getHeight()));

        // ghost notes
        for (const auto& n : pattern.notes)
        {
            if (n.channel == channel)
                continue;

            const auto r = juce::Rectangle<float> ((float) tickToX ((double) n.start), (float) keyToY (n.key) + 1,
                                                   (float) (n.length * pixelsPerTick), (float) rowHeight - 2);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (r, 2.0f);
        }

        for (const auto& n : pattern.notes)
        {
            if (n.channel != channel)
                continue;

            const auto r = juce::Rectangle<float> ((float) tickToX ((double) n.start), (float) keyToY (n.key) + 1,
                                                   juce::jmax (4.0f, (float) (n.length * pixelsPerTick) - 1.0f), (float) rowHeight - 2);
            g.setColour (theme::noteColour (n.velocity));
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 2.5f, 1.0f);

            if (r.getWidth() > 30)
            {
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (keyName (n.key), r.reduced (3, 0), juce::Justification::centredLeft, false);
            }
        }

        if (playheadTick >= 0.0)
        {
            g.setColour (theme::playhead);
            g.drawVerticalLine ((int) tickToX (playheadTick), (float) grid.getY(), (float) grid.getBottom());
        }
    }

    // --- régua de tempo ----------------------------------------------------
    {
        const auto ruler = juce::Rectangle<int> (keyboardWidth, 0, grid.getWidth(), timelineHeight);
        g.setColour (theme::toolbar);
        g.fillRect (ruler);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (ruler);
        g.setFont (juce::FontOptions (12.0f));

        const auto firstBar = (std::int64_t) std::floor (scrollTicks / (double) bar);
        const auto lastBar = (std::int64_t) std::ceil (xToTick (grid.getRight()) / (double) bar);

        for (auto b = firstBar; b <= lastBar; ++b)
        {
            const auto x = (int) tickToX ((double) (b * bar));
            g.setColour (theme::text.withAlpha (0.7f));
            g.drawVerticalLine (x, 10.0f, (float) timelineHeight);
            g.drawText (juce::String (b + 1), x + 3, 2, 40, timelineHeight - 4, juce::Justification::centredLeft);
        }

        if (playheadTick >= 0.0)
        {
            const auto x = (float) tickToX (playheadTick);
            juce::Path tri;
            tri.addTriangle (x - 5, 0, x + 5, 0, x, 8);
            g.setColour (theme::playhead);
            g.fillPath (tri);
        }
    }

    // --- teclado -------------------------------------------------------------
    {
        const auto keys = juce::Rectangle<int> (0, timelineHeight, keyboardWidth, grid.getHeight());
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (keys);
        g.setFont (juce::FontOptions (10.0f));

        for (int key = 0; key < 128; ++key)
        {
            const auto y = keyToY (key);

            if (y > keys.getBottom() || y + rowHeight < keys.getY())
                continue;

            const auto black = isBlackKey (key);
            const auto pressed = key == previewKey;
            g.setColour (pressed ? theme::accentOrange : black ? juce::Colour (0xff1c1c1c) : juce::Colour (0xffdedede));
            g.fillRect (0, y, black ? keyboardWidth * 2 / 3 : keyboardWidth, rowHeight);
            g.setColour (juce::Colour (0xff888888));
            g.drawHorizontalLine (y + rowHeight - 1, 0.0f, (float) keyboardWidth);

            if (key % 12 == 0)
            {
                g.setColour (juce::Colours::black);
                g.drawText (keyName (key), keyboardWidth - 30, y, 28, rowHeight, juce::Justification::centredRight);
            }
        }
    }

    // canto superior esquerdo: nome do canal
    g.setColour (theme::toolbar);
    g.fillRect (0, 0, keyboardWidth, timelineHeight);

    if (channel < (int) song.channels.size())
    {
        g.setColour (theme::accentOrange);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str()), 4, 0, keyboardWidth - 6,
                    timelineHeight, juce::Justification::centredLeft, true);
    }
}

int PianoRollComponent::noteAt (int x, int y, bool& onRightEdge) const
{
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto channel = engine.getSelectedChannel();
    const auto key = yToKey (y);

    // de trás para frente: a nota desenhada por cima ganha
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
    repaint (0, timelineHeight, keyboardWidth, getHeight());
}

void PianoRollComponent::previewOff()
{
    if (previewKey >= 0)
    {
        engine.sendLiveNote (engine.getSelectedChannel(), previewKey, 0.0f, false);
        previewKey = -1;
        repaint (0, timelineHeight, keyboardWidth, getHeight());
    }
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& e)
{
    if (engine.getSong().channels.empty())
        return;

    if (e.x < keyboardWidth && e.y >= timelineHeight)
    {
        drag = Drag::keyboard;
        previewOn (yToKey (e.y), 0.8f);
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    bool rightEdge = false;
    const auto hit = noteAt (e.x, e.y, rightEdge);

    if (e.mods.isRightButtonDown())
    {
        drag = Drag::erase;

        if (hit >= 0)
            engine.editSong ([hit] (Song& s) {
                auto& notes = s.patterns[(std::size_t) s.currentPattern].notes;
                notes.erase (notes.begin() + hit);
            });
        return;
    }

    const auto snapOn = ! e.mods.isAltDown();

    if (hit >= 0)
    {
        const auto& n = currentPattern (engine.getSong()).notes[(std::size_t) hit];
        dragNote = hit;
        drag = rightEdge ? Drag::resize : Drag::move;
        grabOffset = n.start - snap (xToTick (e.x), snapOn);
        lastLength = n.length;
        previewOn (n.key, n.velocity);
        return;
    }

    // nota nova
    Note n;
    n.start = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
    n.length = lastLength;
    n.key = yToKey (e.y);
    n.channel = engine.getSelectedChannel();

    engine.editSong ([n] (Song& s) { s.patterns[(std::size_t) s.currentPattern].notes.push_back (n); });
    dragNote = (int) currentPattern (engine.getSong()).notes.size() - 1;
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

        case Drag::erase:
        {
            bool edge = false;
            const auto hit = noteAt (e.x, e.y, edge);

            if (hit >= 0)
                engine.editSong ([hit] (Song& s) {
                    auto& notes = s.patterns[(std::size_t) s.currentPattern].notes;
                    notes.erase (notes.begin() + hit);
                });
            return;
        }

        case Drag::move:
        {
            const auto start = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) + grabOffset);
            const auto key = yToKey (e.y);
            const auto index = dragNote;
            const auto& current = currentPattern (engine.getSong()).notes[(std::size_t) index];

            if (current.start == start && current.key == key)
                return;

            if (current.key != key)
                previewOn (key, current.velocity);

            // sem reordenar durante o arraste, para o índice continuar válido
            engine.editSong ([index, start, key] (Song& s) {
                auto& n = s.patterns[(std::size_t) s.currentPattern].notes[(std::size_t) index];
                n.start = start;
                n.key = key;
            });
            return;
        }

        case Drag::resize:
        {
            const auto index = dragNote;
            const auto& current = currentPattern (engine.getSong()).notes[(std::size_t) index];
            const auto minimum = snapOn ? (std::int64_t) ticksPerStep : (std::int64_t) 10;
            auto end = snapOn ? snap (xToTick (e.x) + ticksPerStep / 2.0, true) : (std::int64_t) xToTick (e.x);
            const auto length = juce::jmax (minimum, end - current.start);

            if (length == current.length)
                return;

            lastLength = length;
            engine.editSong ([index, length] (Song& s) {
                s.patterns[(std::size_t) s.currentPattern].notes[(std::size_t) index].length = length;
            });
            return;
        }
    }
}

void PianoRollComponent::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::move || drag == Drag::resize)
        engine.editSong ([] (Song& s) { s.patterns[(std::size_t) s.currentPattern].sortNotes(); });

    previewOff();
    drag = Drag::none;
    dragNote = -1;
}

void PianoRollComponent::mouseMove (const juce::MouseEvent& e)
{
    bool rightEdge = false;
    const auto hit = gridArea().contains (e.getPosition()) ? noteAt (e.x, e.y, rightEdge) : -1;
    setMouseCursor (hit >= 0 ? (rightEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                             : juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        // zoom mantendo o tick sob o mouse no lugar
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
    updateScrollBars();
    repaint();
}

void PianoRollComponent::timerCallback()
{
    auto& transport = engine.getTransport();
    auto tick = -1.0;

    if (transport.playing.load() && isShowing())
    {
        const auto& song = engine.getSong();
        tick = std::fmod (transport.positionTicks.load(), (double) currentPattern (song).effectiveLength (song.beatsPerBar));
    }

    if (tick != playheadTick)
    {
        playheadTick = tick;
        repaint();
    }
}
} // namespace lynx
