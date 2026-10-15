#include "ui/ChannelRackComponent.h"

#include "ui/AutomationUi.h"
#include "core/Slicer.h"
#include "ui/ChannelSettings.h"

#include "engine/Text.h"
#include "ui/Dialogs.h"
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
        case InstrumentKind::harmor: return "Harmor";
        case InstrumentKind::ogun: return "Ogun";
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

juce::String channelName (const Song& song, int row)
{
    return row >= 0 && row < (int) song.channels.size() ? juce::String::fromUTF8 (song.channels[(std::size_t) row].name.c_str()) : juce::String();
}
} // namespace

bool ChannelRackComponent::keepEditorsOpen = false;

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
    if (x < selectLeft())
        return Area::name;
    if (x < gridLeft())
        return Area::select;

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
    const auto primary = engine.getSelectedChannel();
    const auto& selection = engine.getSelectedChannels();
    g.setFont (theme::font (theme::type::body));

    for (int row = 0; row < (int) song.channels.size(); ++row)
    {
        const auto y = 4 + row * rowHeight;

        if (y > clip.getBottom() || y + rowHeight < clip.getY())
            continue;

        const auto& channel = song.channels[(std::size_t) row];
        const auto selected = std::binary_search (selection.begin(), selection.end(), row);
        const auto nameArea = juce::Rectangle<float> ((float) nameLeft(), (float) y + 3, (float) nameWidth, (float) rowHeight - 6);

        if (selected) // selected rows are subtly lifted
        {
            g.setColour (bg3);
            g.fillRect (0, y + 1, getWidth(), rowHeight - 2);
        }

        g.setColour (theme::muteLed (channel.muted));
        g.fillEllipse ((float) 9, (float) y + rowHeight / 2.0f - 4.5f, 9, 9);

        const auto touched = knobDrag != Area::none && knobRow == row;
        theme::drawKnob (g, juce::Rectangle<float> ((float) muteWidth, (float) y, (float) knobWidth, (float) rowHeight).reduced (1.0f, 2.0f),
                         (channel.pan + 1.0f) / 2.0f, true, touched && knobDrag == Area::pan ? accent : info);
        theme::drawKnob (g, juce::Rectangle<float> ((float) muteWidth + knobWidth, (float) y, (float) knobWidth, (float) rowHeight).reduced (1.0f, 2.0f),
                         channel.volume, false, touched && knobDrag == Area::volume ? accent : knobArc);

        // small box with the mixer insert ("-" = straight to Master)
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
        g.setColour (row == primary ? bg6 : bg4);
        g.fillRoundedRectangle (nameArea, theme::radius::md);

        if (row == primary)
        {
            g.setColour (accent);
            g.drawRoundedRectangle (nameArea.reduced (0.5f), theme::radius::md, 1.0f);
        }

        g.setColour (channel.colour != 0 ? theme::tame (juce::Colour (channel.colour)) : bg6.brighter (0.2f));
        g.fillRoundedRectangle (nameArea.withWidth (4.0f).reduced (0.0f, 3.0f).translated (3.0f, 0.0f), 1.5f);

        g.setColour (channel.muted ? textDisabled : textPrimary);
        g.drawText (juce::String::fromUTF8 (channel.name.c_str()), nameArea.withTrimmedLeft (14).withTrimmedRight (6).toNearestInt(),
                    juce::Justification::centredLeft, true);

        // selector: a slim pill, lit when the row is selected
        const auto pill = juce::Rectangle<float> ((float) selectLeft() + 3.0f, (float) y + 7.0f, (float) selectWidth - 6.0f, (float) rowHeight - 14.0f);
        g.setColour (selected ? accent : ledOff);
        g.fillRoundedRectangle (pill, 3.0f);

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

// --- the control contract for the knobs and the route box ----------------------------------------

ui::ValueSpec ChannelRackComponent::specFor (Area area, int row) const
{
    const auto name = channelName (engine.getSong(), row);

    if (area == Area::pan)
    {
        auto spec = ui::panSpec (name + " - " + tr ("Pan"));
        spec.target = AutomationTarget { AutomationTargetKind::channelPan, row };
        return spec;
    }

    if (area == Area::volume)
    {
        auto spec = ui::percentSpec (name + " - " + tr ("Volume"), 0.78);
        spec.target = AutomationTarget { AutomationTargetKind::channelVolume, row };
        return spec;
    }

    // route box: the mixer insert, as a number
    ui::ValueSpec spec;
    spec.name = name + " - " + tr ("Mixer insert");
    spec.maximum = juce::jmax (0, (int) engine.getSong().mixer.size() - 1);
    spec.interval = 1.0;
    spec.pixelsForRange = 6.0 * juce::jmax (1.0, spec.maximum);
    spec.toText = [] (double v) { return v < 0.5 ? tr ("Master") : tr ("Insert {0}", { juce::String (juce::roundToInt (v)) }); };
    spec.fromText = [] (const juce::String& t) -> std::optional<double> {
        if (t.trim().startsWithIgnoreCase ("m"))
            return 0.0;
        const auto digits = t.retainCharacters ("0123456789");
        return digits.isEmpty() ? std::nullopt : std::optional<double> (digits.getDoubleValue());
    };
    return spec;
}

double ChannelRackComponent::valueOf (Area area, int row) const
{
    const auto& c = engine.getSong().channels[(std::size_t) row];
    return area == Area::pan ? c.pan : area == Area::volume ? c.volume : (double) c.mixerTrack;
}

void ChannelRackComponent::setValue (Area area, int row, double value)
{
    if (row < 0 || row >= (int) engine.getSong().channels.size())
        return;

    if (area == Area::pan)
        engine.editSong ([row, v = (float) value] (Song& s) { s.channels[(std::size_t) row].pan = v; });
    else if (area == Area::volume)
        engine.editSong ([row, v = (float) value] (Song& s) { s.channels[(std::size_t) row].volume = v; });
    else if (area == Area::route)
    {
        const auto track = juce::roundToInt (value);
        if (track != engine.getSong().channels[(std::size_t) row].mixerTrack)
            engine.editSong ([row, track] (Song& s) { s.channels[(std::size_t) row].mixerTrack = track; });
    }
}

// --- selection -----------------------------------------------------------------------------------

void ChannelRackComponent::selectRows (std::vector<int> rows, int primary)
{
    engine.setChannelSelection (std::move (rows), primary);
    repaint();
}

void ChannelRackComponent::clickSelector (int row, const juce::ModifierKeys& mods)
{
    ChannelSelection selection;
    selection.rows = engine.getSelectedChannels();
    selection.anchor = selectionAnchor;

    if (mods.isShiftDown())
        selection.extendTo (row);
    else if (mods.isCtrlDown() || mods.isPopupMenu())
        selection.toggle (row);
    else
        selection.selectOnly (row);

    selectionAnchor = selection.anchor;
    const auto primary = selection.contains (row) ? row : selection.rows.front();
    selectRows (selection.rows, primary);
}

void ChannelRackComponent::selectRelative (int delta, bool extend)
{
    const auto count = (int) engine.getSong().channels.size();

    if (count == 0)
        return;

    const auto row = juce::jlimit (0, count - 1, engine.getSelectedChannel() + delta);

    if (extend)
    {
        ChannelSelection selection;
        selection.anchor = selectionAnchor;
        selection.extendTo (row);
        selectRows (selection.rows, row);
    }
    else
    {
        selectionAnchor = row;
        selectRows ({ row }, row);
    }
}

void ChannelRackComponent::selectAll()
{
    ChannelSelection selection;
    selection.selectAll ((int) engine.getSong().channels.size());
    selectRows (selection.rows, engine.getSelectedChannel());
}

bool ChannelRackComponent::moveSelected (int delta)
{
    auto rows = engine.getSelectedChannels();
    const auto primary = engine.getSelectedChannel();
    const auto primaryIndex = (int) (std::find (rows.begin(), rows.end(), primary) - rows.begin());
    bool moved = false;
    std::vector<int> after;

    engine.editSong ([&] (Song& s) {
        after = rows;
        moved = moveChannels (s, after, delta);
    });

    if (! moved)
        return false;

    const auto newPrimary = primaryIndex < (int) after.size() ? after[(std::size_t) primaryIndex] : after.front();
    openEditorRow = -1;
    selectionAnchor = juce::jlimit (0, (int) engine.getSong().channels.size() - 1, selectionAnchor + delta);
    selectRows (after, newPrimary);
    return true;
}

void ChannelRackComponent::cloneSelected()
{
    auto rows = engine.getSelectedChannels();
    std::vector<int> clones;

    engine.editSong ([&] (Song& s) {
        // from the bottom up, so earlier clones don't shift the rows still to clone
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
            cloneChannel (s, *it);
    });

    // each clone sits right below its original: original i (sorted) ends at rows[i] + i, its clone one below
    for (std::size_t i = 0; i < rows.size(); ++i)
        clones.push_back (rows[i] + (int) i + 1);

    if (! clones.empty())
        selectRows (clones, clones.front());
}

void ChannelRackComponent::deleteSelected()
{
    const auto rows = engine.getSelectedChannels();
    const auto count = (int) engine.getSong().channels.size();

    if (rows.empty() || (int) rows.size() >= count) // at least one channel stays
        return;

    for (auto r : rows)
        closeEditor (r);

    engine.editSong ([rows] (Song& s) { removeChannels (s, rows); });
    openEditorRow = -1;
    const auto row = juce::jlimit (0, juce::jmax (0, (int) engine.getSong().channels.size() - 1), rows.front());
    selectRows ({ row }, row);
}

void ChannelRackComponent::sendSelectedToInserts()
{
    const auto rows = engine.getSelectedChannels();
    engine.editSong ([rows] (Song& s) { sendChannelsToFreeInserts (s, rows); });
}

void ChannelRackComponent::toggleMute (int channel)
{
    if (channel >= 0 && channel < (int) engine.getSong().channels.size())
        engine.editSong ([channel] (Song& s) { s.channels[(std::size_t) channel].muted = ! s.channels[(std::size_t) channel].muted; });
}

void ChannelRackComponent::solo (int row)
{
    if (row < 0 || row >= (int) engine.getSong().channels.size())
        return;

    // only this channel plays; repeating on the already-soloed channel unmutes all
    engine.editSong ([row] (Song& s) {
        const auto alone = ! s.channels[(std::size_t) row].muted
                           && std::all_of (s.channels.begin(), s.channels.end(), [&] (const ChannelDef& c) {
                                  return &c == &s.channels[(std::size_t) row] || c.muted;
                              });

        for (std::size_t i = 0; i < s.channels.size(); ++i)
            s.channels[i].muted = alone ? false : i != (std::size_t) row;
    });
}

// --- channel editors ---------------------------------------------------------------------------

bool ChannelRackComponent::isEditorOpen (int channel)
{
    if (channel < 0 || channel >= (int) engine.getSong().channels.size())
        return false;

    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::plugin)
    {
        if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
            return PluginWindows::isOpen (plugin->getInstance());
        return false;
    }

    return isChannelSettingsOpen (channel);
}

void ChannelRackComponent::closeEditor (int channel)
{
    if (channel < 0 || channel >= (int) engine.getSong().channels.size())
        return;

    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::plugin)
    {
        if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
            PluginWindows::close (plugin->getInstance());
    }
    else
        closeChannelSettings (channel);
}

void ChannelRackComponent::toggleEditor (int channel, bool keepOthers)
{
    if (channel < 0 || channel >= (int) engine.getSong().channels.size())
        return;

    const auto kind = engine.getSong().channels[(std::size_t) channel].instrument;

    if (kind != InstrumentKind::plugin && ! hasChannelSettings (kind))
        return; // built-in drums and audio clips have no editor window: the click only selects

    if (isEditorOpen (channel))
    {
        closeEditor (channel);
        if (openEditorRow == channel)
            openEditorRow = -1;
        return;
    }

    if (! keepOthers && ! keepEditorsOpen && openEditorRow != channel)
        closeEditor (openEditorRow);

    if (kind == InstrumentKind::plugin)
        openPluginWindow (channel);
    else
        openChannelSettings (engine, channel);

    openEditorRow = channel;
}

// --- mouse -------------------------------------------------------------------------------------

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
                solo (row);
            else
                toggleMute (row);
            return;

        case Area::pan:
        case Area::volume:
        case Area::route:
        {
            const auto spec = specFor (area, row);

            if (e.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                menu.addSectionHeader (spec.name + ": " + spec.text (valueOf (area, row)));
                ui::addValueItems (menu, *this, &engine, spec, valueOf (area, row), [this, area, row] (double v) { setValue (area, row, v); });

                if (area == Area::route)
                {
                    menu.addSeparator();
                    menu.addItem (tr ("Next free insert"), [this, row] {
                        engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].mixerTrack = s.nextFreeInsert(); });
                    });
                    menu.addItem (tr ("Send the selected channels to free inserts") + "\tCtrl+L", [this] { sendSelectedToInserts(); });
                }

                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
                return;
            }

            if (ui::isResetClick (e))
            {
                setValue (area, row, spec.defaultValue);
                return;
            }

            knobDrag = area;
            knobRow = row;
            drag.begin (e, valueOf (area, row));
            repaint();
            return;
        }

        case Area::name:
            if (e.mods.isPopupMenu())
            {
                if (! std::binary_search (engine.getSelectedChannels().begin(), engine.getSelectedChannels().end(), row))
                    clickSelector (row, {});
                else
                    selectRows (engine.getSelectedChannels(), row);
                showChannelMenu (row);
                return;
            }

            if (e.mods.isShiftDown())
            {
                clickSelector (row, e.mods);
                return;
            }

            if (! std::binary_search (engine.getSelectedChannels().begin(), engine.getSelectedChannels().end(), row)
                || engine.getSelectedChannels().size() > 1)
            {
                selectionAnchor = row;
                selectRows ({ row }, row);
            }

            if (e.mods.isCtrlDown()) // audition without opening anything
            {
                auditionRow = row;
                engine.sendLiveNote (row, 60, 0.8f, true);
                return;
            }

            toggleEditor (row, e.mods.isAltDown());
            return;

        case Area::select:
            clickSelector (row, e.mods);
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
    if (knobDrag != Area::none && drag.isActive())
    {
        setValue (knobDrag, knobRow, drag.update (e, specFor (knobDrag, knobRow)));
        return;
    }

    if (paintMode != PaintMode::none)
        applyPaint (e);
}

void ChannelRackComponent::mouseUp (const juce::MouseEvent&)
{
    if (auditionRow >= 0)
        engine.sendLiveNote (auditionRow, 60, 0.0f, false);

    auditionRow = -1;
    paintMode = PaintMode::none;

    if (knobDrag != Area::none)
        repaint();

    knobDrag = Area::none;
    drag.end();
}

void ChannelRackComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    int row = 0;
    const auto area = areaAt (e.x, e.y, row);

    if (area == Area::pan || area == Area::volume)
        setValue (area, row, specFor (area, row).defaultValue);
    else if (area == Area::route)
        engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].mixerTrack = s.nextFreeInsert(); });
    else if (area == Area::select && ! e.mods.isPopupMenu())
        selectAll();
    else if (area == Area::name && ! e.mods.isAnyModifierKeyDown() && onOpenPianoRoll)
        onOpenPianoRoll();
}

void ChannelRackComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int row = 0;
    const auto area = areaAt (e.x, e.y, row);

    if (area == Area::pan || area == Area::volume || area == Area::route)
    {
        if (const auto v = ui::wheelValue (valueOf (area, row), e, wheel, specFor (area, row)))
            setValue (area, row, *v);
        return;
    }

    if ((area == Area::name || area == Area::select) && e.mods.isShiftDown()) // Shift+wheel moves the channel
    {
        const auto delta = (wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX) > 0.0f ? -1 : 1;
        selectRows ({ row }, row);
        moveSelected (delta);
        return;
    }

    juce::Component::mouseWheelMove (e, wheel); // scroll the viewport
}

ui::Hint ChannelRackComponent::hintAt (juce::Point<int> p)
{
    int row = 0;
    const auto area = areaAt (p.x, p.y, row);
    const auto& song = engine.getSong();
    ui::Hint h;

    if (row >= 0 && row < (int) song.channels.size() && (area == Area::pan || area == Area::volume || area == Area::route))
    {
        const auto active = knobDrag != Area::none && drag.isActive();
        const auto shownArea = active ? knobDrag : area;
        const auto shownRow = active ? knobRow : row;
        h = ui::valueHint (specFor (shownArea, shownRow), valueOf (shownArea, shownRow),
                           shownArea == Area::route ? tr ("drag or wheel to route, double-click: next free insert")
                                                    : tr ("drag (Ctrl: fine), wheel, Alt+click resets"));
        return h;
    }

    const auto name = channelName (song, row);

    switch (area)
    {
        case Area::mute:
            h.text = name + " - " + tr ("Click: mute.  Ctrl+click or right-click: solo");
            break;
        case Area::name:
            h.text = name + " - " + tr ("Click: open/close the editor (Alt keeps others open).  Ctrl+click: audition.  Double-click: Piano Roll");
            h.hasMenu = true;
            break;
        case Area::select:
            h.text = name + " - " + tr ("Select (Ctrl: add/remove, Shift: range, double-click: all).  Alt+Up/Down moves the selection");
            break;
        case Area::grid:
            h.text = name + " - " + tr ("Steps: click turns on and drags to paint, right-click erases");
            break;
        case Area::addRow:
            h.text = tr ("Add a channel (or drop audio files on the rack)");
            break;
        default:
            break;
    }

    return h;
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

// --- menus -------------------------------------------------------------------------------------

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
    else if (c.instrument == InstrumentKind::synth)
        menu.addItem (tr ("Synth settings (oscillators, envelope, filter)..."), [this, channel] { openChannelSettings (engine, channel); });
    else if (hasChannelSettings (c.instrument))
        menu.addItem (tr ("Channel settings..."), [this, channel] { openChannelSettings (engine, channel); });

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

    menu.addItem (tr ("Rename and color...") + "\tF2", [this, channel] { rename (channel); });
    {
        juce::PopupMenu colours;
        colours.addItem (tr ("Default"), [this, channel] { engine.editSong ([channel] (Song& s) { s.channels[(std::size_t) channel].colour = 0; }); });

        for (auto colour : lynxPalette())
        {
            juce::Image swatch (juce::Image::ARGB, 16, 16, true);
            juce::Graphics (swatch).fillAll (juce::Colour (colour));
            juce::PopupMenu::Item item;
            item.text = "#" + juce::String::toHexString ((juce::int64) (colour & 0xFFFFFF)).paddedLeft ('0', 6).toUpperCase();
            item.image = std::make_unique<juce::DrawableImage> (swatch);
            item.isTicked = c.colour == colour;
            item.action = [this, channel, colour] { engine.editSong ([channel, colour] (Song& s) { s.channels[(std::size_t) channel].colour = colour; }); };
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

    // organisation (acts on the selection)
    const auto selected = (int) engine.getSelectedChannels().size();
    const auto count = (int) engine.getSong().channels.size();
    menu.addSeparator();
    menu.addItem (selected > 1 ? tr ("Clone {0} channels", { juce::String (selected) }) : tr ("Clone channel"), [this] { cloneSelected(); });
    {
        juce::PopupMenu insert;
        addInstrumentItems (insert, channel);
        menu.addSubMenu (tr ("Insert channel here"), insert);
    }
    menu.addItem (tr ("Move up") + "\tAlt+Up", engine.getSelectedChannels().front() > 0, false, [this] { moveSelected (-1); });
    menu.addItem (tr ("Move down") + "\tAlt+Down", engine.getSelectedChannels().back() < count - 1, false, [this] { moveSelected (1); });
    menu.addItem (tr ("Send to free mixer inserts") + "\tCtrl+L", [this] { sendSelectedToInserts(); });
    menu.addSeparator();

    if (selected > 1)
        menu.addItem (tr ("Delete {0} channels", { juce::String (selected) }) + "\tAlt+Del", selected < count, false, [this] { deleteSelected(); });
    else
        menu.addItem (tr ("Delete channel") + "\tAlt+Del", count > 1, false, [this, channel] {
            closeEditor (channel);
            engine.editSong ([channel] (Song& s) { s.removeChannel (channel); });
            openEditorRow = -1;
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ChannelRackComponent::addInstrumentItems (juce::PopupMenu& menu, int insertAt)
{
    // a new channel of `kind`, at the end (insertAt < 0) or at a row
    auto add = [this, insertAt] (ChannelDef c) {
        int index = 0;
        engine.editSong ([&] (Song& s) {
            c.mixerTrack = s.nextFreeInsert();
            index = insertAt < 0 ? (s.channels.push_back (c), (int) s.channels.size() - 1) : insertChannel (s, insertAt, c);
        });
        selectionAnchor = index;
        selectRows ({ index }, index);
        return index;
    };

    for (auto kind : builtInKinds)
        menu.addItem (instrumentLabel (kind), [add, kind] {
            ChannelDef c;
            c.name = instrumentName (kind).toStdString();
            c.instrument = kind;
            add (c);
        });

    menu.addSeparator();

    if (insertAt < 0)
        menu.addItem (tr ("Sampler (choose file)..."), [this] { chooseSample (-1); });

    juce::PopupMenu plugins;
    addPluginItems (plugins, engine, true, [this, add] (PluginRef ref) {
        ChannelDef c;
        c.name = ref.name;
        c.instrument = InstrumentKind::plugin;
        c.plugin = ref;
        const auto added = add (c);
        openPluginWindow (added); // the plugin opens when added
        openEditorRow = added;
    });
    menu.addSubMenu (tr ("Plugin (VST3/CLAP)"), plugins);
}

void ChannelRackComponent::showAddChannelMenu (int insertAt)
{
    juce::PopupMenu menu;
    addInstrumentItems (menu, insertAt);
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
    const auto& c = engine.getSong().channels[(std::size_t) channel];
    showNameAndColourDialog (tr ("Rename channel"), juce::String::fromUTF8 (c.name.c_str()), c.colour,
                             [this, channel] (const juce::String& name, std::uint32_t colour) {
                                 if (channel < (int) engine.getSong().channels.size())
                                     engine.editSong ([channel, n = name.toStdString(), colour] (Song& s) {
                                         s.channels[(std::size_t) channel].name = n;
                                         s.channels[(std::size_t) channel].colour = colour;
                                     });
                             });
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
