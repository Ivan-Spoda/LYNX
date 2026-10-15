#include "ui/MixerComponent.h"

#include "core/FlEffects.h"
#include "core/MixerOps.h"
#include "ui/AutomationUi.h"
#include "ui/BrowserComponent.h"
#include "ui/ExportDialog.h"

#include "core/Effects.h"
#include "engine/Text.h"
#include "ui/ParametricEqEditor.h"
#include "ui/PluginWindow.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ValueControl.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
constexpr int headerHeight = 34;
constexpr int panHeight = 34;
constexpr int buttonsHeight = 22;
constexpr int feedsHeight = 46;

// Fader 0..1 -> dB, with 0.8 = 0 dB (as in FL).
juce::String faderToDb (float volume)
{
    if (volume <= 0.0001f)
        return juce::String::fromUTF8 ("-\xe2\x88\x9e");

    const auto db = juce::Decibels::gainToDecibels (faderGain (volume));
    return (db > 0 ? "+" : "") + juce::String (db, 1);
}

// Meter level 0..1 on a dB scale (-60..+6).
float meterPosition (float gain)
{
    const auto db = juce::Decibels::gainToDecibels (gain, -60.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
}
} // namespace

MixerStrips::MixerStrips (AudioEngine& e)
    : engine (e)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    engine.addChangeListener (this);
    updateSize();
    startTimerHz (30);
}

MixerStrips::~MixerStrips()
{
    engine.removeChangeListener (this);
}

void MixerStrips::setVisibleHeight (int height)
{
    visibleHeight = juce::jmax (260, height);
    updateSize();
}

void MixerStrips::updateSize()
{
    const auto tracks = (int) engine.getSong().mixer.size();
    setSize (stripX (tracks) + stripWidth + 8, visibleHeight);
    levels.resize ((std::size_t) juce::jmax (1, tracks) * 2, 0.0f);
}

int MixerStrips::stripX (int track) const noexcept
{
    return track == 0 ? 4 : 4 + masterWidth + 10 + (track - 1) * stripWidth;
}

int MixerStrips::trackAt (int x) const noexcept
{
    const auto tracks = (int) engine.getSong().mixer.size();

    for (int t = 0; t <= tracks; ++t) // t == tracks = "+" button
        if (x >= stripX (t) && x < stripX (t) + stripWidthOf (t))
            return t;

    return -1;
}

juce::Rectangle<int> MixerStrips::panArea (int t) const
{
    return { stripX (t), headerHeight, stripWidthOf (t), panHeight };
}

juce::Rectangle<int> MixerStrips::muteArea (int t) const
{
    return { stripX (t) + 6, headerHeight + panHeight, 26, buttonsHeight };
}

juce::Rectangle<int> MixerStrips::soloArea (int t) const
{
    return { stripX (t) + stripWidthOf (t) - 32, headerHeight + panHeight, 26, buttonsHeight };
}

juce::Rectangle<int> MixerStrips::routeArea (int t) const
{
    return { stripX (t) + 6, getHeight() - feedsHeight - 2, stripWidthOf (t) - 16, 16 };
}

juce::Rectangle<int> MixerStrips::armArea (int t) const
{
    return { stripX (t) + 4, 4, 14, 14 };
}

juce::Rectangle<int> MixerStrips::faderArea (int t) const
{
    const auto top = headerHeight + panHeight + buttonsHeight + 8;
    return { stripX (t) + 6, top, stripWidthOf (t) - 12, getHeight() - top - feedsHeight - 22 };
}

MixerStrips::Part MixerStrips::partAt (int t, juce::Point<int> p) const
{
    if (t == (int) engine.getSong().mixer.size())
        return Part::add;

    if (t > 0 && armArea (t).contains (p))
        return Part::arm;
    if (panArea (t).contains (p))
        return Part::pan;
    if (muteArea (t).contains (p))
        return Part::mute;
    if (t > 0 && soloArea (t).contains (p))
        return Part::solo;
    if (routeArea (t).contains (p) && selected > 0 && t != selected)
        return Part::route;
    if (faderArea (t).expanded (0, 6).contains (p))
        return Part::fader;

    return Part::body;
}

void MixerStrips::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto& song = engine.getSong();
    const auto tracks = (int) song.mixer.size();
    const auto clip = g.getClipBounds();
    g.fillAll (colour::bg1);

    for (int t = 0; t < tracks; ++t)
    {
        const auto x = stripX (t);
        const auto w = stripWidthOf (t);

        if (x > clip.getRight() || x + w < clip.getX())
            continue;

        const auto& track = song.mixer[(std::size_t) t];
        const auto strip = juce::Rectangle<int> (x, 2, w - 4, getHeight() - 4);
        const auto isSelected = t == selected;
        const auto inSelection = std::binary_search (selection.begin(), selection.end(), t);

        g.setColour (isSelected || inSelection ? colour::bg4 : (t == 0 ? colour::bg3 : colour::bg2));
        g.fillRoundedRectangle (strip.toFloat(), radius::md);

        if (track.colour != 0) // thin colour band at the top
        {
            g.setColour (tame (juce::Colour (track.colour)));
            g.fillRoundedRectangle (strip.toFloat().withHeight (3.0f).reduced (6.0f, 0.0f), 1.5f);
        }

        if (inSelection && ! isSelected)
        {
            g.setColour (colour::accent.withAlpha (0.3f));
            g.drawRoundedRectangle (strip.toFloat().reduced (0.5f), radius::md, 1.0f);
        }

        if (isSelected)
        {
            g.setColour (colour::accent.withAlpha (0.55f));
            g.drawRoundedRectangle (strip.toFloat().reduced (0.5f), radius::md, 1.0f);
            g.setColour (colour::accent);
            g.fillRoundedRectangle (strip.toFloat().withHeight (3.0f).reduced (6.0f, 0.0f), 1.5f);
        }

        // record arm: the input recording goes through this insert
        if (t > 0)
        {
            const auto armed = engine.isInsertArmed (t);
            const auto r = armArea (t).toFloat().reduced (2.0f);
            g.setColour (armed ? colour::record : colour::bg0);
            g.fillEllipse (r);
            g.setColour (armed ? colour::record.brighter (0.3f) : colour::borderStrong);
            g.drawEllipse (r, 1.0f);
        }

        // header: number and name
        g.setColour (isSelected ? colour::accent : colour::textSecondary);
        g.setFont (font (type::caption, isSelected));
        g.drawText (t == 0 ? juce::String ("M") : juce::String (t), x, 5, w - 4, 12, juce::Justification::centred);
        g.setColour (track.muted ? colour::textDisabled : colour::textPrimary);
        g.setFont (font (type::small));
        g.drawFittedText (juce::String::fromUTF8 (track.name.c_str()), x + 3, 17, w - 10, 15, juce::Justification::centred, 1, 0.7f);

        // compensated latency (PDC) on the Master
        if (t == 0 && engine.getLatencySamples() > 0)
        {
            const auto ms = 1000.0 * engine.getLatencySamples() / juce::jmax (1.0, engine.getTransport().sampleRate.load());
            g.setColour (colour::textSecondary);
            g.setFont (font (10.0f));
            g.drawFittedText ("PDC " + juce::String (ms, 1) + " ms", soloArea (t).withX (x + 34).withWidth (w - 40),
                              juce::Justification::centredLeft, 1, 0.8f); // the Master has no solo: use its spot
        }

        if (! track.effects.empty())
        {
            g.setColour (colour::info);
            g.setFont (font (9.0f, true));
            g.drawText ("FX " + juce::String ((int) track.effects.size()), x + w - 31, 5, 24, 11, juce::Justification::centredRight);
        }

        // pan
        drawKnob (g, panArea (t).toFloat().withSizeKeepingCentre (28.0f, 28.0f), (track.pan + 1.0f) * 0.5f, true, colour::info);

        // mute and solo
        {
            const auto m = muteArea (t).toFloat().withSizeKeepingCentre (12.0f, 12.0f);
            g.setColour (muteLed (track.muted));
            g.fillEllipse (m);
            if (! track.muted)
            {
                g.setColour (colour::ledOn.withAlpha (0.25f));
                g.drawEllipse (m.expanded (2.0f), 1.0f);
            }

            if (t > 0)
            {
                const auto s = soloArea (t).toFloat().reduced (2.0f);
                g.setColour (track.solo ? colour::accent : colour::bg0);
                g.fillRoundedRectangle (s, radius::sm);
                g.setColour (track.solo ? colour::onAccent : colour::textSecondary);
                g.setFont (font (type::caption, true));
                g.drawText ("S", s.toNearestInt(), juce::Justification::centred);
            }
        }

        // meters + fader
        {
            const auto area = faderArea (t);
            const auto meterW = 5;
            const auto meters = area.withWidth (meterW * 2 + 2);
            const auto fader = area.withTrimmedLeft (meterW * 2 + 8);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto bar = juce::Rectangle<int> (meters.getX() + ch * (meterW + 2), meters.getY(), meterW, meters.getHeight());
                g.setColour (colour::bg0);
                g.fillRoundedRectangle (bar.toFloat(), 1.5f);

                const auto level = (std::size_t) t * 2 + (std::size_t) ch < levels.size() ? levels[(std::size_t) t * 2 + (std::size_t) ch] : 0.0f;
                const auto h = (int) (meterPosition (level) * (float) bar.getHeight());
                const auto lit = bar.withTop (bar.getBottom() - h);
                juce::ColourGradient gradient (colour::meterClip, (float) bar.getX(), (float) bar.getY(), colour::meterLow, (float) bar.getX(),
                                               (float) bar.getBottom(), false);
                gradient.addColour (1.0 - meterPosition (juce::Decibels::decibelsToGain (0.0f)), colour::meterClip); // above 0 dB: clip zone
                gradient.addColour (1.0 - meterPosition (juce::Decibels::decibelsToGain (-6.0f)), colour::meterMid);
                gradient.addColour (1.0 - meterPosition (juce::Decibels::decibelsToGain (-18.0f)), colour::meterLow);
                g.setGradientFill (gradient);
                g.fillRect (lit);

                if (level >= 1.0f) // clip indicator
                {
                    g.setColour (colour::meterClip);
                    g.fillRect (bar.withHeight (3));
                }
            }

            // fader track and 0 dB mark
            const auto railX = (float) fader.getCentreX();
            const auto capY = (float) fader.getBottom() - track.volume * (float) fader.getHeight();
            g.setColour (colour::bg0);
            g.fillRoundedRectangle (railX - 2.0f, (float) fader.getY(), 4.0f, (float) fader.getHeight(), 2.0f);
            g.setColour (colour::bg6.brighter (0.15f).withAlpha (track.muted ? 0.4f : 1.0f));
            g.fillRoundedRectangle (railX - 2.0f, capY, 4.0f, juce::jmax (0.0f, (float) fader.getBottom() - capY), 2.0f);
            const auto unityY = (float) fader.getBottom() - unityFader * (float) fader.getHeight();
            g.setColour (colour::textDisabled);
            g.drawHorizontalLine ((int) unityY, (float) fader.getX(), railX - 5.0f);
            g.drawHorizontalLine ((int) unityY, railX + 5.0f, (float) fader.getRight());

            const auto capWidth = juce::jmin ((float) fader.getWidth() - 2.0f, 26.0f);
            const auto cap = juce::Rectangle<float> (capWidth, 12.0f).withCentre ({ railX, capY });
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), radius::sm);
            g.setColour (juce::Colour (0xffd4d7dd)); // same cap as the LookAndFeel's linear faders
            g.fillRoundedRectangle (cap, radius::sm);
            g.setColour (colour::bg0.withAlpha (0.8f));
            g.fillRect (cap.withSizeKeepingCentre (capWidth - 8.0f, 1.5f));

            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            g.drawText (faderToDb (track.volume) + " dB", x, area.getBottom() + 4, w - 4, 14, juce::Justification::centred);
        }

        // send from the selected track to this one
        if (selected > 0 && t != selected && selected < tracks)
        {
            const auto r = routeArea (t).toFloat();
            const auto on = song.routes (selected, t);
            const auto possible = on || song.canRoute (selected, t);

            if (possible)
            {
                g.setColour (on ? colour::accent : colour::bg0);
                g.fillRoundedRectangle (r, radius::sm);
                g.setColour (on ? colour::onAccent : colour::textSecondary);
                g.setFont (font (10.0f, on));
                juce::String label = juce::String::fromUTF8 ("\xe2\x86\x92"); // arrow

                if (on && t > 0)
                    for (const auto& send : song.mixer[(std::size_t) selected].sends)
                        if (send.target == t)
                            label << " " << juce::roundToInt (send.level * 100.0f) << "%";

                g.drawText (label, r.toNearestInt(), juce::Justification::centred);
            }
        }

        // channels routed into this track
        {
            juce::StringArray feeds;
            for (const auto& c : song.channels)
                if (juce::jlimit (0, tracks - 1, c.mixerTrack) == t)
                    feeds.add (juce::String::fromUTF8 (c.name.c_str()));

            g.setColour (colour::textSecondary);
            g.setFont (font (10.0f));
            const auto text = feeds.size() > 3 ? feeds[0] + ", " + feeds[1] + " +" + juce::String (feeds.size() - 2) : feeds.joinIntoString (", ");
            g.drawFittedText (text, x + 3, getHeight() - feedsHeight + 16, w - 10, feedsHeight - 20, juce::Justification::centredTop, 2, 0.8f);
        }
    }

    // "+" button
    if (tracks < maxMixerTracks)
    {
        const auto add = juce::Rectangle<int> (stripX (tracks), 2, stripWidth - 4, 60).toFloat();
        g.setColour (colour::bg2);
        g.fillRoundedRectangle (add, radius::md);
        g.setColour (colour::border);
        g.drawRoundedRectangle (add.reduced (0.5f), radius::md, 1.0f);
        icons::draw (g, Icon::plus, add.withSizeKeepingCentre (22.0f, 22.0f), colour::textSecondary);
    }
}

void MixerStrips::mouseDown (const juce::MouseEvent& e)
{
    const auto t = trackAt (e.x);

    if (t < 0)
        return;

    const auto part = partAt (t, e.getPosition());

    if (part == Part::add)
    {
        engine.editSong ([] (Song& s) {
            if (s.mixer.empty())
                s.mixer = Song::defaultMixer (0);

            auto& track = s.mixer.emplace_back();
            track.name = "Insert " + std::to_string (s.mixer.size() - 1);
        });
        return;
    }

    if (part == Part::arm)
    {
        engine.setInsertArmed (t, ! engine.isInsertArmed (t));
        repaint();
        return;
    }

    if (part == Part::route && e.mods.isPopupMenu())
    {
        showSendMenu (t);
        return;
    }

    if (part == Part::mute && (e.mods.isPopupMenu() || e.mods.isCtrlDown())) // solo on the same LED
    {
        soloExclusive (t);
        return;
    }

    if (part == Part::route)
    {
        // the selection is the send source: don't change it
        const auto from = selected;
        const auto on = engine.getSong().routes (from, t);
        engine.editSong ([from, t, on] (Song& s) { s.setRoute (from, t, ! on); });

        if (! on && t > 0) // just enabled: dragging adjusts the level
        {
            dragPart = Part::route;
            dragTrack = t;
            dragStartValue = 1.0f;
        }
        return;
    }

    if (part == Part::body && ! e.mods.isPopupMenu() && (e.mods.isCtrlDown() || e.mods.isShiftDown()) && t > 0)
    {
        auto s = selection;
        if (e.mods.isShiftDown())
        {
            s.clear();
            for (int i = juce::jmin (selected, t); i <= juce::jmax (selected, t); ++i)
                s.push_back (i);
        }
        else if (auto it = std::find (s.begin(), s.end(), t); it != s.end() && s.size() > 1)
            s.erase (it);
        else
            s.push_back (t);

        setSelection (s, std::find (s.begin(), s.end(), t) != s.end() ? t : s.front());
        return;
    }

    // a plain click on a track outside the selection selects only it; inside, the group stays
    if (! std::binary_search (selection.begin(), selection.end(), t))
        setSelection ({ t }, t);
    else if (selected != t)
        setSelection (selection, t);

    if (part == Part::pan || part == Part::fader)
    {
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (part, t), valueOf (part, t), [this, part, t] (double v) { setValue (part, t, v); });
            return;
        }

        if (ui::isResetClick (e))
        {
            setValue (part, t, specFor (part, t).defaultValue);
            return;
        }
    }

    if (e.mods.isPopupMenu())
    {
        showMenu (t);
        return;
    }

    const auto& track = engine.getSong().mixer[(std::size_t) t];

    switch (part)
    {
        case Part::mute:
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].muted = ! s.mixer[(std::size_t) t].muted; });
            return;

        case Part::solo:
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].solo = ! s.mixer[(std::size_t) t].solo; });
            return;

        case Part::pan:
        case Part::fader:
            dragPart = part;
            dragTrack = t;
            dragStartValue = part == Part::pan ? track.pan : track.volume;

            if (part == Part::fader && ! e.mods.isCtrlDown()) // clicking the track jumps the fader there
            {
                const auto area = faderArea (t);
                const auto capY = area.getBottom() - track.volume * (float) area.getHeight();

                if (std::abs ((float) e.y - capY) > 9.0f)
                {
                    dragStartValue = juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - e.y) / (float) area.getHeight());
                    engine.editSong ([t, v = dragStartValue] (Song& s) { s.mixer[(std::size_t) t].volume = v; });
                }
            }

            drag.begin (e, dragStartValue);
            groupStart.clear();
            groupGrab = part == Part::pan ? track.pan : track.volume;
            if (selection.size() > 1) // the selected strips follow, keeping their offsets
                for (auto s : selection)
                    if (s != t && s < (int) engine.getSong().mixer.size())
                        groupStart.push_back ({ s, part == Part::pan ? engine.getSong().mixer[(std::size_t) s].pan
                                                                      : engine.getSong().mixer[(std::size_t) s].volume });
            return;

        case Part::arm:
        case Part::route:
        case Part::body:
        case Part::none:
        case Part::add:
            return;
    }
}

void MixerStrips::mouseDrag (const juce::MouseEvent& e)
{
    if (dragPart == Part::none)
        return;

    const auto t = dragTrack;

    if (dragPart == Part::route)
    {
        const auto from = selected;
        const auto v = juce::jlimit (0.0f, 1.0f, dragStartValue - (float) e.getDistanceFromDragStartY() / 120.0f);
        engine.editSong ([from, t, v] (Song& s) { s.setRoute (from, t, true, v); });
        return;
    }

    setValue (dragPart, t, drag.update (e, specFor (dragPart, t)));
}

ui::ValueSpec MixerStrips::specFor (Part part, int t) const
{
    const auto& song = engine.getSong();
    const auto name = t >= 0 && t < (int) song.mixer.size() ? juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str()) : juce::String();

    if (part == Part::pan)
    {
        auto spec = ui::panSpec (name + " - " + tr ("Pan"));
        spec.pixelsForRange = 150.0;
        spec.target = AutomationTarget { AutomationTargetKind::mixerPan, t };
        return spec;
    }

    auto spec = ui::faderSpec (name + " - " + tr ("Volume"), unityFader);
    spec.pixelsForRange = juce::jmax (60, faderArea (juce::jmax (0, t)).getHeight());
    spec.target = AutomationTarget { AutomationTargetKind::mixerVolume, t };
    return spec;
}

double MixerStrips::valueOf (Part part, int t) const
{
    const auto& track = engine.getSong().mixer[(std::size_t) t];
    return part == Part::pan ? track.pan : track.volume;
}

void MixerStrips::setValue (Part part, int t, double value)
{
    if (t < 0 || t >= (int) engine.getSong().mixer.size())
        return;

    // during a drag of a selected strip, the other selected strips move by the same amount
    const auto delta = (float) value - groupGrab;
    const auto group = dragTrack == t && dragPart == part ? groupStart : std::vector<std::pair<int, float>> {};

    if (part == Part::pan)
        engine.editSong ([t, v = (float) value, group, delta] (Song& s) {
            s.mixer[(std::size_t) t].pan = v;
            for (auto [other, start] : group)
                if (other < (int) s.mixer.size())
                    s.mixer[(std::size_t) other].pan = juce::jlimit (-1.0f, 1.0f, start + delta);
        });
    else if (part == Part::fader)
        engine.editSong ([t, v = (float) value, group, delta] (Song& s) {
            s.mixer[(std::size_t) t].volume = v;
            for (auto [other, start] : group)
                if (other < (int) s.mixer.size())
                    s.mixer[(std::size_t) other].volume = juce::jlimit (0.0f, 1.0f, start + delta);
        });
}

void MixerStrips::setSelection (std::vector<int> tracks, int primary)
{
    const auto count = (int) engine.getSong().mixer.size();
    tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [count] (int t) { return t < 0 || t >= count; }), tracks.end());
    primary = juce::jlimit (0, juce::jmax (0, count - 1), primary);
    tracks.push_back (primary);
    std::sort (tracks.begin(), tracks.end());
    tracks.erase (std::unique (tracks.begin(), tracks.end()), tracks.end());
    selection = tracks;
    const auto changed = primary != selected;
    selected = primary;
    repaint();

    if (changed && onSelectionChanged)
        onSelectionChanged (selected);
}

bool MixerStrips::moveSelected (int delta)
{
    auto moved = selection;
    bool ok = false;
    engine.editSong ([&] (Song& s) { ok = mixerops::moveTracks (s, moved, delta); });

    if (ok)
        setSelection (moved, juce::jlimit (1, (int) engine.getSong().mixer.size() - 1, selected + (delta < 0 ? -1 : 1)));

    return ok;
}

void MixerStrips::linkSelectedChannels (bool startingHere)
{
    const auto channels = engine.getSelectedChannels();
    engine.editSong ([channels, t = selected, startingHere] (Song& s) { mixerops::linkChannels (s, channels, t, startingHere); });
}

void MixerStrips::selectLinkedChannels()
{
    const auto channels = mixerops::channelsOn (engine.getSong(), selected);

    if (! channels.empty())
        engine.setChannelSelection (channels, channels.front());
}

void MixerStrips::soloExclusive (int t)
{
    if (t <= 0)
        return;

    // solo only this track; again on the only soloed track clears the solo
    engine.editSong ([t] (Song& s) {
        const auto alone = s.mixer[(std::size_t) t].solo
                           && std::none_of (s.mixer.begin() + 1, s.mixer.end(), [&] (const MixerTrack& m) { return &m != &s.mixer[(std::size_t) t] && m.solo; });
        for (std::size_t i = 1; i < s.mixer.size(); ++i)
            s.mixer[i].solo = ! alone && (int) i == t;
    });
}

void MixerStrips::showSendMenu (int t)
{
    const auto& song = engine.getSong();
    const auto from = selected;

    if (from <= 0 || from == t)
        return;

    const auto fromName = juce::String::fromUTF8 (song.mixer[(std::size_t) from].name.c_str());
    juce::PopupMenu menu;
    menu.addSectionHeader (fromName + juce::String::fromUTF8 (" \xe2\x86\x92 ") + juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str()));
    const auto on = song.routes (from, t);
    menu.addItem (on ? tr ("Remove this send") : tr ("Send here"), on || song.canRoute (from, t), false, [this, from, t, on] {
        engine.editSong ([from, t, on] (Song& s) { s.setRoute (from, t, ! on); });
    });
    menu.addItem (tr ("Send only here"), on || song.canRoute (from, t), false, [this, from, t] {
        engine.editSong ([from, t] (Song& s) { mixerops::routeTo (s, { from }, t, true); });
    });
    menu.addItem (tr ("Sidechain only (no audio): the compressors here listen to it"), t > 0 && (on || song.canRoute (from, t)), false, [this, from, t] {
        int linked = 0;
        engine.editSong ([from, t, &linked] (Song& s) { linked = mixerops::sidechainTo (s, from, t); });
        if (linked == 0)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Sidechain"),
                                                    tr ("The link was made; add a compressor to this insert to use it."));
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void MixerStrips::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto t = trackAt (e.x);

    if (t > 0 && t < (int) engine.getSong().mixer.size() && e.mods.isShiftDown() && partAt (t, e.getPosition()) == Part::body)
    {
        if (! std::binary_search (selection.begin(), selection.end(), t))
            setSelection ({ t }, t);
        moveSelected ((wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX) > 0.0f ? -1 : 1);
        return;
    }

    if (t >= 0 && t < (int) engine.getSong().mixer.size())
        if (const auto part = partAt (t, e.getPosition()); part == Part::pan || part == Part::fader)
        {
            if (const auto v = ui::wheelValue (valueOf (part, t), e, wheel, specFor (part, t)))
                setValue (part, t, *v);
            return;
        }

    juce::Component::mouseWheelMove (e, wheel);
}

ui::Hint MixerStrips::hintAt (juce::Point<int> p)
{
    const auto active = dragPart == Part::pan || dragPart == Part::fader;
    const auto t = active ? dragTrack : trackAt (p.x);
    ui::Hint h;

    if (t < 0 || t >= (int) engine.getSong().mixer.size())
    {
        if (t == (int) engine.getSong().mixer.size())
            h.text = tr ("Add a mixer insert");
        return h;
    }

    const auto part = active ? dragPart : partAt (t, p);
    const auto name = juce::String::fromUTF8 (engine.getSong().mixer[(std::size_t) t].name.c_str());

    switch (part)
    {
        case Part::pan:
        case Part::fader:
            return ui::valueHint (specFor (part, t), valueOf (part, t), tr ("drag (Ctrl: fine), wheel, Alt+click resets"));
        case Part::mute: h.text = name + " - " + tr ("Click: mute.  Right-click or Ctrl+click: solo"); break;
        case Part::arm: h.text = name + " - " + tr ("Record arm: audio takes from the input play through this insert"); break;
        case Part::solo: h.text = name + " - " + tr ("Solo"); break;
        case Part::route: h.text = name + " - " + tr ("Send from the selected insert to this one (drag after enabling: level; right-click: sidechain, only here)"); h.hasMenu = true; break;
        case Part::add: h.text = tr ("Add a mixer insert"); break;
        case Part::body:
        case Part::none:
            h.text = name + " - " + tr ("Click selects the insert (Ctrl: add, Shift: range), right-click opens its menu; Alt+Left/Right moves the selection");
            h.hasMenu = true;
            break;
    }

    return h;
}

void MixerStrips::mouseUp (const juce::MouseEvent&)
{
    dragPart = Part::none;
    dragTrack = -1;
    drag.end();
}

void MixerStrips::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto t = trackAt (e.x);

    if (t < 0 || t >= (int) engine.getSong().mixer.size())
        return;

    const auto part = partAt (t, e.getPosition());

    if (part == Part::fader)
        engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].volume = unityFader; });
    else if (part == Part::pan)
        engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].pan = 0.0f; });
}

void MixerStrips::selectTrack (int track)
{
    selected = juce::jlimit (0, juce::jmax (0, (int) engine.getSong().mixer.size() - 1), track);
    selection = { selected };
    repaint();

    if (onSelectionChanged)
        onSelectionChanged (selected);
}

namespace
{
// The 3 knobs of each band of the track's built-in EQ (gain, frequency, width).
class TrackEqEditor final : public juce::Component
{
public:
    TrackEqEditor (AudioEngine& e, int t)
        : engine (e),
          track (t)
    {
        const juce::String bands[] = { tr ("Low"), tr ("Mid"), tr ("High") };
        const auto& eq = engine.getSong().mixer[(std::size_t) track].eq;

        for (int b = 0; b < 3; ++b)
        {
            titles[(std::size_t) b].setText (bands[b], juce::dontSendNotification);
            titles[(std::size_t) b].setJustificationType (juce::Justification::centred);
            addAndMakeVisible (titles[(std::size_t) b]);

            auto& gain = knobs[(std::size_t) b][0];
            setup (gain, -18.0, 18.0, 0.1, " dB", eq.gainDb[(std::size_t) b]);
            gain.setDoubleClickReturnValue (true, 0.0);

            auto& hz = knobs[(std::size_t) b][1];
            setup (hz, 10.0, 16000.0, 1.0, " Hz", eq.hz[(std::size_t) b]);
            hz.setSkewFactorFromMidPoint (1000.0);

            auto& width = knobs[(std::size_t) b][2];
            setup (width, 0.0, 1.0, 0.01, "", eq.width[(std::size_t) b]);
            width.setTooltip (b == 1 ? tr ("Peak width (Q)") : tr ("Shelf slope"));
        }

        setSize (3 * 90, 3 * 74 + 24);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (20);

        for (int b = 0; b < 3; ++b)
        {
            titles[(std::size_t) b].setBounds (header.removeFromLeft (90));
            auto column = area.removeFromLeft (90);

            for (auto& k : knobs[(std::size_t) b])
                k.setBounds (column.removeFromTop (74));
        }
    }

private:
    void setup (juce::Slider& s, double lo, double hi, double step, const char* suffix, float value)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setValue (value, juce::dontSendNotification);
        s.onValueChange = [this] { apply(); };
        addAndMakeVisible (s);
    }

    void apply()
    {
        TrackEq eq;

        for (std::size_t b = 0; b < 3; ++b)
        {
            eq.gainDb[b] = (float) knobs[b][0].getValue();
            eq.hz[b] = (float) knobs[b][1].getValue();
            eq.width[b] = (float) knobs[b][2].getValue();
        }

        engine.editSong ([t = track, eq] (Song& s) {
            if (t < (int) s.mixer.size())
                s.mixer[(std::size_t) t].eq = eq;
        });
    }

    AudioEngine& engine;
    int track;
    std::array<juce::Label, 3> titles;
    std::array<std::array<juce::Slider, 3>, 3> knobs; // [band][gain, frequency, width]
};
} // namespace

void MixerStrips::showTrackEq (int t)
{
    auto editor = std::make_unique<TrackEqEditor> (engine, t);
    juce::CallOutBox::launchAsynchronously (std::move (editor), getScreenBounds().withSize (10, 10).translated (getMouseXYRelative().x, getMouseXYRelative().y), nullptr);
}

void MixerStrips::showMenu (int t)
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();
    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str()));

    if (channel < (int) song.channels.size())
        menu.addItem (tr ("Route channel \"{0}\" here", { juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str()) }),
                      [this, channel, t] { engine.editSong ([channel, t] (Song& s) { s.channels[(std::size_t) channel].mixerTrack = t; }); });

    menu.addItem (tr ("Link the selected channels here") + "\tCtrl+L", [this, t] {
        setSelection (selection, t);
        linkSelectedChannels (false);
    });
    menu.addItem (tr ("Link the selected channels starting here") + "\tCtrl+Shift+L", [this, t] {
        setSelection (selection, t);
        linkSelectedChannels (true);
    });
    menu.addItem (tr ("Select the channels linked here") + "\tAlt+L", ! mixerops::channelsOn (song, t).empty(), false, [this, t] {
        setSelection (selection, t);
        selectLinkedChannels();
    });

    {
        // route the other selected inserts into this one
        std::vector<int> sources;
        for (auto s : selection)
            if (s > 0 && s != t)
                sources.push_back (s);

        if (! sources.empty())
        {
            menu.addItem (tr ("Route the selected inserts here"), [this, sources, t] {
                engine.editSong ([sources, t] (Song& s) { mixerops::routeTo (s, sources, t, false); });
            });
            menu.addItem (tr ("Route the selected inserts only here"), [this, sources, t] {
                engine.editSong ([sources, t] (Song& s) { mixerops::routeTo (s, sources, t, true); });
            });
        }
    }

    if (t > 0)
    {
        menu.addItem (tr ("Move left") + "\tAlt+Left", t > 1, false, [this, t] {
            setSelection ({ t }, t);
            moveSelected (-1);
        });
        menu.addItem (tr ("Move right") + "\tAlt+Right", t + 1 < (int) song.mixer.size(), false, [this, t] {
            setSelection ({ t }, t);
            moveSelected (1);
        });
    }
    menu.addSeparator();

    if (selected > 0 && selected != t && (t == 0 || song.canRoute (selected, t) || song.routes (selected, t)))
        menu.addItem (tr ("Send \"{0}\" only to this track", { juce::String::fromUTF8 (song.mixer[(std::size_t) selected].name.c_str()) }),
                      [this, from = selected, t] {
                          engine.editSong ([from, t] (Song& s) {
                              s.mixer[(std::size_t) from].sends.clear();
                              s.mixer[(std::size_t) from].toMaster = false;
                              s.setRoute (from, t, true);
                          });
                      });

    if (t > 0 && (! song.mixer[(std::size_t) t].sends.empty() || ! song.mixer[(std::size_t) t].toMaster))
        menu.addItem (tr ("Send only to the Master again"), [this, t] {
            engine.editSong ([t] (Song& s) {
                s.mixer[(std::size_t) t].sends.clear();
                s.mixer[(std::size_t) t].toMaster = true;
            });
        });

    {
        const auto& track = song.mixer[(std::size_t) t];
        juce::PopupMenu stereo;
        const std::pair<const char*, float> widths[] = { { TR_NOOP ("Mono"), 1.0f }, { TR_NOOP ("Narrower"), 0.5f }, { TR_NOOP ("Normal"), 0.0f },
                                                          { TR_NOOP ("Wider"), -0.5f }, { TR_NOOP ("Very wide"), -1.0f } };

        for (const auto& [label, value] : widths)
            stereo.addItem (tr (label), true, std::abs (track.stereo - value) < 0.01f,
                            [this, t, v = value] { engine.editSong ([t, v] (Song& s) { s.mixer[(std::size_t) t].stereo = v; }); });

        menu.addSubMenu (tr ("Stereo separation"), stereo);
        menu.addItem (tr ("Track EQ..."), [this, t] { showTrackEq (t); });
        menu.addItem (tr ("Invert phase"), true, track.invertPhase, [this, t] {
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].invertPhase = ! s.mixer[(std::size_t) t].invertPhase; });
        });
        menu.addItem (tr ("Swap left/right"), true, track.swapChannels, [this, t] {
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].swapChannels = ! s.mixer[(std::size_t) t].swapChannels; });
        });
        menu.addSeparator();
    }

    if (t > 0)
        menu.addItem (tr ("Render to audio (freeze)..."), [this, t] { bounceInsertToAudio (engine, t); });

    menu.addItem (tr ("Rename..."), [this, t] {
        auto* window = new juce::AlertWindow (tr ("Rename track"), {}, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", juce::String::fromUTF8 (engine.getSong().mixer[(std::size_t) t].name.c_str()));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, t, window] (int result) {
                                     const auto name = window->getTextEditorContents ("name").trim().toStdString();
                                     if (result == 1 && ! name.empty() && t < (int) engine.getSong().mixer.size())
                                         engine.editSong ([t, name] (Song& s) { s.mixer[(std::size_t) t].name = name; });
                                 }),
                                 true);
    });
    menu.addItem (tr ("Reset (0 dB, center, no mute/solo)"), [this, t] {
        engine.editSong ([t] (Song& s) {
            auto& track = s.mixer[(std::size_t) t];
            track.volume = unityFader;
            track.pan = 0.0f;
            track.muted = track.solo = false;
        });
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void MixerStrips::changeListenerCallback (juce::ChangeBroadcaster*)
{
    selected = juce::jlimit (0, juce::jmax (0, (int) engine.getSong().mixer.size() - 1), selected);
    const auto count = (int) engine.getSong().mixer.size();
    selection.erase (std::remove_if (selection.begin(), selection.end(), [count] (int t) { return t >= count; }), selection.end());
    if (! std::binary_search (selection.begin(), selection.end(), selected))
        selection = { selected };
    updateSize();
    repaint();
}

void MixerStrips::timerCallback()
{
    auto& meters = engine.getMeters();
    const auto showing = isShowing();

    for (std::size_t i = 0; i < levels.size(); ++i)
    {
        const auto peak = meters.takePeak ((int) (i / 2), (int) (i % 2));
        const auto next = juce::jmax (peak, levels[i] * 0.86f); // ~20 dB/s falloff at 30 Hz
        const auto shown = next < 0.001f ? 0.0f : next;

        // only the meter that changed is repainted
        if (showing && std::abs (shown - levels[i]) > 1e-4f)
        {
            const auto t = (int) (i / 2);
            const auto area = faderArea (t);
            repaint (area.getX(), area.getY(), 16, area.getHeight());
        }

        levels[i] = shown;
    }
}

// ---------------------------------------------------------------------------

namespace
{
constexpr int panelHeader = 30;
constexpr int slotHeight = 24;
const EffectKind allEffects[] = { EffectKind::eq,     EffectKind::parametricEq, EffectKind::compressor, EffectKind::limiter,
                                      EffectKind::delay,  EffectKind::reverb,       EffectKind::chorus,     EffectKind::filter,
                                      EffectKind::distortion, EffectKind::utility, EffectKind::volumeGate,
                                      EffectKind::pitchShift, EffectKind::freqShift, EffectKind::lofi, EffectKind::effector, EffectKind::crossover, EffectKind::autoPan, EffectKind::vocoder, EffectKind::pitchCorrect,
                                      EffectKind::fruityLimiter, EffectKind::fruityCompressor,
                                      EffectKind::fruityWaveShaper, EffectKind::fruitySoftClipper,
                                      EffectKind::reeverb2, EffectKind::reeverb1,
                                      EffectKind::fruityDelay2, EffectKind::fruityDelay3,
                                      EffectKind::stereoShaper, EffectKind::stereoEnhancer, EffectKind::transientProcessor,
                                      EffectKind::spreader, EffectKind::fruityChorus, EffectKind::hyperChorus, EffectKind::vintageChorus,
                                      EffectKind::fruityDelayBank, EffectKind::fruityMultiband };
} // namespace

const std::vector<EffectKind>& addableEffects()
{
    static const std::vector<EffectKind> list (std::begin (allEffects), std::end (allEffects));
    return list;
}

EffectsPanel::EffectsPanel (AudioEngine& e)
    : engine (e)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    engine.addChangeListener (this);
}

EffectsPanel::~EffectsPanel()
{
    engine.removeChangeListener (this);
}

void EffectsPanel::setTrack (int t)
{
    track = t;
    const auto* s = slots();
    selectedSlot = s != nullptr && ! s->empty() ? 0 : -1;
    repaint();
}

const std::vector<EffectSlot>* EffectsPanel::slots() const
{
    const auto& mixer = engine.getSong().mixer;
    return track >= 0 && track < (int) mixer.size() ? &mixer[(std::size_t) track].effects : nullptr;
}

juce::Rectangle<int> EffectsPanel::mixBounds (int slot) const
{
    return slotBounds (slot).removeFromRight (26);
}

juce::Rectangle<int> EffectsPanel::slotBounds (int slot) const
{
    return { 8, panelHeader + slot * slotHeight, getWidth() - 16, slotHeight - 3 };
}

namespace
{
// parameters shown as knobs: in the rhythmic envelope, only those of the points in use
int shownParams (const EffectSlot& slot)
{
    const auto all = (int) effectInfo (slot.kind).params.size();
    if (slot.kind == EffectKind::rhythmGain)
        return std::min (all, 2 + 3 * (int) std::lround (slot.params[1] * 12.0f));
    return all;
}
} // namespace

juce::Rectangle<int> EffectsPanel::knobBounds (int param) const
{
    auto top = panelHeader + maxEffectSlots * slotHeight + 40;
    // many parameters (Effector: 12): 4 columns and shorter rows to fit the height
    int count = 0;
    if (const auto* s = slots(); ! gateSelected() && s != nullptr && selectedSlot >= 0 && selectedSlot < (int) s->size())
        count = shownParams ((*s)[(std::size_t) selectedSlot]);
    const auto columns = count > 9 ? 4 : 3;
    const auto w = (getWidth() - 16) / columns;
    const auto rows = juce::jmax (1, (count + columns - 1) / columns);
    const auto rowHeight = count > 9 ? juce::jlimit (64, 86, (getHeight() - top - 4) / rows) : 86;

    // volume gate: the 32 steps are bars (gateStepsBounds); the knobs are mix/attack/release
    if (gateSelected())
    {
        if (param < 32)
            return {};
        param -= 32;
        top = gateStepsBounds().getBottom() + 22;
    }

    return { 8 + (param % columns) * w, top + (param / columns) * rowHeight, w, rowHeight - 4 };
}

bool EffectsPanel::gateSelected() const
{
    const auto* s = slots();
    return s != nullptr && selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind == EffectKind::volumeGate;
}

juce::Rectangle<int> EffectsPanel::gateStepsBounds() const
{
    return { 10, panelHeader + maxEffectSlots * slotHeight + 40, getWidth() - 20, 90 };
}

void EffectsPanel::setGateStep (juce::Point<int> p)
{
    const auto area = gateStepsBounds();
    const auto step = juce::jlimit (0, 31, (int) ((p.x - area.getX()) * 32 / juce::jmax (1, area.getWidth())));
    const auto value = juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - p.y) / (float) area.getHeight());
    engine.editSong ([t = track, slot = selectedSlot, step, value] (Song& song) {
        song.mixer[(std::size_t) t].effects[(std::size_t) slot].params[(std::size_t) step] = value;
    });
}

void EffectsPanel::showGatePatterns()
{
    // ready-made patterns (like Gross Beat's slots): value of step i of 32 (1/8 beat each)
    struct Pattern
    {
        const char* name;
        float (*level) (int);
    };
    static const Pattern patterns[] = {
        { TR_NOOP ("All open"), [] (int) { return 1.0f; } },
        { TR_NOOP ("1/2 beat gate"), [] (int i) { return (i % 4) < 2 ? 1.0f : 0.0f; } },
        { TR_NOOP ("1 beat gate"), [] (int i) { return (i % 8) < 4 ? 1.0f : 0.0f; } },
        { TR_NOOP ("Offbeat"), [] (int i) { return (i % 8) >= 4 ? 1.0f : 0.0f; } },
        { TR_NOOP ("Sidechain (ramp per beat)"), [] (int i) { return std::min (1.0f, 0.15f + (float) (i % 8) / 5.0f); } },
        { TR_NOOP ("Trance gate"), [] (int i) { return (i % 4) == 3 ? 0.0f : (i % 16) >= 12 ? 0.4f : 1.0f; } },
        { TR_NOOP ("Fade-in over the bar"), [] (int i) { return (float) (i + 1) / 32.0f; } },
        { TR_NOOP ("Fade-out over the bar"), [] (int i) { return 1.0f - (float) i / 32.0f; } },
    };

    juce::PopupMenu menu;
    for (const auto& p : patterns)
        menu.addItem (tr (p.name), [this, level = p.level] {
            engine.editSong ([t = track, slot = selectedSlot, level] (Song& song) {
                auto& params = song.mixer[(std::size_t) t].effects[(std::size_t) slot].params;
                for (int i = 0; i < 32; ++i)
                    params[(std::size_t) i] = level (i);
            });
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

int EffectsPanel::knobAt (juce::Point<int> p) const
{
    const auto* s = slots();

    if (s == nullptr || selectedSlot < 0 || selectedSlot >= (int) s->size())
        return -1;

    if ((*s)[(std::size_t) selectedSlot].kind == EffectKind::parametricEq)
        return -1; // edited in its own window

    const auto count = shownParams ((*s)[(std::size_t) selectedSlot]);

    for (int i = gateSelected() ? 32 : 0; i < count; ++i)
        if (knobBounds (i).contains (p))
            return i;

    return -1;
}

void EffectsPanel::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg2);
    g.setColour (colour::border);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    const auto* s = slots();

    if (s == nullptr)
        return;

    const auto& song = engine.getSong();
    g.setColour (colour::textPrimary);
    g.setFont (font (type::title, true));
    g.drawText (tr ("Effects: {0}", { juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str()) }), 10, 4,
                getWidth() - 70, panelHeader - 8, juce::Justification::centredLeft, true);

    // the insert's effects switch
    if (! s->empty())
    {
        const auto on = mixerops::anyEffectEnabled (song, track);
        const auto r = fxSwitchBounds().toFloat();
        g.setColour (on ? colour::bg5 : colour::bg0);
        g.fillRoundedRectangle (r, radius::sm);
        g.setColour (muteLed (! on));
        g.fillEllipse (r.withWidth (r.getHeight()).reduced (6.0f).translated (3.0f, 0.0f));
        g.setColour (on ? colour::textPrimary : colour::textDisabled);
        g.setFont (font (type::caption, true));
        g.drawText ("FX", r.withTrimmedLeft (r.getHeight()).toNearestInt(), juce::Justification::centredLeft);
    }

    g.setFont (font (type::body));

    for (int i = 0; i < maxEffectSlots; ++i)
    {
        const auto r = slotBounds (i);
        const auto filled = i < (int) s->size();
        const auto isSelected = i == selectedSlot && filled;
        g.setColour (isSelected ? colour::bg5 : filled ? colour::bg3 : colour::bg2.brighter (0.03f));
        g.fillRoundedRectangle (r.toFloat(), radius::sm);

        if (! filled)
        {
            g.setColour (colour::border);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), radius::sm, 1.0f);
        }

        if (isSelected)
        {
            g.setColour (colour::accent);
            g.fillRoundedRectangle (r.toFloat().withWidth (3.0f).reduced (0.0f, 3.0f), 1.5f);
        }

        if (filled)
        {
            const auto& slot = (*s)[(std::size_t) i];
            g.setColour (muteLed (! slot.enabled));
            g.fillEllipse ((float) r.getX() + 8, (float) r.getCentreY() - 4, 8, 8);
            g.setColour (slot.enabled ? colour::textPrimary : colour::textDisabled);
            auto name = slot.kind == EffectKind::plugin ? juce::String::fromUTF8 (slot.plugin.name.c_str())
                                                        : juce::String::fromUTF8 (effectInfo (slot.kind).name);
            if (slot.sidechain > 0 && slot.sidechain < (int) engine.getSong().mixer.size())
                name << juce::String::fromUTF8 (" \xe2\x86\x90 ") << juce::String::fromUTF8 (engine.getSong().mixer[(std::size_t) slot.sidechain].name.c_str());
            g.drawText (juce::String (i + 1) + ". " + name, r.withTrimmedLeft (24).withTrimmedRight (28), juce::Justification::centredLeft);

            // slot's dry/wet knob
            drawKnob (g, mixBounds (i).toFloat().withSizeKeepingCentre (20.0f, 20.0f), slot.mix, false,
                      slot.enabled ? colour::info : colour::textDisabled);
        }
        else if (i == (int) s->size())
        {
            g.setColour (colour::textDisabled);
            g.drawText (juce::String (i + 1) + ". " + tr ("(empty: click to add)"), r.withTrimmedLeft (24), juce::Justification::centredLeft);
        }
    }

    if (dragSlot >= 0 && dropSlot >= 0 && dropSlot != dragSlot) // where the dragged slot lands
    {
        const auto r = slotBounds (dropSlot);
        const auto y = dropSlot > dragSlot ? r.getBottom() + 1 : r.getY() - 2;
        g.setColour (colour::accent);
        g.fillRect (r.getX(), y, r.getWidth(), 2);
    }

    if (selectedSlot < 0 || selectedSlot >= (int) s->size())
        return;

    const auto& slot = (*s)[(std::size_t) selectedSlot];

    auto drawButton = [&g] (juce::Rectangle<int> button, const juce::String& text, Icon icon) {
        g.setColour (colour::bg4);
        g.fillRoundedRectangle (button.toFloat(), radius::md);
        const auto f = juce::Font (font (type::body));
        const auto textWidth = juce::GlyphArrangement::getStringWidth (f, text);
        auto content = button.toFloat().withSizeKeepingCentre (juce::jmin ((float) button.getWidth() - 12.0f, textWidth + 22.0f), (float) button.getHeight());
        icons::draw (g, icon, content.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f), colour::textSecondary);
        content.removeFromLeft (6.0f);
        g.setColour (colour::textPrimary);
        g.setFont (f);
        g.drawFittedText (text, content.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
    };

    if (slot.kind == EffectKind::plugin)
    {
        const auto button = pluginButtonBounds();
        drawButton (button, tr ("Open plugin interface"), Icon::plugin);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        g.drawText (juce::String::fromUTF8 (slot.plugin.name.c_str()), button.translated (0, 34), juce::Justification::centred);
        return;
    }

    if (slot.kind == EffectKind::parametricEq)
    {
        const auto button = pluginButtonBounds();
        drawButton (button, tr ("Open parametric EQ"), Icon::settings);
        const auto curve = juce::Rectangle<int> (button.getX(), button.getBottom() + 10, button.getWidth(), 120).toFloat();
        drawParametricEqCurve (g, curve, slot.params, juce::jmax (8000.0, engine.getTransport().sampleRate.load()), slot.enabled);
        return;
    }

    if (slot.kind == EffectKind::parallel) // container: list the branches
    {
        auto y = knobBounds (0).getY() - 28;
        g.setColour (colour::textPrimary);
        g.setFont (font (type::title, true));
        g.drawText (juce::String::fromUTF8 (effectInfo (slot.kind).name), 10, y, getWidth() - 20, 20, juce::Justification::centredLeft);
        g.setFont (font (type::small));
        for (std::size_t b = 0; b < slot.branches.size(); ++b)
        {
            y += 22;
            juce::String chain;
            if (slot.branches[b].chain.empty())
                chain = tr ("dry");
            for (std::size_t c = 0; c < slot.branches[b].chain.size(); ++c)
                chain += (c > 0 ? " -> " : "") + juce::String::fromUTF8 (effectInfo (slot.branches[b].chain[c].kind).name);
            const auto line = tr ("Branch {0} (x{1}): {2}", { juce::String ((int) b + 1), juce::String (slot.branches[b].gain, 2), chain });
            g.setColour (colour::textSecondary);
            g.drawText (line, 14, y, getWidth() - 28, 20, juce::Justification::centredLeft, true);
        }
        return;
    }

    const auto& info = effectInfo (slot.kind);
    const auto gate = slot.kind == EffectKind::volumeGate;
    g.setColour (colour::textPrimary);
    g.setFont (font (type::title, true));
    g.drawText (juce::String::fromUTF8 (info.name), 10, (gate ? gateStepsBounds().getY() : knobBounds (0).getY()) - 28, getWidth() - 20, 20,
                juce::Justification::centredLeft);

    if (gate) // 32 steps = 1 bar of 4 beats
    {
        const auto area = gateStepsBounds().toFloat();
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (area, radius::sm);
        const auto w = area.getWidth() / 32.0f;
        for (int i = 0; i < 32; ++i)
        {
            const auto h = area.getHeight() * juce::jlimit (0.0f, 1.0f, slot.params[(std::size_t) i]);
            g.setColour ((slot.enabled ? colour::accent : colour::textDisabled).withAlpha ((i / 8) % 2 == 0 ? 1.0f : 0.7f));
            g.fillRect (area.getX() + i * w + 1.0f, area.getBottom() - h, w - 2.0f, h);
        }
        g.setColour (colour::gridBar);
        for (int beat = 1; beat < 4; ++beat)
            g.drawVerticalLine ((int) (area.getX() + beat * 8 * w), area.getY(), area.getBottom());
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        g.drawFittedText (tr ("1 bar: draw the steps; right-click = patterns"), area.withY (area.getBottom() + 2).withHeight (14).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }

    for (int p = gate ? 32 : 0; p < shownParams (slot); ++p)
    {
        const auto r = knobBounds (p);
        const auto value = slot.params[(std::size_t) p];
        // 38 px knob; in tight grids (Effector) it shrinks to fit name and value below
        const auto knobHeight = juce::jlimit (30, 44, r.getHeight() - 30);
        drawKnob (g, r.withHeight (knobHeight).toFloat(), value, false, slot.enabled ? colour::knobArc : colour::textDisabled, dragParam == p);

        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        g.drawFittedText (juce::String::fromUTF8 (info.params[(std::size_t) p].name), r.getX(), r.getY() + knobHeight + 2, r.getWidth(), 14,
                          juce::Justification::centred, 1, 0.8f);
        g.setColour (colour::textPrimary);
        g.drawText (juce::String::fromUTF8 (formatEffectParam (slot.kind, p, value, song.bpm, slot.params).c_str()), r.getX(), r.getY() + knobHeight + 16,
                    r.getWidth(), 14, juce::Justification::centred);
    }
}

void EffectsPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto* s = slots();

    if (s == nullptr)
        return;

    if (! s->empty() && fxSwitchBounds().contains (e.getPosition()))
    {
        if (e.mods.isPopupMenu())
            toggleAllWindows();
        else
        {
            const auto on = ! mixerops::anyEffectEnabled (engine.getSong(), track);
            engine.editSong ([t = track, on] (Song& song) { mixerops::setAllEffectsEnabled (song, t, on); });
        }
        return;
    }

    for (int i = 0; i < maxEffectSlots; ++i)
    {
        const auto r = slotBounds (i);

        if (! r.contains (e.getPosition()))
            continue;

        if (i >= (int) s->size())
        {
            showAddMenu (i);
            return;
        }

        selectedSlot = i;
        repaint();

        if (mixBounds (i).contains (e.getPosition()))
        {
            if (e.mods.isPopupMenu())
                ui::showValueMenu (*this, &engine, specFor (mixParam, i), valueOf (mixParam, i), [this, i] (double v) { setValue (mixParam, i, v); });
            else if (ui::isResetClick (e))
                setValue (mixParam, i, 1.0);
            else
            {
                dragParam = mixParam;
                dragStart = (*s)[(std::size_t) i].mix;
                drag.begin (e, dragStart);
            }
            return;
        }

        if (e.mods.isPopupMenu())
            showSlotMenu (i);
        else if (e.x < r.getX() + 22) // LED: bypass
            engine.editSong ([t = track, i] (Song& song) {
                auto& slot = song.mixer[(std::size_t) t].effects[(std::size_t) i];
                slot.enabled = ! slot.enabled;
            });
        else
            dragSlot = i; // dragging the name reorders the slot
        return;
    }

    if (selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind == EffectKind::plugin
        && pluginButtonBounds().contains (e.getPosition()))
    {
        openPluginWindow (selectedSlot);
        return;
    }

    if (selectedSlot >= 0 && selectedSlot < (int) s->size() && (*s)[(std::size_t) selectedSlot].kind == EffectKind::parametricEq
        && pluginButtonBounds().withHeight (170).contains (e.getPosition()))
    {
        ParametricEqWindows::open (engine, track, selectedSlot);
        return;
    }

    if (gateSelected() && gateStepsBounds().contains (e.getPosition()))
    {
        if (e.mods.isPopupMenu())
            showGatePatterns();
        else
        {
            dragParam = gateStepParam;
            setGateStep (e.getPosition());
        }
        return;
    }

    dragParam = knobAt (e.getPosition());

    if (dragParam >= 0 && e.mods.isPopupMenu())
    {
        const auto p = dragParam;
        ui::showValueMenu (*this, &engine, specFor (p, selectedSlot), valueOf (p, selectedSlot),
                           [this, p, slot = selectedSlot] (double v) { setValue (p, slot, v); });
        dragParam = -1;
        return;
    }

    if (dragParam >= 0 && ui::isResetClick (e))
    {
        setValue (dragParam, selectedSlot, specFor (dragParam, selectedSlot).defaultValue);
        dragParam = -1;
        return;
    }

    if (dragParam >= 0)
    {
        dragStart = (*s)[(std::size_t) selectedSlot].params[(std::size_t) dragParam];
        drag.begin (e, dragStart);
    }
}

ui::ValueSpec EffectsPanel::specFor (int param, int slot) const
{
    const auto& song = engine.getSong();
    ui::ValueSpec spec;
    spec.pixelsForRange = 150.0;

    if (const auto* s = slots(); s == nullptr || slot < 0 || slot >= (int) s->size())
        return spec;

    const auto& fx = (*slots())[(std::size_t) slot];
    const auto& info = effectInfo (fx.kind);
    const auto trackName = juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str());

    if (param == mixParam)
    {
        spec = ui::percentSpec (trackName + " - " + utf8 (info.name) + " - " + tr ("Mix"), 1.0);
        spec.target = AutomationTarget { AutomationTargetKind::effectMix, track, slot };
        return spec;
    }

    if (param < 0 || param >= (int) info.params.size())
        return spec;

    spec.name = trackName + " - " + utf8 (info.name) + " - " + utf8 (info.params[(std::size_t) param].name);
    spec.defaultValue = info.params[(std::size_t) param].defaultValue;
    spec.toText = [kind = fx.kind, param, bpm = song.bpm, context = fx.params] (double v) { return utf8 (formatEffectParam (kind, param, (float) v, bpm, context).c_str()); };
    spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
    spec.target = AutomationTarget { AutomationTargetKind::effectParam, track, slot, param };
    return spec;
}

double EffectsPanel::valueOf (int param, int slot) const
{
    const auto* s = slots();

    if (s == nullptr || slot < 0 || slot >= (int) s->size())
        return 0.0;

    const auto& fx = (*s)[(std::size_t) slot];
    return param == mixParam ? fx.mix : (param >= 0 && param < (int) fx.params.size() ? fx.params[(std::size_t) param] : 0.0f);
}

void EffectsPanel::setValue (int param, int slot, double value)
{
    const auto* s = slots();

    if (s == nullptr || slot < 0 || slot >= (int) s->size())
        return;

    if (param == mixParam)
        engine.editSong ([t = track, slot, v = (float) value] (Song& song) { song.mixer[(std::size_t) t].effects[(std::size_t) slot].mix = v; });
    else if (param >= 0)
        engine.editSong ([t = track, slot, param, v = (float) value] (Song& song) {
            song.mixer[(std::size_t) t].effects[(std::size_t) slot].params[(std::size_t) param] = v;
        });
}

int EffectsPanel::mixKnobAt (juce::Point<int> p) const
{
    for (int i = 0; slots() != nullptr && i < (int) slots()->size(); ++i)
        if (mixBounds (i).contains (p))
            return i;

    return -1;
}

void EffectsPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (const auto slot = mixKnobAt (e.getPosition()); slot >= 0)
    {
        if (const auto v = ui::wheelValue (valueOf (mixParam, slot), e, wheel, specFor (mixParam, slot)))
            setValue (mixParam, slot, *v);
        return;
    }

    if (const auto p = knobAt (e.getPosition()); p >= 0)
    {
        if (const auto v = ui::wheelValue (valueOf (p, selectedSlot), e, wheel, specFor (p, selectedSlot)))
            setValue (p, selectedSlot, *v);
        return;
    }

    // the wheel over a slot's name moves it up or down
    if (const auto slot = slotAt (e.getPosition()); slot >= 0)
    {
        const auto to = slot + ((wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX) > 0.0f ? -1 : 1);
        bool moved = false;
        engine.editSong ([t = track, slot, to, &moved] (Song& song) { moved = mixerops::moveEffectSlot (song, t, slot, to); });
        if (moved)
            selectedSlot = to;
        repaint();
        return;
    }

    juce::Component::mouseWheelMove (e, wheel);
}

ui::Hint EffectsPanel::hintAt (juce::Point<int> p)
{
    const auto gesture = tr ("drag (Ctrl: fine), wheel, Alt+click resets");

    if (dragParam == mixParam || (dragParam >= 0 && drag.isActive()))
        return ui::valueHint (specFor (dragParam, dragParam == mixParam ? juce::jmax (0, selectedSlot) : selectedSlot),
                              valueOf (dragParam, dragParam == mixParam ? juce::jmax (0, selectedSlot) : selectedSlot), gesture);

    if (const auto slot = mixKnobAt (p); slot >= 0)
        return ui::valueHint (specFor (mixParam, slot), valueOf (mixParam, slot), gesture);

    if (const auto param = knobAt (p); param >= 0)
        return ui::valueHint (specFor (param, selectedSlot), valueOf (param, selectedSlot), gesture);

    ui::Hint h;
    if (fxSwitchBounds().contains (p) && slots() != nullptr && ! slots()->empty())
    {
        h.text = tr ("All effects of this insert on/off; right-click opens or closes their plugin windows");
        h.hasMenu = true;
        return h;
    }

    for (int i = 0; i < maxEffectSlots; ++i)
        if (slotBounds (i).contains (p))
        {
            const auto used = slots() != nullptr && i < (int) slots()->size();
            h.text = used ? tr ("Effect slot: click edits, the LED bypasses, drag or wheel reorders, right-click: replace, move, delete")
                          : tr ("Empty slot: click to add an effect");
            h.hasMenu = used;
        }

    return h;
}

void EffectsPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSlot >= 0)
    {
        if (e.getDistanceFromDragStart() > 4)
        {
            const auto count = slots() != nullptr ? (int) slots()->size() : 0;
            const auto target = juce::jlimit (0, juce::jmax (0, count - 1), (e.y - panelHeader) / slotHeight);
            if (target != dropSlot)
            {
                dropSlot = target;
                setMouseCursor (juce::MouseCursor::DraggingHandCursor);
                repaint();
            }
        }
        return;
    }

    if (dragParam == gateStepParam)
    {
        setGateStep (e.getPosition());
        return;
    }

    if (dragParam == mixParam)
    {
        setValue (mixParam, selectedSlot, drag.update (e, specFor (mixParam, selectedSlot)));
        return;
    }

    if (dragParam < 0)
        return;

    setValue (dragParam, selectedSlot, drag.update (e, specFor (dragParam, selectedSlot)));
}

void EffectsPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragSlot >= 0 && dropSlot >= 0 && dropSlot != dragSlot)
    {
        bool moved = false;
        engine.editSong ([t = track, from = dragSlot, to = dropSlot, &moved] (Song& song) { moved = mixerops::moveEffectSlot (song, t, from, to); });
        if (moved)
            selectedSlot = dropSlot;
    }

    if (dragSlot >= 0)
        setMouseCursor (juce::MouseCursor::NormalCursor);

    dragSlot = dropSlot = -1;
    repaint();
    dragParam = -1;
    drag.end();
}

void EffectsPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int i = 0; slots() != nullptr && i < (int) slots()->size(); ++i)
        if (mixBounds (i).contains (e.getPosition()))
        {
            engine.editSong ([t = track, i] (Song& song) { song.mixer[(std::size_t) t].effects[(std::size_t) i].mix = 1.0f; });
            return;
        }

    const auto p = knobAt (e.getPosition());

    if (p < 0)
        return;

    const auto kind = (*slots())[(std::size_t) selectedSlot].kind;
    const auto value = effectInfo (kind).params[(std::size_t) p].defaultValue;
    engine.editSong ([t = track, slot = selectedSlot, p, value] (Song& song) {
        song.mixer[(std::size_t) t].effects[(std::size_t) slot].params[(std::size_t) p] = value;
    });
}

bool EffectsPanel::isInterestedInDragSource (const SourceDetails& details)
{
    return BrowserComponent::isBrowserDrag (details) && BrowserComponent::draggedFile (details).hasFileExtension ("fst");
}

void EffectsPanel::itemDropped (const SourceDetails& details)
{
    dropPreset (BrowserComponent::draggedFile (details), details.localPosition);
}

bool EffectsPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && juce::File (files[0]).hasFileExtension ("fst");
}

void EffectsPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (! files.isEmpty())
        dropPreset (juce::File (files[0]), { x, y });
}

bool EffectsPanel::dropPreset (const juce::File& file, juce::Point<int> position)
{
    juce::MemoryBlock data;

    if (slots() == nullptr || ! file.loadFileAsData (data))
        return false;

    const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
    const auto fx = effectFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), engine.getSong().bpm);

    if (! fx)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Preset"),
                                                tr ("LYNX can't build an effect from this preset."));
        return false;
    }

    auto target = -1;
    for (int i = 0; i < (int) slots()->size(); ++i)
        if (slotBounds (i).contains (position))
            target = i;

    if (target < 0 && (int) slots()->size() >= maxEffectSlots)
        return false;

    engine.editSong ([t = track, target, fx = *fx] (Song& song) {
        auto& effects = song.mixer[(std::size_t) t].effects;
        if (target >= 0 && target < (int) effects.size())
            effects[(std::size_t) target] = fx;
        else
            effects.push_back (fx);
    });
    selectedSlot = target >= 0 ? target : (int) slots()->size() - 1;
    repaint();
    return true;
}

juce::Rectangle<int> EffectsPanel::fxSwitchBounds() const
{
    return { getWidth() - 58, 6, 48, panelHeader - 12 };
}

int EffectsPanel::slotAt (juce::Point<int> p) const
{
    const auto* s = slots();
    for (int i = 0; s != nullptr && i < (int) s->size(); ++i)
        if (slotBounds (i).contains (p) && ! mixBounds (i).contains (p) && p.x >= slotBounds (i).getX() + 22)
            return i;
    return -1;
}

void EffectsPanel::toggleAllWindows()
{
    // opens every plugin effect window of the insert, or closes them all when one is open
    const auto* s = slots();
    if (s == nullptr)
        return;

    bool anyOpen = false;
    for (int i = 0; i < (int) s->size(); ++i)
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (track, i).get()))
            anyOpen = anyOpen || PluginWindows::isOpen (p->getInstance());

    for (int i = 0; i < (int) s->size(); ++i)
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (track, i).get()))
        {
            if (anyOpen)
                PluginWindows::close (p->getInstance());
            else
                openPluginWindow (i);
        }
}

juce::Rectangle<int> EffectsPanel::pluginButtonBounds() const
{
    return { 20, panelHeader + maxEffectSlots * slotHeight + 30, getWidth() - 40, 30 };
}

void EffectsPanel::openPluginWindow (int slot)
{
    auto effect = engine.getEffect (track, slot);

    if (auto* plugin = dynamic_cast<PluginEffect*> (effect.get()))
    {
        const auto& s = engine.getSong().mixer[(std::size_t) track].effects[(std::size_t) slot];
        PluginWindows::open (effect, plugin->getInstance(), juce::String::fromUTF8 (s.plugin.name.c_str()));
        return;
    }

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Plugin"),
                                            tr ("This plugin could not be loaded (it is not installed or has not been scanned)."));
}

void EffectsPanel::showAddMenu (int)
{
    juce::PopupMenu menu;

    for (auto kind : allEffects)
        menu.addItem (juce::String::fromUTF8 (effectInfo (kind).name), [this, kind] {
            engine.editSong ([t = track, kind] (Song& song) {
                auto& effects = song.mixer[(std::size_t) t].effects;
                if ((int) effects.size() < maxEffectSlots)
                    effects.push_back (makeEffectSlot (kind));
            });
            selectedSlot = (int) slots()->size() - 1;
            repaint();
        });

    juce::PopupMenu plugins;
    addPluginItems (plugins, engine, false, [this] (PluginRef ref) {
        engine.editSong ([t = track, ref] (Song& song) {
            auto& effects = song.mixer[(std::size_t) t].effects;
            if ((int) effects.size() >= maxEffectSlots)
                return;
            EffectSlot slot;
            slot.kind = EffectKind::plugin;
            slot.plugin = ref;
            effects.push_back (slot);
        });
        selectedSlot = (int) slots()->size() - 1;
        openPluginWindow (selectedSlot);
        repaint();
    });
    menu.addSeparator();
    menu.addSubMenu (tr ("Plugin (VST3/CLAP)"), plugins);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void EffectsPanel::showSlotMenu (int slot)
{
    juce::PopupMenu replace;

    for (auto kind : allEffects)
        replace.addItem (juce::String::fromUTF8 (effectInfo (kind).name), [this, slot, kind] {
            engine.editSong ([t = track, slot, kind] (Song& song) { song.mixer[(std::size_t) t].effects[(std::size_t) slot] = makeEffectSlot (kind); });
        });

    const auto count = (int) slots()->size();
    juce::PopupMenu menu;
    menu.addSubMenu (tr ("Replace with"), replace);

    // FL presets (Plugin presets\Effects\<plugin>) that map to this effect
    {
        juce::PopupMenu presets;
        const auto roots = findFlStudioRoots();
        const auto base = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches/Plugin presets/Effects");

        for (const auto& plugin : flPluginsForEffect ((*slots())[(std::size_t) slot].kind))
        {
            juce::PopupMenu sub;
            auto files = base.getChildFile (juce::String::fromUTF8 (plugin.c_str())).findChildFiles (juce::File::findFiles, true, "*.fst");
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });

            for (const auto& f : files)
                sub.addItem (f.getFileNameWithoutExtension(), [this, slot, f] {
                    juce::MemoryBlock data;
                    if (! f.loadFileAsData (data))
                        return;
                    const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
                    if (auto fx = effectFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), engine.getSong().bpm))
                        engine.editSong ([t = track, slot, fx = *fx] (Song& song) {
                            auto& target = song.mixer[(std::size_t) t].effects[(std::size_t) slot];
                            const auto enabled = target.enabled;
                            const auto mix = target.mix;
                            target = fx;
                            target.enabled = enabled;
                            target.mix = mix;
                        });
                });

            if (sub.getNumItems() > 0)
                presets.addSubMenu (juce::String::fromUTF8 (plugin.c_str()), sub);
        }

        if (presets.getNumItems() > 0)
            menu.addSubMenu (tr ("FL presets"), presets);
    }

    // side inputs: another track's output feeds the compressor's detector, or the vocoder's modulator/carrier
    const auto kind = (*slots())[(std::size_t) slot].kind;
    auto sideMenu = [&] (const juce::String& title, bool second) {
        juce::PopupMenu side;
        const auto& fx = (*slots())[(std::size_t) slot];
        const auto current = second ? fx.sidechain2 : fx.sidechain;
        auto assign = [this, slot, second] (int source) {
            engine.editSong ([tk = track, slot, second, source] (Song& s) {
                auto& target = s.mixer[(std::size_t) tk].effects[(std::size_t) slot];
                (second ? target.sidechain2 : target.sidechain) = source;
                if (target.kind == EffectKind::vocoder && source >= 0)
                    target.params[16] = 1.0f; // separate inputs (otherwise L = modulator, R = carrier)
            });
        };
        side.addItem (tr ("None"), true, current < 0, [assign] { assign (-1); });
        const auto& song = engine.getSong();
        for (int t = 1; t < (int) song.mixer.size(); ++t)
            if (t != track)
                side.addItem (juce::String (t) + " " + juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str()), true, current == t,
                              [assign, t] { assign (t); });
        menu.addSubMenu (title, side);
    };
    if (kind == EffectKind::compressor || kind == EffectKind::fruityLimiter || kind == EffectKind::fruityCompressor)
        sideMenu (tr ("Sidechain from"), false);
    if (kind == EffectKind::vocoder || kind == EffectKind::vocodex)
    {
        sideMenu (tr ("Modulator from"), false);
        sideMenu (tr ("Carrier from"), true);
    }
    {
        // A/B compare: keep a copy of the settings, then swap between it and the current ones
        const auto key = std::make_pair (track, slot);
        const auto hasSpare = spare.count (key) > 0 && spare[key].kind == (*slots())[(std::size_t) slot].kind;
        juce::PopupMenu ab;
        ab.addItem (tr ("Store the current settings as B"), [this, key, slot] {
            spare[key] = (*slots())[(std::size_t) slot];
        });
        ab.addItem (tr ("Swap with B"), hasSpare, false, [this, key, slot] {
            const auto stored = spare[key];
            spare[key] = (*slots())[(std::size_t) slot];
            engine.editSong ([t = track, slot, stored] (Song& song) {
                auto& target = song.mixer[(std::size_t) t].effects[(std::size_t) slot];
                const auto enabled = target.enabled;
                target = stored;
                target.enabled = enabled;
            });
        });
        ab.addItem (tr ("Forget B"), hasSpare, false, [this, key] { spare.erase (key); });
        menu.addSubMenu (hasSpare ? tr ("A/B compare (B stored)") : tr ("A/B compare"), ab);
    }
    menu.addItem (tr ("Move up"), slot > 0, false, [this, slot] {
        engine.editSong ([t = track, slot] (Song& song) { song.swapEffectSlots (t, slot, slot - 1); });
        selectedSlot = slot - 1;
    });
    menu.addItem (tr ("Move down"), slot < count - 1, false, [this, slot] {
        engine.editSong ([t = track, slot] (Song& song) { song.swapEffectSlots (t, slot, slot + 1); });
        selectedSlot = slot + 1;
    });
    if ((*slots())[(std::size_t) slot].kind == EffectKind::plugin)
    {
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (track, slot).get()))
            addPluginParameterItems (menu, engine, p->getInstance(), { AutomationTargetKind::effectParam, track, slot, 0 });
    }

    menu.addSeparator();
    menu.addItem (tr ("Delete"), [this, slot] {
        engine.editSong ([t = track, slot] (Song& song) { song.removeEffectSlot (t, slot); });
        selectedSlot = juce::jmin (slot, (int) slots()->size() - 1);
        repaint();
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void EffectsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto* s = slots();

    if (s == nullptr)
        track = 0;
    else if (selectedSlot >= (int) s->size())
        selectedSlot = (int) s->size() - 1;

    repaint();
}

// ---------------------------------------------------------------------------

MixerComponent::MixerComponent (AudioEngine& e)
    : engine (e),
      strips (e),
      effects (e)
{
    viewport.setViewedComponent (&strips, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (effects);
    strips.onSelectionChanged = [this] (int track) { effects.setTrack (track); };
}

bool MixerComponent::runAction (const std::string& id)
{
    if (id == "mixer.moveLeft")
        strips.moveSelected (-1);
    else if (id == "mixer.moveRight")
        strips.moveSelected (1);
    else if (id == "mixer.linkChannels")
        strips.linkSelectedChannels (false);
    else if (id == "mixer.linkChannelsFrom")
        strips.linkSelectedChannels (true);
    else if (id == "mixer.selectLinkedChannels")
        strips.selectLinkedChannels();
    else
        return false;

    return true;
}

bool MixerComponent::addEffectToSelected (EffectSlot slot)
{
    const auto t = strips.getSelectedTrack();
    bool added = false;
    engine.editSong ([t, &slot, &added] (Song& s) {
        if (s.mixer.empty())
            s.mixer = Song::defaultMixer (0);
        if (t < (int) s.mixer.size() && (int) s.mixer[(std::size_t) t].effects.size() < maxEffectSlots)
        {
            s.mixer[(std::size_t) t].effects.push_back (std::move (slot));
            added = true;
        }
    });

    if (added)
    {
        effects.setTrack (t);
        effects.selectSlot ((int) engine.getSong().mixer[(std::size_t) t].effects.size() - 1);
    }

    return added;
}

void MixerComponent::selectFirstTrackWithEffects()
{
    const auto& mixer = engine.getSong().mixer;

    for (int t = 0; t < (int) mixer.size(); ++t)
        if (! mixer[(std::size_t) t].effects.empty())
        {
            strips.selectTrack (t);
            return;
        }
}

void MixerComponent::resized()
{
    auto area = getLocalBounds();
    effects.setBounds (area.removeFromRight (effectsWidth));
    viewport.setBounds (area);
    strips.setVisibleHeight (area.getHeight() - viewport.getScrollBarThickness());
}
} // namespace lynx
