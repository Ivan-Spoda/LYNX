#include "ui/MixerComponent.h"

#include "core/FlEffects.h"
#include "ui/AutomationUi.h"
#include "ui/ExportDialog.h"

#include "core/Effects.h"
#include "engine/Text.h"
#include "ui/ParametricEqEditor.h"
#include "ui/PluginWindow.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
constexpr int headerHeight = 34;
constexpr int panHeight = 34;
constexpr int buttonsHeight = 22;
constexpr int feedsHeight = 46;

// Fader 0..1 -> dB, com 0,8 = 0 dB (como no FL).
juce::String faderToDb (float volume)
{
    if (volume <= 0.0001f)
        return "-inf";

    const auto db = juce::Decibels::gainToDecibels (faderGain (volume));
    return (db > 0 ? "+" : "") + juce::String (db, 1);
}

// Nível 0..1 do medidor em escala de dB (-60..+6).
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

    for (int t = 0; t <= tracks; ++t) // t == tracks = botão "+"
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

juce::Rectangle<int> MixerStrips::faderArea (int t) const
{
    const auto top = headerHeight + panHeight + buttonsHeight + 8;
    return { stripX (t) + 6, top, stripWidthOf (t) - 12, getHeight() - top - feedsHeight - 22 };
}

MixerStrips::Part MixerStrips::partAt (int t, juce::Point<int> p) const
{
    if (t == (int) engine.getSong().mixer.size())
        return Part::add;

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
    const auto& song = engine.getSong();
    const auto tracks = (int) song.mixer.size();
    const auto clip = g.getClipBounds();
    g.fillAll (theme::window);

    for (int t = 0; t < tracks; ++t)
    {
        const auto x = stripX (t);
        const auto w = stripWidthOf (t);

        if (x > clip.getRight() || x + w < clip.getX())
            continue;

        const auto& track = song.mixer[(std::size_t) t];
        const auto strip = juce::Rectangle<int> (x, 2, w - 4, getHeight() - 4);

        g.setColour (t == selected ? juce::Colour (0xff3c444d) : (t == 0 ? juce::Colour (0xff343a41) : theme::panel));
        g.fillRoundedRectangle (strip.toFloat(), 4.0f);

        if (track.colour != 0) // faixa colorida no topo, como no mixer do FL
        {
            g.setColour (juce::Colour (track.colour));
            g.fillRoundedRectangle (strip.toFloat().withHeight (4.0f), 2.0f);
        }

        if (t == selected)
        {
            g.setColour (theme::accentOrange);
            g.drawRoundedRectangle (strip.toFloat(), 4.0f, 1.5f);
        }

        // cabeçalho: número e nome
        g.setColour (theme::text.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (t == 0 ? juce::String ("M") : juce::String (t), x, 4, w - 4, 12, juce::Justification::centred);
        g.setColour (track.muted ? juce::Colours::grey : theme::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (juce::String::fromUTF8 (track.name.c_str()), x + 2, 16, w - 8, 16, juce::Justification::centred, 1, 0.7f);

        // latência compensada (PDC) no Master
        if (t == 0 && engine.getLatencySamples() > 0)
        {
            const auto ms = 1000.0 * engine.getLatencySamples() / juce::jmax (1.0, engine.getTransport().sampleRate.load());
            g.setColour (theme::accentOrange);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("PDC " + juce::String (ms, 1) + " ms", soloArea (t).withX (x + 36).withWidth (w - 40),
                        juce::Justification::centredLeft); // o Master não tem solo: usa o lugar dele
        }

        if (! track.effects.empty())
        {
            g.setColour (theme::accentBlue);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("FX " + juce::String ((int) track.effects.size()), x + w - 30, 3, 24, 11, juce::Justification::centredRight);
        }

        // pan
        {
            const auto area = panArea (t).toFloat().withSizeKeepingCentre (26.0f, 26.0f);
            const auto centre = area.getCentre();
            constexpr auto start = juce::MathConstants<float>::pi * 1.25f;
            constexpr auto range = juce::MathConstants<float>::pi * 1.5f;
            const auto to = start + range * (track.pan + 1.0f) * 0.5f;
            const auto mid = start + range * 0.5f;

            g.setColour (theme::knobBackground);
            g.fillEllipse (area);
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, 11.5f, 11.5f, 0.0f, juce::jmin (mid, to), juce::jmax (mid, to), true);
            g.setColour (theme::accentBlue);
            g.strokePath (arc, juce::PathStrokeType (2.5f));
            g.setColour (juce::Colours::white);
            g.drawLine (centre.x, centre.y, centre.x + std::sin (to) * 9.0f, centre.y - std::cos (to) * 9.0f, 1.5f);
        }

        // mute e solo
        {
            const auto m = muteArea (t).toFloat();
            g.setColour (theme::muteLed (track.muted));
            g.fillEllipse (m.withSizeKeepingCentre (12.0f, 12.0f));

            if (t > 0)
            {
                const auto s = soloArea (t).toFloat().reduced (2.0f);
                g.setColour (track.solo ? theme::accentOrange : theme::knobBackground);
                g.fillRoundedRectangle (s, 3.0f);
                g.setColour (track.solo ? juce::Colours::black : theme::text.withAlpha (0.6f));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText ("S", s.toNearestInt(), juce::Justification::centred);
            }
        }

        // medidores + fader
        {
            const auto area = faderArea (t);
            const auto meterW = 6;
            const auto meters = area.withWidth (meterW * 2 + 3);
            const auto fader = area.withTrimmedLeft (meterW * 2 + 8);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto bar = juce::Rectangle<int> (meters.getX() + ch * (meterW + 3), meters.getY(), meterW, meters.getHeight());
                g.setColour (theme::knobBackground);
                g.fillRect (bar);

                const auto level = (std::size_t) t * 2 + (std::size_t) ch < levels.size() ? levels[(std::size_t) t * 2 + (std::size_t) ch] : 0.0f;
                const auto h = (int) (meterPosition (level) * (float) bar.getHeight());
                const auto lit = bar.withTop (bar.getBottom() - h);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff4040), (float) bar.getX(), (float) bar.getY(),
                                                         theme::accentGreen, (float) bar.getX(), (float) bar.getBottom(), false));
                g.fillRect (lit);
            }

            // trilho do fader e marca de 0 dB
            const auto railX = (float) fader.getCentreX();
            g.setColour (theme::knobBackground);
            g.fillRoundedRectangle (railX - 2.0f, (float) fader.getY(), 4.0f, (float) fader.getHeight(), 2.0f);
            const auto unityY = (float) fader.getBottom() - unityFader * (float) fader.getHeight();
            g.setColour (theme::text.withAlpha (0.3f));
            g.drawHorizontalLine ((int) unityY, (float) fader.getX(), (float) fader.getRight());

            const auto capY = (float) fader.getBottom() - track.volume * (float) fader.getHeight();
            const auto cap = juce::Rectangle<float> ((float) fader.getX() + 2.0f, capY - 7.0f, (float) fader.getWidth() - 4.0f, 14.0f);
            g.setColour (juce::Colour (0xffb8bec5));
            g.fillRoundedRectangle (cap, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawHorizontalLine ((int) capY, cap.getX() + 2.0f, cap.getRight() - 2.0f);

            g.setColour (theme::text.withAlpha (0.6f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (faderToDb (track.volume) + " dB", x, area.getBottom() + 3, w - 4, 14, juce::Justification::centred);
        }

        // envio da trilha selecionada para esta
        if (selected > 0 && t != selected && selected < tracks)
        {
            const auto r = routeArea (t).toFloat();
            const auto on = song.routes (selected, t);
            const auto possible = on || song.canRoute (selected, t);

            if (possible)
            {
                g.setColour (on ? theme::accentOrange.withAlpha (0.85f) : theme::knobBackground);
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (on ? juce::Colours::black : theme::text.withAlpha (0.45f));
                g.setFont (juce::FontOptions (10.0f));
                juce::String label = juce::String::fromUTF8 ("\xe2\x86\x92"); // seta

                if (on && t > 0)
                    for (const auto& send : song.mixer[(std::size_t) selected].sends)
                        if (send.target == t)
                            label << " " << juce::roundToInt (send.level * 100.0f) << "%";

                g.drawText (label, r.toNearestInt(), juce::Justification::centred);
            }
        }

        // canais que chegam nesta trilha
        {
            juce::StringArray feeds;
            for (const auto& c : song.channels)
                if (juce::jlimit (0, tracks - 1, c.mixerTrack) == t)
                    feeds.add (juce::String::fromUTF8 (c.name.c_str()));

            g.setColour (theme::accentOrange.withAlpha (0.8f));
            g.setFont (juce::FontOptions (10.0f));
            const auto text = feeds.size() > 3 ? feeds[0] + ", " + feeds[1] + " +" + juce::String (feeds.size() - 2) : feeds.joinIntoString (", ");
            g.drawFittedText (text, x + 3, getHeight() - feedsHeight + 16, w - 10, feedsHeight - 20, juce::Justification::centredTop, 2, 0.8f);
        }
    }

    // botão "+"
    if (tracks < maxMixerTracks)
    {
        const auto add = juce::Rectangle<int> (stripX (tracks), 2, stripWidth - 4, 60);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (add.toFloat(), 4.0f);
        g.setColour (theme::text.withAlpha (0.6f));
        g.setFont (juce::FontOptions (22.0f));
        g.drawText ("+", add, juce::Justification::centred);
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

    if (part == Part::route)
    {
        // a seleção é a origem do envio: não muda
        const auto from = selected;
        const auto on = engine.getSong().routes (from, t);
        engine.editSong ([from, t, on] (Song& s) { s.setRoute (from, t, ! on); });

        if (! on && t > 0) // acabou de ligar: arrastar ajusta o nível
        {
            dragPart = Part::route;
            dragTrack = t;
            dragStartValue = 1.0f;
        }
        return;
    }

    if (selected != t)
    {
        selected = t;
        repaint();

        if (onSelectionChanged)
            onSelectionChanged (t);
    }

    if (e.mods.isPopupMenu() && (part == Part::pan || part == Part::fader))
    {
        juce::PopupMenu menu;
        addAutomationItem (menu, engine, { part == Part::pan ? AutomationTargetKind::mixerPan : AutomationTargetKind::mixerVolume, t });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
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

            if (part == Part::fader) // clicar no trilho pula o fader para lá
            {
                const auto area = faderArea (t);
                const auto capY = area.getBottom() - track.volume * (float) area.getHeight();

                if (std::abs ((float) e.y - capY) > 9.0f)
                {
                    dragStartValue = juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - e.y) / (float) area.getHeight());
                    engine.editSong ([t, v = dragStartValue] (Song& s) { s.mixer[(std::size_t) t].volume = v; });
                }
            }
            return;

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

    if (dragPart == Part::fader)
    {
        const auto height = (float) faderArea (t).getHeight();
        const auto fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        const auto v = juce::jlimit (0.0f, 1.0f, dragStartValue - (float) e.getDistanceFromDragStartY() / height * fine);
        engine.editSong ([t, v] (Song& s) { s.mixer[(std::size_t) t].volume = v; });
    }
    else
    {
        const auto v = juce::jlimit (-1.0f, 1.0f, dragStartValue - (float) e.getDistanceFromDragStartY() / 75.0f);
        engine.editSong ([t, v] (Song& s) { s.mixer[(std::size_t) t].pan = v; });
    }
}

void MixerStrips::mouseUp (const juce::MouseEvent&)
{
    dragPart = Part::none;
    dragTrack = -1;
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
    repaint();

    if (onSelectionChanged)
        onSelectionChanged (selected);
}

namespace
{
// Os 3 knobs de cada banda do EQ embutido da trilha (ganho, frequência, largura).
class TrackEqEditor final : public juce::Component
{
public:
    TrackEqEditor (AudioEngine& e, int t)
        : engine (e),
          track (t)
    {
        const juce::String bands[] = { "Graves", utf8 ("Médios"), "Agudos" };
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
            width.setTooltip (b == 1 ? utf8 ("Largura do pico (Q)") : utf8 ("Inclinação da prateleira"));
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
    std::array<std::array<juce::Slider, 3>, 3> knobs; // [banda][ganho, frequência, largura]
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
        menu.addItem (utf8 ("Rotear o canal \"") + juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str())
                          + utf8 ("\" para cá"),
                      [this, channel, t] { engine.editSong ([channel, t] (Song& s) { s.channels[(std::size_t) channel].mixerTrack = t; }); });

    if (selected > 0 && selected != t && (t == 0 || song.canRoute (selected, t) || song.routes (selected, t)))
        menu.addItem (utf8 ("Enviar \"") + juce::String::fromUTF8 (song.mixer[(std::size_t) selected].name.c_str())
                          + utf8 ("\" só para esta trilha"),
                      [this, from = selected, t] {
                          engine.editSong ([from, t] (Song& s) {
                              s.mixer[(std::size_t) from].sends.clear();
                              s.mixer[(std::size_t) from].toMaster = false;
                              s.setRoute (from, t, true);
                          });
                      });

    if (t > 0 && (! song.mixer[(std::size_t) t].sends.empty() || ! song.mixer[(std::size_t) t].toMaster))
        menu.addItem (utf8 ("Voltar a enviar só para o Master"), [this, t] {
            engine.editSong ([t] (Song& s) {
                s.mixer[(std::size_t) t].sends.clear();
                s.mixer[(std::size_t) t].toMaster = true;
            });
        });

    {
        const auto& track = song.mixer[(std::size_t) t];
        juce::PopupMenu stereo;
        const std::pair<const char*, float> widths[] = { { "Mono", 1.0f }, { "Mais estreito", 0.5f }, { "Normal", 0.0f },
                                                          { "Mais largo", -0.5f }, { "Bem largo", -1.0f } };

        for (const auto& [label, value] : widths)
            stereo.addItem (juce::String::fromUTF8 (label), true, std::abs (track.stereo - value) < 0.01f,
                            [this, t, v = value] { engine.editSong ([t, v] (Song& s) { s.mixer[(std::size_t) t].stereo = v; }); });

        menu.addSubMenu (utf8 ("Separação estéreo"), stereo);
        menu.addItem (utf8 ("EQ da trilha..."), [this, t] { showTrackEq (t); });
        menu.addItem (utf8 ("Inverter fase"), true, track.invertPhase, [this, t] {
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].invertPhase = ! s.mixer[(std::size_t) t].invertPhase; });
        });
        menu.addItem (utf8 ("Trocar esquerda/direita"), true, track.swapChannels, [this, t] {
            engine.editSong ([t] (Song& s) { s.mixer[(std::size_t) t].swapChannels = ! s.mixer[(std::size_t) t].swapChannels; });
        });
        menu.addSeparator();
    }

    if (t > 0)
        menu.addItem (utf8 ("Renderizar para áudio (congelar)..."), [this, t] { bounceInsertToAudio (engine, t); });

    menu.addItem ("Renomear...", [this, t] {
        auto* window = new juce::AlertWindow ("Renomear trilha", {}, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", juce::String::fromUTF8 (engine.getSong().mixer[(std::size_t) t].name.c_str()));
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, t, window] (int result) {
                                     const auto name = window->getTextEditorContents ("name").trim().toStdString();
                                     if (result == 1 && ! name.empty() && t < (int) engine.getSong().mixer.size())
                                         engine.editSong ([t, name] (Song& s) { s.mixer[(std::size_t) t].name = name; });
                                 }),
                                 true);
    });
    menu.addItem ("Resetar (0 dB, centro, sem mute/solo)", [this, t] {
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
        const auto next = juce::jmax (peak, levels[i] * 0.86f); // queda de ~20 dB/s a 30 Hz
        const auto shown = next < 0.001f ? 0.0f : next;

        // só o medidor que mudou é redesenhado
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
constexpr EffectKind allEffects[] = { EffectKind::eq,     EffectKind::parametricEq, EffectKind::compressor, EffectKind::limiter,
                                      EffectKind::delay,  EffectKind::reverb,       EffectKind::chorus,     EffectKind::filter,
                                      EffectKind::distortion, EffectKind::utility, EffectKind::volumeGate };
} // namespace

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

juce::Rectangle<int> EffectsPanel::knobBounds (int param) const
{
    auto top = panelHeader + maxEffectSlots * slotHeight + 40;
    const auto w = (getWidth() - 16) / 3;

    // gate de volume: os 32 passos são barras (gateStepsBounds); os knobs são mix/ataque/release
    if (gateSelected())
    {
        if (param < 32)
            return {};
        param -= 32;
        top = gateStepsBounds().getBottom() + 22;
    }

    return { 8 + (param % 3) * w, top + (param / 3) * 86, w, 82 };
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
    // padrões prontos (como os slots do Gross Beat): valor do passo i de 32 (1/8 de tempo cada)
    struct Pattern
    {
        const char* name;
        float (*level) (int);
    };
    static const Pattern patterns[] = {
        { "Tudo aberto", [] (int) { return 1.0f; } },
        { "Gate de 1/2 tempo", [] (int i) { return (i % 4) < 2 ? 1.0f : 0.0f; } },
        { "Gate de 1 tempo", [] (int i) { return (i % 8) < 4 ? 1.0f : 0.0f; } },
        { "Contratempo", [] (int i) { return (i % 8) >= 4 ? 1.0f : 0.0f; } },
        { "Sidechain (rampa por tempo)", [] (int i) { return std::min (1.0f, 0.15f + (float) (i % 8) / 5.0f); } },
        { "Trance gate", [] (int i) { return (i % 4) == 3 ? 0.0f : (i % 16) >= 12 ? 0.4f : 1.0f; } },
        { "Fade-in no compasso", [] (int i) { return (float) (i + 1) / 32.0f; } },
        { "Fade-out no compasso", [] (int i) { return 1.0f - (float) i / 32.0f; } },
    };

    juce::PopupMenu menu;
    for (const auto& p : patterns)
        menu.addItem (utf8 (p.name), [this, level = p.level] {
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
        return -1; // editado na janela própria

    const auto count = (int) effectInfo ((*s)[(std::size_t) selectedSlot].kind).params.size();

    for (int i = gateSelected() ? 32 : 0; i < count; ++i)
        if (knobBounds (i).contains (p))
            return i;

    return -1;
}

void EffectsPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
    g.setColour (theme::window);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    const auto* s = slots();

    if (s == nullptr)
        return;

    const auto& song = engine.getSong();
    g.setColour (theme::accentOrange);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (utf8 ("Efeitos — ") + juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str()), 10, 4,
                getWidth() - 20, panelHeader - 8, juce::Justification::centredLeft, true);

    g.setFont (juce::FontOptions (13.0f));

    for (int i = 0; i < maxEffectSlots; ++i)
    {
        const auto r = slotBounds (i);
        const auto filled = i < (int) s->size();
        g.setColour (i == selectedSlot ? theme::channelButtonSelected : theme::channelButton.darker (filled ? 0.0f : 0.4f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        if (filled)
        {
            const auto& slot = (*s)[(std::size_t) i];
            g.setColour (theme::muteLed (! slot.enabled));
            g.fillEllipse ((float) r.getX() + 6, (float) r.getCentreY() - 5, 10, 10);
            g.setColour (slot.enabled ? theme::text : juce::Colours::grey);
            const auto name = slot.kind == EffectKind::plugin ? juce::String::fromUTF8 (slot.plugin.name.c_str())
                                                              : utf8 (effectInfo (slot.kind).name);
            g.drawText (juce::String (i + 1) + ". " + name, r.withTrimmedLeft (24).withTrimmedRight (28), juce::Justification::centredLeft);

            // knob de dry/wet do slot
            const auto knob = mixBounds (i).toFloat().withSizeKeepingCentre (18.0f, 18.0f);
            constexpr auto start = juce::MathConstants<float>::pi * 1.25f;
            constexpr auto range = juce::MathConstants<float>::pi * 1.5f;
            g.setColour (theme::knobBackground);
            g.fillEllipse (knob);
            juce::Path arc;
            arc.addCentredArc (knob.getCentreX(), knob.getCentreY(), 7.5f, 7.5f, 0.0f, start, start + range * slot.mix, true);
            g.setColour (slot.enabled ? theme::accentBlue : juce::Colours::grey);
            g.strokePath (arc, juce::PathStrokeType (2.0f));
        }
        else if (i == (int) s->size())
        {
            g.setColour (theme::text.withAlpha (0.45f));
            g.drawText (juce::String (i + 1) + utf8 (". (vazio — clique para adicionar)"), r.withTrimmedLeft (24),
                        juce::Justification::centredLeft);
        }
    }

    if (selectedSlot < 0 || selectedSlot >= (int) s->size())
        return;

    const auto& slot = (*s)[(std::size_t) selectedSlot];

    if (slot.kind == EffectKind::plugin)
    {
        const auto button = pluginButtonBounds();
        g.setColour (theme::channelButton);
        g.fillRoundedRectangle (button.toFloat(), 4.0f);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (utf8 ("Abrir interface do plugin"), button, juce::Justification::centred);
        g.setColour (theme::text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String::fromUTF8 (slot.plugin.name.c_str()), button.translated (0, 34), juce::Justification::centred);
        return;
    }

    if (slot.kind == EffectKind::parametricEq)
    {
        const auto button = pluginButtonBounds();
        g.setColour (theme::channelButton);
        g.fillRoundedRectangle (button.toFloat(), 4.0f);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (utf8 ("Abrir EQ paramétrico"), button, juce::Justification::centred);
        const auto curve = juce::Rectangle<int> (button.getX(), button.getBottom() + 10, button.getWidth(), 120).toFloat();
        drawParametricEqCurve (g, curve, slot.params, juce::jmax (8000.0, engine.getTransport().sampleRate.load()), slot.enabled);
        return;
    }

    const auto& info = effectInfo (slot.kind);
    const auto gate = slot.kind == EffectKind::volumeGate;
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (utf8 (info.name), 10, (gate ? gateStepsBounds().getY() : knobBounds (0).getY()) - 28, getWidth() - 20, 20,
                juce::Justification::centredLeft);

    if (gate) // 32 passos = 1 compasso de 4 tempos
    {
        const auto area = gateStepsBounds().toFloat();
        g.setColour (theme::knobBackground);
        g.fillRoundedRectangle (area, 3.0f);
        const auto w = area.getWidth() / 32.0f;
        for (int i = 0; i < 32; ++i)
        {
            const auto h = area.getHeight() * juce::jlimit (0.0f, 1.0f, slot.params[(std::size_t) i]);
            g.setColour ((slot.enabled ? theme::accentOrange : juce::Colours::grey).withAlpha ((i / 8) % 2 == 0 ? 1.0f : 0.75f));
            g.fillRect (area.getX() + i * w + 1.0f, area.getBottom() - h, w - 2.0f, h);
        }
        g.setColour (theme::text.withAlpha (0.3f));
        for (int beat = 1; beat < 4; ++beat)
            g.drawVerticalLine ((int) (area.getX() + beat * 8 * w), area.getY(), area.getBottom());
        g.setColour (theme::text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (utf8 ("1 compasso: desenhe os passos; clique direito = padrões"), area.withY (area.getBottom() + 2).withHeight (14),
                    juce::Justification::centredLeft);
    }

    for (int p = gate ? 32 : 0; p < (int) info.params.size(); ++p)
    {
        const auto r = knobBounds (p);
        const auto value = slot.params[(std::size_t) p];
        const auto area = r.withHeight (44).toFloat().withSizeKeepingCentre (38.0f, 38.0f);
        const auto centre = area.getCentre();
        constexpr auto start = juce::MathConstants<float>::pi * 1.25f;
        constexpr auto range = juce::MathConstants<float>::pi * 1.5f;
        const auto to = start + range * value;

        g.setColour (theme::knobBackground);
        g.fillEllipse (area);
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, 17.0f, 17.0f, 0.0f, start, to, true);
        g.setColour (slot.enabled ? theme::accentOrange : juce::Colours::grey);
        g.strokePath (arc, juce::PathStrokeType (3.0f));
        g.setColour (juce::Colours::white);
        g.drawLine (centre.x, centre.y, centre.x + std::sin (to) * 13.0f, centre.y - std::cos (to) * 13.0f, 2.0f);

        g.setColour (theme::text.withAlpha (0.8f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (utf8 (info.params[(std::size_t) p].name), r.getX(), r.getY() + 46, r.getWidth(), 14, juce::Justification::centred);
        g.setColour (theme::accentBlue);
        g.drawText (juce::String::fromUTF8 (formatEffectParam (slot.kind, p, value, song.bpm).c_str()), r.getX(), r.getY() + 60,
                    r.getWidth(), 14, juce::Justification::centred);
    }
}

void EffectsPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto* s = slots();

    if (s == nullptr)
        return;

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

        if (! e.mods.isPopupMenu() && mixBounds (i).contains (e.getPosition()))
        {
            dragParam = mixParam;
            dragStart = (*s)[(std::size_t) i].mix;
            return;
        }

        if (e.mods.isPopupMenu())
            showSlotMenu (i);
        else if (e.x < r.getX() + 22) // LED: bypass
            engine.editSong ([t = track, i] (Song& song) {
                auto& slot = song.mixer[(std::size_t) t].effects[(std::size_t) i];
                slot.enabled = ! slot.enabled;
            });
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
        juce::PopupMenu menu;
        addAutomationItem (menu, engine, { AutomationTargetKind::effectParam, track, selectedSlot, dragParam });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        dragParam = -1;
        return;
    }

    if (dragParam >= 0)
        dragStart = (*s)[(std::size_t) selectedSlot].params[(std::size_t) dragParam];
}

void EffectsPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragParam == gateStepParam)
    {
        setGateStep (e.getPosition());
        return;
    }

    if (dragParam == mixParam)
    {
        const auto v = juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 150.0f);
        engine.editSong ([t = track, slot = selectedSlot, v] (Song& song) { song.mixer[(std::size_t) t].effects[(std::size_t) slot].mix = v; });
        return;
    }

    if (dragParam < 0)
        return;

    const auto fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto v = juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 150.0f * fine);
    engine.editSong ([t = track, slot = selectedSlot, p = dragParam, v] (Song& song) {
        song.mixer[(std::size_t) t].effects[(std::size_t) slot].params[(std::size_t) p] = v;
    });
}

void EffectsPanel::mouseUp (const juce::MouseEvent&)
{
    dragParam = -1;
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

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Plugin",
                                            utf8 ("Este plugin não pôde ser carregado (não está instalado ou não foi escaneado)."));
}

void EffectsPanel::showAddMenu (int)
{
    juce::PopupMenu menu;

    for (auto kind : allEffects)
        menu.addItem (utf8 (effectInfo (kind).name), [this, kind] {
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
    menu.addSubMenu ("Plugin (VST3/CLAP)", plugins);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void EffectsPanel::showSlotMenu (int slot)
{
    juce::PopupMenu replace;

    for (auto kind : allEffects)
        replace.addItem (utf8 (effectInfo (kind).name), [this, slot, kind] {
            engine.editSong ([t = track, slot, kind] (Song& song) { song.mixer[(std::size_t) t].effects[(std::size_t) slot] = makeEffectSlot (kind); });
        });

    const auto count = (int) slots()->size();
    juce::PopupMenu menu;
    menu.addSubMenu ("Trocar por", replace);

    // presets do FL (Plugin presets\Effects\<plugin>) que viram este efeito
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
            menu.addSubMenu ("Presets do FL", presets);
    }
    menu.addItem ("Mover para cima", slot > 0, false, [this, slot] {
        engine.editSong ([t = track, slot] (Song& song) { song.swapEffectSlots (t, slot, slot - 1); });
        selectedSlot = slot - 1;
    });
    menu.addItem ("Mover para baixo", slot < count - 1, false, [this, slot] {
        engine.editSong ([t = track, slot] (Song& song) { song.swapEffectSlots (t, slot, slot + 1); });
        selectedSlot = slot + 1;
    });
    if ((*slots())[(std::size_t) slot].kind == EffectKind::plugin)
    {
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (track, slot).get()))
            addPluginParameterItems (menu, engine, p->getInstance(), { AutomationTargetKind::effectParam, track, slot, 0 });
    }

    menu.addSeparator();
    menu.addItem ("Remover", [this, slot] {
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
