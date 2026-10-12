#include "ui/ChannelRackComponent.h"

namespace daw
{
namespace
{
const juce::Colour background { 0xff2b3036 };
const juce::Colour rowColour { 0xff353b42 };
const juce::Colour stepOffA { 0xff4a525b };  // grupos de 4 steps alternados, como no FL
const juce::Colour stepOffB { 0xff5b4f4f };
const juce::Colour stepOn { 0xffe8e8e8 };
const juce::Colour stepOnImported { 0xff9fd3ff }; // nota fora da grade (veio do piano roll)
const juce::Colour playheadColour { 0x5530c0ff };
const juce::Colour muteOn { 0xff6fe36f };
const juce::Colour muteOff { 0xff505050 };

const char* instrumentName (InstrumentKind k)
{
    switch (k)
    {
        case InstrumentKind::kick: return "Kick";
        case InstrumentKind::snare: return "Snare";
        case InstrumentKind::hat: return "Hat";
        case InstrumentKind::clap: return "Clap";
        case InstrumentKind::synth: return "Synth";
    }
    return "?";
}

void setStep (Pattern& p, int channel, int step, bool on)
{
    if (p.hasStep (channel, step) != on)
        p.toggleStep (channel, step);
}
} // namespace

ChannelRackComponent::ChannelRackComponent (AudioEngine& e)
    : engine (e)
{
    engine.addChangeListener (this);
    updateSize();
    startTimerHz (30);
}

ChannelRackComponent::~ChannelRackComponent()
{
    engine.removeChangeListener (this);
}

int ChannelRackComponent::numSteps() const
{
    const auto& song = engine.getSong();

    if (song.patterns.empty())
        return 16;

    const auto& p = song.patterns[(std::size_t) song.currentPattern];
    const auto steps = (int) (p.effectiveLength (song.beatsPerBar) / ticksPerStep);
    return juce::jlimit (16, 256, steps);
}

void ChannelRackComponent::updateSize()
{
    const auto rows = (int) engine.getSong().channels.size() + 1; // +1 = linha de "adicionar canal"
    setSize (gridLeft() + numSteps() * stepWidth + 8, rows * rowHeight + 8);
}

juce::Rectangle<int> ChannelRackComponent::stepBounds (int row, int step) const
{
    return { gridLeft() + step * stepWidth + 1, 4 + row * rowHeight + 3, stepWidth - 3, rowHeight - 6 };
}

bool ChannelRackComponent::cellHasNote (int channel, int step) const
{
    const auto& song = engine.getSong();
    const auto& p = song.patterns[(std::size_t) song.currentPattern];
    const auto from = (std::int64_t) step * ticksPerStep;
    const auto to = from + ticksPerStep;

    for (const auto& n : p.notes)
    {
        if (n.start >= to)
            break; // notas ordenadas

        if (n.channel == channel && n.start >= from)
            return true;
    }

    return false;
}

void ChannelRackComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);

    const auto& song = engine.getSong();

    if (song.patterns.empty())
        return;

    const auto& pattern = song.patterns[(std::size_t) song.currentPattern];
    const auto steps = numSteps();
    const auto clip = g.getClipBounds();
    g.setFont (juce::FontOptions (14.0f));

    for (int row = 0; row < (int) song.channels.size(); ++row)
    {
        const auto y = 4 + row * rowHeight;

        if (y > clip.getBottom() || y + rowHeight < clip.getY())
            continue;

        const auto& channel = song.channels[(std::size_t) row];

        g.setColour (rowColour);
        g.fillRoundedRectangle ({ 4.0f, (float) y + 2, (float) (muteWidth + nameWidth), (float) rowHeight - 4 }, 4.0f);

        g.setColour (channel.muted ? muteOff : muteOn);
        g.fillEllipse ((float) 10, (float) y + rowHeight / 2.0f - 5, 10, 10);

        g.setColour (channel.muted ? juce::Colours::grey : juce::Colours::white);
        g.drawText (juce::String::fromUTF8 (channel.name.c_str()), muteWidth + 4, y, nameWidth - 8, rowHeight,
                    juce::Justification::centredLeft, true);

        for (int step = 0; step < steps; ++step)
        {
            const auto r = stepBounds (row, step);

            if (r.getRight() < clip.getX() || r.getX() > clip.getRight())
                continue;

            const auto onGrid = pattern.hasStep (row, step);
            const auto any = onGrid || cellHasNote (row, step);

            g.setColour (any ? (onGrid ? stepOn : stepOnImported) : ((step / 4) % 2 == 0 ? stepOffA : stepOffB));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
        }
    }

    // linha "adicionar canal"
    const auto addY = 4 + (int) song.channels.size() * rowHeight;
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText ("+ adicionar canal", muteWidth + 4, addY, nameWidth, rowHeight, juce::Justification::centredLeft);

    if (playheadStep >= 0 && playheadStep < steps)
    {
        g.setColour (playheadColour);
        g.fillRect (gridLeft() + playheadStep * stepWidth, 0, stepWidth - 2, addY);
    }
}

void ChannelRackComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto& song = engine.getSong();
    const auto row = (e.y - 4) / rowHeight;

    if (e.y < 4 || row > (int) song.channels.size())
        return;

    if (row == (int) song.channels.size())
    {
        if (e.x < gridLeft())
            addChannel();
        return;
    }

    if (e.x < muteWidth + 4)
    {
        engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].muted = ! s.channels[(std::size_t) row].muted; });
        return;
    }

    if (e.x < gridLeft())
    {
        showChannelMenu (row);
        return;
    }

    const auto step = (e.x - gridLeft()) / stepWidth;
    const auto& pattern = song.patterns[(std::size_t) song.currentPattern];

    if (e.mods.isRightButtonDown())
        paintMode = PaintMode::remove;
    else
        paintMode = pattern.hasStep (row, step) ? PaintMode::remove : PaintMode::add;

    lastPaintedRow = lastPaintedStep = -1;
    applyPaint (e);
}

void ChannelRackComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (paintMode != PaintMode::none)
        applyPaint (e);
}

void ChannelRackComponent::mouseUp (const juce::MouseEvent&)
{
    paintMode = PaintMode::none;
}

void ChannelRackComponent::applyPaint (const juce::MouseEvent& e)
{
    const auto& song = engine.getSong();
    const auto row = (e.y - 4) / rowHeight;
    const auto step = (e.x - gridLeft()) / stepWidth;

    if (row < 0 || row >= (int) song.channels.size() || step < 0 || step >= numSteps() || e.x < gridLeft())
        return;

    if (row == lastPaintedRow && step == lastPaintedStep)
        return;

    lastPaintedRow = row;
    lastPaintedStep = step;
    const auto on = paintMode == PaintMode::add;

    engine.editSong ([row, step, on] (Song& s) { setStep (s.patterns[(std::size_t) s.currentPattern], row, step, on); });
}

void ChannelRackComponent::showChannelMenu (int channel)
{
    const auto& c = engine.getSong().channels[(std::size_t) channel];
    juce::PopupMenu instruments;

    for (auto kind : { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat, InstrumentKind::clap,
                       InstrumentKind::synth })
        instruments.addItem (instrumentName (kind), true, c.instrument == kind, [this, channel, kind] {
            engine.editSong ([channel, kind] (Song& s) { s.channels[(std::size_t) channel].instrument = kind; });
        });

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 (c.name.c_str()));
    menu.addSubMenu ("Instrumento", instruments);
    menu.addItem ("Limpar steps", [this, channel] {
        engine.editSong ([channel] (Song& s) {
            auto& notes = s.patterns[(std::size_t) s.currentPattern].notes;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [channel] (const Note& n) { return n.channel == channel; }),
                         notes.end());
        });
    });
    menu.addItem ("Remover canal", engine.getSong().channels.size() > 1, false, [this, channel] {
        engine.editSong ([channel] (Song& s) {
            s.channels.erase (s.channels.begin() + channel);

            for (auto& p : s.patterns)
            {
                auto& notes = p.notes;
                notes.erase (std::remove_if (notes.begin(), notes.end(), [channel] (const Note& n) { return n.channel == channel; }),
                             notes.end());
                for (auto& n : notes)
                    if (n.channel > channel)
                        --n.channel;
            }
        });
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ChannelRackComponent::addChannel()
{
    juce::PopupMenu menu;

    for (auto kind : { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat, InstrumentKind::clap,
                       InstrumentKind::synth })
        menu.addItem (instrumentName (kind), [this, kind] {
            engine.editSong ([kind] (Song& s) {
                ChannelDef c;
                c.name = instrumentName (kind);
                c.instrument = kind;
                s.channels.push_back (c);
            });
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ChannelRackComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateSize();
    repaint();
}

void ChannelRackComponent::timerCallback()
{
    auto& transport = engine.getTransport();
    auto step = -1;

    if (transport.playing.load())
    {
        const auto& song = engine.getSong();
        const auto& p = song.patterns[(std::size_t) song.currentPattern];
        const auto length = (double) p.effectiveLength (song.beatsPerBar);
        step = (int) (std::fmod (transport.positionTicks.load(), length) / ticksPerStep);
    }

    if (step != playheadStep)
    {
        auto dirty = [this] (int s) {
            if (s >= 0)
                repaint (gridLeft() + s * stepWidth, 0, stepWidth, getHeight());
        };
        dirty (playheadStep);
        playheadStep = step;
        dirty (playheadStep);
    }
}
} // namespace daw
