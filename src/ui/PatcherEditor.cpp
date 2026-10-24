#include "ui/PatcherEditor.h"

#include <algorithm>
#include <cmath>

#include "core/Effects.h"
#include "core/Patcher.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/ChannelRackComponent.h"
#include "ui/PluginWindow.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
namespace col = theme::colour;

constexpr float nodeWidth = 150.0f;
constexpr float headerHeight = 24.0f;
constexpr float pinSpacing = 17.0f;
constexpr float pinRadius = 5.0f;

// Where the graph lives in the song: a mixer slot or a channel
struct Target
{
    bool channel = false;
    int index = 0; // track or channel
    int slot = 0;

    const PatcherDoc* doc (const Song& song) const
    {
        if (channel)
            return index >= 0 && index < (int) song.channels.size() && song.channels[(std::size_t) index].instrument == InstrumentKind::patcher
                       ? &song.channels[(std::size_t) index].patcher
                       : nullptr;
        if (index < 0 || index >= (int) song.mixer.size() || slot < 0 || slot >= (int) song.mixer[(std::size_t) index].effects.size())
            return nullptr;
        const auto& fx = song.mixer[(std::size_t) index].effects[(std::size_t) slot];
        return fx.kind == EffectKind::patcher ? &fx.patcher : nullptr;
    }
    PatcherDoc* doc (Song& song) const { return const_cast<PatcherDoc*> (doc (static_cast<const Song&> (song))); }

    juce::String key() const { return channel ? "patcher-ch-" + juce::String (index) : "patcher-fx-" + juce::String (index) + "-" + juce::String (slot); }
};

struct Pin
{
    int node = patchFromHost; // node index, patchFromHost / patchToHost
    bool output = false;      // carries a signal out of its node
    PatchWire::Kind kind = PatchWire::Kind::audio;
    int port = 0;
    juce::Point<float> at; // world position
    juce::String label;
    bool adds = false;     // "To LYNX": the pin that makes a new extra output
};

juce::Colour wireColour (PatchWire::Kind kind)
{
    switch (kind)
    {
        case PatchWire::Kind::audio: return col::knobArc;
        case PatchWire::Kind::midi: return col::info;
        case PatchWire::Kind::control: return col::automation;
    }
    return col::knobArc;
}

class PatcherEditor final : public juce::Component, private juce::Timer
{
public:
    PatcherEditor (AudioEngine& e, Target t) : engine (e), target (t)
    {
        setSize (900, 560);
        setWantsKeyboardFocus (true);
        startTimerHz (10);
        fit();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg0);
        const auto* doc = currentDoc();
        if (doc == nullptr)
        {
            g.setColour (col::textSecondary);
            g.drawText (tr ("This Patcher is gone."), getLocalBounds(), juce::Justification::centred);
            return;
        }

        // grid
        g.setColour (col::gridStep);
        const auto step = 40.0f * zoom;
        for (auto x = std::fmod (-offset.x * zoom, step); x < (float) getWidth(); x += step)
            g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        for (auto y = std::fmod (-offset.y * zoom, step); y < (float) getHeight(); y += step)
            g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());

        const auto pins = allPins (*doc);

        // cables
        for (std::size_t w = 0; w < doc->wires.size(); ++w)
            drawWire (g, *doc, pins, doc->wires[w], (int) w == selectedWire);

        // nodes
        drawHost (g, *doc, true);
        drawHost (g, *doc, false);
        for (int n = 0; n < (int) doc->nodes.size(); ++n)
            drawNode (g, *doc, n);
        for (const auto& p : pins)
            drawPin (g, p);

        // the cable being drawn
        if (dragging == Drag::wire)
        {
            g.setColour (wireColour (dragFrom.kind).withAlpha (0.8f));
            drawCurve (g, toScreen (dragFrom.at), dragPoint, 2.0f);
        }

        g.setColour (col::textSecondary);
        g.setFont (theme::type::small);
        g.drawText (tr ("Right-click: add a plugin. Drag from an output to an input to connect. Double-click a node: its editor."),
                    getLocalBounds().removeFromBottom (20).reduced (8, 0), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto* doc = currentDoc();
        if (doc == nullptr)
            return;
        const auto world = toWorld (e.position);
        if (e.mods.isPopupMenu())
        {
            if (const auto n = nodeAt (*doc, world); n != noNode)
                showNodeMenu (n);
            else if (const auto w = wireAt (*doc, e.position); w >= 0)
                showWireMenu (w);
            else
                showAddMenu (world);
            return;
        }
        if (const auto pin = pinAt (*doc, world); pin && pin->output)
        {
            dragging = Drag::wire;
            dragFrom = *pin;
            dragPoint = e.position;
            return;
        }
        if (const auto n = nodeAt (*doc, world); n != noNode)
        {
            selectedNode = n;
            selectedWire = -1;
            dragging = Drag::node;
            dragStart = world;
            dragOrigin = nodePosition (*doc, n);
            repaint();
            return;
        }
        selectedWire = wireAt (*doc, e.position);
        selectedNode = noNode;
        if (selectedWire < 0)
        {
            dragging = Drag::view;
            dragStart = e.position;
            dragOrigin = offset;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == Drag::wire)
            dragPoint = e.position;
        else if (dragging == Drag::node)
            moved = dragOrigin + (toWorld (e.position) - dragStart);
        else if (dragging == Drag::view)
            offset = dragOrigin - (e.position - dragStart) / zoom;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto mode = std::exchange (dragging, Drag::none);
        const auto* doc = currentDoc();
        if (doc == nullptr)
            return;
        if (mode == Drag::node && moved)
        {
            const auto n = selectedNode;
            const auto p = *moved;
            moved.reset();
            edit ([n, p] (PatcherDoc& d) { setNodePosition (d, n, p); });
        }
        else if (mode == Drag::wire)
            finishWire (*doc, toWorld (e.position));
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const auto* doc = currentDoc())
            if (const auto n = nodeAt (*doc, toWorld (e.position)); n >= 0)
                openNodeEditor (n);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto before = toWorld (e.position);
        zoom = std::clamp (zoom * (wheel.deltaY > 0 ? 1.1f : 1.0f / 1.1f), 0.25f, 3.0f);
        offset = before - e.position / zoom;
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (selectedNode >= 0)
            {
                const auto n = selectedNode;
                selectedNode = noNode;
                edit ([n] (PatcherDoc& d) { patcher::removeNode (d, n); });
                return true;
            }
            if (selectedWire >= 0)
            {
                const auto w = selectedWire;
                selectedWire = -1;
                edit ([w] (PatcherDoc& d) {
                    if (w < (int) d.wires.size())
                        d.wires.erase (d.wires.begin() + w);
                });
                return true;
            }
        }
        return false;
    }

private:
    enum class Drag { none, wire, node, view };
    static constexpr int noNode = -100;

    AudioEngine& engine;
    Target target;
    float zoom = 1.0f;
    juce::Point<float> offset;
    int selectedNode = noNode, selectedWire = -1;
    Drag dragging = Drag::none;
    Pin dragFrom;
    juce::Point<float> dragPoint, dragStart, dragOrigin;
    std::optional<juce::Point<float>> moved; // the node being dragged (committed on release)
    PatcherDoc shown;                        // what was last painted (to repaint on changes from elsewhere)

    const PatcherDoc* currentDoc() const { return target.doc (engine.getSong()); }

    void timerCallback() override
    {
        const auto* doc = currentDoc();
        if (doc == nullptr ? ! shown.empty() : ! (*doc == shown))
        {
            shown = doc != nullptr ? *doc : PatcherDoc {};
            repaint();
        }
    }

    void edit (std::function<void (PatcherDoc&)> fn)
    {
        engine.editSong ([t = target, fn] (Song& song) {
            if (auto* d = t.doc (song))
                fn (*d);
        });
        repaint();
    }

    // --- geometry ----------------------------------------------------------------------------------------------
    juce::Point<float> toScreen (juce::Point<float> world) const { return (world - offset) * zoom; }
    juce::Point<float> toWorld (juce::Point<float> screen) const { return screen / zoom + offset; }

    static juce::Point<float> rawPosition (const PatcherDoc& d, int n)
    {
        if (n == patchFromHost)
            return { d.inputX, d.inputY };
        if (n == patchToHost)
            return { d.outputX, d.outputY };
        return { d.nodes[(std::size_t) n].x, d.nodes[(std::size_t) n].y };
    }
    juce::Point<float> nodePosition (const PatcherDoc& d, int n) const
    {
        if (moved && n == selectedNode && dragging == Drag::node)
            return *moved;
        return rawPosition (d, n);
    }
    static void setNodePosition (PatcherDoc& d, int n, juce::Point<float> p)
    {
        if (n == patchFromHost)
            d.inputX = p.x, d.inputY = p.y;
        else if (n == patchToHost)
            d.outputX = p.x, d.outputY = p.y;
        else if (n >= 0 && n < (int) d.nodes.size())
            d.nodes[(std::size_t) n].x = p.x, d.nodes[(std::size_t) n].y = p.y;
    }

    int extraOutputs (const PatcherDoc& d) const
    {
        int k = (int) d.outputTracks.size();
        for (const auto& w : d.wires)
            if (w.kind == PatchWire::Kind::audio && w.to == patchToHost)
                k = std::max (k, w.toPort);
        return k;
    }

    static std::vector<int> midiPorts (const PatcherDoc& d)
    {
        std::vector<int> ports;
        for (const auto& w : d.wires)
            if (w.kind == PatchWire::Kind::midi && w.to == patchToHost && std::find (ports.begin(), ports.end(), w.toPort) == ports.end())
                ports.push_back (w.toPort);
        std::sort (ports.begin(), ports.end());
        return ports;
    }

    std::vector<Pin> hostPins (const PatcherDoc& d, bool input) const
    {
        std::vector<Pin> pins;
        const auto at = nodePosition (d, input ? patchFromHost : patchToHost);
        auto y = at.y + headerHeight + pinSpacing * 0.5f;
        auto add = [&] (PatchWire::Kind kind, int port, const juce::String& label, bool adds = false) {
            Pin p;
            p.node = input ? patchFromHost : patchToHost;
            p.output = input;
            p.kind = kind;
            p.port = port;
            p.at = { input ? at.x + nodeWidth : at.x, y };
            p.label = label;
            p.adds = adds;
            pins.push_back (p);
            y += pinSpacing;
        };
        if (input)
        {
            if (! target.channel)
            {
                add (PatchWire::Kind::audio, 0, tr ("Audio input"));
                add (PatchWire::Kind::audio, 1, tr ("Side-chain"));
            }
            else
                add (PatchWire::Kind::midi, 0, tr ("Notes")); // (a mixer slot has no notes to give)
        }
        else
        {
            add (PatchWire::Kind::audio, 0, tr ("Audio output"));
            for (int k = 1; k <= extraOutputs (d); ++k)
            {
                const auto track = k <= (int) d.outputTracks.size() ? d.outputTracks[(std::size_t) k - 1] : -1;
                add (PatchWire::Kind::audio, k, track > 0 ? tr ("Insert {0}", { juce::String (track) }) : tr ("Output {0}", { juce::String (k + 1) }));
            }
            add (PatchWire::Kind::audio, extraOutputs (d) + 1, tr ("+ output"), true);
            if (target.channel) // the notes out to the plugins on a MIDI port (FL's "MIDI port N")
            {
                const auto ports = midiPorts (d);
                for (const auto port : ports)
                    add (PatchWire::Kind::midi, port, tr ("MIDI port {0}", { juce::String (port) }));
                add (PatchWire::Kind::midi, ports.empty() ? 1 : ports.back() + 1, tr ("+ MIDI port"), true);
            }
        }
        return pins;
    }

    std::vector<Pin> nodePins (const PatcherDoc& d, int n) const
    {
        std::vector<Pin> pins;
        const auto& node = d.nodes[(std::size_t) n];
        const auto ports = patcher::nodePorts (node);
        const auto at = nodePosition (d, n);
        auto left = at.y + headerHeight + pinSpacing * 0.5f, right = left;
        auto add = [&] (bool output, PatchWire::Kind kind, int port, const juce::String& label) {
            Pin p;
            p.node = n;
            p.output = output;
            p.kind = kind;
            p.port = port;
            auto& y = output ? right : left;
            p.at = { output ? at.x + nodeWidth : at.x, y };
            y += pinSpacing;
            p.label = label;
            pins.push_back (p);
        };
        if (ports.midiIn)
            add (false, PatchWire::Kind::midi, 0, tr ("Notes"));
        for (int i = 0; i < ports.audioIns; ++i)
            add (false, PatchWire::Kind::audio, i, i == 0 ? tr ("In") : tr ("Side-chain"));
        const auto splitter = ! node.generator && node.effect.kind == EffectKind::frequencySplitter && node.busHeads.empty();
        for (int o = 0; o < ports.audioOuts; ++o)
            add (true, PatchWire::Kind::audio, o,
                 o == 0                    ? tr ("Out")
                 : splitter && o == 1      ? tr ("Low")
                 : splitter && o == 2      ? tr ("Mid")
                 : splitter && o == 3      ? tr ("High")
                                           : tr ("Out {0}", { juce::String (o + 1) }));
        for (int c = 0; c < ports.controlOuts; ++c)
            add (true, PatchWire::Kind::control, c, tr ("Control {0}", { juce::String (c + 1) }));
        return pins;
    }

    std::vector<Pin> allPins (const PatcherDoc& d) const
    {
        auto pins = hostPins (d, true);
        for (auto& p : hostPins (d, false))
            pins.push_back (p);
        for (int n = 0; n < (int) d.nodes.size(); ++n)
            for (auto& p : nodePins (d, n))
                pins.push_back (p);
        return pins;
    }

    juce::Rectangle<float> nodeBounds (const PatcherDoc& d, int n) const
    {
        const auto at = nodePosition (d, n);
        int rows = 1;
        if (n == patchFromHost)
            rows = target.channel ? 1 : 2;
        else if (n == patchToHost)
            rows = extraOutputs (d) + 2 + (target.channel ? (int) midiPorts (d).size() + 1 : 0);
        else
        {
            const auto ports = patcher::nodePorts (d.nodes[(std::size_t) n]);
            rows = std::max ({ 1, ports.audioIns + (ports.midiIn ? 1 : 0), ports.audioOuts + ports.controlOuts });
        }
        return { at.x, at.y, nodeWidth, headerHeight + (float) rows * pinSpacing + 4.0f };
    }

    int nodeAt (const PatcherDoc& d, juce::Point<float> world) const
    {
        for (int n = (int) d.nodes.size() - 1; n >= 0; --n)
            if (nodeBounds (d, n).contains (world))
                return n;
        for (const auto host : { patchFromHost, patchToHost })
            if (nodeBounds (d, host).contains (world))
                return host;
        return noNode;
    }

    std::optional<Pin> pinAt (const PatcherDoc& d, juce::Point<float> world) const
    {
        for (const auto& p : allPins (d))
            if (p.at.getDistanceFrom (world) <= pinRadius + 4.0f / zoom)
                return p;
        return std::nullopt;
    }

    const Pin* findPin (const std::vector<Pin>& pins, int node, bool output, PatchWire::Kind kind, int port) const
    {
        for (const auto& p : pins)
            if (p.node == node && p.output == output && p.kind == kind && p.port == port && ! p.adds)
                return &p;
        return nullptr;
    }

    // a control cable ends on the top of its target node
    juce::Point<float> controlTarget (const PatcherDoc& d, int node) const
    {
        const auto b = nodeBounds (d, node);
        return { b.getCentreX(), b.getY() };
    }

    bool wireEnds (const PatcherDoc& d, const std::vector<Pin>& pins, const PatchWire& w, juce::Point<float>& a, juce::Point<float>& b) const
    {
        const auto* from = findPin (pins, w.from, true, w.kind, w.fromPort);
        if (from == nullptr)
            return false;
        a = from->at;
        if (w.kind == PatchWire::Kind::control)
        {
            if (w.to < 0 || w.to >= (int) d.nodes.size())
                return false;
            b = controlTarget (d, w.to);
            return true;
        }
        const auto* to = findPin (pins, w.to, false, w.kind, w.toPort);
        if (to == nullptr)
            return false;
        b = to->at;
        return true;
    }

    int wireAt (const PatcherDoc& d, juce::Point<float> screen) const
    {
        const auto pins = allPins (d);
        for (int w = (int) d.wires.size() - 1; w >= 0; --w)
        {
            juce::Point<float> a, b;
            if (! wireEnds (d, pins, d.wires[(std::size_t) w], a, b))
                continue;
            const auto path = curve (toScreen (a), toScreen (b));
            juce::Point<float> nearest;
            path.getNearestPoint (screen, nearest);
            if (nearest.getDistanceFrom (screen) < 5.0f)
                return w;
        }
        return -1;
    }

    static juce::Path curve (juce::Point<float> a, juce::Point<float> b)
    {
        juce::Path p;
        const auto dx = std::max (40.0f, std::abs (b.x - a.x) * 0.5f);
        p.startNewSubPath (a);
        p.cubicTo (a.x + dx, a.y, b.x - dx, b.y, b.x, b.y);
        return p;
    }
    static void drawCurve (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, float thickness)
    {
        g.strokePath (curve (a, b), juce::PathStrokeType (thickness));
    }

    // --- drawing -----------------------------------------------------------------------------------------------
    void drawWire (juce::Graphics& g, const PatcherDoc& d, const std::vector<Pin>& pins, const PatchWire& w, bool selected) const
    {
        juce::Point<float> a, b;
        if (! wireEnds (d, pins, w, a, b))
            return;
        auto colour = selected ? col::accent : wireColour (w.kind);
        if (! w.enabled)
            colour = colour.withAlpha (0.3f);
        g.setColour (colour);
        const auto sa = toScreen (a), sb = toScreen (b);
        drawCurve (g, sa, sb, selected ? 2.5f : 1.6f);
        juce::String text;
        if (w.kind == PatchWire::Kind::audio && std::abs (w.gain - 1.0f) > 1e-4f)
            text = juce::String (w.gain, 2) + "x";
        if (w.kind == PatchWire::Kind::control && w.to >= 0 && w.to < (int) d.nodes.size())
            text = paramName (d.nodes[(std::size_t) w.to], w.toPort);
        if (text.isNotEmpty())
        {
            g.setFont (theme::type::caption);
            const auto mid = (sa + sb) * 0.5f;
            g.drawText (text, juce::Rectangle<float> (mid.x - 60.0f, mid.y - 16.0f, 120.0f, 14.0f), juce::Justification::centred);
        }
    }

    void drawBox (juce::Graphics& g, juce::Rectangle<float> world, const juce::String& title, bool selected, bool host, bool off) const
    {
        const auto r = juce::Rectangle<float> (toScreen (world.getTopLeft()), toScreen (world.getBottomRight()));
        g.setColour (host ? col::bg3 : col::bg2);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (host ? col::accentMuted : col::bg4);
        g.fillRoundedRectangle (r.withHeight (headerHeight * zoom), 5.0f);
        g.setColour (selected ? col::accent : col::borderStrong);
        g.drawRoundedRectangle (r, 5.0f, selected ? 2.0f : 1.0f);
        g.setColour (off ? col::textDisabled : col::textPrimary);
        g.setFont (theme::type::body * std::clamp (zoom, 0.7f, 1.3f));
        g.drawText (title, r.withHeight (headerHeight * zoom).reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
    }

    void drawHost (juce::Graphics& g, const PatcherDoc& d, bool input) const
    {
        const auto n = input ? patchFromHost : patchToHost;
        drawBox (g, nodeBounds (d, n), input ? tr ("From LYNX") : tr ("To LYNX"), selectedNode == n, true, false);
    }

    void drawNode (juce::Graphics& g, const PatcherDoc& d, int n) const
    {
        const auto& node = d.nodes[(std::size_t) n];
        const auto off = ! node.generator && ! node.effect.enabled;
        drawBox (g, nodeBounds (d, n), juce::String::fromUTF8 (patcher::nodeName (node).c_str()), selectedNode == n, false, off);
        if (node.generator)
        {
            const auto r = nodeBounds (d, n);
            g.setColour (col::info);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (toScreen ({ r.getRight() - 10.0f, r.getY() + headerHeight * 0.5f })));
        }
    }

    void drawPin (juce::Graphics& g, const Pin& p) const
    {
        const auto s = toScreen (p.at);
        const auto r = pinRadius * std::clamp (zoom, 0.6f, 1.5f);
        g.setColour (p.adds ? col::textDisabled : wireColour (p.kind));
        if (p.kind == PatchWire::Kind::midi)
            g.fillRect (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (s));
        else
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (s));
        if (zoom >= 0.6f)
        {
            g.setColour (col::textSecondary);
            g.setFont (theme::type::caption);
            const auto w = 64.0f;
            const auto box = p.output ? juce::Rectangle<float> (s.x - r - 4.0f - w, s.y - 7.0f, w, 14.0f)
                                      : juce::Rectangle<float> (s.x + r + 4.0f, s.y - 7.0f, w, 14.0f);
            g.drawText (p.label, box, p.output ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
        }
    }

    // --- editing -----------------------------------------------------------------------------------------------
    static juce::String paramName (const PatchNode& node, int param)
    {
        if (! node.generator && node.effect.kind != EffectKind::plugin)
        {
            const auto& info = effectInfo (node.effect.kind);
            if (param >= 0 && param < (int) info.params.size())
                return juce::String::fromUTF8 (info.params[(std::size_t) param].name);
        }
        return tr ("Parameter {0}", { juce::String (param + 1) });
    }

    void finishWire (const PatcherDoc& d, juce::Point<float> world)
    {
        const auto from = dragFrom;
        if (from.kind == PatchWire::Kind::control)
        {
            const auto n = nodeAt (d, world);
            if (n < 0 || n == from.node)
                return;
            chooseParameter (d.nodes[(std::size_t) n], [this, from, n] (int param) {
                PatchWire w;
                w.kind = PatchWire::Kind::control;
                w.from = from.node;
                w.fromPort = from.port;
                w.to = n;
                w.toPort = param;
                edit ([w] (PatcherDoc& doc) { patcher::connect (doc, w); });
            });
            return;
        }
        const auto pin = pinAt (d, world);
        if (! pin || pin->output || pin->kind != from.kind)
            return;
        PatchWire w;
        w.kind = from.kind;
        w.from = from.node;
        w.fromPort = from.port;
        w.to = pin->node;
        w.toPort = pin->port;
        edit ([w] (PatcherDoc& doc) {
            if (w.to == patchToHost && w.toPort > (int) doc.outputTracks.size())
                doc.outputTracks.resize ((std::size_t) w.toPort, -1);
            patcher::connect (doc, w);
        });
    }

    void chooseParameter (const PatchNode& node, std::function<void (int)> done)
    {
        juce::PopupMenu menu;
        if (! node.generator && node.effect.kind != EffectKind::plugin)
        {
            const auto& info = effectInfo (node.effect.kind);
            for (int p = 0; p < (int) info.params.size(); ++p)
                menu.addItem (juce::String::fromUTF8 (info.params[(std::size_t) p].name), [done, p] { done (p); });
        }
        else
        {
            // a plugin: its parameter list, from the running instance
            juce::AudioPluginInstance* instance = nullptr;
            if (const auto* graph = runningGraph())
            {
                const auto index = selectedOrDropped (node);
                if (auto* fx = dynamic_cast<PluginEffect*> (graph->effectAt (index).get()))
                    instance = &fx->getInstance();
                else if (auto* in = dynamic_cast<PluginInstrument*> (graph->instrumentAt (index).get()))
                    instance = &in->getInstance();
            }
            if (instance != nullptr)
            {
                const auto& params = instance->getParameters();
                for (int p = 0; p < params.size() && p < 512; ++p)
                    menu.addItem (params[p]->getName (40), [done, p] { done (p); });
            }
            else
                for (int p = 0; p < 16; ++p)
                    menu.addItem (tr ("Parameter {0}", { juce::String (p + 1) }), [done, p] { done (p); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    int selectedOrDropped (const PatchNode& node) const
    {
        if (const auto* d = currentDoc())
            for (int n = 0; n < (int) d->nodes.size(); ++n)
                if (&d->nodes[(std::size_t) n] == &node)
                    return n;
        return -1;
    }

    patcher::Graph* runningGraph() const
    {
        if (target.channel)
            return patcher::graphOf (engine.getChannelInstrument (target.index).get());
        return patcher::graphOf (engine.getEffect (target.index, target.slot).get());
    }

    void addNode (PatchNode node, juce::Point<float> at)
    {
        node.x = at.x;
        node.y = at.y;
        const auto channel = target.channel;
        edit ([node, channel] (PatcherDoc& d) {
            const auto n = patcher::addNode (d, node);
            // a new instrument gets the notes (FL cables them on the generator Patcher's first instrument too)
            if (node.generator && channel)
            {
                PatchWire notes;
                notes.kind = PatchWire::Kind::midi;
                notes.to = n;
                patcher::connect (d, notes);
                if (std::none_of (d.wires.begin(), d.wires.end(), [] (const PatchWire& w) { return w.kind == PatchWire::Kind::audio && w.to == patchToHost; }))
                {
                    PatchWire out;
                    out.from = n;
                    patcher::connect (d, out);
                }
            }
        });
    }

    void showAddMenu (juce::Point<float> at)
    {
        juce::PopupMenu effects, instruments, pluginEffects, pluginInstruments;
        for (const auto kind : addableEffects())
            if (kind != EffectKind::patcher)
                effects.addItem (addableEffectName (kind), addableEffectAvailable (kind), false, [this, kind, at] {
                    const auto fx = newAddableEffect (kind, engine.getSong().bpm);
                    if (! fx)
                        return;
                    PatchNode n;
                    n.effect = *fx;
                    addNode (n, at);
                });
        for (const auto kind : { InstrumentKind::synth, InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat, InstrumentKind::clap })
            instruments.addItem (kind == InstrumentKind::synth ? tr ("Synth") : kind == InstrumentKind::kick ? tr ("Kick")
                                 : kind == InstrumentKind::snare ? tr ("Snare") : kind == InstrumentKind::hat ? tr ("Hat") : tr ("Clap"),
                                 [this, kind, at] {
                                     PatchNode n;
                                     n.generator = true;
                                     n.channel.instrument = kind;
                                     n.channel.name = effectNameOf (kind);
                                     addNode (n, at);
                                 });
        {
            // FL's own instrument engines (round 142)
            juce::PopupMenu engines;
            addFlEngineItems (engines, [this, at] (ChannelDef c) {
                PatchNode n;
                n.generator = true;
                n.channel = c;
                addNode (n, at);
            });
            instruments.addSeparator();
            instruments.addSubMenu (tr ("FL engines"), engines);
        }
        addPluginItems (pluginEffects, engine, false, [this, at] (PluginRef ref) {
            PatchNode n;
            n.effect.kind = EffectKind::plugin;
            n.effect.plugin = ref;
            addNode (n, at);
        });
        addPluginItems (pluginInstruments, engine, true, [this, at] (PluginRef ref) {
            PatchNode n;
            n.generator = true;
            n.channel.instrument = InstrumentKind::plugin;
            n.channel.name = ref.name;
            n.channel.plugin = ref;
            addNode (n, at);
        });
        juce::PopupMenu menu;
        menu.addSubMenu (tr ("Add effect"), effects);
        menu.addSubMenu (tr ("Add plugin effect (VST/VST3/CLAP)"), pluginEffects);
        menu.addSubMenu (tr ("Add instrument"), instruments);
        menu.addSubMenu (tr ("Add plugin instrument (VST/VST3/CLAP)"), pluginInstruments);
        menu.addSeparator();
        menu.addItem (tr ("Fit to window"), [this] {
            fit();
            repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    static std::string effectNameOf (InstrumentKind kind)
    {
        switch (kind)
        {
            case InstrumentKind::kick: return "Kick";
            case InstrumentKind::snare: return "Snare";
            case InstrumentKind::hat: return "Hat";
            case InstrumentKind::clap: return "Clap";
            default: return "Synth";
        }
    }

    void showNodeMenu (int n)
    {
        selectedNode = n;
        repaint();
        if (n < 0)
            return;
        const auto* d = currentDoc();
        const auto& node = d->nodes[(std::size_t) n];
        juce::PopupMenu menu;
        menu.addItem (tr ("Open editor"), [this, n] { openNodeEditor (n); });
        menu.addItem (tr ("Rename..."), [this, n] { renameNode (n); });
        if (! node.generator)
            menu.addItem (tr ("Bypass"), true, ! node.effect.enabled, [this, n] {
                edit ([n] (PatcherDoc& doc) {
                    if (n < (int) doc.nodes.size())
                        doc.nodes[(std::size_t) n].effect.enabled = ! doc.nodes[(std::size_t) n].effect.enabled;
                });
            });
        menu.addSeparator();
        menu.addItem (tr ("Delete"), [this, n] {
            selectedNode = noNode;
            edit ([n] (PatcherDoc& doc) { patcher::removeNode (doc, n); });
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showWireMenu (int w)
    {
        selectedWire = w;
        repaint();
        const auto* d = currentDoc();
        const auto wire = d->wires[(std::size_t) w];
        juce::PopupMenu menu;
        if (wire.kind == PatchWire::Kind::audio)
        {
            juce::PopupMenu gains;
            for (const auto g : { 0.25f, 0.5f, 0.707f, 1.0f, 1.41f, 2.0f })
                gains.addItem (juce::String (g, 2) + "x", true, std::abs (wire.gain - g) < 1e-3f, [this, w, g] {
                    edit ([w, g] (PatcherDoc& doc) {
                        if (w < (int) doc.wires.size())
                            doc.wires[(std::size_t) w].gain = g;
                    });
                });
            gains.addItem (tr ("Other..."), [this, w, wire] { askGain (w, wire.gain); });
            menu.addSubMenu (tr ("Cable gain"), gains);
        }
        menu.addItem (tr ("Enabled"), true, wire.enabled, [this, w] {
            edit ([w] (PatcherDoc& doc) {
                if (w < (int) doc.wires.size())
                    doc.wires[(std::size_t) w].enabled = ! doc.wires[(std::size_t) w].enabled;
            });
        });
        menu.addItem (tr ("Delete"), [this, w] {
            selectedWire = -1;
            edit ([w] (PatcherDoc& doc) {
                if (w < (int) doc.wires.size())
                    doc.wires.erase (doc.wires.begin() + w);
            });
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void askGain (int w, float gain)
    {
        auto* box = new juce::AlertWindow (tr ("Cable gain"), tr ("Linear gain of this cable (1 = unchanged, 0.5 = -6 dB):"), juce::MessageBoxIconType::NoIcon);
        box->addTextEditor ("gain", juce::String (gain, 3));
        box->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        box->enterModalState (true, juce::ModalCallbackFunction::create ([this, safe = juce::Component::SafePointer<PatcherEditor> (this), box, w] (int r) {
                                  if (r == 1 && safe != nullptr)
                                  {
                                      const auto g = std::clamp (box->getTextEditorContents ("gain").getFloatValue(), 0.0f, 16.0f);
                                      edit ([w, g] (PatcherDoc& doc) {
                                          if (w < (int) doc.wires.size())
                                              doc.wires[(std::size_t) w].gain = g;
                                      });
                                  }
                              }),
                              true);
    }

    void renameNode (int n)
    {
        const auto* d = currentDoc();
        auto* box = new juce::AlertWindow (tr ("Rename"), {}, juce::MessageBoxIconType::NoIcon);
        box->addTextEditor ("name", juce::String::fromUTF8 (patcher::nodeName (d->nodes[(std::size_t) n]).c_str()));
        box->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        box->enterModalState (true, juce::ModalCallbackFunction::create ([this, safe = juce::Component::SafePointer<PatcherEditor> (this), box, n] (int r) {
                                  if (r == 1 && safe != nullptr)
                                  {
                                      const auto name = box->getTextEditorContents ("name").toStdString();
                                      edit ([n, name] (PatcherDoc& doc) {
                                          if (n < (int) doc.nodes.size())
                                              doc.nodes[(std::size_t) n].label = name;
                                      });
                                  }
                              }),
                              true);
    }

    void openNodeEditor (int n)
    {
        const auto* d = currentDoc();
        auto* graph = runningGraph();
        if (d == nullptr || n < 0 || n >= (int) d->nodes.size())
            return;
        const auto& node = d->nodes[(std::size_t) n];
        const auto title = juce::String::fromUTF8 (patcher::nodeName (node).c_str());
        if (graph != nullptr)
        {
            auto effect = graph->effectAt (n);
            auto instrument = graph->instrumentAt (n);
            if (auto* fx = dynamic_cast<PluginEffect*> (effect.get()); fx != nullptr && ! fx->hasCrashed())
            {
                PluginWindows::open (effect, fx->getInstance(), title);
                return;
            }
            if (auto* in = dynamic_cast<PluginInstrument*> (instrument.get()); in != nullptr && ! in->hasCrashed())
            {
                PluginWindows::open (instrument, in->getInstance(), title);
                return;
            }
        }
        if (! node.generator && node.effect.kind != EffectKind::plugin && node.effect.kind != EffectKind::patcher)
        {
            openToolWindow (std::make_unique<NodeParams> (engine, target, n), title + " - " + tr ("Patcher"), target.key() + "-node-" + juce::String (n));
            return;
        }
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Patcher"),
                                                node.effect.kind == EffectKind::plugin || node.channel.instrument == InstrumentKind::plugin
                                                    ? tr ("This plugin could not be loaded (it is not installed or has not been scanned).")
                                                    : tr ("This node has no editor of its own here."));
    }

    void fit()
    {
        const auto* d = currentDoc();
        if (d == nullptr)
            return;
        auto box = nodeBounds (*d, patchFromHost).getUnion (nodeBounds (*d, patchToHost));
        for (int n = 0; n < (int) d->nodes.size(); ++n)
            box = box.getUnion (nodeBounds (*d, n));
        box = box.expanded (40.0f);
        zoom = std::clamp (std::min ((float) getWidth() / box.getWidth(), (float) (getHeight() - 24) / box.getHeight()), 0.25f, 1.25f);
        offset = box.getCentre() - juce::Point<float> ((float) getWidth(), (float) getHeight() - 24.0f) / (2.0f * zoom);
    }

    // The parameters of a built-in effect node, as sliders
    class NodeParams final : public juce::Component, private juce::Timer
    {
    public:
        NodeParams (AudioEngine& e, Target t, int n) : engine (e), target (t), node (n)
        {
            if (const auto* slot = slotOf (engine.getSong()))
            {
                const auto& info = effectInfo (slot->kind);
                for (int p = 0; p < (int) info.params.size(); ++p)
                {
                    auto row = std::make_unique<Row>();
                    row->label.setText (juce::String::fromUTF8 (info.params[(std::size_t) p].name), juce::dontSendNotification);
                    row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
                    row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 18);
                    row->slider.setRange (0.0, 1.0);
                    row->slider.setValue (slot->params[(std::size_t) p], juce::dontSendNotification);
                    row->slider.textFromValueFunction = [kind = slot->kind, p, &song = engine.getSong()] (double v) {
                        return juce::String::fromUTF8 (formatEffectParam (kind, p, (float) v, song.bpm).c_str());
                    };
                    row->slider.updateText();
                    row->slider.onValueChange = [this, p, s = &row->slider] { set (p, (float) s->getValue(), false); };
                    row->slider.onDragEnd = [this, p, s = &row->slider] { set (p, (float) s->getValue(), true); };
                    addAndMakeVisible (row->label);
                    addAndMakeVisible (row->slider);
                    rows.push_back (std::move (row));
                }
            }
            setSize (420, std::max (60, (int) rows.size() * 26 + 12));
            startTimerHz (8);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6);
            for (auto& row : rows)
            {
                auto line = r.removeFromTop (26);
                row->label.setBounds (line.removeFromLeft (140));
                row->slider.setBounds (line);
            }
        }

        void paint (juce::Graphics& g) override { g.fillAll (col::bg2); }

    private:
        struct Row
        {
            juce::Label label;
            juce::Slider slider;
        };
        AudioEngine& engine;
        Target target;
        int node;
        std::vector<std::unique_ptr<Row>> rows;

        const EffectSlot* slotOf (const Song& song) const
        {
            const auto* d = target.doc (song);
            return d != nullptr && node < (int) d->nodes.size() && ! d->nodes[(std::size_t) node].generator ? &d->nodes[(std::size_t) node].effect : nullptr;
        }

        void set (int p, float v, bool undoable)
        {
            engine.editSong (
                [t = target, n = node, p, v] (Song& song) {
                    if (auto* d = t.doc (song); d != nullptr && n < (int) d->nodes.size())
                        d->nodes[(std::size_t) n].effect.params[(std::size_t) p] = v;
                },
                undoable);
        }

        void timerCallback() override // (control cables and undo move the values too)
        {
            if (const auto* slot = slotOf (engine.getSong()))
                for (std::size_t p = 0; p < rows.size(); ++p)
                    if (! rows[p]->slider.isMouseButtonDown())
                        rows[p]->slider.setValue (slot->params[p], juce::dontSendNotification);
        }
    };
};

void open (AudioEngine& engine, Target target, const juce::String& title)
{
    auto* window = openToolWindow (std::make_unique<PatcherEditor> (engine, target), title, target.key());
    if (window != nullptr)
    {
        window->setResizable (true, false);
        window->setResizeLimits (480, 320, 2400, 1600);
    }
}
} // namespace

namespace PatcherWindows
{
void openSlot (AudioEngine& engine, int track, int slot)
{
    const auto& song = engine.getSong();
    const auto name = track >= 0 && track < (int) song.mixer.size() ? juce::String::fromUTF8 (song.mixer[(std::size_t) track].name.c_str()) : juce::String();
    open (engine, { false, track, slot }, tr ("Patcher") + (name.isNotEmpty() ? " - " + name : juce::String()));
}

void openChannel (AudioEngine& engine, int channel)
{
    const auto& song = engine.getSong();
    const auto name = channel >= 0 && channel < (int) song.channels.size() ? juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str())
                                                                            : juce::String();
    open (engine, { true, channel, 0 }, tr ("Patcher") + (name.isNotEmpty() ? " - " + name : juce::String()));
}

bool isChannelOpen (int channel) { return findToolWindow (Target { true, channel, 0 }.key()) != nullptr; }

void closeChannel (int channel)
{
    if (auto* w = findToolWindow (Target { true, channel, 0 }.key()))
        delete w;
}
} // namespace PatcherWindows
} // namespace lynx
