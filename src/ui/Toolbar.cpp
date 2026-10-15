#include "ui/Toolbar.h"

#include <juce_dsp/juce_dsp.h>

#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace lynx::toolbar
{
namespace
{
juce::String barBeatStep (const Song& song, double ticks, int beatsPerBar)
{
    const auto beats = ticks / ppq;
    int bar = 0, beat = 0;

    if (song.mode == PlayMode::song) // the Playlist ruler applies (with time signature changes)
    {
        const auto current = song.barAt ((std::int64_t) ticks);
        bar = current.number;
        beat = (int) ((ticks - (double) current.tick) / ppq) + 1;
    }
    else
    {
        const auto perBar = juce::jmax (1, beatsPerBar);
        bar = (int) (beats / perBar) + 1;
        beat = (int) std::fmod (beats, (double) perBar) + 1;
    }

    const auto step = (int) (std::fmod (beats, 1.0) * stepsPerBeat) + 1;
    return juce::String::formatted ("%d : %d : %d", bar, beat, step);
}

double loopLength (const Song& song)
{
    if (song.patterns.empty())
        return 1.0;

    const auto length = song.mode == PlayMode::song
                            ? song.songLength()
                            : song.patterns[(std::size_t) juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern)].effectiveLength (song.beatsPerBar);
    return (double) juce::jmax<std::int64_t> (1, length);
}

double playheadTicks (AudioEngine& engine)
{
    return std::fmod (juce::jmax (0.0, engine.getTransport().positionTicks.load()), loopLength (engine.getSong()));
}

juce::String centsText (double cents)
{
    const auto c = juce::roundToInt (cents);
    return (c > 0 ? "+" : "") + juce::String (c) + " " + tr ("cents");
}
} // namespace

// --- song position -------------------------------------------------------------------------------

SongPositionBar::SongPositionBar (AudioEngine& e) : engine (e)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

double SongPositionBar::length() const { return loopLength (engine.getSong()); }
double SongPositionBar::position() const { return playheadTicks (engine); }

juce::Rectangle<float> SongPositionBar::track() const
{
    return getLocalBounds().toFloat().reduced (5.0f, 0.0f).withSizeKeepingCentre ((float) getWidth() - 10.0f, 4.0f);
}

void SongPositionBar::update()
{
    const auto fraction = position() / length();

    if (std::abs (fraction - shownFraction) * getWidth() >= 0.5)
    {
        shownFraction = fraction;
        repaint();
    }
}

void SongPositionBar::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto t = track();
    const auto& song = engine.getSong();
    const auto active = dragging || isMouseOver();

    g.setColour (colour::bg0);
    g.fillRoundedRectangle (t.expanded (0.0f, 1.0f), 2.5f);

    // the Playlist loop (Song mode)
    if (song.mode == PlayMode::song && song.hasLoop())
    {
        const auto x0 = t.getX() + t.getWidth() * (float) ((double) song.loopStart / length());
        const auto x1 = t.getX() + t.getWidth() * (float) juce::jmin (1.0, (double) song.loopEnd / length());
        g.setColour (colour::info.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x0, t.getY() - 1.0f, juce::jmax (1.0f, x1 - x0), t.getHeight() + 2.0f));
    }

    const auto fraction = (float) juce::jlimit (0.0, 1.0, position() / length());
    const auto x = t.getX() + t.getWidth() * fraction;
    g.setColour (engine.getTransport().playing.load() ? colour::accent : colour::accent.withMultipliedSaturation (0.55f).withMultipliedBrightness (0.8f));
    g.fillRoundedRectangle (t.withRight (juce::jmax (t.getX() + 1.0f, x)), 2.0f);

    const auto radius = active ? 5.0f : 3.5f;
    g.setColour (active ? colour::textPrimary : colour::textSecondary);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre ({ x, t.getCentreY() }));
}

void SongPositionBar::seekTo (int x)
{
    const auto t = track();
    const auto fraction = juce::jlimit (0.0, 1.0, (double) ((float) x - t.getX()) / juce::jmax (1.0f, t.getWidth()));
    auto ticks = fraction * length();

    if (! juce::ModifierKeys::currentModifiers.isAltDown()) // Alt: no snap (as everywhere)
        ticks = std::round (ticks / ticksPerStep) * ticksPerStep;

    engine.seek (juce::jlimit (0.0, length() - 1.0, ticks));
    update();
    repaint();
}

void SongPositionBar::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown())
        return;

    dragging = true;
    seekTo (e.x);
}

void SongPositionBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        seekTo (e.x);
}

void SongPositionBar::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void SongPositionBar::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;

    if (delta == 0.0f)
        return;

    const auto bar = (double) ppq * juce::jmax (1, engine.getSong().beatsPerBar);
    const auto current = std::round (position() / bar) * bar;
    engine.seek (juce::jlimit (0.0, juce::jmax (0.0, length() - bar), current + (delta > 0.0f ? -bar : bar) * (wheel.isReversed ? -1.0 : 1.0)));
    update();
}

ui::Hint SongPositionBar::hintAt (juce::Point<int>)
{
    const auto& song = engine.getSong();
    ui::Hint h;
    h.text = (song.mode == PlayMode::song ? tr ("Song position") : tr ("Pattern position"))
             + ": " + tr ("[LMB] click or drag to move the playhead  [Wheel] one bar  [Alt] no snap");
    const auto bars = juce::jmax (1, juce::roundToInt (length() / ((double) ppq * juce::jmax (1, song.beatsPerBar))));
    h.value = tr ("Bar {0} of {1}", { barBeatStep (song, position(), song.beatsPerBar).upToFirstOccurrenceOf (" ", false, false), juce::String (bars) });
    return h;
}

// --- time display --------------------------------------------------------------------------------

juce::String TimeDisplay::text() const
{
    const auto& song = engine.getSong();
    const auto ticks = playheadTicks (engine);

    if (! clockMode)
        return barBeatStep (song, ticks, engine.getTransport().beatsPerBar.load());

    const auto seconds = song.secondsForTicks ((std::int64_t) ticks);
    const auto whole = (int) seconds;
    return juce::String::formatted ("%d : %02d : %02d", whole / 60, whole % 60, (int) ((seconds - whole) * 100.0));
}

void TimeDisplay::update()
{
    if (const auto t = text(); t != shown)
    {
        shown = t;
        repaint();
    }
}

void TimeDisplay::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto area = getLocalBounds();
    g.setColour (colour::bg0);
    g.fillRoundedRectangle (area.toFloat(), radius::md);

    if (isMouseOver())
    {
        g.setColour (colour::borderStrong);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), radius::md, 1.0f);
    }

    g.setColour (colour::textPrimary);
    g.setFont (monoFont (juce::jmin (19.0f, (float) area.getHeight() * 0.5f)));
    g.drawText (shown.isEmpty() ? text() : shown, area.withTrimmedBottom (12), juce::Justification::centred, false);
    g.setColour (colour::textDisabled);
    g.setFont (font (type::caption - 1.0f));
    g.drawText (clockMode ? tr ("MIN : SEC : CS") : tr ("BAR : BEAT : STEP"), area.withTrimmedTop (area.getHeight() - 14).withTrimmedBottom (2),
                juce::Justification::centred, false);
}

void TimeDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! getLocalBounds().contains (e.getPosition()))
        return;

    clockMode = ! clockMode;
    shown = {};
    update();
    repaint();
}

ui::Hint TimeDisplay::hintAt (juce::Point<int>)
{
    ui::Hint h;
    h.text = (clockMode ? tr ("Time: minutes : seconds : hundredths") : tr ("Time: bar : beat : step"))
             + "  " + tr ("[LMB] switch between bars and minutes");
    h.value = text();
    return h;
}

// --- master volume and pitch ---------------------------------------------------------------------

MasterKnob::MasterKnob (AudioEngine& e, Kind k) : engine (e), kind (k)
{
    setRepaintsOnMouseActivity (true);
}

bool MasterKnob::available() const
{
    return kind == Kind::pitch || ! engine.getSong().mixer.empty();
}

double MasterKnob::value() const
{
    const auto& song = engine.getSong();

    if (kind == Kind::pitch)
        return song.masterPitch;

    return song.mixer.empty() ? (double) unityFader : (double) song.mixer.front().volume;
}

void MasterKnob::setValue (double v)
{
    const auto s = spec();
    v = juce::jlimit (s.minimum, s.maximum, v);

    if (! available() || std::abs (v - value()) < 1e-9)
        return;

    if (kind == Kind::pitch)
        engine.editSong ([v] (Song& song) { song.masterPitch = (float) v; });
    else
        engine.editSong ([v] (Song& song) {
            if (! song.mixer.empty())
                song.mixer.front().volume = (float) v;
        });

    update();
}

ui::ValueSpec MasterKnob::spec() const
{
    if (kind == Kind::volume)
    {
        auto s = ui::faderSpec (tr ("Master volume"), unityFader);
        s.target = AutomationTarget { AutomationTargetKind::mixerVolume, 0 };
        return s;
    }

    ui::ValueSpec s;
    s.name = tr ("Master pitch");
    s.minimum = -1200.0;
    s.maximum = 1200.0;
    s.defaultValue = 0.0;
    s.pixelsForRange = 360.0;
    s.toText = [] (double v) { return centsText (v); };
    s.fromText = [] (const juce::String& t) -> std::optional<double> {
        const auto number = t.retainCharacters ("+-0123456789.");
        if (number.isEmpty())
            return std::nullopt;
        return number.getDoubleValue();
    };
    return s;
}

void MasterKnob::update()
{
    if (const auto v = value(); v != shown)
    {
        shown = v;
        repaint();
    }
}

void MasterKnob::paint (juce::Graphics& g)
{
    using namespace theme;
    auto area = getLocalBounds().toFloat();
    const auto knob = area.removeFromLeft (juce::jmin (area.getHeight(), 26.0f));
    const auto s = spec();
    const auto v = value();
    const auto hover = isMouseOver() || drag.isActive();
    const auto value01 = (float) ((v - s.minimum) / (s.maximum - s.minimum));
    drawKnob (g, knob, value01, kind == Kind::pitch, available() ? colour::knobArc : colour::textDisabled, hover && available());

    area.removeFromLeft (5.0f);
    auto caption = area.removeFromTop (area.getHeight() / 2.0f);
    g.setColour (colour::textDisabled);
    g.setFont (font (type::caption - 1.0f, true));
    g.drawText (kind == Kind::volume ? tr ("MASTER") : tr ("PITCH"), caption.withTrimmedTop (1.0f), juce::Justification::bottomLeft, true);
    g.setColour (drag.isActive() ? colour::textPrimary : colour::textSecondary);
    g.setFont (font (type::caption));
    g.drawText (available() ? s.text (v) : tr ("no mixer"), area, juce::Justification::topLeft, true);
}

void MasterKnob::mouseDown (const juce::MouseEvent& e)
{
    if (! available())
        return;

    if (e.mods.isPopupMenu())
    {
        ui::showValueMenu (*this, kind == Kind::volume ? &engine : nullptr, spec(), value(), [this] (double v) { setValue (v); });
        return;
    }

    if (ui::isResetClick (e))
    {
        setValue (spec().defaultValue);
        return;
    }

    drag.begin (e, value());
    repaint();
}

void MasterKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag.isActive())
        return;

    auto v = drag.update (e, spec());

    if (kind == Kind::pitch && ! e.mods.isShiftDown()) // semitone steps; Shift: free (as FL's master pitch)
        v = std::round (v / 100.0) * 100.0;

    setValue (v);
}

void MasterKnob::mouseUp (const juce::MouseEvent&)
{
    drag.end();
    repaint();
}

void MasterKnob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (available() && ! e.mods.isPopupMenu() && ! ui::isResetClick (e))
        ui::typeValue (*this, spec(), value(), [this] (double v) { setValue (v); });
}

void MasterKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! available())
        return;

    if (kind == Kind::pitch)
    {
        const auto delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (delta != 0.0f)
            setValue (value() + (delta > 0.0f ? 1.0 : -1.0) * (wheel.isReversed ? -1.0 : 1.0) * (e.mods.isCtrlDown() ? 1.0 : 100.0));
        return;
    }

    if (const auto v = ui::wheelValue (value(), e, wheel, spec()))
        setValue (*v);
}

ui::Hint MasterKnob::hintAt (juce::Point<int>)
{
    if (! available())
        return { tr ("Master volume: this project has no mixer"), {}, false, false };

    const auto gesture = kind == Kind::volume
                             ? tr ("level of everything you hear (the Master insert's fader)") + "  " + ui::valueGestures()
                             : tr ("tunes the built-in instruments  [LMB] drag (semitones)  [Shift] cents  [Wheel] semitone  [Ctrl][Wheel] cent  [Alt][LMB] reset  [RMB] menu");
    return ui::valueHint (spec(), value(), gesture);
}

// --- CPU, memory, polyphony ----------------------------------------------------------------------

void PerformanceMeter::setValues (double cpu01, std::int64_t memoryBytes, int polyphony)
{
    cpu = cpu01;
    cpuSmoothed = cpuSmoothed <= 0.0 ? cpu01 : cpuSmoothed * 0.8 + cpu01 * 0.2;
    memory = memoryBytes;
    voices = polyphony;
    peakVoices = juce::jmax (peakVoices, polyphony);
    repaint();
}

void PerformanceMeter::paint (juce::Graphics& g)
{
    using namespace theme;
    auto area = getLocalBounds();
    auto row1 = area.removeFromTop (area.getHeight() / 2).reduced (0, 4);
    auto row2 = area.reduced (0, 4);
    const juce::Font caption (font (type::caption - 1.0f, true)), number (font (type::caption));

    // CPU: caption, bar, percent
    g.setFont (caption);
    g.setColour (colour::textDisabled);
    g.drawText (tr ("CPU"), row1.removeFromLeft (30), juce::Justification::centredLeft, false);
    g.setFont (number);
    g.setColour (colour::textSecondary);
    g.drawText (juce::String (juce::roundToInt (cpuSmoothed * 100.0)) + "%", row1.removeFromRight (34), juce::Justification::centredRight, false);
    const auto bar = row1.reduced (2, 3).toFloat();
    g.setColour (colour::bg0);
    g.fillRoundedRectangle (bar, 2.0f);
    const auto load = (float) juce::jlimit (0.0, 1.0, cpuSmoothed);
    g.setColour (load < 0.6f ? colour::meterLow : load < 0.85f ? colour::meterMid : colour::meterClip);
    if (load > 0.0f)
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * load)), 2.0f);

    // memory and polyphony
    auto left = row2.removeFromLeft (row2.getWidth() * 58 / 100);
    g.setFont (caption);
    g.setColour (colour::textDisabled);
    g.drawText (tr ("RAM"), left.removeFromLeft (30), juce::Justification::centredLeft, false);
    g.setFont (number);
    g.setColour (colour::textSecondary);
    g.drawText (memory > 0 ? juce::String (juce::roundToInt ((double) memory / (1024.0 * 1024.0))) + " MB" : juce::String ("-"), left,
                juce::Justification::centredLeft, false);
    g.setFont (caption);
    g.setColour (colour::textDisabled);
    g.drawText (tr ("POLY"), row2.removeFromLeft (32), juce::Justification::centredLeft, false);
    g.setFont (number);
    g.setColour (voices > 0 ? colour::textPrimary : colour::textSecondary);
    g.drawText (juce::String (voices), row2, juce::Justification::centredRight, false);
}

ui::Hint PerformanceMeter::hintAt (juce::Point<int> p)
{
    ui::Hint h;

    if (p.y < getHeight() / 2)
    {
        h.text = tr ("CPU: how much of the audio deadline the processing takes; near 100% the sound breaks up");
        h.value = juce::String (cpu * 100.0, 1) + "%";
    }
    else if (p.x < getWidth() * 58 / 100)
    {
        h.text = tr ("RAM: memory used by LYNX (samples, plugins, undo history)");
        h.value = juce::String ((double) memory / (1024.0 * 1024.0), 0) + " MB";
    }
    else
    {
        h.text = tr ("Polyphony: notes sounding now in the song and from the keyboard (voices still releasing are not counted)");
        h.value = juce::String (voices) + "  (" + tr ("max {0}", { juce::String (peakVoices) }) + ")";
    }

    return h;
}

// --- output monitor ------------------------------------------------------------------------------

void OutputMonitor::update()
{
    if (! isShowing() || getWidth() <= 0)
        return;

    engine.copyScope (samples.data(), (int) samples.size());
    float now = 0.0f;
    for (auto s : samples)
        now = juce::jmax (now, std::abs (s));

    peak = juce::jmax (now, peak * 0.82f); // fast attack, smooth fall

    if (now >= held || holdFrames <= 0)
    {
        held = now;
        holdFrames = 30; // about a second at 30 Hz
    }
    else
        --holdFrames;

    if (now >= 0.999f)
        clipFrames = 60;
    else if (clipFrames > 0)
        --clipFrames;

    // long view: the level scrolls from right to left
    std::rotate (history.begin(), history.begin() + 1, history.end());
    history.back() = now;

    if (mode == spectrum)
    {
        // Hann-windowed FFT of the last 1024 samples, folded into log-spaced bands (20 Hz..20 kHz)
        static juce::dsp::FFT fft (10);
        std::array<float, 2048> data {};
        for (std::size_t i = 0; i < samples.size(); ++i)
            data[i] = samples[i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (samples.size() - 1)));
        fft.performFrequencyOnlyForwardTransform (data.data(), true);

        const auto rate = juce::jmax (8000.0, engine.getTransport().sampleRate.load());
        for (std::size_t b = 0; b < bands.size(); ++b)
        {
            const auto f0 = 20.0 * std::pow (1000.0, (double) b / (double) bands.size());
            const auto f1 = 20.0 * std::pow (1000.0, (double) (b + 1) / (double) bands.size());
            auto i0 = (int) (f0 / rate * 1024.0), i1 = juce::jmax (i0 + 1, (int) (f1 / rate * 1024.0));
            i0 = juce::jlimit (1, 511, i0);
            i1 = juce::jlimit (i0 + 1, 512, i1);
            float m = 0.0f;
            for (auto i = i0; i < i1; ++i)
                m = juce::jmax (m, data[(std::size_t) i]);
            const auto db = juce::Decibels::gainToDecibels (m / 256.0f, -72.0f);
            const auto level = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f);
            bands[b] = juce::jmax (level, bands[b] * 0.85f); // fast up, smooth fall
        }
    }

    repaint();
}

juce::String OutputMonitor::modeName (int m)
{
    switch (m)
    {
        case longView: return tr ("Long view (level over time)");
        case spectrum: return tr ("Spectrum (low frequencies on the left)");
        default: return tr ("Oscilloscope (waveform)");
    }
}

void OutputMonitor::setMode (int newMode)
{
    mode = juce::jlimit (0, modeCount - 1, newMode);
    bands.fill (0.0f);
    repaint();
    if (onModeChanged)
        onModeChanged();
}

void OutputMonitor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Output display"));
        for (int m = 0; m < modeCount; ++m)
            menu.addItem (modeName (m), true, mode == m, [this, m] { setMode (m); });
        menu.addSeparator();
        menu.addItem (tr ("Reset the peak hold and clip light"), [this] {
            held = 0.0f;
            clipFrames = holdFrames = 0;
            repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        return;
    }

    if (e.mods.isLeftButtonDown())
        setMode ((mode + 1) % modeCount);
}

void OutputMonitor::paint (juce::Graphics& g)
{
    using namespace theme;
    auto area = getLocalBounds().toFloat();
    g.setColour (colour::bg0);
    g.fillRoundedRectangle (area, radius::md);

    // peak meter on the right: -60..0 dB, the top segment red while it clipped recently
    auto meter = area.removeFromRight (8.0f).reduced (1.5f, 4.0f);
    auto level = [] (float gain) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (gain, -60.0f) + 60.0f) / 60.0f); };
    g.setColour (colour::bg3);
    g.fillRoundedRectangle (meter, 1.5f);
    const auto filled = meter.withTop (meter.getBottom() - meter.getHeight() * level (peak));
    g.setGradientFill (juce::ColourGradient::vertical (colour::meterMid, meter.getY() + meter.getHeight() * 0.1f, colour::meterLow,
                                                       meter.getY() + meter.getHeight() * 0.4f));
    g.fillRect (filled);
    if (held > 0.0f)
    {
        g.setColour (colour::textSecondary);
        g.fillRect (meter.withHeight (1.0f).withY (meter.getBottom() - meter.getHeight() * level (held)));
    }
    if (clipFrames > 0)
    {
        g.setColour (colour::meterClip);
        g.fillRoundedRectangle (meter.withHeight (4.0f), 1.0f);
    }

    auto scope = area.reduced (4.0f, 3.0f);
    const auto lineColour = (clipFrames > 0 ? colour::meterClip : colour::accent).withAlpha (0.9f);

    if (mode == longView) // the level of the last seconds, newest on the right, mirrored around the centre
    {
        g.setColour (colour::border);
        g.drawHorizontalLine ((int) scope.getCentreY(), scope.getX(), scope.getRight());
        const auto columns = juce::jmax (1, (int) scope.getWidth());
        g.setColour (lineColour.withAlpha (0.75f));
        for (int x = 0; x < columns; ++x)
        {
            const auto index = (std::size_t) (x * (int) history.size() / columns);
            const auto h = juce::jmax (0.5f, level (history[index]) * scope.getHeight() * 0.5f);
            g.fillRect (scope.getX() + (float) x, scope.getCentreY() - h, 1.0f, h * 2.0f);
        }
        return;
    }

    if (mode == spectrum) // bars per log-spaced band
    {
        const auto w = scope.getWidth() / (float) bands.size();
        g.setColour (lineColour.withAlpha (0.8f));
        for (std::size_t b = 0; b < bands.size(); ++b)
        {
            const auto h = bands[b] * scope.getHeight();
            g.fillRect (scope.getX() + (float) b * w, scope.getBottom() - h, juce::jmax (1.0f, w - 0.5f), h);
        }
        return;
    }

    // oscilloscope
    g.setColour (colour::border);
    g.drawHorizontalLine ((int) scope.getCentreY(), scope.getX(), scope.getRight());
    juce::Path wave;
    const auto mid = scope.getCentreY();
    const auto half = scope.getHeight() * 0.47f;
    const auto width = juce::jmax (1, (int) scope.getWidth());

    for (int x = 0; x < width; ++x)
    {
        const auto index = (std::size_t) (x * (int) samples.size() / width);
        const auto y = mid - juce::jlimit (-1.0f, 1.0f, samples[index]) * half;

        if (x == 0)
            wave.startNewSubPath (scope.getX(), y);
        else
            wave.lineTo (scope.getX() + (float) x, y);
    }

    g.setColour (lineColour);
    g.strokePath (wave, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

ui::Hint OutputMonitor::hintAt (juce::Point<int>)
{
    ui::Hint h;
    h.text = tr ("Output: {0} and the peak level of what you hear (the meter's top turns red when it clipped)  [LMB] next display  [RMB] display menu",
                 { modeName (mode).toLowerCase() });
    h.hasMenu = true;
    h.value = held <= 1.0e-5f ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (held), 1) + " dB";
    return h;
}
} // namespace lynx::toolbar

// The process memory comes from the OS; its headers go last (windows.h defines macros such as `small`).
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define PSAPI_VERSION 2 // GetProcessMemoryInfo from kernel32 (K32GetProcessMemoryInfo): no extra library
 #include <windows.h>
 #include <psapi.h>
#endif

std::int64_t lynx::toolbar::PerformanceMeter::processMemoryBytes()
{
#if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS counters {};
    if (GetProcessMemoryInfo (GetCurrentProcess(), &counters, sizeof (counters)))
        return (std::int64_t) counters.WorkingSetSize;
    return 0;
#elif JUCE_LINUX
    // resident pages, from /proc
    const auto fields = juce::StringArray::fromTokens (juce::File ("/proc/self/statm").loadFileAsString(), " ", "");
    return fields.size() > 1 ? (std::int64_t) fields[1].getLargeIntValue() * 4096 : 0;
#else
    return 0;
#endif
}
