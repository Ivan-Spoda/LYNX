#include "ui/FacetEqEditor.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <map>

#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/RackRenderer.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
using namespace facet;

constexpr double minHz = 10.0, maxHz = 30000.0;      // the graph's frequency axis (the bands' whole range)
constexpr double specTop = 0.0, specBottom = -96.0;  // analyser scale (dBFS, tilted 4.5 dB/oct around 1 kHz)
constexpr int fftOrder = 13, fftSize = 1 << fftOrder;

juce::Colour bandColour (int band)
{
    // one hue per band at the same lightness; the first is LYNX's red
    static const juce::Colour colours[] = { juce::Colour (0xffef5b5f), juce::Colour (0xfff2a46b), juce::Colour (0xffe8cf6a),
                                            juce::Colour (0xff86d993), juce::Colour (0xff62cfd0), juce::Colour (0xff7aa9f5),
                                            juce::Colour (0xffb59af2), juce::Colour (0xffef7fc0) };
    return colours[band & 7];
}

// display names of facet::Shape (core names, translated here)
const char* const shapeNames[] = { TR_NOOP ("Off"), TR_NOOP ("Bell"), TR_NOOP ("Low shelf"), TR_NOOP ("High shelf"), TR_NOOP ("Low cut"),
                                   TR_NOOP ("High cut"), TR_NOOP ("Notch"), TR_NOOP ("Band pass"), TR_NOOP ("Tilt shelf") };
const char* const phaseNames[] = { TR_NOOP ("Zero latency"), TR_NOOP ("Natural phase"), TR_NOOP ("Linear phase") };
const char* const analyzerNames[] = { TR_NOOP ("Analyzer off"), TR_NOOP ("Analyzer: pre"), TR_NOOP ("Analyzer: post"), TR_NOOP ("Analyzer: pre + post") };

juce::String noteName (double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto n = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const auto k = (int) std::lround (n);
    const auto cents = (int) std::lround ((n - k) * 100.0);
    return juce::String (names[((k % 12) + 12) % 12]) + juce::String (k / 12 - 1) + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents));
}

juce::String hzText (double hz)
{
    return hz < 1000.0 ? juce::String (hz, hz < 100.0 ? 1 : 0) + " Hz" : juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1) + " kHz";
}

class FacetEqEditor final : public juce::Component,
                            public ui::HintSource,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    FacetEqEditor (AudioEngine& e, int t, int s, bool demoSpectrum)
        : engine (e),
          track (t),
          slot (s),
          demo (demoSpectrum),
          fft (fftOrder)
    {
        fftData.assign ((std::size_t) fftSize * 2, 0.0f);
        window.resize ((std::size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(std::size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
        if (const auto* fx = current())
            for (int b = 0; b < bands; ++b)
                if (shape (fx->params[(std::size_t) param (b, 0)]) != Shape::off) { selected = b; break; }
        engine.addChangeListener (this);
        setWantsKeyboardFocus (true);
        setSize (960, 560);
        if (demo) updateSpectrum();
        claimTap();
        startTimerHz (30);
    }

    ~FacetEqEditor() override
    {
        stopTimer();
        engine.removeChangeListener (this);
        if (auto* times = engine.getProcessTimes(); times != nullptr && times->analyzer.key.load() == tapKey())
            times->analyzer.key.store (-1);
        // solo is a listening aid: it never stays on once the editor is gone
        if (const auto* fx = current(); fx != nullptr && soloBand (fx->params[(std::size_t) soloParam]) >= 0)
            engine.editSong ([tk = track, sl = slot] (Song& song) { song.mixer[(std::size_t) tk].effects[(std::size_t) sl].params[(std::size_t) soloParam] = 0.0f; }, false);
        ui::hideValueTip();
    }

    const EffectSlot* current() const
    {
        const auto& mixer = engine.getSong().mixer;
        if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
            return nullptr;
        const auto& fx = mixer[(std::size_t) track].effects[(std::size_t) slot];
        return fx.kind == EffectKind::facetEq ? &fx : nullptr;
    }

    // --- geometry ------------------------------------------------------------------------------------

    juce::Rectangle<float> headerArea() const { return { 0.0f, 0.0f, (float) getWidth(), 38.0f }; }
    juce::Rectangle<float> panelArea() const { return { 0.0f, (float) getHeight() - 126.0f, (float) getWidth(), 126.0f }; }
    juce::Rectangle<float> graphArea() const { return { 10.0f, 46.0f, (float) getWidth() - 20.0f, panelArea().getY() - 54.0f }; }
    juce::Rectangle<float> plotArea() const { return graphArea().reduced (1.0f).withTrimmedRight (34.0f).withTrimmedBottom (16.0f); }

    float xForHz (double f) const
    {
        const auto a = plotArea();
        return a.getX() + a.getWidth() * (float) (std::log (juce::jlimit (minHz, maxHz, f) / minHz) / std::log (maxHz / minHz));
    }
    double hzForX (float x) const
    {
        const auto a = plotArea();
        return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth())));
    }
    float yForDb (double db) const
    {
        const auto a = plotArea();
        return a.getCentreY() - a.getHeight() * 0.5f * (float) (juce::jlimit (-rangeDb * 1.1, rangeDb * 1.1, db) / rangeDb) * 0.92f;
    }
    double dbForY (float y) const
    {
        const auto a = plotArea();
        return (double) ((a.getCentreY() - y) / (a.getHeight() * 0.5f * 0.92f)) * rangeDb;
    }
    float yForSpectrum (double db) const
    {
        const auto a = plotArea();
        return a.getY() + a.getHeight() * (float) ((specTop - juce::jlimit (specBottom, specTop, db)) / (specTop - specBottom));
    }

    float param01 (int p) const
    {
        const auto* fx = current();
        return fx != nullptr && p >= 0 && p < maxEffectParams ? fx->params[(std::size_t) p] : 0.0f;
    }

    Shape bandShape (int b) const { return shape (param01 (param (b, 0))); }
    bool bandOn (int b) const { return b >= 0 && b < bands && bandShape (b) != Shape::off; }

    // the live dynamic gain of a band (dB), from the audio thread
    double liveDynamic (int b) const
    {
        const auto* times = engine.getProcessTimes();
        const auto key = (std::size_t) (track * maxEffectSlots + slot) * ProcessTimes::meterOutputs + (std::size_t) b;
        if (times == nullptr || key >= times->effectMeters.size()) return 0.0;
        return (double) times->effectMeters[key].load (std::memory_order_relaxed) * 60.0;
    }

    juce::Point<float> nodePosition (int b) const
    {
        const auto f = hz (param01 (param (b, 1)));
        const auto db = hasGain (bandShape (b)) ? gainDb (param01 (param (b, 2))) * gainScale (param01 (scaleParam)) : 0.0;
        return { xForHz (f), yForDb (db) };
    }

    // the range handle of a dynamic band: at static gain + range
    juce::Point<float> rangeHandle (int b) const
    {
        const auto scale = gainScale (param01 (scaleParam));
        const auto db = (gainDb (param01 (param (b, 2))) + rangeDb_ (b)) * scale;
        return { nodePosition (b).x, yForDb (db) };
    }

    double rangeDb_ (int b) const { return facet::rangeDb (param01 (rangeParam0 + b)); }

    int nodeAt (juce::Point<float> p) const
    {
        int best = -1;
        float bestDistance = 11.0f;
        for (int b = 0; b < bands; ++b)
            if (bandOn (b))
                if (const auto d = nodePosition (b).getDistanceFrom (p); d < bestDistance)
                {
                    best = b;
                    bestDistance = d;
                }
        return best;
    }

    int rangeHandleAt (juce::Point<float> p) const
    {
        for (int b = 0; b < bands; ++b)
            if (bandOn (b) && hasGain (bandShape (b)) && b == selected && rangeHandle (b).getDistanceFrom (p) < 8.0f)
                return b;
        return -1;
    }

    // --- controls of the bottom strip and the header -------------------------------------------------

    enum class Ctl { none, chip, shapeMenu, freq, gain, q, slopeMenu, range, threshold, solo, dynamic, remove, attack, release,
                     phase, analyzer, detector, output, scale, scaleDb };
    struct Control { Ctl type; juce::Rectangle<float> r; int param = -1; int index = 0; };

    std::vector<Control> controls() const
    {
        std::vector<Control> out;
        const auto h = headerArea();
        // header: phase mode (segmented), analyser, detector, output and gain scale on the right
        auto x = 150.0f;
        for (int m = 0; m < phaseCount; ++m)
        {
            out.push_back ({ Ctl::phase, { x, h.getCentreY() - 12.0f, 96.0f, 24.0f }, phaseParam, m });
            x += 97.0f;
        }
        x += 8.0f;
        out.push_back ({ Ctl::analyzer, { x, h.getCentreY() - 12.0f, 150.0f, 24.0f }, analyzerParam });
        x += 156.0f;
        out.push_back ({ Ctl::detector, { x, h.getCentreY() - 12.0f, 130.0f, 24.0f }, detectorParam });
        auto right = h.getRight() - 10.0f;
        out.push_back ({ Ctl::output, { right - 92.0f, h.getCentreY() - 12.0f, 92.0f, 24.0f }, outputParam });
        right -= 98.0f;
        out.push_back ({ Ctl::scale, { right - 84.0f, h.getCentreY() - 12.0f, 84.0f, 24.0f }, scaleParam });
        // the graph's dB scale (click: ±6 / 12 / 24 / 30)
        const auto g = graphArea();
        out.push_back ({ Ctl::scaleDb, { g.getRight() - 34.0f, g.getY(), 34.0f, g.getHeight() - 16.0f } });

        // bottom strip
        const auto p = panelArea().reduced (12.0f, 10.0f);
        for (int b = 0; b < bands; ++b)
            out.push_back ({ Ctl::chip, { p.getX() + (float) (b % 4) * 30.0f, p.getY() + 4.0f + (float) (b / 4) * 30.0f, 26.0f, 26.0f }, -1, b });
        if (selected >= 0 && bandOn (selected))
        {
            const auto b = selected;
            auto cx = p.getX() + 134.0f;
            out.push_back ({ Ctl::shapeMenu, { cx, p.getY() + 4.0f, 104.0f, 24.0f }, param (b, 0) });
            const auto sh = bandShape (b);
            if (sh == Shape::lowCut || sh == Shape::highCut || sh == Shape::lowShelf || sh == Shape::highShelf || sh == Shape::tiltShelf)
                out.push_back ({ Ctl::slopeMenu, { cx, p.getY() + 34.0f, 104.0f, 24.0f }, param (b, 4) });
            out.push_back ({ Ctl::remove, { cx, p.getY() + 64.0f, 50.0f, 24.0f } });
            out.push_back ({ Ctl::solo, { cx + 54.0f, p.getY() + 64.0f, 50.0f, 24.0f }, soloParam });
            cx += 118.0f;
            const auto knob = [&] (Ctl type, int prm) {
                out.push_back ({ type, { cx, p.getY(), 66.0f, p.getHeight() }, prm });
                cx += 70.0f;
            };
            knob (Ctl::freq, param (b, 1));
            if (hasGain (sh)) knob (Ctl::gain, param (b, 2));
            knob (Ctl::q, param (b, 3));
            if (hasGain (sh))
            {
                cx += 14.0f;
                out.push_back ({ Ctl::dynamic, { cx, p.getY() + 4.0f, 84.0f, 24.0f }, rangeParam0 + b });
                cx += 92.0f;
                if (isDynamic (current()->params, b))
                {
                    knob (Ctl::range, rangeParam0 + b);
                    knob (Ctl::threshold, thresholdParam0 + b);
                }
            }
        }
        // global dynamics timing (right)
        auto rx = p.getRight() - 140.0f;
        out.push_back ({ Ctl::attack, { rx, p.getY(), 66.0f, p.getHeight() }, attackParam });
        out.push_back ({ Ctl::release, { rx + 70.0f, p.getY(), 66.0f, p.getHeight() }, releaseParam });
        return out;
    }

    const Control* controlAt (juce::Point<float> p, std::vector<Control>& all) const
    {
        all = controls();
        for (const auto& c : all)
            if (c.r.contains (p)) return &c;
        return nullptr;
    }

    juce::Rectangle<float> knobCircle (const Control& c) const
    {
        const auto d = juce::jmin (c.r.getWidth() - 14.0f, c.r.getHeight() - 44.0f);
        return { c.r.getCentreX() - d / 2.0f, c.r.getY() + 18.0f, d, d };
    }

    juce::String trackName() const
    {
        const auto& mixer = engine.getSong().mixer;
        if (track == 0) return tr ("Master");
        const auto name = track < (int) mixer.size() ? juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str()) : juce::String();
        return name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) });
    }

    ui::ValueSpec specFor (int prm) const
    {
        ui::ValueSpec spec;
        const auto& in = info();
        if (prm < 0 || prm >= (int) in.params.size()) return spec;
        spec.name = trackName() + " - Facet EQ - " + utf8 (in.params[(std::size_t) prm].name);
        spec.defaultValue = in.params[(std::size_t) prm].defaultValue;
        spec.pixelsForRange = 200.0;
        spec.toText = [prm] (double v) { return utf8 (paramText (prm, (float) v).c_str()); };
        spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
        spec.target = AutomationTarget { AutomationTargetKind::effectParam, track, slot, prm };
        return spec;
    }

    juce::String valueText (int prm) const { return utf8 (paramText (prm, param01 (prm)).c_str()); }

    // --- editing -------------------------------------------------------------------------------------

    void set (std::vector<std::pair<int, float>> changes, bool undoable = true)
    {
        if (current() == nullptr) return;
        engine.editSong ([tk = track, sl = slot, changes] (Song& song) {
            auto& params = song.mixer[(std::size_t) tk].effects[(std::size_t) sl].params;
            for (const auto& [p, v] : changes)
                if (p >= 0 && p < maxEffectParams) params[(std::size_t) p] = juce::jlimit (0.0f, 1.0f, v);
        }, undoable);
    }

    void addBand (juce::Point<float> at)
    {
        for (int b = 0; b < bands; ++b)
            if (! bandOn (b))
            {
                const auto f = hzForX (at.x);
                // the ends of the spectrum get cuts, as a quick start; anything else is a bell at the clicked gain
                const auto sh = f < 25.0 ? Shape::lowCut : f > 18000.0 ? Shape::highCut : Shape::bell;
                const auto scale = juce::jmax (0.01, gainScale (param01 (scaleParam)));
                set ({ { param (b, 0), shapeValue (sh) }, { param (b, 1), hzValue (f) }, { param (b, 2), sh == Shape::bell ? gainValue (dbForY (at.y) / scale) : 0.5f },
                       { param (b, 3), qValue (1.0) }, { param (b, 4), slopeValue (sh == Shape::bell ? 12 : 24) }, { rangeParam0 + b, 0.5f } });
                selected = b;
                return;
            }
    }

    void removeBand (int b)
    {
        std::vector<std::pair<int, float>> c { { param (b, 0), 0.0f }, { rangeParam0 + b, 0.5f } };
        if (soloBand (param01 (soloParam)) == b) c.push_back ({ soloParam, 0.0f });
        set (c);
        if (selected == b)
        {
            selected = -1;
            for (int k = 0; k < bands; ++k)
                if (k != b && bandOn (k)) { selected = k; break; }
        }
    }

    void toggleSolo (int b)
    {
        set ({ { soloParam, soloBand (param01 (soloParam)) == b ? 0.0f : soloValue (b) } }, false);
    }

    void showBandMenu (int b)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Band {0}", { juce::String (b + 1) }));
        const auto sh = bandShape (b);
        for (int k = 1; k < shapeCount; ++k)
            menu.addItem (tr (shapeNames[k]), true, (int) sh == k, [this, b, k] { set ({ { param (b, 0), shapeValue ((Shape) k) } }); });
        if (sh == Shape::lowCut || sh == Shape::highCut || sh == Shape::lowShelf || sh == Shape::highShelf || sh == Shape::tiltShelf)
        {
            juce::PopupMenu slopes;
            for (const auto d : facet::slopes)
                slopes.addItem (juce::String (d) + " dB/oct", true, slopeDb (param01 (param (b, 4))) == d, [this, b, d] { set ({ { param (b, 4), slopeValue (d) } }); });
            menu.addSubMenu (tr ("Slope"), slopes);
        }
        if (hasGain (sh))
        {
            juce::PopupMenu dyn;
            for (const auto r : { 0.0, -3.0, -6.0, -12.0, 3.0, 6.0 })
                dyn.addItem (r == 0.0 ? tr ("Off") : juce::String (r > 0 ? "+" : "") + juce::String ((int) r) + " dB", true, std::abs (rangeDb_ (b) - r) < 0.05,
                             [this, b, r] { set ({ { rangeParam0 + b, rangeValue (r) } }); });
            menu.addSubMenu (tr ("Dynamic range"), dyn);
        }
        menu.addSeparator();
        menu.addItem (tr ("Solo band"), true, soloBand (param01 (soloParam)) == b, [this, b] { toggleSolo (b); });
        menu.addItem (tr ("Reset gain"), hasGain (sh), false, [this, b] { set ({ { param (b, 2), 0.5f } }); });
        menu.addItem (tr ("Remove band"), [this, b] { removeBand (b); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showDetectorMenu()
    {
        juce::PopupMenu menu;
        const auto* fx = current();
        if (fx == nullptr) return;
        const auto side = sidechainDetector (param01 (detectorParam));
        menu.addItem (tr ("Detect the EQ's input"), true, ! side, [this] { set ({ { detectorParam, 0.0f } }); });
        menu.addItem (tr ("Detect the sidechain"), true, side, [this] { set ({ { detectorParam, 1.0f } }); });
        juce::PopupMenu from;
        const auto assign = [this] (int source) {
            engine.editSong ([tk = track, sl = slot, source] (Song& s) {
                auto& target = s.mixer[(std::size_t) tk].effects[(std::size_t) sl];
                target.sidechain = source;
                if (source >= 0) target.params[(std::size_t) detectorParam] = 1.0f;
            });
        };
        from.addItem (tr ("None"), true, fx->sidechain < 0, [assign] { assign (-1); });
        const auto& song = engine.getSong();
        for (int t = 1; t < (int) song.mixer.size(); ++t)
            if (t != track)
                from.addItem (juce::String (t) + " " + juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str()), true, fx->sidechain == t, [assign, t] { assign (t); });
        menu.addSubMenu (tr ("Sidechain from"), from);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showAnalyzerMenu()
    {
        juce::PopupMenu menu;
        const auto a = (int) analyzer (param01 (analyzerParam));
        for (int k = 0; k < 4; ++k)
            menu.addItem (tr (analyzerNames[k]), true, a == k, [this, k] { set ({ { analyzerParam, analyzerValue ((Analyzer) k) } }, false); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showShapeMenu (int b)
    {
        juce::PopupMenu menu;
        const auto sh = bandShape (b);
        for (int k = 1; k < shapeCount; ++k)
            menu.addItem (tr (shapeNames[k]), true, (int) sh == k, [this, b, k] { set ({ { param (b, 0), shapeValue ((Shape) k) } }); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showSlopeMenu (int b)
    {
        juce::PopupMenu menu;
        for (const auto d : facet::slopes)
            menu.addItem (juce::String (d) + " dB/oct", true, slopeDb (param01 (param (b, 4))) == d, [this, b, d] { set ({ { param (b, 4), slopeValue (d) } }); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    // --- mouse ---------------------------------------------------------------------------------------

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        dragging = Drag::none;
        std::vector<Control> all;
        if (const auto* c = controlAt (p, all))
        {
            const auto type = c->type;
            const auto prm = c->param;
            const auto index = c->index;
            if (e.mods.isPopupMenu())
            {
                if (type == Ctl::analyzer) { showAnalyzerMenu(); return; }
                if (type == Ctl::detector) { showDetectorMenu(); return; }
                if (prm >= 0 && type != Ctl::phase && type != Ctl::solo && type != Ctl::dynamic && type != Ctl::shapeMenu && type != Ctl::slopeMenu)
                    ui::showValueMenu (*this, &engine, specFor (prm), param01 (prm), [this, prm] (double v) { set ({ { prm, (float) v } }); });
                else if (type == Ctl::chip && bandOn (index))
                    showBandMenu (index);
                return;
            }
            switch (type)
            {
                case Ctl::chip:
                    if (bandOn (index)) selected = index;
                    else
                    {
                        // an unused band: switch it on as a bell at its default frequency
                        set ({ { param (index, 0), shapeValue (Shape::bell) }, { param (index, 2), 0.5f } });
                        selected = index;
                    }
                    repaint();
                    return;
                case Ctl::shapeMenu: showShapeMenu (selected); return;
                case Ctl::slopeMenu: showSlopeMenu (selected); return;
                case Ctl::remove: removeBand (selected); return;
                case Ctl::solo: toggleSolo (selected); return;
                case Ctl::dynamic: set ({ { prm, isDynamic (current()->params, selected) ? 0.5f : rangeValue (-6.0) } }); return;
                case Ctl::phase: set ({ { phaseParam, phaseValue ((Phase) index) } }); return;
                case Ctl::analyzer:
                    set ({ { analyzerParam, analyzerValue ((Analyzer) (((int) analyzer (param01 (analyzerParam)) + 1) % 4)) } }, false);
                    return;
                case Ctl::detector: showDetectorMenu(); return;
                case Ctl::scaleDb:
                    rangeDb = rangeDb >= 30.0 ? 6.0 : rangeDb >= 24.0 ? 30.0 : rangeDb >= 12.0 ? 24.0 : 12.0;
                    repaint();
                    return;
                default: break;
            }
            if (prm >= 0)
            {
                if (ui::isResetClick (e))
                {
                    set ({ { prm, (float) specFor (prm).defaultValue } });
                    return;
                }
                dragging = Drag::control;
                dragParam = prm;
                dragRect = c->r;
                valueDrag.begin (e, param01 (prm));
            }
            return;
        }

        if (! plotArea().contains (p)) return;
        if (const auto rb = rangeHandleAt (p); rb >= 0 && ! e.mods.isPopupMenu())
        {
            dragging = Drag::range;
            dragBand = rb;
            return;
        }
        const auto b = nodeAt (p);
        if (b >= 0)
        {
            selected = b;
            if (e.mods.isPopupMenu()) { showBandMenu (b); return; }
            if (ui::isResetClick (e)) { set ({ { param (b, 2), 0.5f } }); return; }
            dragging = Drag::node;
            dragBand = b;
            dragOffset = nodePosition (b) - p;
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == Drag::control && dragParam >= 0)
        {
            const auto spec = specFor (dragParam);
            const auto v = valueDrag.update (e, spec);
            set ({ { dragParam, (float) v } });
            ui::showValueTip (*this, dragRect.toNearestInt(), spec.text (v));
            return;
        }
        if (dragging == Drag::node && bandOn (dragBand))
        {
            auto p = e.position + dragOffset;
            const auto b = dragBand;
            std::vector<std::pair<int, float>> c;
            const auto fine = e.mods.isCtrlDown() || e.mods.isCommandDown();
            if (fine) // fine: a quarter of the mouse movement
                p = nodePosition (b) + (p - nodePosition (b)) * 0.25f;
            if (! e.mods.isAltDown()) c.push_back ({ param (b, 1), hzValue (hzForX (p.x)) });
            if (hasGain (bandShape (b)) && ! e.mods.isShiftDown())
            {
                const auto scale = juce::jmax (0.01, gainScale (param01 (scaleParam)));
                c.push_back ({ param (b, 2), gainValue (juce::jlimit (-30.0, 30.0, dbForY (p.y) / scale)) });
            }
            set (c);
            ui::showValueTip (*this, juce::Rectangle<int> (16, 16).withCentre (nodePosition (b).toInt()), bandSummary (b));
            return;
        }
        if (dragging == Drag::range && bandOn (dragBand))
        {
            const auto b = dragBand;
            const auto scale = juce::jmax (0.01, gainScale (param01 (scaleParam)));
            const auto r = dbForY (e.position.y) / scale - gainDb (param01 (param (b, 2)));
            set ({ { rangeParam0 + b, rangeValue (juce::jlimit (-30.0, 30.0, r)) } });
            ui::showValueTip (*this, juce::Rectangle<int> (16, 16).withCentre (rangeHandle (b).toInt()), tr ("Range") + " " + valueText (rangeParam0 + b));
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = Drag::none;
        valueDrag.end();
        ui::hideValueTip();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! plotArea().contains (e.position)) return;
        if (const auto b = nodeAt (e.position); b >= 0) removeBand (b);
        else addBand (e.position);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        std::vector<Control> all;
        if (const auto* c = controlAt (e.position, all); c != nullptr && c->param >= 0 && c->type != Ctl::phase)
        {
            const auto prm = c->param;
            const auto spec = specFor (prm);
            if (const auto v = ui::wheelValue (param01 (prm), e, wheel, spec))
            {
                set ({ { prm, (float) *v } });
                ui::flashValueTip (*this, c->r.toNearestInt(), spec.text (*v));
            }
            return;
        }
        // over the graph: the Q of the band under the mouse (or the selected one)
        if (! plotArea().contains (e.position)) return;
        auto b = nodeAt (e.position);
        if (b < 0) b = selected;
        if (! bandOn (b) || wheel.deltaY == 0.0f) return;
        const auto step = (e.mods.isCtrlDown() ? 0.04 : 0.15) * (wheel.deltaY > 0 ? 1.0 : -1.0);
        const auto q0 = q (param01 (param (b, 3)));
        set ({ { param (b, 3), qValue (juce::jlimit (0.025, 40.0, q0 * std::exp (step * 2.0))) } });
        ui::flashValueTip (*this, juce::Rectangle<int> (16, 16).withCentre (nodePosition (b).toInt()), "Q " + valueText (param (b, 3)));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        mouse = e.position;
        const auto h = nodeAt (e.position);
        if (h != hover) hover = h;
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        mouse = { -1.0f, -1.0f };
        hover = -1;
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && bandOn (selected))
        {
            removeBand (selected);
            return true;
        }
        if (key.getTextCharacter() == 's' && bandOn (selected))
        {
            toggleSolo (selected);
            return true;
        }
        return false;
    }

    juce::String bandSummary (int b) const
    {
        juce::String t = tr (shapeNames[(int) bandShape (b)]) + "  " + hzText (hz (param01 (param (b, 1))));
        if (hasGain (bandShape (b))) t << "  " << valueText (param (b, 2));
        t << "  Q " << valueText (param (b, 3));
        return t;
    }

    ui::Hint hintAt (juce::Point<int> position) override
    {
        ui::Hint h;
        const auto p = position.toFloat();
        std::vector<Control> all;
        if (const auto* c = controlAt (p, all))
        {
            switch (c->type)
            {
                case Ctl::chip: h.text = tr ("Band {0}: click to select (an unused band is switched on); right-click: band menu", { juce::String (c->index + 1) }); h.hasMenu = true; return h;
                case Ctl::phase: h.text = tr ("Processing: zero latency (minimum phase), natural phase (the analog curve's phase) or linear phase (no phase shift); the latency is reported to PDC"); return h;
                case Ctl::analyzer: h.text = tr ("Spectrum analyzer: click to switch off / pre / post / pre + post"); h.hasMenu = true; return h;
                case Ctl::detector: h.text = tr ("What the dynamic bands listen to: the EQ's input or a sidechain track"); h.hasMenu = true; return h;
                case Ctl::scaleDb: h.text = tr ("Graph scale: click to switch between 12, 24, 30 and 6 dB"); return h;
                case Ctl::remove: h.text = tr ("Remove the band (Delete)"); return h;
                case Ctl::solo: h.text = tr ("Solo: hear only this band's region (S)"); return h;
                case Ctl::dynamic: h.text = tr ("Dynamic EQ: the band's gain moves by up to its range when its region goes over the threshold"); return h;
                case Ctl::shapeMenu: h.text = tr ("Band shape"); h.hasMenu = true; return h;
                case Ctl::slopeMenu: h.text = tr ("Slope"); h.hasMenu = true; return h;
                default: break;
            }
            if (c->param >= 0)
                return ui::valueHint (specFor (c->param), param01 (c->param));
            return h;
        }
        if (plotArea().contains (p))
        {
            if (const auto b = nodeAt (p); b >= 0)
                h.text = tr ("Band {0}", { juce::String (b + 1) }) + ": " + bandSummary (b) + " - "
                         + tr ("drag = frequency and gain (Ctrl fine, Shift frequency only, Alt gain only); wheel = Q; double-click = remove; right-click = menu");
            else if (rangeHandleAt (p) >= 0)
                h.text = tr ("Dynamic range: drag up or down");
            else
                h.text = tr ("Double-click to add a band; mouse wheel = Q of the selected band");
            h.hasMenu = true;
        }
        return h;
    }

    // --- painting ------------------------------------------------------------------------------------

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg1);
        const auto* fx = current();
        if (fx == nullptr)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::body));
            g.drawText (tr ("The effect was removed from this slot."), getLocalBounds(), juce::Justification::centred);
            return;
        }
        auto all = controls();
        paintHeader (g, *fx, all);
        paintGraph (g, *fx);
        paintPanel (g, *fx, all);
    }

    void paintHeader (juce::Graphics& g, const EffectSlot& fx, const std::vector<Control>& all)
    {
        using namespace theme;
        const auto h = headerArea();
        g.setColour (colour::bg3);
        g.fillRect (h);
        g.setColour (colour::border);
        g.drawHorizontalLine ((int) h.getBottom() - 1, 0.0f, h.getRight());
        g.setColour (fx.enabled ? colour::ledOn : colour::ledOff);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ 16.0f, h.getCentreY() }));
        g.setColour (colour::textPrimary);
        g.setFont (font (type::title, true));
        g.drawText ("Facet EQ", juce::Rectangle<float> (28.0f, 0.0f, 90.0f, h.getHeight()), juce::Justification::centredLeft);
        g.setColour (colour::accent);
        g.fillRect (juce::Rectangle<float> (28.0f, h.getBottom() - 4.0f, 70.0f, 2.0f));

        const auto mode = (int) phase (param01 (phaseParam));
        for (const auto& c : all)
        {
            if (c.type == Ctl::phase)
            {
                const auto on = c.index == mode;
                g.setColour (on ? colour::accentMuted : colour::bg4);
                g.fillRoundedRectangle (c.r, radius::md);
                g.setColour (on ? colour::accent : colour::borderStrong);
                g.drawRoundedRectangle (c.r.reduced (0.5f), radius::md, 1.0f);
                g.setColour (on ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (type::small, on));
                g.drawText (tr (phaseNames[c.index]), c.r, juce::Justification::centred);
            }
            else if (c.type == Ctl::analyzer || c.type == Ctl::detector)
            {
                g.setColour (colour::bg4);
                g.fillRoundedRectangle (c.r, radius::md);
                g.setColour (colour::textSecondary);
                g.setFont (font (type::small));
                juce::String text;
                if (c.type == Ctl::analyzer) text = tr (analyzerNames[(int) analyzer (param01 (analyzerParam))]);
                else
                {
                    const auto side = sidechainDetector (param01 (detectorParam));
                    text = side ? tr ("Detector: sidechain") : tr ("Detector: input");
                    if (side && fx.sidechain > 0 && fx.sidechain < (int) engine.getSong().mixer.size())
                        text << " " << juce::String (fx.sidechain);
                }
                g.drawText (text, c.r.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
            }
            else if (c.type == Ctl::output || c.type == Ctl::scale)
            {
                g.setColour (colour::bg0);
                g.fillRoundedRectangle (c.r, radius::md);
                g.setColour (colour::textDisabled);
                g.setFont (font (type::caption));
                g.drawText (c.type == Ctl::output ? tr ("OUT") : tr ("SCALE"), c.r.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
                g.setColour (colour::textPrimary);
                g.setFont (font (type::small, true));
                g.drawText (valueText (c.param), c.r.reduced (6.0f, 0.0f), juce::Justification::centredRight);
            }
        }
        // latency read-out of the mode (PDC compensates it)
        const auto lat = latencyFor ((Phase) mode, displayRate());
        g.setColour (colour::textDisabled);
        g.setFont (font (type::caption));
        if (lat > 0)
            g.drawText (tr ("latency {0} samples", { juce::String (lat) }), juce::Rectangle<float> (150.0f, h.getBottom() - 10.0f, 3.0f * 97.0f, 10.0f),
                        juce::Justification::centredRight);
    }

    double displayRate() const
    {
        const auto* times = engine.getProcessTimes();
        const auto sr = times != nullptr ? times->analyzer.sampleRate.load (std::memory_order_relaxed) : 48000.0;
        return sr > 8000.0 ? sr : 48000.0;
    }

    void paintGraph (juce::Graphics& g, const EffectSlot& fx)
    {
        using namespace theme;
        const auto area = graphArea();
        const auto plot = plotArea();
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (area, radius::lg);
        g.setColour (colour::border);
        g.drawRoundedRectangle (area.reduced (0.5f), radius::lg, 1.0f);

        // grid
        g.setFont (font (type::caption));
        for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const auto x = xForHz (f);
            g.setColour (colour::gridBeat);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (colour::textDisabled);
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f),
                        juce::Justification::centred);
        }
        const auto step = rangeDb <= 6.0 ? 2.0 : rangeDb <= 12.0 ? 3.0 : 6.0;
        for (double db = -rangeDb; db <= rangeDb + 0.01; db += step)
        {
            const auto y = yForDb (db);
            g.setColour (std::abs (db) < 0.01 ? colour::gridBar : colour::gridBeat);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (std::abs (db) < 0.01 ? colour::textSecondary : colour::textDisabled);
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getRight() + 2.0f, y - 6.0f, 30.0f, 12.0f),
                        juce::Justification::centredLeft);
        }

        // analyser (behind the curves)
        const auto mode = analyzer (param01 (analyzerParam));
        if (mode != Analyzer::off && ! specPre.empty())
        {
            auto spectrumPath = [&] (const std::vector<float>& db, bool closed) {
                juce::Path p;
                const auto n = (int) db.size();
                for (int i = 0; i < n; ++i)
                {
                    const auto x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
                    const auto y = yForSpectrum (db[(std::size_t) i]);
                    if (i == 0) p.startNewSubPath (x, closed ? plot.getBottom() : y);
                    if (i == 0 && closed) p.lineTo (x, y);
                    else p.lineTo (x, y);
                }
                if (closed)
                {
                    p.lineTo (plot.getRight(), plot.getBottom());
                    p.closeSubPath();
                }
                return p;
            };
            g.saveState();
            g.reduceClipRegion (plot.toNearestInt());
            if (mode == Analyzer::pre || mode == Analyzer::both)
            {
                g.setColour (colour::bg5.withAlpha (0.55f));
                g.fillPath (spectrumPath (specPre, true));
            }
            if (mode == Analyzer::post || mode == Analyzer::both)
            {
                const auto p = spectrumPath (specPost, true);
                g.setGradientFill (juce::ColourGradient (colour::accent.withAlpha (0.30f), 0.0f, plot.getY(), colour::accent.withAlpha (0.04f), 0.0f, plot.getBottom(), false));
                g.fillPath (p);
                g.setColour (colour::accent.withAlpha (0.55f));
                g.strokePath (spectrumPath (specPost, false), juce::PathStrokeType (1.0f));
            }
            g.restoreState();
        }

        // curves: each band (its own colour, faint), the dynamic span, then the sum
        const auto columns = juce::jmax (64, (int) plot.getWidth() / 2);
        std::vector<double> freqs ((std::size_t) columns + 1), sum ((std::size_t) columns + 1, outputDb (fx.params[(std::size_t) outputParam])), band ((std::size_t) columns + 1),
            span ((std::size_t) columns + 1);
        for (int i = 0; i <= columns; ++i)
            freqs[(std::size_t) i] = hzForX (plot.getX() + plot.getWidth() * (float) i / (float) columns);
        const auto sr = displayRate();
        const auto solo = soloBand (fx.params[(std::size_t) soloParam]);
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        for (int b = 0; b < bands; ++b)
        {
            if (! bandOn (b)) continue;
            const auto dyn = isDynamic (fx.params, b);
            const auto live = dyn ? liveDynamic (b) : 0.0;
            bandCurveDb (fx.params, b, sr, freqs.data(), columns + 1, band.data(), live);
            for (int i = 0; i <= columns; ++i) sum[(std::size_t) i] += band[(std::size_t) i];
            const auto colour = bandColour (b);
            const auto isSel = b == selected || b == hover;
            juce::Path p;
            for (int i = 0; i <= columns; ++i)
            {
                const auto x = plot.getX() + plot.getWidth() * (float) i / (float) columns;
                const auto y = yForDb (band[(std::size_t) i]);
                if (i == 0) p.startNewSubPath (x, y);
                else p.lineTo (x, y);
            }
            if (isSel)
            {
                auto fill = p;
                fill.lineTo (plot.getRight(), yForDb (0.0));
                fill.lineTo (plot.getX(), yForDb (0.0));
                fill.closeSubPath();
                g.setColour (colour.withAlpha (solo == b ? 0.30f : 0.16f));
                g.fillPath (fill);
            }
            if (dyn) // the range the band can move through: static curve ↔ static + range
            {
                std::vector<double> lo ((std::size_t) columns + 1), hi ((std::size_t) columns + 1);
                bandCurveDb (fx.params, b, sr, freqs.data(), columns + 1, lo.data(), 0.0);
                bandCurveDb (fx.params, b, sr, freqs.data(), columns + 1, hi.data(), rangeDb_ (b));
                juce::Path area2;
                for (int i = 0; i <= columns; ++i)
                {
                    const auto x = plot.getX() + plot.getWidth() * (float) i / (float) columns;
                    if (i == 0) area2.startNewSubPath (x, yForDb (lo[0]));
                    else area2.lineTo (x, yForDb (lo[(std::size_t) i]));
                }
                for (int i = columns; i >= 0; --i)
                    area2.lineTo (plot.getX() + plot.getWidth() * (float) i / (float) columns, yForDb (hi[(std::size_t) i]));
                area2.closeSubPath();
                g.setColour (colour.withAlpha (0.12f));
                g.fillPath (area2);
            }
            g.setColour (colour.withAlpha (isSel ? 0.9f : 0.45f));
            g.strokePath (p, juce::PathStrokeType (isSel ? 1.6f : 1.0f));
        }

        // the sum
        juce::Path total;
        for (int i = 0; i <= columns; ++i)
        {
            const auto x = plot.getX() + plot.getWidth() * (float) i / (float) columns;
            const auto y = yForDb (sum[(std::size_t) i]);
            if (i == 0) total.startNewSubPath (x, y);
            else total.lineTo (x, y);
        }
        const auto curveColour = fx.enabled ? colour::textPrimary : colour::textDisabled;
        g.setColour (colour::accent.withAlpha (fx.enabled ? 0.25f : 0.0f));
        g.strokePath (total, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (curveColour);
        g.strokePath (total, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.restoreState();

        // nodes
        for (int b = 0; b < bands; ++b)
        {
            if (! bandOn (b)) continue;
            const auto pos = nodePosition (b);
            const auto colour = bandColour (b);
            const auto isSel = b == selected;
            if (hasGain (bandShape (b)) && isDynamic (fx.params, b))
            {
                // the live position and the range handle
                const auto scale = gainScale (param01 (scaleParam));
                const auto liveY = yForDb ((gainDb (param01 (param (b, 2))) + liveDynamic (b)) * scale);
                const auto rh = rangeHandle (b);
                g.setColour (colour.withAlpha (0.5f));
                g.drawLine (pos.x, pos.y, rh.x, rh.y, 1.0f);
                juce::Path diamond;
                diamond.addPolygon (rh, 4, 5.0f, juce::MathConstants<float>::pi * 0.25f);
                g.setColour (isSel ? colour : colour.withAlpha (0.6f));
                g.fillPath (diamond);
                g.setColour (colour.withAlpha (0.9f));
                g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ pos.x, liveY }), 1.5f);
            }
            const auto r = isSel ? 9.0f : 7.5f;
            if (isSel)
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (pos));
            }
            g.setColour (soloBand (fx.params[(std::size_t) soloParam]) == b ? colour::warning : colour);
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pos));
            g.setColour (colour::bg0);
            g.setFont (font (type::caption, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pos), juce::Justification::centred);
            if (b == hover || isSel)
            {
                g.setColour (colour::textPrimary);
                g.drawEllipse (juce::Rectangle<float> (r * 2.0f + 3.0f, r * 2.0f + 3.0f).withCentre (pos), 1.2f);
            }
        }

        // what is under the mouse: frequency, note, gain of the curve
        if (plot.contains (mouse))
        {
            const auto f = hzForX (mouse.x);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            g.drawText (hzText (f) + "  " + noteName (f) + "   " + juce::String (dbForY (mouse.y), 1) + " dB",
                        juce::Rectangle<float> (plot.getX() + 8.0f, plot.getY() + 6.0f, 260.0f, 14.0f), juce::Justification::centredLeft);
        }
        bool any = false;
        for (int b = 0; b < bands; ++b) any = any || bandOn (b);
        if (! any)
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::body));
            g.drawText (tr ("Double-click to add a band"), plot, juce::Justification::centred);
        }
    }

    void paintPanel (juce::Graphics& g, const EffectSlot& fx, const std::vector<Control>& all)
    {
        using namespace theme;
        const auto panel = panelArea();
        g.setColour (colour::bg2);
        g.fillRect (panel);
        g.setColour (colour::border);
        g.drawHorizontalLine ((int) panel.getY(), 0.0f, panel.getRight());
        const auto solo = soloBand (fx.params[(std::size_t) soloParam]);

        auto drawButton = [&] (juce::Rectangle<float> r, const juce::String& text, bool on, juce::Colour onColour) {
            g.setColour (on ? onColour.withAlpha (0.25f) : colour::bg4);
            g.fillRoundedRectangle (r, radius::md);
            g.setColour (on ? onColour : colour::borderStrong);
            g.drawRoundedRectangle (r.reduced (0.5f), radius::md, 1.0f);
            g.setColour (on ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (type::small, on));
            g.drawText (text, r.reduced (6.0f, 0.0f), juce::Justification::centred, true);
        };

        for (const auto& c : all)
        {
            switch (c.type)
            {
                case Ctl::chip:
                {
                    const auto on = bandOn (c.index);
                    const auto colour = bandColour (c.index);
                    g.setColour (on ? colour.withAlpha (c.index == selected ? 1.0f : 0.55f) : colour::bg4);
                    g.fillRoundedRectangle (c.r, radius::md);
                    if (c.index == selected)
                    {
                        g.setColour (colour::textPrimary);
                        g.drawRoundedRectangle (c.r.expanded (1.5f), radius::md, 1.5f);
                    }
                    g.setColour (on ? colour::bg0 : colour::textDisabled);
                    g.setFont (font (type::small, true));
                    g.drawText (juce::String (c.index + 1), c.r, juce::Justification::centred);
                    if (solo == c.index)
                    {
                        g.setColour (colour::warning);
                        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c.r.getTopRight().translated (-4.0f, 4.0f)));
                    }
                    break;
                }
                case Ctl::shapeMenu:
                    drawButton (c.r, tr (shapeNames[(int) bandShape (selected)]) + juce::String::fromUTF8 (" \xe2\x96\xbe"), false, colour::accent);
                    break;
                case Ctl::slopeMenu:
                    drawButton (c.r, juce::String (slopeDb (param01 (c.param))) + " dB/oct" + juce::String::fromUTF8 (" \xe2\x96\xbe"), false, colour::accent);
                    break;
                case Ctl::remove: drawButton (c.r, tr ("Remove"), false, colour::accent); break;
                case Ctl::solo: drawButton (c.r, tr ("Solo"), solo == selected, colour::warning); break;
                case Ctl::dynamic: drawButton (c.r, tr ("Dynamic"), isDynamic (fx.params, selected), colour::accent); break;
                case Ctl::freq: case Ctl::gain: case Ctl::q: case Ctl::range: case Ctl::threshold: case Ctl::attack: case Ctl::release:
                {
                    static const std::map<Ctl, const char*> captions { { Ctl::freq, TR_NOOP ("FREQ") }, { Ctl::gain, TR_NOOP ("GAIN") }, { Ctl::q, "Q" },
                                                                       { Ctl::range, TR_NOOP ("RANGE") }, { Ctl::threshold, TR_NOOP ("THRESHOLD") },
                                                                       { Ctl::attack, TR_NOOP ("ATTACK") }, { Ctl::release, TR_NOOP ("RELEASE") } };
                    const auto bipolar = c.type == Ctl::gain || c.type == Ctl::range;
                    const auto active = dragging == Drag::control && dragParam == c.param;
                    const auto arc = c.type == Ctl::attack || c.type == Ctl::release ? colour::knobArc : bandColour (selected);
                    g.setColour (colour::textDisabled);
                    g.setFont (font (type::caption, true));
                    g.drawText (tr (captions.at (c.type)), c.r.withHeight (14.0f), juce::Justification::centred);
                    drawKnob (g, knobCircle (c), param01 (c.param), bipolar, active ? colour::accent : arc, c.r.contains (mouse));
                    g.setColour (colour::textPrimary);
                    g.setFont (font (type::small));
                    g.drawText (valueText (c.param), c.r.withTop (c.r.getBottom() - 18.0f), juce::Justification::centred, true);
                    break;
                }
                default: break;
            }
        }
        // captions
        g.setColour (colour::textDisabled);
        g.setFont (font (type::caption, true));
        const auto p = panel.reduced (12.0f, 10.0f);
        g.drawText (tr ("BANDS"), juce::Rectangle<float> (p.getX(), p.getBottom() - 16.0f, 120.0f, 14.0f), juce::Justification::centredLeft);
        g.drawText (tr ("DYNAMICS"), juce::Rectangle<float> (p.getRight() - 140.0f, p.getBottom() + 0.0f, 136.0f, 10.0f), juce::Justification::centred);
        if (! bandOn (selected))
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::body));
            g.drawText (tr ("Select a band, or double-click the graph to add one"), juce::Rectangle<float> (p.getX() + 134.0f, p.getY(), 420.0f, p.getHeight()),
                        juce::Justification::centredLeft);
        }
    }

private:
    enum class Drag { none, node, range, control };

    int tapKey() const { return track * maxEffectSlots + slot; }

    void claimTap()
    {
        if (auto* times = engine.getProcessTimes()) times->analyzer.key.store (tapKey());
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (current() == nullptr) selected = -1;
        repaint();
    }

    void timerCallback() override
    {
        updateSpectrum();
        if (isShowing()) repaint (graphArea().toNearestInt());
    }

    void updateSpectrum()
    {
        const auto columns = juce::jmax (64, (int) plotArea().getWidth() / 3);
        if ((int) specPre.size() != columns)
        {
            specPre.assign ((std::size_t) columns, (float) specBottom);
            specPost.assign ((std::size_t) columns, (float) specBottom);
        }
        if (analyzer (param01 (analyzerParam)) == Analyzer::off) return;
        if (demo)
        {
            for (int i = 0; i < columns; ++i)
            {
                const auto f = hzForX (plotArea().getX() + plotArea().getWidth() * (float) i / (float) (columns - 1));
                // a mix's typical spectrum: pink-ish slope, a kick bump, a presence region, roll-off
                const auto db = -30.0 - 9.0 * std::log2 (juce::jmax (20.0, f) / 1000.0) * 0.5 + 9.0 * std::exp (-std::pow (std::log2 (f / 60.0), 2.0))
                                + 4.0 * std::sin (f * 0.0021) - (f > 14000.0 ? (f - 14000.0) * 0.004 : 0.0) + tiltDb (f);
                specPre[(std::size_t) i] = (float) db;
                const auto* fx = current();
                specPost[(std::size_t) i] = (float) (db + (fx != nullptr ? responseDb (fx->params, 48000.0, f) : 0.0));
            }
            return;
        }
        auto* times = engine.getProcessTimes();
        if (times == nullptr) return;
        auto& tap = times->analyzer;
        if (tap.key.load (std::memory_order_relaxed) != tapKey())
        {
            if (hasKeyboardFocus (true) || isMouseOver (true)) claimTap(); // the editor in use takes the analyser
            decay();
            return;
        }
        const auto written = tap.written.load (std::memory_order_acquire);
        if (written == lastWritten) { decay(); return; }
        lastWritten = written;
        const auto sr = displayRate();
        for (int which = 0; which < 2; ++which)
        {
            auto& ring = which == 0 ? tap.pre : tap.post;
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
                fftData[(std::size_t) i] = ring[(std::size_t) ((written - (std::uint32_t) fftSize + (std::uint32_t) i) & (ProcessTimes::AnalyzerTap::size - 1))]
                                               .load (std::memory_order_relaxed) * window[(std::size_t) i];
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
            auto& out = which == 0 ? specPre : specPost;
            const auto norm = 4.0f / (float) fftSize; // Hann (coherent gain ½) and one side
            for (int i = 0; i < columns; ++i)
            {
                const auto f0 = hzForX (plotArea().getX() + plotArea().getWidth() * ((float) i - 0.5f) / (float) (columns - 1));
                const auto f1 = hzForX (plotArea().getX() + plotArea().getWidth() * ((float) i + 0.5f) / (float) (columns - 1));
                auto k0 = (int) std::floor (f0 * fftSize / sr), k1 = (int) std::ceil (f1 * fftSize / sr);
                k0 = juce::jlimit (1, fftSize / 2 - 1, k0);
                k1 = juce::jlimit (k0, fftSize / 2 - 1, k1);
                float m = 0.0f;
                for (int k = k0; k <= k1; ++k) m = juce::jmax (m, fftData[(std::size_t) k]);
                const auto f = hzForX (plotArea().getX() + plotArea().getWidth() * (float) i / (float) (columns - 1));
                const auto db = (float) (20.0 * std::log10 (m * norm + 1e-9) + tiltDb (f));
                auto& v = out[(std::size_t) i];
                v = db > v ? db : juce::jmax (db, v - 1.5f); // fast up, ~45 dB/s down
            }
        }
    }

    static double tiltDb (double f) { return 4.5 * std::log2 (juce::jmax (10.0, f) / 1000.0); }

    void decay()
    {
        for (auto* v : { &specPre, &specPost })
            for (auto& x : *v) x = juce::jmax ((float) specBottom, x - 1.5f);
    }

    AudioEngine& engine;
    int track, slot;
    bool demo = false;
    int selected = -1, hover = -1, dragBand = -1, dragParam = -1;
    double rangeDb = 24.0;
    Drag dragging = Drag::none;
    juce::Point<float> dragOffset, mouse { -1.0f, -1.0f };
    juce::Rectangle<float> dragRect;
    ui::ValueDrag valueDrag;
    juce::dsp::FFT fft;
    std::vector<float> fftData, window, specPre, specPost;
    std::uint32_t lastWritten = 0;
};

class FacetEqWindow final : public juce::DocumentWindow
{
public:
    FacetEqWindow (AudioEngine& engine, int track, int slot, const juce::String& title)
        : DocumentWindow (title, theme::colour::bg1, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new FacetEqEditor (engine, track, slot, false), true);
        setResizable (true, false);
        if (auto* c = getConstrainer())
        {
            c->setMinimumSize (860, 440);
            c->setMaximumSize (2400, 1400);
        }
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
    }

    void closeButtonPressed() override;
};

std::map<std::pair<int, int>, std::unique_ptr<FacetEqWindow>>& windows()
{
    static std::map<std::pair<int, int>, std::unique_ptr<FacetEqWindow>> open;
    return open;
}

void FacetEqWindow::closeButtonPressed()
{
    for (auto& [key, window] : windows())
        if (window.get() == this)
        {
            juce::MessageManager::callAsync ([k = key] { windows().erase (k); });
            return;
        }
}
} // namespace

void drawFacetEqCurve (juce::Graphics& g, juce::Rectangle<float> area, const EffectParams& params, double sampleRate, bool enabled)
{
    using namespace theme;
    g.setColour (colour::bg0);
    g.fillRoundedRectangle (area, radius::md);
    const auto xFor = [&] (double f) { return area.getX() + area.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
    g.setColour (colour::gridBeat);
    for (double f : { 100.0, 1000.0, 10000.0 }) g.drawVerticalLine ((int) xFor (f), area.getY(), area.getBottom());
    g.setColour (colour::gridBar);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    const int n = juce::jmax (32, (int) area.getWidth() / 2);
    std::vector<double> freqs ((std::size_t) n + 1), sum ((std::size_t) n + 1, outputDb (params[(std::size_t) outputParam])), band ((std::size_t) n + 1);
    for (int i = 0; i <= n; ++i) freqs[(std::size_t) i] = 20.0 * std::pow (1000.0, (double) i / n);
    for (int b = 0; b < bands; ++b)
    {
        if (shape (params[(std::size_t) param (b, 0)]) == Shape::off) continue;
        bandCurveDb (params, b, sampleRate, freqs.data(), n + 1, band.data());
        for (int i = 0; i <= n; ++i) sum[(std::size_t) i] += band[(std::size_t) i];
    }
    juce::Path p;
    for (int i = 0; i <= n; ++i)
    {
        const auto x = area.getX() + area.getWidth() * (float) i / (float) n;
        const auto y = area.getCentreY() - area.getHeight() * 0.45f * (float) (juce::jlimit (-24.0, 24.0, sum[(std::size_t) i]) / 24.0);
        if (i == 0) p.startNewSubPath (x, y);
        else p.lineTo (x, y);
    }
    g.setColour (enabled ? colour::accent : colour::textDisabled);
    g.strokePath (p, juce::PathStrokeType (2.0f));
    for (int b = 0; b < bands; ++b)
    {
        const auto sh = shape (params[(std::size_t) param (b, 0)]);
        if (sh == Shape::off) continue;
        const auto f = juce::jlimit (20.0, 20000.0, hz (params[(std::size_t) param (b, 1)]));
        const auto db = hasGain (sh) ? gainDb (params[(std::size_t) param (b, 2)]) * gainScale (params[(std::size_t) scaleParam]) : 0.0;
        g.setColour (bandColour (b));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ xFor (f), area.getCentreY() - area.getHeight() * 0.45f * (float) (juce::jlimit (-24.0, 24.0, db) / 24.0) }));
    }
}

std::unique_ptr<juce::Component> makeFacetEqEditor (AudioEngine& engine, int track, int slot, bool demo)
{
    return std::make_unique<FacetEqEditor> (engine, track, slot, demo);
}

namespace FacetEqWindows
{
void open (AudioEngine& engine, int track, int slot)
{
    const auto& mixer = engine.getSong().mixer;
    if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
        return;
    auto& open = windows();
    const auto key = std::make_pair (track, slot);
    if (auto it = open.find (key); it != open.end())
    {
        if (auto* editor = dynamic_cast<FacetEqEditor*> (it->second->getContentComponent()); editor != nullptr && editor->current() != nullptr)
        {
            it->second->toFront (true);
            return;
        }
        open.erase (it);
    }
    const auto name = track == 0 ? tr ("Master") : juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str());
    const auto title = juce::String ("Facet EQ - ") + (name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) })) + " ("
                       + tr ("slot {0}", { juce::String (slot + 1) }) + ")";
    open[key] = std::make_unique<FacetEqWindow> (engine, track, slot, title);
}

void closeAll()
{
    windows().clear();
}
} // namespace FacetEqWindows
} // namespace lynx
