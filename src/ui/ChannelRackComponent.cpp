#include "ui/ChannelRackComponent.h"

#include "ui/AutomationUi.h"
#include "ui/ChannelSettings.h"

#include "engine/Text.h"

#include "ui/PluginWindow.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
juce::String instrumentName (InstrumentKind k)
{
    switch (k)
    {
        case InstrumentKind::kick: return "Kick";
        case InstrumentKind::snare: return "Snare";
        case InstrumentKind::hat: return "Hat";
        case InstrumentKind::clap: return "Clap";
        case InstrumentKind::synth: return "Synth";
        case InstrumentKind::sampler: return "Sampler";
        case InstrumentKind::audioClip: return utf8 ("Clip de áudio");
        case InstrumentKind::plugin: return "Plugin";
    }
    return "?";
}

constexpr InstrumentKind builtInKinds[] = { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat,
                                            InstrumentKind::clap, InstrumentKind::synth };

void setStep (Pattern& p, int channel, int step, bool on)
{
    if (p.hasStep (channel, step) != on)
        p.toggleStep (channel, step);
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> r, float value01, bool bipolar, juce::Colour colour)
{
    const auto size = juce::jmin (r.getWidth(), r.getHeight()) - 6.0f;
    const auto area = r.withSizeKeepingCentre (size, size);
    const auto centre = area.getCentre();
    const auto radius = size / 2.0f;
    constexpr auto start = juce::MathConstants<float>::pi * 1.25f;
    constexpr auto range = juce::MathConstants<float>::pi * 1.5f;

    g.setColour (theme::knobBackground);
    g.fillEllipse (area);

    const auto from = bipolar ? start + range * 0.5f : start;
    const auto to = start + range * value01;
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - 1.5f, radius - 1.5f, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
    g.setColour (colour);
    g.strokePath (arc, juce::PathStrokeType (2.5f));

    // ângulos do JUCE: 0 = meio-dia, sentido horário
    const auto length = radius - 3.0f;
    g.setColour (juce::Colours::white);
    g.drawLine (centre.x, centre.y, centre.x + std::sin (to) * length, centre.y - std::cos (to) * length, 1.5f);
}
} // namespace

ChannelRackComponent::ChannelRackComponent (AudioEngine& e)
    : engine (e)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
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
    return juce::jlimit (16, 512, steps);
}

void ChannelRackComponent::updateSize()
{
    const auto rows = (int) engine.getSong().channels.size() + 1; // +1 = linha de "adicionar canal"
    setSize (gridLeft() + numSteps() * stepWidth + 8, rows * rowHeight + 8);
}

juce::Rectangle<int> ChannelRackComponent::rowBounds (int row) const
{
    return { 0, 4 + row * rowHeight, getWidth(), rowHeight };
}

juce::Rectangle<int> ChannelRackComponent::stepBounds (int row, int step) const
{
    return { gridLeft() + step * stepWidth + 1, 4 + row * rowHeight + 4, stepWidth - 3, rowHeight - 8 };
}

ChannelRackComponent::Area ChannelRackComponent::areaAt (int x, int y, int& row) const
{
    const auto channels = (int) engine.getSong().channels.size();
    row = (y - 4) / rowHeight;

    if (y < 4 || row > channels)
        return Area::none;

    if (row == channels)
        return x < gridLeft() ? Area::addRow : Area::none;

    if (x < muteWidth)
        return Area::mute;
    if (x < muteWidth + knobWidth)
        return Area::pan;
    if (x < muteWidth + 2 * knobWidth)
        return Area::volume;
    if (x < nameLeft())
        return Area::route;
    if (x < gridLeft())
        return Area::name;

    return Area::grid;
}

bool ChannelRackComponent::cellHasNote (int channel, int step) const
{
    const auto& song = engine.getSong();
    const auto& p = song.patterns[(std::size_t) song.currentPattern];
    const auto from = (std::int64_t) step * ticksPerStep;
    const auto to = from + ticksPerStep;

    for (const auto& n : p.notes)
        if (n.channel == channel && n.start >= from && n.start < to)
            return true;

    return false;
}

void ChannelRackComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);

    const auto& song = engine.getSong();

    if (song.patterns.empty())
        return;

    const auto& pattern = song.patterns[(std::size_t) song.currentPattern];
    const auto steps = numSteps();
    const auto clip = g.getClipBounds();
    const auto selected = engine.getSelectedChannel();
    g.setFont (juce::FontOptions (14.0f));

    for (int row = 0; row < (int) song.channels.size(); ++row)
    {
        const auto y = 4 + row * rowHeight;

        if (y > clip.getBottom() || y + rowHeight < clip.getY())
            continue;

        const auto& channel = song.channels[(std::size_t) row];
        const auto nameArea = juce::Rectangle<float> ((float) nameLeft(), (float) y + 3, (float) nameWidth, (float) rowHeight - 6);

        g.setColour (theme::muteLed (channel.muted));
        g.fillEllipse ((float) 9, (float) y + rowHeight / 2.0f - 5, 10, 10);

        drawKnob (g, { (float) muteWidth, (float) y, (float) knobWidth, (float) rowHeight }, (channel.pan + 1.0f) / 2.0f,
                  true, theme::accentBlue);
        drawKnob (g, { (float) muteWidth + knobWidth, (float) y, (float) knobWidth, (float) rowHeight }, channel.volume,
                  false, theme::accentGreen);

        // caixinha com o insert do mixer ("-" = direto no Master), como no FL
        const auto routeArea = juce::Rectangle<float> ((float) (muteWidth + 2 * knobWidth) + 1, (float) y + 7,
                                                       (float) routeWidth - 3, (float) rowHeight - 14);
        g.setColour (theme::knobBackground);
        g.fillRoundedRectangle (routeArea, 3.0f);
        g.setColour (channel.mixerTrack > 0 ? theme::accentOrange : theme::text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (channel.mixerTrack > 0 ? juce::String (channel.mixerTrack) : juce::String ("-"), routeArea.toNearestInt(),
                    juce::Justification::centred);
        g.setFont (juce::FontOptions (14.0f));

        g.setColour (row == selected ? theme::channelButtonSelected : theme::channelButton);
        g.fillRoundedRectangle (nameArea, 4.0f);

        if (row == selected)
        {
            g.setColour (theme::accentOrange);
            g.fillRoundedRectangle (nameArea.withWidth (4.0f), 2.0f);
        }

        g.setColour (channel.muted ? juce::Colours::grey : juce::Colours::white);
        g.drawText (juce::String::fromUTF8 (channel.name.c_str()), nameArea.reduced (10, 0).toNearestInt(),
                    juce::Justification::centredLeft, true);

        if (channel.instrument == InstrumentKind::audioClip)
        {
            g.setColour (theme::text.withAlpha (0.35f));
            g.drawText (utf8 ("clip de áudio — toca pela Playlist"), gridLeft() + 4, y, 400, rowHeight, juce::Justification::centredLeft);
            g.setFont (juce::FontOptions (14.0f));
            continue;
        }

        for (int step = 0; step < steps; ++step)
        {
            const auto r = stepBounds (row, step);

            if (r.getRight() < clip.getX() || r.getX() > clip.getRight())
                continue;

            const auto onGrid = pattern.hasStep (row, step);
            const auto any = onGrid || cellHasNote (row, step);

            g.setColour (any ? (onGrid ? theme::stepOn : theme::stepOnOffGrid) : theme::stepOff (step));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
        }
    }

    const auto addY = 4 + (int) song.channels.size() * rowHeight;
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawText (utf8 ("+ adicionar canal (ou arraste um arquivo de áudio)"), nameLeft() + 2, addY,
                400, rowHeight, juce::Justification::centredLeft);

    if (playheadStep >= 0 && playheadStep < steps)
    {
        g.setColour (theme::playhead.withAlpha (0.35f));
        g.fillRect (gridLeft() + playheadStep * stepWidth, 0, stepWidth - 2, addY);
    }
}

void ChannelRackComponent::mouseDown (const juce::MouseEvent& e)
{
    int row = 0;
    const auto area = areaAt (e.x, e.y, row);
    const auto& song = engine.getSong();

    switch (area)
    {
        case Area::none:
            return;

        case Area::addRow:
            showAddChannelMenu();
            return;

        case Area::mute:
            if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
            {
                // solo como no FL: só este canal toca; repetir no canal que já está sozinho libera todos
                engine.editSong ([row] (Song& s) {
                    const auto alone = ! s.channels[(std::size_t) row].muted
                                       && std::all_of (s.channels.begin(), s.channels.end(), [&] (const ChannelDef& c) {
                                              return &c == &s.channels[(std::size_t) row] || c.muted;
                                          });

                    for (std::size_t i = 0; i < s.channels.size(); ++i)
                        s.channels[i].muted = alone ? false : i != (std::size_t) row;
                });
            }
            else
                engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].muted = ! s.channels[(std::size_t) row].muted; });
            return;

        case Area::pan:
        case Area::volume:
            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                addAutomationItem (menu, engine, { area == Area::pan ? AutomationTargetKind::channelPan : AutomationTargetKind::channelVolume, row });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
                return;
            }

            knobDrag = area;
            knobRow = row;
            knobStartValue = area == Area::pan ? song.channels[(std::size_t) row].pan : song.channels[(std::size_t) row].volume;
            return;

        case Area::route:
            knobDrag = area;
            knobRow = row;
            routeStartValue = song.channels[(std::size_t) row].mixerTrack;
            return;

        case Area::name:
            engine.setSelectedChannel (row);

            if (e.mods.isPopupMenu())
                showChannelMenu (row);
            else
                engine.sendLiveNote (row, 60, 0.8f, true);
            return;

        case Area::grid:
        {
            const auto step = (e.x - gridLeft()) / stepWidth;
            const auto& pattern = song.patterns[(std::size_t) song.currentPattern];

            if (e.mods.isRightButtonDown())
                paintMode = PaintMode::remove;
            else
                paintMode = pattern.hasStep (row, step) ? PaintMode::remove : PaintMode::add;

            lastPaintedRow = lastPaintedStep = -1;
            applyPaint (e);
            return;
        }
    }
}

void ChannelRackComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (knobDrag != Area::none)
    {
        const auto delta = (float) -e.getDistanceFromDragStartY() / 150.0f;
        const auto row = knobRow;

        if (knobDrag == Area::route)
        {
            const auto tracks = juce::jmax (1, (int) engine.getSong().mixer.size());
            const auto value = juce::jlimit (0, tracks - 1, routeStartValue - e.getDistanceFromDragStartY() / 6);
            if (value != engine.getSong().channels[(std::size_t) row].mixerTrack)
                engine.editSong ([row, value] (Song& s) { s.channels[(std::size_t) row].mixerTrack = value; });
        }
        else if (knobDrag == Area::pan)
        {
            const auto value = juce::jlimit (-1.0f, 1.0f, knobStartValue + delta * 2.0f);
            engine.editSong ([row, value] (Song& s) { s.channels[(std::size_t) row].pan = value; });
        }
        else
        {
            const auto value = juce::jlimit (0.0f, 1.0f, knobStartValue + delta);
            engine.editSong ([row, value] (Song& s) { s.channels[(std::size_t) row].volume = value; });
        }
        return;
    }

    if (paintMode != PaintMode::none)
        applyPaint (e);
}

void ChannelRackComponent::mouseUp (const juce::MouseEvent& e)
{
    int row = 0;

    if (areaAt (e.getMouseDownX(), e.getMouseDownY(), row) == Area::name && ! e.mods.isPopupMenu())
        engine.sendLiveNote (row, 60, 0.0f, false);

    paintMode = PaintMode::none;
    knobDrag = Area::none;
}

void ChannelRackComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    int row = 0;
    const auto area = areaAt (e.x, e.y, row);

    if (area == Area::pan)
        engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].pan = 0.0f; });
    else if (area == Area::volume)
        engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].volume = 0.8f; });
    else if (area == Area::route)
        engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].mixerTrack = s.nextFreeInsert(); });
    else if (area == Area::name && engine.getSong().channels[(std::size_t) row].instrument == InstrumentKind::plugin)
        openPluginWindow (row);
    else if (area == Area::name && onOpenPianoRoll)
        onOpenPianoRoll();
}

void ChannelRackComponent::applyPaint (const juce::MouseEvent& e)
{
    const auto& song = engine.getSong();
    const auto row = (e.y - 4) / rowHeight;
    const auto step = (e.x - gridLeft()) / stepWidth;

    if (row < 0 || row >= (int) song.channels.size() || step < 0 || step >= numSteps() || e.x < gridLeft()
        || song.channels[(std::size_t) row].instrument == InstrumentKind::audioClip)
        return;

    if (row == lastPaintedRow && step == lastPaintedStep)
        return;

    lastPaintedRow = row;
    lastPaintedStep = step;
    const auto on = paintMode == PaintMode::add;

    engine.editSong ([row, step, on] (Song& s) { setStep (s.patterns[(std::size_t) s.currentPattern], row, step, on); });

    if (on)
        engine.sendLiveNote (row, 60, 0.78f, true);
}

void ChannelRackComponent::showChannelMenu (int channel)
{
    const auto& c = engine.getSong().channels[(std::size_t) channel];
    juce::PopupMenu instruments;

    for (auto kind : builtInKinds)
        instruments.addItem (instrumentName (kind), true, c.instrument == kind, [this, channel, kind] {
            engine.editSong ([channel, kind] (Song& s) { s.channels[(std::size_t) channel].instrument = kind; });
        });

    instruments.addSeparator();
    instruments.addItem ("Sampler (escolher arquivo)...", [this, channel] { chooseSample (channel); });

    juce::PopupMenu plugins;
    addPluginItems (plugins, engine, true, [this, channel] (PluginRef ref) {
        engine.editSong ([channel, ref] (Song& s) {
            auto& c = s.channels[(std::size_t) channel];
            c.instrument = InstrumentKind::plugin;
            c.plugin = ref;
            c.name = ref.name;
        });
        openPluginWindow (channel);
    });
    instruments.addSubMenu ("Plugin (VST3/CLAP)", plugins);

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 (c.name.c_str()));

    if (c.instrument == InstrumentKind::sampler)
    {
        menu.addItem (juce::String ("Sample: ") + juce::File (juce::String::fromUTF8 (c.samplePath.c_str())).getFileName(),
                      false, false, [] {});
        menu.addItem (utf8 ("Ajustes do sampler (afinação, envelope, ao contrário)..."),
                      [this, channel] { openChannelSettings (engine, channel); });
    }

    if (c.instrument == InstrumentKind::plugin)
    {
        menu.addItem (utf8 ("Abrir interface do plugin"), [this, channel] { openPluginWindow (channel); });

        if (auto* p = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
            addPluginParameterItems (menu, engine, p->getInstance(), { AutomationTargetKind::channelParam, channel, 0, 0 });
    }

    menu.addItem ("Piano Roll", [this] {
        if (onOpenPianoRoll)
            onOpenPianoRoll();
    });
    menu.addSubMenu ("Instrumento", instruments);
    menu.addItem ("Renomear...", [this, channel] { rename (channel); });
    menu.addItem ("Limpar notas desta pattern", [this, channel] {
        engine.editSong ([channel] (Song& s) {
            auto& notes = s.patterns[(std::size_t) s.currentPattern].notes;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [channel] (const Note& n) { return n.channel == channel; }),
                         notes.end());
        });
    });
    menu.addSeparator();
    menu.addItem ("Remover canal", engine.getSong().channels.size() > 1, false, [this, channel] {
        engine.editSong ([channel] (Song& s) { s.removeChannel (channel); });
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ChannelRackComponent::showAddChannelMenu()
{
    juce::PopupMenu menu;

    for (auto kind : builtInKinds)
        menu.addItem (instrumentName (kind), [this, kind] {
            engine.editSong ([kind] (Song& s) {
                ChannelDef c;
                c.name = instrumentName (kind).toStdString();
                c.instrument = kind;
                c.mixerTrack = s.nextFreeInsert();
                s.channels.push_back (c);
            });
            engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
        });

    menu.addSeparator();
    menu.addItem ("Sampler (escolher arquivo)...", [this] { chooseSample (-1); });

    juce::PopupMenu plugins;
    addPluginItems (plugins, engine, true, [this] (PluginRef ref) {
        engine.editSong ([ref] (Song& s) {
            ChannelDef c;
            c.name = ref.name;
            c.instrument = InstrumentKind::plugin;
            c.plugin = ref;
            c.mixerTrack = s.nextFreeInsert();
            s.channels.push_back (c);
        });
        const auto added = (int) engine.getSong().channels.size() - 1;
        engine.setSelectedChannel (added);
        openPluginWindow (added); // como no FL: o plugin abre ao ser adicionado
    });
    menu.addSubMenu ("Plugin (VST3/CLAP)", plugins);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ChannelRackComponent::chooseSample (int channel)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Escolher sample", juce::File(), engine.getSampleLibrary().wildcard());
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, channel] (const juce::FileChooser& chooser) {
                                  if (chooser.getResult().existsAsFile())
                                      assignSample (channel, chooser.getResult());
                              });
}

void ChannelRackComponent::assignSample (int channel, const juce::File& file)
{
    if (engine.getSampleLibrary().load (file.getFullPathName()) == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sample",
                                                utf8 ("Não consegui ler ") + file.getFileName());
        return;
    }

    const auto path = file.getFullPathName().toStdString();
    const auto name = file.getFileNameWithoutExtension().toStdString();

    engine.editSong ([channel, path, name] (Song& s) {
        const auto isNew = channel < 0 || channel >= (int) s.channels.size();
        const auto freeInsert = s.nextFreeInsert();
        auto& c = isNew ? s.channels.emplace_back() : s.channels[(std::size_t) channel];

        if (isNew)
            c.mixerTrack = freeInsert;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = path;
        c.name = name;
    });

    if (channel < 0)
        engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
}

void ChannelRackComponent::openPluginWindow (int channel)
{
    auto instrument = engine.getChannelInstrument (channel);

    if (auto* plugin = dynamic_cast<PluginInstrument*> (instrument.get()))
    {
        PluginWindows::open (instrument, plugin->getInstance(),
                             juce::String::fromUTF8 (engine.getSong().channels[(std::size_t) channel].name.c_str()));
        return;
    }

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugin",
                                            utf8 ("Este plugin não pôde ser carregado (não está instalado ou não foi escaneado)."));
}

void ChannelRackComponent::rename (int channel)
{
    auto* window = new juce::AlertWindow ("Renomear canal", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String::fromUTF8 (engine.getSong().channels[(std::size_t) channel].name.c_str()));
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, channel, window] (int result) {
                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();

                                 if (result == 1 && ! name.empty() && channel < (int) engine.getSong().channels.size())
                                     engine.editSong ([channel, name] (Song& s) { s.channels[(std::size_t) channel].name = name; });
                             }),
                             true);
}

bool ChannelRackComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            return true;

    return false;
}

void ChannelRackComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    int row = 0;
    const auto area = areaAt (x, y, row);
    auto target = (area == Area::none || area == Area::addRow) ? -1 : row;

    for (const auto& f : files)
    {
        const juce::File file (f);

        if (! file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
            continue;

        assignSample (target, file);
        target = -1; // os próximos arquivos viram canais novos
    }
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

    if (transport.playing.load() && engine.getSong().mode == PlayMode::pattern)
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
} // namespace lynx
