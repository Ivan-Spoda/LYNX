#include "ui/ChannelRackComponent.h"

#include "ui/AutomationUi.h"
#include "core/Slicer.h"
#include "ui/ChannelSettings.h"

#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/BrowserComponent.h"

#include "ui/PluginWindow.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// English name (used for new channel names stored in the song)
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
        case InstrumentKind::audioClip: return "Audio clip";
        case InstrumentKind::plugin: return "Plugin";
        case InstrumentKind::controller: return "Controller";
        case InstrumentKind::sakura: return "Sakura";
        case InstrumentKind::drumpad: return "Drumpad";
        case InstrumentKind::boobass: return "BooBass";
        case InstrumentKind::drumsynth: return "DrumSynth Live";
        case InstrumentKind::harmless: return "Harmless";
        case InstrumentKind::sytrus: return "Sytrus";
    }
    return "?";
}

// name shown in menus (translated)
juce::String instrumentLabel (InstrumentKind k)
{
    switch (k)
    {
        case InstrumentKind::kick: return tr ("Kick");
        case InstrumentKind::snare: return tr ("Snare");
        case InstrumentKind::hat: return tr ("Hat");
        case InstrumentKind::clap: return tr ("Clap");
        case InstrumentKind::synth: return tr ("Synth");
        case InstrumentKind::sampler: return tr ("Sampler");
        case InstrumentKind::audioClip: return tr ("Audio clip");
        case InstrumentKind::controller: return tr ("Controller");
        default: return instrumentName (k);
    }
}

constexpr InstrumentKind builtInKinds[] = { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat,
                                            InstrumentKind::clap, InstrumentKind::synth };

void setStep (Pattern& p, int channel, int step, bool on)
{
    if (p.hasStep (channel, step) != on)
        p.toggleStep (channel, step);
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
    const auto rows = (int) engine.getSong().channels.size() + 1; // +1 = "add channel" row
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
    using namespace theme::colour;
    g.fillAll (bg2);

    const auto& song = engine.getSong();

    if (song.patterns.empty())
        return;

    const auto& pattern = song.patterns[(std::size_t) song.currentPattern];
    const auto steps = numSteps();
    const auto clip = g.getClipBounds();
    const auto selected = engine.getSelectedChannel();
    g.setFont (theme::font (theme::type::body));

    for (int row = 0; row < (int) song.channels.size(); ++row)
    {
        const auto y = 4 + row * rowHeight;

        if (y > clip.getBottom() || y + rowHeight < clip.getY())
            continue;

        const auto& channel = song.channels[(std::size_t) row];
        const auto nameArea = juce::Rectangle<float> ((float) nameLeft(), (float) y + 3, (float) nameWidth, (float) rowHeight - 6);

        if (row == selected) // the whole row is subtly lifted
        {
            g.setColour (bg3);
            g.fillRect (0, y + 1, getWidth(), rowHeight - 2);
        }

        g.setColour (theme::muteLed (channel.muted));
        g.fillEllipse ((float) 9, (float) y + rowHeight / 2.0f - 4.5f, 9, 9);

        theme::drawKnob (g, juce::Rectangle<float> ((float) muteWidth, (float) y, (float) knobWidth, (float) rowHeight).reduced (1.0f, 2.0f),
                         (channel.pan + 1.0f) / 2.0f, true, info);
        theme::drawKnob (g, juce::Rectangle<float> ((float) muteWidth + knobWidth, (float) y, (float) knobWidth, (float) rowHeight).reduced (1.0f, 2.0f),
                         channel.volume, false, knobArc);

        // small box with the mixer insert ("-" = straight to Master), as in FL
        const auto routeArea = juce::Rectangle<float> ((float) (muteWidth + 2 * knobWidth) + 1, (float) y + 7,
                                                       (float) routeWidth - 3, (float) rowHeight - 14);
        g.setColour (bg0);
        g.fillRoundedRectangle (routeArea, theme::radius::sm);
        g.setColour (channel.mixerTrack > 0 ? textPrimary : textDisabled);
        g.setFont (theme::font (theme::type::caption, true));
        g.drawText (channel.mixerTrack > 0 ? juce::String (channel.mixerTrack) : juce::String ("-"), routeArea.toNearestInt(),
                    juce::Justification::centred);
        g.setFont (theme::font (theme::type::body));

        // name button: neutral surface with the channel colour (from the .flp or user-picked) as a strip
        g.setColour (row == selected ? bg6 : bg4);
        g.fillRoundedRectangle (nameArea, theme::radius::md);

        if (row == selected)
        {
            g.setColour (accent);
            g.drawRoundedRectangle (nameArea.reduced (0.5f), theme::radius::md, 1.0f);
        }

        g.setColour (channel.colour != 0 ? theme::tame (juce::Colour (channel.colour)) : bg6.brighter (0.2f));
        g.fillRoundedRectangle (nameArea.withWidth (4.0f).reduced (0.0f, 3.0f).translated (3.0f, 0.0f), 1.5f);

        g.setColour (channel.muted ? textDisabled : textPrimary);
        g.drawText (juce::String::fromUTF8 (channel.name.c_str()), nameArea.withTrimmedLeft (14).withTrimmedRight (6).toNearestInt(),
                    juce::Justification::centredLeft, true);

        if (channel.instrument == InstrumentKind::audioClip)
        {
            g.setColour (textDisabled);
            g.setFont (theme::font (theme::type::small));
            g.drawText (tr ("audio clip — plays from the Playlist"), gridLeft() + 4, y, 400, rowHeight, juce::Justification::centredLeft);
            g.setFont (theme::font (theme::type::body));
            continue;
        }

        for (int step = 0; step < steps; ++step)
        {
            const auto r = stepBounds (row, step);

            if (r.getRight() < clip.getX() || r.getX() > clip.getRight())
                continue;

            const auto onGrid = pattern.hasStep (row, step);
            const auto any = onGrid || cellHasNote (row, step);

            g.setColour (any ? (onGrid ? stepOn : stepNote) : theme::stepOff (step));
            g.fillRoundedRectangle (r.toFloat(), theme::radius::sm);
        }
    }

    const auto addY = 4 + (int) song.channels.size() * rowHeight;
    {
        const auto addArea = juce::Rectangle<float> ((float) nameLeft(), (float) addY + 3, (float) nameWidth, (float) rowHeight - 6);
        const float dash[] = { 3.0f, 3.0f };
        juce::Path outline, dashed;
        outline.addRoundedRectangle (addArea.reduced (0.5f), theme::radius::md);
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
        g.setColour (borderStrong);
        g.fillPath (dashed);
        g.setColour (textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (tr ("+ add channel"), addArea.toNearestInt(), juce::Justification::centred);
        g.setColour (textDisabled);
        g.drawText (tr ("or drop an audio file here"), gridLeft() + 4, addY, 400, rowHeight, juce::Justification::centredLeft);
    }

    if (playheadStep >= 0 && playheadStep < steps)
    {
        g.setColour (playhead.withAlpha (0.28f));
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) (gridLeft() + playheadStep * stepWidth), 2.0f, (float) stepWidth - 2.0f,
                                                        (float) addY - 2.0f), theme::radius::sm);
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
                // solo as in FL: only this channel plays; repeating on the already-soloed channel unmutes all
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
        instruments.addItem (instrumentLabel (kind), true, c.instrument == kind, [this, channel, kind] {
            engine.editSong ([channel, kind] (Song& s) { s.channels[(std::size_t) channel].instrument = kind; });
        });

    instruments.addSeparator();
    instruments.addItem (tr ("Sampler (choose file)..."), [this, channel] { chooseSample (channel); });

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
    instruments.addSubMenu (tr ("Plugin (VST3/CLAP)"), plugins);

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 (c.name.c_str()));

    if (c.instrument == InstrumentKind::sampler)
    {
        menu.addItem (tr ("Sample: {0}", { juce::File (juce::String::fromUTF8 (c.samplePath.c_str())).getFileName() }),
                      false, false, [] {});
        menu.addItem (tr ("Sampler settings (pitch, envelope, reverse)..."),
                      [this, channel] { openChannelSettings (engine, channel); });

        if (c.sampler.slices.empty())
            menu.addItem (tr ("Slice sample (slicer): one slice per key and a pattern with the loop"), [this, channel] { sliceChannel (channel); });
        else
            menu.addItem (tr ("Undo slices ({0})", { juce::String ((int) c.sampler.slices.size()) }), [this, channel] {
                engine.editSong ([channel] (Song& s) { s.channels[(std::size_t) channel].sampler.slices.clear(); });
            });
    }

    if (c.instrument == InstrumentKind::synth)
        menu.addItem (tr ("Synth settings (oscillators, envelope, filter)..."), [this, channel] { openChannelSettings (engine, channel); });

    if (c.instrument == InstrumentKind::plugin)
    {
        menu.addItem (tr ("Open plugin editor"), [this, channel] { openPluginWindow (channel); });

        if (auto* p = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
            addPluginParameterItems (menu, engine, p->getInstance(), { AutomationTargetKind::channelParam, channel, 0, 0 });
    }

    menu.addItem (tr ("Piano Roll"), [this] {
        if (onOpenPianoRoll)
            onOpenPianoRoll();
    });
    menu.addSubMenu (tr ("Instrument"), instruments);
    {
        // channel arpeggiator
        const auto arp = c.arp;
        auto set = [this, channel] (std::function<void (ArpSettings&)> change) {
            engine.editSong ([channel, change] (Song& s) { change (s.channels[(std::size_t) channel].arp); });
        };
        juce::PopupMenu arpMenu;
        const char* directions[] = { TR_NOOP ("Off"), TR_NOOP ("Up"), TR_NOOP ("Down"), TR_NOOP ("Up and down"), TR_NOOP ("Random") };

        for (int d = 0; d < 5; ++d)
            arpMenu.addItem (tr (directions[d]), true, arp.direction == d, [set, d] { set ([d] (ArpSettings& a) { a.direction = d; }); });

        arpMenu.addSeparator();
        juce::PopupMenu range, speed, gate, repeat;

        for (int o = 1; o <= 4; ++o)
            range.addItem (o == 1 ? tr ("1 octave") : tr ("{0} octaves", { juce::String (o) }), true, arp.range == o, [set, o] { set ([o] (ArpSettings& a) { a.range = o; }); });

        const std::pair<const char*, float> speeds[] = { { "1/4", 1.0f }, { "1/8", 0.5f }, { TR_NOOP ("1/8 triplet"), 1.0f / 3.0f },
                                                          { "1/16", 0.25f }, { TR_NOOP ("1/16 triplet"), 1.0f / 6.0f }, { "1/32", 0.125f } };
        for (const auto& [name, beats] : speeds)
            speed.addItem (tr (name), true, std::abs (arp.stepBeats - beats) < 1e-4f, [set, b = beats] { set ([b] (ArpSettings& a) { a.stepBeats = b; }); });

        for (int g : { 25, 50, 75, 100 })
            gate.addItem (juce::String (g) + "%", true, std::abs (arp.gate - g / 100.0f) < 1e-4f, [set, g] { set ([g] (ArpSettings& a) { a.gate = g / 100.0f; }); });

        for (int n : { 1, 2, 3, 4 })
            repeat.addItem (juce::String (n) + "x", true, arp.repeat == n, [set, n] { set ([n] (ArpSettings& a) { a.repeat = n; }); });

        arpMenu.addSubMenu (tr ("Range"), range);
        arpMenu.addSubMenu (tr ("Speed"), speed);
        arpMenu.addSubMenu (tr ("Gate"), gate);
        arpMenu.addSubMenu (tr ("Repeat"), repeat);
        menu.addSubMenu (arp.enabled() ? tr ("Arpeggiator (on)") : tr ("Arpeggiator"), arpMenu);
    }

    menu.addItem (tr ("Rename..."), [this, channel] { rename (channel); });
    {
        juce::PopupMenu colours;
        colours.addItem (tr ("Default"), [this, channel] { engine.editSong ([channel] (Song& s) { s.channels[(std::size_t) channel].colour = 0; }); });

        for (auto c : { 0xff485156u, 0xffa14547u, 0xffa16f45u, 0xffa19d45u, 0xff6ca145u, 0xff45a175u, 0xff45a1a0u, 0xff457ca1u,
                        0xff4f45a1u, 0xff8a5096u, 0xffa1458au })
        {
            juce::Image swatch (juce::Image::ARGB, 16, 16, true);
            juce::Graphics (swatch).fillAll (juce::Colour (c));
            juce::PopupMenu::Item item;
            item.text = "#" + juce::String::toHexString ((juce::int64) (c & 0xFFFFFF)).paddedLeft ('0', 6).toUpperCase();
            item.image = std::make_unique<juce::DrawableImage> (swatch);
            item.action = [this, channel, c] { engine.editSong ([channel, c] (Song& s) { s.channels[(std::size_t) channel].colour = c; }); };
            colours.addItem (std::move (item));
        }

        menu.addSubMenu (tr ("Color"), colours);
    }
    menu.addItem (tr ("Clear notes in this pattern"), [this, channel] {
        engine.editSong ([channel] (Song& s) {
            auto& notes = s.patterns[(std::size_t) s.currentPattern].notes;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [channel] (const Note& n) { return n.channel == channel; }),
                         notes.end());
        });
    });
    menu.addSeparator();
    menu.addItem (tr ("Delete channel"), engine.getSong().channels.size() > 1, false, [this, channel] {
        engine.editSong ([channel] (Song& s) { s.removeChannel (channel); });
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ChannelRackComponent::showAddChannelMenu()
{
    juce::PopupMenu menu;

    for (auto kind : builtInKinds)
        menu.addItem (instrumentLabel (kind), [this, kind] {
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
    menu.addItem (tr ("Sampler (choose file)..."), [this] { chooseSample (-1); });

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
        openPluginWindow (added); // as in FL: the plugin opens when added
    });
    menu.addSubMenu (tr ("Plugin (VST3/CLAP)"), plugins);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ChannelRackComponent::chooseSample (int channel)
{
    fileChooser = std::make_unique<juce::FileChooser> (tr ("Choose sample"), juce::File(), engine.getSampleLibrary().wildcard());
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
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Sample"),
                                                tr ("Could not read {0}", { file.getFileName() }));
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

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Plugin"),
                                            tr ("This plugin could not be loaded (it is not installed or has not been scanned)."));
}

void ChannelRackComponent::rename (int channel)
{
    auto* window = new juce::AlertWindow (tr ("Rename channel"), {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", juce::String::fromUTF8 (engine.getSong().channels[(std::size_t) channel].name.c_str()));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, channel, window] (int result) {
                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();

                                 if (result == 1 && ! name.empty() && channel < (int) engine.getSong().channels.size())
                                     engine.editSong ([channel, name] (Song& s) { s.channels[(std::size_t) channel].name = name; });
                             }),
                             true);
}

void ChannelRackComponent::sliceChannel (int channel)
{
    const auto& song = engine.getSong();
    const auto& c = song.channels[(std::size_t) channel];
    const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (c.samplePath.c_str()));

    if (sample == nullptr || sample->frames() == 0)
        return;

    const auto slices = detectSlices (*sample, 0.5f);
    const auto seconds = (double) sample->frames() / sample->sampleRate;
    const auto ticksPerSecond = song.bpm * ppq / 60.0;
    const auto name = c.name + " (slices)";

    engine.editSong ([&] (Song& s) {
        s.channels[(std::size_t) channel].sampler.slices = slices;

        // pattern that plays the slices in their original order and timing (at the project BPM)
        auto& p = s.patterns.emplace_back();
        p.name = name;

        for (std::size_t i = 0; i < slices.size(); ++i)
        {
            const auto start = slices[i] * seconds;
            const auto end = i + 1 < slices.size() ? slices[i + 1] * seconds : seconds;
            Note n;
            n.start = (std::int64_t) std::llround (start * ticksPerSecond);
            n.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((end - start) * ticksPerSecond));
            n.key = 60 + (int) i;
            n.channel = channel;
            p.notes.push_back (n);
        }

        p.sortNotes();
        s.currentPattern = (int) s.patterns.size() - 1;
    });
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
        target = -1; // the remaining files become new channels
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

bool ChannelRackComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString() == BrowserComponent::dragDescription;
}

void ChannelRackComponent::itemDropped (const SourceDetails& details)
{
    if (auto* tree = dynamic_cast<juce::FileTreeComponent*> (details.sourceComponent.get()))
        if (tree->getSelectedFile().existsAsFile())
            filesDropped ({ tree->getSelectedFile().getFullPathName() }, details.localPosition.x, details.localPosition.y);
}
} // namespace lynx
