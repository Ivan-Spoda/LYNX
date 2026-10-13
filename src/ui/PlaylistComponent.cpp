#include "ui/PlaylistComponent.h"

#include "core/NoteEdit.h"
#include "engine/Text.h"
#include "ui/BrowserComponent.h"
#include "ui/AutomationUi.h"

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

    arrangementBox.setTooltip (utf8 ("Arrangement (como no FL, um projeto pode ter vários)"));
    arrangementBox.setWantsKeyboardFocus (false);
    arrangementBox.setMouseClickGrabsKeyboardFocus (false);
    arrangementBox.onChange = [this] { arrangementChosen(); };
    addAndMakeVisible (arrangementBox);
    refreshArrangementBox();

    engine.addChangeListener (this);
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

    // só reconstrói quando algo mudou (reconstruir fecha o menu aberto)
    bool same = arrangementBox.getNumItems() == names.size() + 4;
    for (int i = 0; same && i < names.size(); ++i)
        same = arrangementBox.getItemText (i) == names[i];

    if (! same)
    {
        arrangementBox.clear (juce::dontSendNotification);

        for (int i = 0; i < names.size(); ++i)
            arrangementBox.addItem (names[i], i + 1);

        arrangementBox.addSeparator();
        arrangementBox.addItem (utf8 ("Novo arrangement"), newArrangement);
        arrangementBox.addItem (utf8 ("Clonar este"), cloneArrangement);
        arrangementBox.addItem (utf8 ("Renomear..."), renameArrangementAction);
        arrangementBox.addItem (utf8 ("Remover este"), removeArrangementAction);
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
                s.addArrangement (s.arrangements[(std::size_t) s.currentArrangement].name + " (cópia)", true);
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
    auto* window = new juce::AlertWindow ("Renomear arrangement", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String::fromUTF8 (engine.getSong().arrangements[(std::size_t) index].name.c_str()));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, index, window] (int result) {
                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();

                                 if (result == 1 && ! name.empty() && index < (int) engine.getSong().arrangements.size())
                                     engine.editSong ([index, name] (Song& s) { s.arrangements[(std::size_t) index].name = name; });
                             }),
                             true);
}

PlaylistComponent::~PlaylistComponent()
{
    engine.removeChangeListener (this);
}

juce::Colour PlaylistComponent::patternColour (const Song& song, int pattern)
{
    if (pattern >= 0 && pattern < (int) song.patterns.size() && song.patterns[(std::size_t) pattern].colour != 0)
        return juce::Colour (song.patterns[(std::size_t) pattern].colour);

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
    arrangementBox.setBounds (2, 1, headerWidth - 4, timelineHeight - 2);
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

        // região de loop
        if (song.hasLoop())
        {
            const auto lx0 = (float) tickToX ((double) song.loopStart), lx1 = (float) tickToX ((double) song.loopEnd);
            g.setColour (theme::accentGreen.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (lx0, (float) grid.getY(), lx1 - lx0, (float) grid.getHeight()));
            g.setColour (theme::accentGreen.withAlpha (0.35f));
            g.drawVerticalLine ((int) lx0, (float) grid.getY(), (float) grid.getBottom());
            g.drawVerticalLine ((int) lx1, (float) grid.getY(), (float) grid.getBottom());
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

            if (c.kind == ClipKind::automation)
            {
                paintAutomationClip (g, c, r);

                if (isSelected (i))
                {
                    g.setColour (juce::Colour (0xffff6f6f));
                    g.drawRoundedRectangle (r, 3.0f, 2.0f);
                }
                continue;
            }

            if (c.kind == ClipKind::audio)
            {
                paintAudioClip (g, c, r);

                if (isSelected (i))
                {
                    g.setColour (juce::Colour (0xffff6f6f));
                    g.drawRoundedRectangle (r, 3.0f, 2.0f);
                }
                continue;
            }

            const auto colour = patternColour (song, c.pattern);
            const auto current = c.pattern == song.currentPattern;
            const auto selected = isSelected (i);

            g.setColour (colour.withAlpha (0.85f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (selected ? juce::Colour (0xffff6f6f) : current ? juce::Colours::white : colour.darker (0.6f));
            g.drawRoundedRectangle (r, 3.0f, selected ? 2.0f : current ? 1.5f : 1.0f);

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

        if (! rubberBand.isEmpty())
        {
            g.setColour (theme::accentBlue.withAlpha (0.15f));
            g.fillRect (rubberBand);
            g.setColour (theme::accentBlue);
            g.drawRect (rubberBand);
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

        if (song.hasLoop())
        {
            const auto lx0 = (float) tickToX ((double) song.loopStart), lx1 = (float) tickToX ((double) song.loopEnd);
            g.setColour (theme::accentGreen.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (lx0, 0.0f, lx1 - lx0, 6.0f));
        }

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

            const auto muted = song.isTrackMuted (t);
            const auto custom = t < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) t].name.empty();
            g.setColour (theme::channelButton.darker (muted ? 0.4f : 0.0f));
            g.fillRect (2, y + 1, headerWidth - 4, trackHeight - 3);
            g.setColour (theme::muteLed (muted));
            g.fillEllipse (6.0f, (float) y + trackHeight / 2.0f - 5.0f, 10.0f, 10.0f);
            g.setColour (theme::text.withAlpha (muted ? 0.4f : 0.8f));
            g.drawFittedText (custom ? juce::String::fromUTF8 (song.playlistTracks[(std::size_t) t].name.c_str())
                                     : "Track " + juce::String (t + 1),
                              20, y, headerWidth - 24, trackHeight, juce::Justification::centredLeft, 2, 0.8f);
        }
    }

    g.setColour (theme::toolbar);
    g.fillRect (0, 0, headerWidth, timelineHeight);
}

const std::vector<float>* PlaylistComponent::waveformFor (const std::string& path)
{
    if (auto it = waveforms.find (path); it != waveforms.end())
        return it->second.empty() ? nullptr : &it->second;

    auto& peaks = waveforms[path]; // vazio = não deu para ler (não tenta de novo)

    if (auto data = engine.getSampleLibrary().load (juce::String::fromUTF8 (path.c_str())))
    {
        const auto frames = data->frames();
        peaks.assign ((frames + waveformBucket - 1) / waveformBucket, 0.0f);

        for (std::size_t i = 0; i < frames; ++i)
        {
            auto v = std::abs (data->leftData()[i]);
            if (data->isStereo())
                v = std::max (v, std::abs (data->rightData()[i]));
            auto& peak = peaks[i / waveformBucket];
            peak = std::max (peak, v);
        }

        waveformRates[path] = data->sampleRate;

        // normaliza pelo pico do arquivo: áudio baixo também fica legível na tela
        const auto loudest = *std::max_element (peaks.begin(), peaks.end());
        if (loudest > 0.0f)
            for (auto& p : peaks)
                p /= loudest;
    }

    return peaks.empty() ? nullptr : &peaks;
}

void PlaylistComponent::paintAudioClip (juce::Graphics& g, const Clip& c, juce::Rectangle<float> r)
{
    const auto& song = engine.getSong();

    if (c.channel < 0 || c.channel >= (int) song.channels.size())
        return;

    const auto& channel = song.channels[(std::size_t) c.channel];
    const auto colour = juce::Colour (0xff4f7f9f); // clips de áudio em azul, como destaque

    g.setColour (colour.withAlpha (0.85f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (colour.darker (0.6f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const auto title = r.withHeight (13.0f);
    g.setColour (colour.darker (0.4f));
    g.fillRoundedRectangle (title, 3.0f);

    if (r.getWidth() > 24)
    {
        g.setColour (juce::Colours::white);
        g.drawText (juce::String::fromUTF8 (channel.name.c_str()), title.reduced (3, 0).toNearestInt(),
                    juce::Justification::centredLeft, true);
    }

    const auto* peaks = waveformFor (channel.samplePath);

    if (peaks == nullptr)
        return;

    // forma de onda: cada coluna de pixel mostra o pico do trecho de áudio que ela cobre
    const auto body = r.withTrimmedTop (14.0f).reduced (1.0f, 1.0f);
    const auto rate = waveformRates[channel.samplePath];
    const auto secondsPerTick = 60.0 / (song.bpm * ppq);
    const auto centreY = body.getCentreY();
    const auto halfHeight = body.getHeight() / 2.0f;
    const auto clip = g.getClipBounds().toFloat();
    g.setColour (colour.darker (0.8f).withAlpha (0.9f));

    for (auto x = std::max (body.getX(), clip.getX()); x < std::min (body.getRight(), clip.getRight()); x += 1.0f)
    {
        const auto tickInClip = (double) c.offset + (xToTick ((int) x) - (double) c.start);
        const auto tickNext = tickInClip + 1.0 / pixelsPerTick;
        auto from = (std::size_t) std::max (0.0, tickInClip * secondsPerTick * rate / waveformBucket);
        auto to = (std::size_t) std::max (0.0, tickNext * secondsPerTick * rate / waveformBucket);

        if (from >= peaks->size())
            break;

        to = std::min (std::max (to, from + 1), peaks->size());
        float peak = 0.0f;
        for (auto i = from; i < to; ++i)
            peak = std::max (peak, (*peaks)[i]);

        const auto h = std::min (1.0f, peak) * halfHeight;
        g.drawVerticalLine ((int) x, centreY - h, centreY + h);
    }
}

void PlaylistComponent::paintAutomationClip (juce::Graphics& g, const Clip& c, juce::Rectangle<float> r)
{
    const auto& song = engine.getSong();

    if (c.automation < 0 || c.automation >= (int) song.automations.size())
        return;

    const auto& automation = song.automations[(std::size_t) c.automation];
    const auto colour = automation.target.kind == AutomationTargetKind::none ? juce::Colour (0xff555555) : juce::Colour (0xff5f7a5a);

    g.setColour (colour.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (colour.darker (0.6f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const auto title = r.withHeight (13.0f);
    g.setColour (colour.darker (0.4f));
    g.fillRoundedRectangle (title, 3.0f);

    if (r.getWidth() > 24)
    {
        g.setColour (juce::Colours::white);
        g.drawText (juce::String::fromUTF8 (automation.name.c_str()), title.reduced (3, 0).toNearestInt(), juce::Justification::centredLeft, true);
    }

    // curva da automação no trecho do clip
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

    g.setColour (juce::Colour (0xffb8f0a8));
    g.strokePath (curve, juce::PathStrokeType (1.5f));
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

void PlaylistComponent::mouseDown (const juce::MouseEvent& e)
{
    // cabeçalho da trilha: LED muta; clique direito renomeia
    if (e.x < headerWidth && e.y >= timelineHeight && e.y < gridArea().getBottom())
    {
        const auto track = yToTrack (e.y);

        if (e.mods.isPopupMenu())
            renameTrack (track);
        else if (e.x < 20)
            engine.editSong ([track] (Song& s) {
                if ((int) s.playlistTracks.size() <= track)
                    s.playlistTracks.resize ((std::size_t) track + 1);
                s.playlistTracks[(std::size_t) track].muted = ! s.playlistTracks[(std::size_t) track].muted;
            });
        return;
    }

    if (e.y < timelineHeight && e.x >= headerWidth)
    {
        if (e.mods.isPopupMenu())
        {
            engine.editSong ([] (Song& s) { s.loopStart = s.loopEnd = 0; }, false);
            return;
        }

        drag = Drag::seek;
        loopAnchor = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), ! e.mods.isAltDown()));
        seekTo (e.x);
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    bool rightEdge = false;
    const auto hit = clipAt (e.x, e.y, rightEdge);

    if (e.mods.isRightButtonDown())
    {
        // na faixa do nome: menu do clip; no corpo: apaga
        if (hit >= 0 && e.y < trackToY (engine.getSong().playlist[(std::size_t) hit].track) + 15)
        {
            showClipMenu (hit);
            return;
        }

        drag = Drag::erase;
        setSelection ({}); // os índices mudam enquanto apaga

        if (hit >= 0)
            engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
        return;
    }

    if (e.mods.isCtrlDown() || (e.mods.isShiftDown() && hit < 0))
    {
        drag = Drag::select;
        selectionBefore = e.mods.isShiftDown() ? selection : std::vector<int> {};
        rubberBand = { e.x, e.y, 0, 0 };
        setSelection (selectionBefore);
        return;
    }

    if (hit >= 0 && e.mods.isShiftDown())
    {
        auto s = selection;

        if (isSelected (hit))
            s.erase (std::find (s.begin(), s.end(), hit));
        else
            s.push_back (hit);

        setSelection (s);
        return;
    }

    const auto snapOn = ! e.mods.isAltDown();

    if (hit >= 0)
    {
        if (! isSelected (hit))
            setSelection ({});

        const auto& playlist = engine.getSong().playlist;
        const auto& c = playlist[(std::size_t) hit];
        dragClip = hit;
        dragIndices = selection.empty() ? std::vector<int> { hit } : selection;
        dragOriginals.clear();

        for (auto i : dragIndices)
            dragOriginals.push_back (playlist[(std::size_t) i]);

        drag = rightEdge ? Drag::resize : Drag::move;
        grabOffset = c.start - snap (xToTick (e.x), snapOn);

        if (c.kind == ClipKind::pattern && c.pattern != engine.getSong().currentPattern)
            engine.editSong ([p = c.pattern] (Song& s) { s.currentPattern = p; }, false);
        return;
    }

    // pinta a pattern atual
    setSelection ({});
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
    dragIndices = { dragClip };
    dragOriginals = { c };
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
            if (std::abs (e.getDistanceFromDragStartX()) < 5)
            {
                seekTo (e.x);
                return;
            }

            drag = Drag::loop;
            [[fallthrough]];

        case Drag::loop:
        {
            const auto here = juce::jmax<std::int64_t> (0, snap (xToTick (e.x) + (snapOn ? ppq / 2.0 : 0.0), snapOn));
            const auto from = juce::jmin (loopAnchor, here), to = juce::jmax (loopAnchor, here);

            engine.editSong ([from, to] (Song& s) {
                s.loopStart = from;
                s.loopEnd = to;
                s.mode = PlayMode::song;
            }, false);
            return;
        }

        case Drag::select:
        {
            rubberBand = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition()).getIntersection (gridArea());
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
            bool edge = false;
            const auto hit = clipAt (e.x, e.y, edge);

            if (hit >= 0)
                engine.editSong ([hit] (Song& s) { s.playlist.erase (s.playlist.begin() + hit); });
            return;
        }

        case Drag::move:
        {
            const auto grabbedAt = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragClip) - dragIndices.begin());
            const auto& grabbed = dragOriginals[grabbedAt];
            auto deltaTicks = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) + grabOffset) - grabbed.start;
            auto deltaTracks = yToTrack (e.y) - grabbed.track;

            // o grupo mantém a forma: limita pelo clip mais à esquerda / mais acima
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
            const auto minimum = snapOn ? (std::int64_t) ppq : (std::int64_t) 10;
            const auto end = snapOn ? snap (xToTick (e.x) + ppq / 2.0, true) : (std::int64_t) xToTick (e.x);
            const auto delta = juce::jmax (minimum, end - grabbed.start) - grabbed.length;

            if (engine.getSong().playlist[(std::size_t) dragClip].length == grabbed.length + delta)
                return;

            engine.editSong ([this, delta, minimum] (Song& s) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                    s.playlist[(std::size_t) dragIndices[j]].length = juce::jmax (minimum, dragOriginals[j].length + delta);
            });
            return;
        }
    }
}

void PlaylistComponent::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::move || drag == Drag::resize)
        sortClips();

    drag = Drag::none;
    dragClip = -1;
    dragIndices.clear();
    dragOriginals.clear();
    rubberBand = {};
    knownClipCount = engine.getSong().playlist.size();
    repaint();
}

void PlaylistComponent::renameTrack (int track)
{
    const auto& tracks = engine.getSong().playlistTracks;
    const auto current = track < (int) tracks.size() ? juce::String::fromUTF8 (tracks[(std::size_t) track].name.c_str()) : juce::String();
    auto* window = new juce::AlertWindow ("Renomear trilha", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", current.isNotEmpty() ? current : "Track " + juce::String (track + 1));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
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

void PlaylistComponent::showClipMenu (int index)
{
    const auto& clip = engine.getSong().playlist[(std::size_t) index];
    const auto targets = isSelected (index) ? selection : std::vector<int> { index };
    juce::PopupMenu menu;

    // dividir no ponto clicado (na grade de 1 tempo)
    const auto splitAt = snap (xToTick (getMouseXYRelative().x) + ppq / 2.0, true);

    if (splitAt > clip.start && splitAt < clip.end())
        menu.addItem (utf8 ("Dividir aqui"), [this, index, splitAt] {
            setSelection ({});
            engine.editSong ([index, splitAt] (Song& s) {
                auto rest = splitClip (s.playlist[(std::size_t) index], splitAt);
                s.playlist.push_back (rest);
            });
            sortClips();
        });

    if (clip.kind == ClipKind::pattern)
    {
        menu.addSectionHeader (juce::String::fromUTF8 (engine.getSong().patterns[(std::size_t) clip.pattern].name.c_str()));
        // como o "Make unique" do FL: os clips escolhidos passam a usar uma cópia da pattern
        menu.addItem (utf8 ("Tornar única"), [this, targets] {
            engine.editSong ([targets] (Song& s) {
                std::map<int, int> copies; // pattern original -> cópia

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
        menu.addItem ("Selecionar todos desta pattern", [this, p = clip.pattern] {
            std::vector<int> same;
            const auto& playlist = engine.getSong().playlist;

            for (int i = 0; i < (int) playlist.size(); ++i)
                if (playlist[(std::size_t) i].kind == ClipKind::pattern && playlist[(std::size_t) i].pattern == p)
                    same.push_back (i);

            setSelection (same);
        });
        menu.addItem ("Abrir no Channel Rack", [this] {
            if (onOpenPattern)
                onOpenPattern();
        });
    }

    if (clip.kind == ClipKind::automation && clip.automation >= 0 && clip.automation < (int) engine.getSong().automations.size())
    {
        menu.addSectionHeader (juce::String::fromUTF8 (engine.getSong().automations[(std::size_t) clip.automation].name.c_str()));
        menu.addItem (utf8 ("Editar pontos..."), [this, a = clip.automation] { openAutomationEditor (engine, a); });
        menu.addItem (utf8 ("Tornar única"), [this, index] {
            engine.editSong ([index] (Song& s) {
                auto& c = s.playlist[(std::size_t) index];
                auto copy = s.automations[(std::size_t) c.automation];
                copy.name += " #2";
                s.automations.push_back (std::move (copy));
                c.automation = (int) s.automations.size() - 1;
            });
        });
        menu.addItem (utf8 ("Apagar a automação (todos os clips dela)"), [this, a = clip.automation] {
            setSelection ({});
            engine.editSong ([a] (Song& s) { s.removeAutomation (a); });
        });
    }

    menu.addItem (targets.size() > 1 ? "Apagar selecionados" : "Apagar", [this, targets] {
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

    // cola no cursor de reprodução (ou onde o primeiro clip estava), mantendo trilhas e distâncias
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
    if (selection.empty())
        return;

    const auto& song = engine.getSong();
    const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);
    std::int64_t from = INT64_MAX, to = 0;

    for (auto i : selection)
    {
        from = juce::jmin (from, song.playlist[(std::size_t) i].start);
        to = juce::jmax (to, song.playlist[(std::size_t) i].end());
    }

    const auto offset = juce::jmax (bar, (to - from + bar - 1) / bar * bar); // como o Ctrl+B do FL
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
    const auto cmd = key.getModifiers().isCommandDown();
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
        std::vector<int> all ((std::size_t) engine.getSong().playlist.size());
        for (std::size_t i = 0; i < all.size(); ++i)
            all[i] = (int) i;
        setSelection (all);
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
        pasteClips();
        return true;
    }

    if (cmd && code == 'B')
    {
        duplicateSelection();
        return true;
    }

    return false;
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
    const auto hit = gridArea().contains (e.getPosition()) ? clipAt (e.x, e.y, edge) : -1;

    if (hit < 0)
        return;

    const auto& clip = engine.getSong().playlist[(std::size_t) hit];

    if (clip.kind == ClipKind::automation)
        openAutomationEditor (engine, clip.automation);
    else if (clip.kind == ClipKind::pattern && onOpenPattern)
        onOpenPattern();
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

        // duração do áudio convertida em ticks no BPM atual
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

        ++track; // vários arquivos: um embaixo do outro
    }
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
    // a lista de clips mudou por fora (desfazer, remover pattern...): a seleção perde o sentido
    const auto count = engine.getSong().playlist.size();

    if (count != knownClipCount && drag == Drag::none)
    {
        knownClipCount = count;
        selection.clear();
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

bool PlaylistComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString() == BrowserComponent::dragDescription;
}

void PlaylistComponent::itemDropped (const SourceDetails& details)
{
    if (auto* tree = dynamic_cast<juce::FileTreeComponent*> (details.sourceComponent.get()))
        if (tree->getSelectedFile().existsAsFile())
            filesDropped ({ tree->getSelectedFile().getFullPathName() }, details.localPosition.x, details.localPosition.y);
}
} // namespace lynx
