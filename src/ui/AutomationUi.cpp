#include "ui/WindowOwner.h"
#include "ui/AutomationUi.h"

#include <map>

#include "core/Effects.h"
#include "engine/Text.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
juce::String nameOf (const std::string& s)
{
    return juce::String::fromUTF8 (s.c_str());
}

juce::AudioPluginInstance* pluginFor (AudioEngine& engine, const AutomationTarget& t)
{
    if (t.kind == AutomationTargetKind::channelParam)
        if (auto* p = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (t.index).get()))
            return &p->getInstance();

    if (t.kind == AutomationTargetKind::effectParam)
        if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (t.index, t.slot).get()))
            return &p->getInstance();

    return nullptr;
}

// Point editor: time horizontal (bars), value vertical.
//  - click on empty space creates a point; drag moves; right-click deletes
//  - Alt+drag over a segment changes the curve (tension)
//  - Ctrl+wheel zooms; wheel scrolls
class AutomationEditor final : public juce::Component,
                               private juce::ChangeListener,
                               private juce::Timer
{
public:
    AutomationEditor (AudioEngine& e, int index)
        : engine (e),
          automation (index)
    {
        engine.addChangeListener (this);
        setSize (760, 300);
        startTimerHz (30);
    }

    ~AutomationEditor() override { engine.removeChangeListener (this); }

    int getIndex() const noexcept { return automation; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::window);
        const auto* a = current();

        if (a == nullptr)
            return;

        const auto& song = engine.getSong();
        const auto area = plotArea();
        const auto bar = (std::int64_t) ppq * juce::jmax (1, song.beatsPerBar);

        g.setColour (juce::Colour (0xff262b30));
        g.fillRect (area);

        // grid: beats and bars
        const auto beatPixels = ppq * pixelsPerTick;
        const auto lastTick = (std::int64_t) xToTick (area.getRight());

        for (auto tick = (std::int64_t) std::floor (scroll / ppq) * ppq; tick <= lastTick; tick += ppq)
        {
            const auto isBar = tick % bar == 0;

            if (! isBar && beatPixels < 8.0)
                continue;

            const auto x = (int) tickToX ((double) tick);
            g.setColour (isBar ? juce::Colour (0xff15181b) : juce::Colour (0xff2c3238));
            g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());

            if (isBar)
            {
                g.setColour (theme::text.withAlpha (0.6f));
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (juce::String (tick / bar + 1), x + 3, 2, 40, 16, juce::Justification::centredLeft);
            }
        }

        for (auto v : { 0.25f, 0.5f, 0.75f })
        {
            g.setColour (juce::Colour (0xff2f353c));
            g.drawHorizontalLine ((int) valueToY (v), (float) area.getX(), (float) area.getRight());
        }

        // curve
        juce::Path curve;
        const auto& points = a->points;

        for (auto x = area.getX(); x <= area.getRight(); x += 2)
        {
            const auto y = valueToY (a->valueAt (xToTick (x)));

            if (x == area.getX())
                curve.startNewSubPath ((float) x, y);
            else
                curve.lineTo ((float) x, y);
        }

        g.setColour (theme::accentGreen);
        g.strokePath (curve, juce::PathStrokeType (2.0f));

        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const auto p = pointPosition (points[i]);
            g.setColour ((int) i == hoverPoint || (int) i == dragPoint ? juce::Colours::white : theme::accentGreen);
            g.fillEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f, 1.0f);
        }

        // playback cursor (at the position of the clip currently playing)
        if (playhead >= 0.0)
        {
            g.setColour (theme::playhead);
            g.drawVerticalLine ((int) tickToX (playhead), (float) area.getY(), (float) area.getBottom());
        }

        g.setColour (theme::text.withAlpha (0.45f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (utf8 ("Click: new point · drag: move · right-click: delete · Alt+drag: curve · Ctrl+wheel: zoom"),
                    area.getX(), getHeight() - 18, area.getWidth(), 16, juce::Justification::centredLeft);

        if (hoverPoint >= 0 && hoverPoint < (int) points.size())
        {
            const auto p = pointPosition (points[(std::size_t) hoverPoint]);
            g.setColour (theme::text);
            const auto value = points[(std::size_t) hoverPoint].value;
            g.drawText (a->target.kind == AutomationTargetKind::tempo ? juce::String (tempoFromValue (value), 1) + " BPM"
                                                                       : juce::String (juce::roundToInt (value * 100.0f)) + "%",
                        (int) p.x + 8, (int) p.y - 18, 60, 14, juce::Justification::centredLeft);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto hit = pointAt (e.position);

        if (hit != hoverPoint)
        {
            hoverPoint = hit;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* a = current();

        if (a == nullptr || ! plotArea().contains (e.getPosition()))
            return;

        const auto hit = pointAt (e.position);

        if (e.mods.isPopupMenu())
        {
            if (hit >= 0 && a->points.size() > 1)
                edit ([hit] (Automation& x) { x.points.erase (x.points.begin() + hit); });
            return;
        }

        if (e.mods.isAltDown())
        {
            // segment under the mouse: the point on the left holds the tension
            tensionPoint = -1;
            const auto tick = xToTick (e.x);

            for (int i = 0; i + 1 < (int) a->points.size(); ++i)
                if (tick >= (double) a->points[(std::size_t) i].tick && tick < (double) a->points[(std::size_t) i + 1].tick)
                    tensionPoint = i;

            if (tensionPoint >= 0)
                tensionStart = a->points[(std::size_t) tensionPoint].tension;
            return;
        }

        if (hit >= 0)
        {
            dragPoint = hit;
            return;
        }

        AutomationPoint p;
        p.tick = snap (xToTick (e.x), ! e.mods.isShiftDown());
        p.value = yToValue (e.y);
        edit ([p] (Automation& x) {
            x.points.push_back (p);
            std::stable_sort (x.points.begin(), x.points.end(), [] (auto& l, auto& r) { return l.tick < r.tick; });
        });

        for (int i = 0; i < (int) current()->points.size(); ++i)
            if (current()->points[(std::size_t) i].tick == p.tick && current()->points[(std::size_t) i].value == p.value)
                dragPoint = i;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto* a = current();

        if (a == nullptr)
            return;

        if (tensionPoint >= 0 && tensionPoint < (int) a->points.size())
        {
            const auto tension = juce::jlimit (-1.0f, 1.0f, tensionStart + (float) e.getDistanceFromDragStartY() / 100.0f);
            edit ([i = tensionPoint, tension] (Automation& x) { x.points[(std::size_t) i].tension = tension; });
            return;
        }

        if (dragPoint < 0 || dragPoint >= (int) a->points.size())
            return;

        // the point can't pass its neighbours (order doesn't change while dragging)
        const auto& points = a->points;
        auto tick = snap (xToTick (e.x), ! e.mods.isShiftDown());
        const auto lo = dragPoint > 0 ? points[(std::size_t) dragPoint - 1].tick : 0;
        const auto hi = dragPoint + 1 < (int) points.size() ? points[(std::size_t) dragPoint + 1].tick : std::numeric_limits<std::int64_t>::max();
        tick = juce::jlimit (lo, hi, tick);
        const auto value = yToValue (e.y);

        edit ([i = dragPoint, tick, value] (Automation& x) {
            x.points[(std::size_t) i].tick = tick;
            x.points[(std::size_t) i].value = value;
        });
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragPoint = tensionPoint = -1;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isCtrlDown())
        {
            const auto anchor = xToTick (e.x);
            pixelsPerTick = juce::jlimit (4.0 / (4 * ppq), 600.0 / (4 * ppq), pixelsPerTick * (wheel.deltaY > 0 ? 1.2 : 1.0 / 1.2));
            scroll = juce::jmax (0.0, anchor - (e.x - plotArea().getX()) / pixelsPerTick);
        }
        else
            scroll = juce::jmax (0.0, scroll - (wheel.deltaY + wheel.deltaX) * 300.0 / pixelsPerTick);

        repaint();
    }

private:
    const Automation* current() const
    {
        const auto& song = engine.getSong();
        return automation >= 0 && automation < (int) song.automations.size() ? &song.automations[(std::size_t) automation] : nullptr;
    }

    void edit (const std::function<void (Automation&)>& fn)
    {
        engine.editSong ([this, &fn] (Song& s) {
            if (automation >= 0 && automation < (int) s.automations.size())
                fn (s.automations[(std::size_t) automation]);
        });
    }

    juce::Rectangle<int> plotArea() const { return getLocalBounds().withTrimmedTop (18).withTrimmedBottom (20).reduced (8, 4); }
    double tickToX (double tick) const { return plotArea().getX() + (tick - scroll) * pixelsPerTick; }
    double xToTick (int x) const { return juce::jmax (0.0, (x - plotArea().getX()) / pixelsPerTick + scroll); }
    float valueToY (float v) const { return (float) plotArea().getBottom() - v * (float) plotArea().getHeight(); }
    float yToValue (int y) const { return juce::jlimit (0.0f, 1.0f, (float) (plotArea().getBottom() - y) / (float) plotArea().getHeight()); }

    std::int64_t snap (double tick, bool on) const
    {
        const auto grid = on ? (double) ticksPerStep : 1.0;
        return (std::int64_t) std::round (tick / grid) * (std::int64_t) grid;
    }

    juce::Point<float> pointPosition (const AutomationPoint& p) const { return { (float) tickToX ((double) p.tick), valueToY (p.value) }; }

    int pointAt (juce::Point<float> pos) const
    {
        const auto* a = current();

        if (a == nullptr)
            return -1;

        for (int i = (int) a->points.size() - 1; i >= 0; --i)
            if (pointPosition (a->points[(std::size_t) i]).getDistanceFrom (pos) < 8.0f)
                return i;

        return -1;
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (current() == nullptr) // automation removed: close
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Component> (getParentComponent())] {
                if (auto* w = dynamic_cast<juce::DocumentWindow*> (safe.getComponent()))
                    w->closeButtonPressed();
            });
            return;
        }

        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
            w->setName (utf8 ("Automation - ") + juce::String::fromUTF8 (current()->name.c_str()));

        repaint();
    }

    void timerCallback() override
    {
        // position within the automation, if one of its clips is under the cursor
        auto tick = -1.0;
        const auto& song = engine.getSong();

        if (song.mode == PlayMode::song && engine.getTransport().playing.load())
        {
            const auto position = std::fmod (engine.getTransport().positionTicks.load(), (double) juce::jmax<std::int64_t> (1, song.songLength()));

            for (const auto& c : song.playlist)
                if (c.kind == ClipKind::automation && c.automation == automation && position >= (double) c.start && position < (double) c.end())
                    tick = position - (double) c.start + (double) c.offset;
        }

        if (tick != playhead)
        {
            playhead = tick;
            repaint();
        }
    }

    AudioEngine& engine;
    int automation;
    double pixelsPerTick = 120.0 / (4 * ppq);
    double scroll = 0.0;
    int dragPoint = -1, hoverPoint = -1, tensionPoint = -1;
    float tensionStart = 0.0f;
    double playhead = -1.0;
};

class AutomationWindow final : public juce::DocumentWindow
{
public:
    AutomationWindow (AudioEngine& engine, int index, const juce::String& title)
        : juce::DocumentWindow (title, theme::window, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new AutomationEditor (engine, index), true);
        setResizable (true, false);
        setResizeLimits (400, 200, 4000, 2000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
    }

    int getIndex() const { return static_cast<AutomationEditor*> (getContentComponent())->getIndex(); }

    void closeButtonPressed() override;
};

std::vector<std::unique_ptr<AutomationWindow>>& windows()
{
    static std::vector<std::unique_ptr<AutomationWindow>> open;
    return open;
}

void AutomationWindow::closeButtonPressed()
{
    auto* self = this;
    juce::MessageManager::callAsync ([self] {
        auto& open = windows();
        open.erase (std::remove_if (open.begin(), open.end(), [self] (auto& w) { return w.get() == self; }), open.end());
    });
}
} // namespace

juce::String describeAutomationTarget (AudioEngine& engine, const AutomationTarget& t)
{
    const auto& song = engine.getSong();
    const auto channelName = t.index >= 0 && t.index < (int) song.channels.size() ? nameOf (song.channels[(std::size_t) t.index].name) : juce::String ("?");
    const auto trackName = t.index >= 0 && t.index < (int) song.mixer.size() ? nameOf (song.mixer[(std::size_t) t.index].name) : juce::String ("?");

    switch (t.kind)
    {
        case AutomationTargetKind::channelVolume: return channelName + " - Volume";
        case AutomationTargetKind::channelPan: return channelName + " - Pan";
        case AutomationTargetKind::mixerVolume: return trackName + " - Volume";
        case AutomationTargetKind::mixerPan: return trackName + " - Pan";

        case AutomationTargetKind::channelParam:
        case AutomationTargetKind::effectParam:
        {
            juce::String param = juce::String ("Param ") + juce::String (t.param + 1);

            if (auto* plugin = pluginFor (engine, t))
            {
                if (t.param >= 0 && t.param < plugin->getParameters().size())
                    param = plugin->getParameters()[t.param]->getName (40);
            }
            else if (t.kind == AutomationTargetKind::effectParam && t.index < (int) song.mixer.size()
                     && t.slot < (int) song.mixer[(std::size_t) t.index].effects.size())
            {
                const auto& info = effectInfo (song.mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind);

                if (t.param >= 0 && t.param < (int) info.params.size())
                    param = utf8 (info.params[(std::size_t) t.param].name);
            }

            return (t.kind == AutomationTargetKind::channelParam ? channelName : trackName) + " - " + param;
        }

        case AutomationTargetKind::tempo: return "Tempo";
        case AutomationTargetKind::effectMix:
        {
            juce::String effect = "FX " + juce::String (t.slot + 1);
            if (t.index < (int) song.mixer.size() && t.slot >= 0 && t.slot < (int) song.mixer[(std::size_t) t.index].effects.size())
                effect = utf8 (effectInfo (song.mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind).name);
            return trackName + " - " + effect + " - Mix";
        }
        case AutomationTargetKind::none: break;
    }

    return utf8 ("(no target)");
}

void createAutomationClip (AudioEngine& engine, const AutomationTarget& target)
{
    auto value = engine.getSong().targetValue (target);

    if (auto* plugin = pluginFor (engine, target); plugin != nullptr && target.param < plugin->getParameters().size())
        value = plugin->getParameters()[target.param]->getValue();

    if (value < 0.0f)
        return;

    const auto name = describeAutomationTarget (engine, target).toStdString();

    engine.editSong ([&] (Song& s) {
        const auto bar = (std::int64_t) ppq * std::max (1, s.beatsPerBar);
        const auto length = s.playlist.empty() ? bar * 4 : s.songLength();

        Automation a;
        a.name = name;
        a.target = target;
        a.points = { { 0, value, 0.0f }, { length, value, 0.0f } };
        s.automations.push_back (std::move (a));

        int track = 0;
        for (const auto& c : s.playlist)
            track = std::max (track, c.track + 1);

        Clip clip;
        clip.kind = ClipKind::automation;
        clip.automation = (int) s.automations.size() - 1;
        clip.track = track;
        clip.length = length;
        s.playlist.push_back (clip);
    });

    openAutomationEditor (engine, (int) engine.getSong().automations.size() - 1);
}

void openAutomationEditor (AudioEngine& engine, int index)
{
    if (index < 0 || index >= (int) engine.getSong().automations.size())
        return;

    for (auto& w : windows())
        if (w->getIndex() == index)
        {
            w->toFront (true);
            return;
        }

    windows().push_back (std::make_unique<AutomationWindow> (
        engine, index, utf8 ("Automation - ") + nameOf (engine.getSong().automations[(std::size_t) index].name)));
}

void closeAutomationEditors()
{
    windows().clear();
}

std::unique_ptr<juce::Component> makeAutomationEditor (AudioEngine& engine, int automation)
{
    return std::make_unique<AutomationEditor> (engine, automation);
}

void addAutomationItem (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target)
{
    menu.addItem (utf8 ("Create automation clip"), [&engine, target] { createAutomationClip (engine, target); });
    menu.addItem (utf8 ("Link to MIDI controller (move a knob on the controller)"), [&engine, target] {
        engine.learnMidiController (target);
    });

    const auto& links = engine.getSong().midiLinks;
    const auto linked = std::find_if (links.begin(), links.end(), [&] (const MidiLink& l) { return l.target == target; });

    if (linked != links.end())
        menu.addItem (utf8 ("Unlink controller (CC ") + juce::String (linked->cc) + ")", [&engine, target] {
            engine.editSong ([target] (Song& s) {
                s.midiLinks.erase (std::remove_if (s.midiLinks.begin(), s.midiLinks.end(), [&] (const MidiLink& l) { return l.target == target; }),
                                   s.midiLinks.end());
            });
        });
}

void addPluginParameterItems (juce::PopupMenu& menu, AudioEngine& engine, juce::AudioPluginInstance& plugin,
                              const AutomationTarget& base)
{
    juce::PopupMenu params;
    const auto& list = plugin.getParameters();

    for (int i = 0; i < list.size() && i < 512; ++i)
    {
        auto target = base;
        target.param = i;
        juce::PopupMenu param;
        addAutomationItem (param, engine, target);
        params.addSubMenu (list[i]->getName (60), param);
    }

    if (list.isEmpty())
        params.addItem (utf8 ("(the plugin exposes no parameters)"), false, false, [] {});

    menu.addSubMenu (utf8 ("Automate plugin parameter"), params);
}
} // namespace lynx
