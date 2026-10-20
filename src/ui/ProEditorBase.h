#pragma once

// Shared machinery of the display-first editors (ui/ProEditors.cpp, REA round 114): parameter access by name, the
// control contract (drag, Ctrl = fine, wheel, double-click / Alt-click = reset, right-click = value / automation menu),
// LYNX-style knobs, switches, option buttons and bottom-bar items, a scrolling level / gain-reduction history fed by
// the slot's analyser tap (or the effect's live meters), a spectrum analyser, level meters and dB scales.
// Only ProEditors.cpp includes it.

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <vector>

#include "core/Effects.h"
#include "core/PluginLayout.h"
#include "core/RackRenderer.h"
#include "engine/AudioEngine.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx::pro
{
inline float dbOf (float linear) noexcept { return linear > 1e-6f ? 20.0f * std::log10 (linear) : -120.0f; }

// one column of the scrolling history: input and output peak (dBFS) and gain reduction (dB, ≥ 0)
struct Column
{
    float in = -120.0f, out = -120.0f, gr = 0.0f;
};

class ProEditor : public juce::Component,
                  public ui::HintSource,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    ProEditor (AudioEngine& e, int t, int s, EffectKind k, bool demoMode, int width, int height)
        : engine (e), track (t), slot (s), kind (k), demo (demoMode), fft (fftOrder)
    {
        history.assign (historyLength, Column {});
        fftData.assign ((std::size_t) fftSize * 2, 0.0f);
        window.resize ((std::size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(std::size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) fftSize);
        engine.addChangeListener (this);
        setWantsKeyboardFocus (true);
        setSize (width, height);
        claimTap();
        startTimerHz (30);
    }

    ~ProEditor() override
    {
        stopTimer();
        engine.removeChangeListener (this);
        if (auto* times = engine.getProcessTimes(); times != nullptr && times->analyzer.key.load() == tapKey())
            times->analyzer.key.store (-1);
        ui::hideValueTip();
    }

    const EffectSlot* current() const
    {
        const auto& mixer = engine.getSong().mixer;
        if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
            return nullptr;
        const auto& fx = mixer[(std::size_t) track].effects[(std::size_t) slot];
        return fx.kind == kind ? &fx : nullptr;
    }

    // the demo history / spectrum (for --plugui snapshots) is filled once the subclass is complete
    void fillDemo()
    {
        if (! demo) return;
        for (int i = 0; i < historyLength; ++i)
            pushColumn (demoColumn (i));
        updateSpectrum();
    }

protected:
    // --- controls ------------------------------------------------------------------------------------
    enum class K { knob, fader, toggle, menu, tabs, barMenu, barValue, button, custom };
    struct Ctl
    {
        K type = K::knob;
        juce::Rectangle<float> r;
        int param = -1;
        juce::String caption;
        int size = 1;               // knobs: 0 small, 1 medium, 2 big
        bool bipolar = false;
        int id = 0;                 // custom / button identifiers
        juce::Colour colour { theme::colour::accent };
        juce::StringArray options;  // menu / tabs / barMenu
        std::vector<float> values;  // the value of each option (empty: i / (n − 1))
        juce::String hint;          // what it does (the hint bar)
        bool lit = false;           // buttons: drawn as on
        bool showValue = true;
    };

    static Ctl knob (juce::Rectangle<float> r, int p, const juce::String& caption, int size = 1, bool bipolar = false)
    {
        Ctl c;
        c.type = K::knob;
        c.r = r;
        c.param = p;
        c.caption = caption;
        c.size = size;
        c.bipolar = bipolar;
        return c;
    }
    static Ctl fader (juce::Rectangle<float> r, int p, const juce::String& caption, bool bipolar = false)
    {
        Ctl c = knob (r, p, caption, 0, bipolar);
        c.type = K::fader;
        return c;
    }
    static Ctl toggle (juce::Rectangle<float> r, int p, const juce::String& caption)
    {
        Ctl c;
        c.type = K::toggle;
        c.r = r;
        c.param = p;
        c.caption = caption;
        return c;
    }
    static Ctl menu (juce::Rectangle<float> r, int p, const juce::String& caption, const juce::String& options, std::vector<float> values = {})
    {
        Ctl c;
        c.type = K::menu;
        c.r = r;
        c.param = p;
        c.caption = caption;
        c.options = juce::StringArray::fromTokens (options, "|", "");
        c.values = std::move (values);
        return c;
    }
    static Ctl tabs (juce::Rectangle<float> r, int p, const juce::String& options, std::vector<float> values = {})
    {
        Ctl c = menu (r, p, {}, options, std::move (values));
        c.type = K::tabs;
        return c;
    }
    static Ctl barMenu (juce::Rectangle<float> r, int p, const juce::String& caption, const juce::String& options, std::vector<float> values = {})
    {
        Ctl c = menu (r, p, caption, options, std::move (values));
        c.type = K::barMenu;
        return c;
    }
    static Ctl barValue (juce::Rectangle<float> r, int p, const juce::String& caption)
    {
        Ctl c;
        c.type = K::barValue;
        c.r = r;
        c.param = p;
        c.caption = caption;
        return c;
    }
    static Ctl button (juce::Rectangle<float> r, int id, const juce::String& caption, bool lit, const juce::String& hint)
    {
        Ctl c;
        c.type = K::button;
        c.r = r;
        c.id = id;
        c.caption = caption;
        c.lit = lit;
        c.hint = hint;
        return c;
    }
    static Ctl custom (juce::Rectangle<float> r, int id, int p = -1, const juce::String& hint = {})
    {
        Ctl c;
        c.type = K::custom;
        c.r = r;
        c.id = id;
        c.param = p;
        c.hint = hint;
        return c;
    }

    // --- what a subclass provides --------------------------------------------------------------------
    virtual void layoutControls (std::vector<Ctl>& out) const = 0;
    virtual void paintBody (juce::Graphics& g, const EffectSlot& fx) = 0;      // under the controls
    virtual void paintOverlay (juce::Graphics&, const EffectSlot&) {}          // over the controls (tips)
    virtual juce::String title() const = 0;
    virtual juce::String gestures() const { return {}; }                       // right of the header
    virtual bool bodyDown (const juce::MouseEvent&) { return false; }
    virtual void bodyDrag (const juce::MouseEvent&) {}
    virtual void bodyUp (const juce::MouseEvent&) {}
    virtual bool bodyWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&) { return false; }
    virtual bool bodyDoubleClick (const juce::MouseEvent&) { return false; }
    virtual void bodyMove (juce::Point<float>) {}
    virtual ui::Hint bodyHint (juce::Point<float>) { return {}; }
    virtual void buttonPressed (const Ctl&, const juce::MouseEvent&) {}
    virtual void paintCustom (juce::Graphics&, const Ctl&) {}
    virtual void customDown (const Ctl&, const juce::MouseEvent&) {}
    virtual void customDrag (const Ctl&, const juce::MouseEvent&) {}
    virtual bool usesHistory() const { return false; }
    virtual bool usesSpectrum() const { return false; }
    virtual float grDb() const { return 0.0f; }                                // the effect's current gain reduction
    virtual Column demoColumn (int) const { return {}; }
    virtual double demoSpectrumDb (double hz) const
    {
        return -42.0 - 4.5 * std::log2 (juce::jmax (20.0, hz) / 1000.0) * 0.5 + 9.0 * std::exp (-std::pow (std::log2 (hz / 60.0), 2.0))
               + 4.0 * std::sin (hz * 0.0021) - (hz > 14000.0 ? (hz - 14000.0) * 0.004 : 0.0) + tiltDb (hz);
    }
    virtual void tick() {}

    // --- parameters ----------------------------------------------------------------------------------
    int P (const char* name) const { return plugui::paramIndex (kind, name); }
    float v (int p) const
    {
        const auto* fx = current();
        return fx != nullptr && p >= 0 && p < maxEffectParams ? fx->params[(std::size_t) p] : 0.0f;
    }
    void set (std::vector<std::pair<int, float>> changes, bool undoable = true)
    {
        if (current() == nullptr) return;
        engine.editSong ([tk = track, sl = slot, changes] (Song& song) {
            auto& params = song.mixer[(std::size_t) tk].effects[(std::size_t) sl].params;
            for (const auto& [p, x] : changes)
                if (p >= 0 && p < maxEffectParams) params[(std::size_t) p] = juce::jlimit (0.0f, 1.0f, x);
        }, undoable);
    }
    juce::String text (int p) const { return text (p, v (p)); }
    juce::String text (int p, float x) const
    {
        if (p < 0) return {};
        const auto* fx = current();
        return utf8 ((fx != nullptr ? formatEffectParam (kind, p, x, engine.getSong().bpm, fx->params) : formatEffectParam (kind, p, x, engine.getSong().bpm)).c_str());
    }

    juce::String trackName() const
    {
        const auto& mixer = engine.getSong().mixer;
        if (track == 0) return tr ("Master");
        const auto name = track < (int) mixer.size() ? juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str()) : juce::String();
        return name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) });
    }

    ui::ValueSpec specFor (int p, int steps = 0) const
    {
        ui::ValueSpec spec;
        const auto& in = effectInfo (kind);
        if (p < 0 || p >= (int) in.params.size()) return spec;
        spec.name = trackName() + " - " + utf8 (in.name) + " - " + utf8 (in.params[(std::size_t) p].name);
        spec.defaultValue = in.params[(std::size_t) p].defaultValue;
        spec.pixelsForRange = 200.0;
        if (steps > 0) spec.interval = 1.0 / steps;
        spec.toText = [this, p] (double x) { return text (p, (float) x); };
        spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
        spec.target = AutomationTarget { AutomationTargetKind::effectParam, track, slot, p };
        return spec;
    }

    // the 0..1 value whose real value is `target` (monotonic mapping `f`, rising or falling)
    static float invert (const std::function<double (float)>& f, double target)
    {
        float lo = 0.0f, hi = 1.0f;
        const auto rising = f (1.0f) >= f (0.0f);
        for (int i = 0; i < 40; ++i)
        {
            const auto mid = 0.5f * (lo + hi);
            if ((f (mid) < target) == rising) lo = mid;
            else hi = mid;
        }
        return 0.5f * (lo + hi);
    }

    float liveMeter (int k) const
    {
        const auto* times = engine.getProcessTimes();
        const auto key = (std::size_t) (track * maxEffectSlots + slot) * ProcessTimes::meterOutputs + (std::size_t) juce::jlimit (0, ProcessTimes::meterOutputs - 1, k);
        return times != nullptr && key < times->effectMeters.size() ? times->effectMeters[key].load (std::memory_order_relaxed) : 0.0f;
    }

    // --- geometry ------------------------------------------------------------------------------------
    static constexpr float headerH = 30.0f, barH = 28.0f;
    juce::Rectangle<float> headerArea() const { return { 0.0f, 0.0f, (float) getWidth(), headerH }; }
    juce::Rectangle<float> barArea() const { return { 0.0f, (float) getHeight() - barH, (float) getWidth(), barH }; }
    juce::Rectangle<float> bodyArea() const { return { 0.0f, headerH, (float) getWidth(), (float) getHeight() - headerH - barH }; }

    std::vector<Ctl> controls() const
    {
        std::vector<Ctl> out;
        layoutControls (out);
        return out;
    }
    bool controlAt (juce::Point<float> p, Ctl& found) const
    {
        const auto all = controls();
        for (auto it = all.rbegin(); it != all.rend(); ++it) // the last drawn is on top
            if (it->r.contains (p))
            {
                found = *it;
                return true;
            }
        return false;
    }

    int optionIndex (const Ctl& c) const
    {
        const auto n = c.options.size();
        if (n <= 0) return 0;
        const auto x = v (c.param);
        auto best = 0;
        auto bestD = 10.0f;
        for (int i = 0; i < n; ++i)
            if (const auto d = std::abs (optionValue (c, i) - x); d < bestD)
            {
                best = i;
                bestD = d;
            }
        return best;
    }
    static float optionValue (const Ctl& c, int i)
    {
        if (i < (int) c.values.size()) return c.values[(std::size_t) i];
        return c.options.size() > 1 ? (float) i / (float) (c.options.size() - 1) : 0.0f;
    }

    void showOptions (const Ctl& c)
    {
        juce::PopupMenu m;
        const auto now = optionIndex (c);
        for (int i = 0; i < c.options.size(); ++i)
            m.addItem (tr (c.options[i]), true, i == now, [this, p = c.param, x = optionValue (c, i)] { set ({ { p, x } }); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (c.r.toNearestInt())));
    }

    // --- mouse ---------------------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragCtl = -1;
        Ctl c;
        if (controlAt (e.position, c))
        {
            pressed = c;
            if (c.type == K::custom)
            {
                customDown (c, e);
                dragCtl = -2;
                return;
            }
            if (c.type == K::button)
            {
                buttonPressed (c, e);
                return;
            }
            if (e.mods.isPopupMenu())
            {
                if (c.type == K::knob || c.type == K::fader || c.type == K::barValue)
                    ui::showValueMenu (*this, &engine, specFor (c.param), v (c.param), [this, p = c.param] (double x) { set ({ { p, (float) x } }); });
                else if (c.type == K::menu || c.type == K::barMenu)
                    showOptions (c);
                return;
            }
            switch (c.type)
            {
                case K::toggle: set ({ { c.param, v (c.param) >= 0.5f ? 0.0f : 1.0f } }); return;
                case K::menu: case K::barMenu: showOptions (c); return;
                case K::tabs:
                {
                    const auto n = juce::jmax (1, c.options.size());
                    const auto i = juce::jlimit (0, n - 1, (int) ((e.position.x - c.r.getX()) / c.r.getWidth() * (float) n));
                    set ({ { c.param, optionValue (c, i) } });
                    return;
                }
                case K::knob: case K::fader: case K::barValue:
                    if (ui::isResetClick (e))
                    {
                        set ({ { c.param, (float) specFor (c.param).defaultValue } });
                        return;
                    }
                    dragCtl = c.param;
                    valueDrag.begin (e, v (c.param));
                    return;
                default: return;
            }
        }
        if (bodyDown (e)) dragCtl = -3;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragCtl >= 0)
        {
            const auto spec = specFor (dragCtl);
            const auto x = valueDrag.update (e, spec);
            set ({ { dragCtl, (float) x } });
            ui::showValueTip (*this, pressed.r.toNearestInt(), spec.text (x));
            return;
        }
        if (dragCtl == -2) { customDrag (pressed, e); return; }
        if (dragCtl == -3) bodyDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragCtl == -3) bodyUp (e);
        dragCtl = -1;
        valueDrag.end();
        ui::hideValueTip();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        Ctl c;
        if (controlAt (e.position, c)) return; // the controls reset on their own double-click (ValueDrag)
        bodyDoubleClick (e);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        Ctl c;
        if (controlAt (e.position, c) && c.param >= 0 && (c.type == K::knob || c.type == K::fader || c.type == K::barValue || c.type == K::custom))
        {
            const auto spec = specFor (c.param);
            if (const auto x = ui::wheelValue (v (c.param), e, wheel, spec))
            {
                set ({ { c.param, (float) *x } });
                ui::flashValueTip (*this, c.r.toNearestInt(), spec.text (*x));
            }
            return;
        }
        bodyWheel (e, wheel);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        mouse = e.position;
        bodyMove (e.position);
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        mouse = { -1.0f, -1.0f };
        bodyMove (mouse);
        repaint();
    }

    ui::Hint hintAt (juce::Point<int> position) override
    {
        const auto p = position.toFloat();
        Ctl c;
        if (controlAt (p, c))
        {
            if (c.param >= 0 && (c.type == K::knob || c.type == K::fader || c.type == K::barValue || (c.type == K::custom && c.hint.isEmpty())))
                return ui::valueHint (specFor (c.param), v (c.param));
            ui::Hint h;
            h.text = c.hint.isNotEmpty() ? c.hint : c.caption;
            if (c.param >= 0) h.value = text (c.param);
            h.hasMenu = c.type == K::menu || c.type == K::barMenu;
            return h;
        }
        return bodyHint (p);
    }

    // --- painting ------------------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg0);
        const auto* fx = current();
        if (fx == nullptr)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::body));
            g.drawText (tr ("The effect was removed from this slot."), getLocalBounds(), juce::Justification::centred);
            return;
        }
        paintBody (g, *fx);
        paintHeader (g, *fx);
        g.setColour (colour::bg2);
        g.fillRect (barArea());
        g.setColour (colour::border);
        g.drawHorizontalLine ((int) barArea().getY(), 0.0f, (float) getWidth());
        for (const auto& c : controls())
            paintControl (g, c);
        paintOverlay (g, *fx);
    }

    void paintHeader (juce::Graphics& g, const EffectSlot& fx)
    {
        using namespace theme;
        const auto h = headerArea();
        g.setColour (colour::bg2);
        g.fillRect (h);
        g.setColour (colour::border);
        g.drawHorizontalLine ((int) h.getBottom() - 1, 0.0f, h.getRight());
        g.setColour (fx.enabled ? colour::ledOn : colour::ledOff);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ 14.0f, h.getCentreY() }));
        g.setColour (colour::textPrimary);
        g.setFont (font (type::title, true));
        const auto name = title();
        const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name);
        g.drawText (name, juce::Rectangle<float> (26.0f, 0.0f, w + 8.0f, h.getHeight()), juce::Justification::centredLeft);
        g.setColour (colour::accent);
        g.fillRect (juce::Rectangle<float> (26.0f, h.getBottom() - 3.0f, w, 2.0f));
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawText (trackName() + "  -  " + tr ("slot {0}", { juce::String (slot + 1) }), juce::Rectangle<float> (40.0f + w, 0.0f, 220.0f, h.getHeight()),
                    juce::Justification::centredLeft, true);
        g.setColour (colour::textDisabled);
        g.setFont (font (type::caption));
        g.drawText (gestures(), h.withTrimmedLeft (w + 250.0f).withTrimmedRight (12.0f), juce::Justification::centredRight, true);
    }

    void paintControl (juce::Graphics& g, const Ctl& c)
    {
        using namespace theme;
        const auto hover = c.r.contains (mouse);
        switch (c.type)
        {
            case K::knob:
            {
                const auto d = c.size == 2 ? juce::jmin (c.r.getWidth() - 4.0f, c.r.getHeight() - 36.0f)
                                           : juce::jmin (c.r.getWidth() - 10.0f, c.r.getHeight() - 34.0f);
                const auto knobR = juce::Rectangle<float> (c.r.getCentreX() - d / 2.0f, c.r.getY() + 2.0f, d, d);
                const auto active = dragCtl == c.param && c.param >= 0;
                drawKnob (g, knobR, v (c.param), c.bipolar, active ? colour::accent : c.colour, hover);
                g.setColour (colour::textSecondary);
                g.setFont (font (c.size == 2 ? type::small : type::caption, true));
                g.drawText (caps (c.caption), juce::Rectangle<float> (c.r.getX() - 14.0f, knobR.getBottom() + 3.0f, c.r.getWidth() + 28.0f, 14.0f),
                            juce::Justification::centred, false);
                if (c.showValue)
                {
                    g.setColour (active || hover ? colour::textPrimary : colour::textSecondary.brighter (0.2f));
                    g.setFont (font (type::small));
                    g.drawText (text (c.param), juce::Rectangle<float> (c.r.getX() - 14.0f, knobR.getBottom() + 17.0f, c.r.getWidth() + 28.0f, 14.0f),
                                juce::Justification::centred, false);
                }
                break;
            }
            case K::fader:
            {
                const auto track = juce::Rectangle<float> (c.r.getCentreX() - 2.0f, c.r.getY() + 6.0f, 4.0f, c.r.getHeight() - 42.0f);
                const auto active = dragCtl == c.param && c.param >= 0;
                g.setColour (colour::knobTrack);
                g.fillRoundedRectangle (track, 2.0f);
                const auto y = track.getBottom() - track.getHeight() * v (c.param);
                const auto from = c.bipolar ? track.getCentreY() : track.getBottom();
                g.setColour ((active ? colour::accent : c.colour).withAlpha (0.8f));
                g.fillRect (track.withTop (juce::jmin (y, from)).withBottom (juce::jmax (y, from)));
                const auto thumb = juce::Rectangle<float> (26.0f, 12.0f).withCentre ({ track.getCentreX(), y });
                g.setColour (hover || active ? colour::bg6.brighter (0.2f) : colour::bg6);
                g.fillRoundedRectangle (thumb, 3.0f);
                g.setColour (active ? colour::accent : colour::borderStrong.brighter (0.3f));
                g.drawRoundedRectangle (thumb, 3.0f, 1.0f);
                g.setColour (colour::textSecondary);
                g.setFont (font (type::caption, true));
                g.drawText (caps (c.caption), juce::Rectangle<float> (c.r.getX() - 12.0f, track.getBottom() + 8.0f, c.r.getWidth() + 24.0f, 13.0f), juce::Justification::centred, false);
                g.setColour (active || hover ? colour::textPrimary : colour::textSecondary.brighter (0.2f));
                g.setFont (font (type::caption));
                g.drawText (text (c.param), juce::Rectangle<float> (c.r.getX() - 12.0f, track.getBottom() + 21.0f, c.r.getWidth() + 24.0f, 13.0f), juce::Justification::centred, false);
                break;
            }
            case K::toggle:
            {
                const auto on = v (c.param) >= 0.5f;
                g.setColour (hover ? colour::bg5 : colour::bg4);
                g.fillRoundedRectangle (c.r, radius::md);
                g.setColour (on ? c.colour : colour::borderStrong);
                g.drawRoundedRectangle (c.r.reduced (0.5f), radius::md, 1.0f);
                g.setColour (on ? c.colour : colour::ledOff);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ c.r.getX() + 11.0f, c.r.getCentreY() }));
                g.setColour (on ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (type::small, on));
                g.drawText (c.caption, c.r.withTrimmedLeft (20.0f).withTrimmedRight (4.0f), juce::Justification::centred, true);
                break;
            }
            case K::button:
            {
                g.setColour (c.lit ? c.colour.withAlpha (0.28f) : (hover ? colour::bg5 : colour::bg4));
                g.fillRoundedRectangle (c.r, radius::md);
                g.setColour (c.lit ? c.colour : colour::borderStrong);
                g.drawRoundedRectangle (c.r.reduced (0.5f), radius::md, 1.0f);
                g.setColour (c.lit || hover ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (type::small, c.lit));
                g.drawText (c.caption, c.r.reduced (4.0f, 0.0f), juce::Justification::centred, true);
                break;
            }
            case K::menu:
            {
                g.setColour (hover ? colour::bg6 : colour::bg5);
                g.fillRoundedRectangle (c.r, radius::lg);
                g.setColour (colour::borderStrong.brighter (0.2f));
                g.drawRoundedRectangle (c.r.reduced (0.5f), radius::lg, 1.0f);
                g.setColour (colour::textPrimary);
                g.setFont (font (type::body, true));
                const auto i = optionIndex (c);
                g.drawText (tr (c.options[i]) + juce::String::fromUTF8 (" \xe2\x96\xbe"), c.r.reduced (6.0f, 0.0f), juce::Justification::centred, true);
                if (c.caption.isNotEmpty())
                {
                    g.setColour (colour::textDisabled);
                    g.setFont (font (type::caption, true));
                    g.drawText (caps (c.caption), c.r.translated (0.0f, c.r.getHeight() + 1.0f).withHeight (13.0f), juce::Justification::centred, false);
                }
                break;
            }
            case K::tabs:
            {
                const auto n = juce::jmax (1, c.options.size());
                const auto sel = optionIndex (c);
                g.setColour (colour::bg1);
                g.fillRoundedRectangle (c.r, radius::md);
                for (int i = 0; i < n; ++i)
                {
                    const auto cell = juce::Rectangle<float> (c.r.getX() + c.r.getWidth() * (float) i / (float) n, c.r.getY(), c.r.getWidth() / (float) n, c.r.getHeight()).reduced (1.5f);
                    const auto on = i == sel;
                    if (on || cell.contains (mouse))
                    {
                        g.setColour (on ? colour::bg6 : colour::bg4);
                        g.fillRoundedRectangle (cell, radius::sm);
                    }
                    if (on)
                    {
                        g.setColour (c.colour);
                        g.fillRect (cell.withTrimmedTop (cell.getHeight() - 2.0f).reduced (4.0f, 0.0f));
                    }
                    g.setColour (on ? colour::textPrimary : colour::textSecondary);
                    g.setFont (font (type::small, on));
                    g.drawText (caps (tr (c.options[i])), cell, juce::Justification::centred, true);
                }
                break;
            }
            case K::barMenu: case K::barValue:
            {
                if (hover)
                {
                    g.setColour (colour::bg4);
                    g.fillRoundedRectangle (c.r, radius::md);
                }
                g.setFont (font (type::small));
                g.setColour (colour::textDisabled);
                const auto cap = c.caption + ":";
                const auto cw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), cap) + 6.0f;
                g.drawText (cap, c.r.withWidth (cw).translated (6.0f, 0.0f), juce::Justification::centredLeft);
                g.setColour (dragCtl == c.param && c.param >= 0 ? colour::accent : colour::textPrimary);
                const auto value = c.type == K::barMenu ? tr (c.options[optionIndex (c)]) + juce::String::fromUTF8 (" \xe2\x96\xbe") : text (c.param);
                g.drawText (value, c.r.withTrimmedLeft (cw + 6.0f), juce::Justification::centredLeft, true);
                break;
            }
            case K::custom: paintCustom (g, c); break;
        }
    }

    // a floating panel (the knob panels over the display)
    static void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour edge = theme::colour::borderStrong)
    {
        using namespace theme;
        juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 14, { 0, 4 }).drawForRectangle (g, r.toNearestInt());
        g.setColour (colour::bg2.withAlpha (0.94f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (edge.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.2f);
    }

    static void caption (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Justification j = juce::Justification::centred)
    {
        using namespace theme;
        g.setColour (colour::textDisabled);
        g.setFont (font (type::caption, true));
        g.drawText (caps (t), r, j, false);
    }

    // --- level history -------------------------------------------------------------------------------
    static constexpr int historyLength = 1400; // columns of 10 ms: 14 s
    std::vector<Column> history;
    int historyPos = 0; // the next column to write
    std::uint32_t lastWritten = 0;
    float pendingIn = -120.0f, pendingOut = -120.0f;
    int pendingCount = 0;

    void pushColumn (Column c)
    {
        history[(std::size_t) historyPos] = c;
        historyPos = (historyPos + 1) % historyLength;
    }
    // column k back from the newest (0 = newest)
    const Column& columnBack (int k) const { return history[(std::size_t) ((historyPos - 1 - k + historyLength * 4) % historyLength)]; }

    void pullHistory()
    {
        if (demo) return;
        auto* times = engine.getProcessTimes();
        const auto gr = grDb();
        if (times != nullptr && times->analyzer.key.load (std::memory_order_relaxed) == tapKey())
        {
            auto& tap = times->analyzer;
            const auto written = tap.written.load (std::memory_order_acquire);
            auto fresh = written - lastWritten;
            lastWritten = written;
            if (fresh == 0 || fresh > (std::uint32_t) ProcessTimes::AnalyzerTap::size / 2)
            {
                if (fresh == 0) pushColumn ({ -120.0f, -120.0f, 0.0f }); // silence keeps the display moving
                return;
            }
            const auto colSamples = juce::jmax (64, (int) (tap.sampleRate.load (std::memory_order_relaxed) / 100.0));
            for (std::uint32_t i = 0; i < fresh; ++i)
            {
                const auto at = (written - fresh + i) & (ProcessTimes::AnalyzerTap::size - 1);
                pendingIn = juce::jmax (pendingIn, std::abs (tap.pre[(std::size_t) at].load (std::memory_order_relaxed)));
                pendingOut = juce::jmax (pendingOut, std::abs (tap.post[(std::size_t) at].load (std::memory_order_relaxed)));
                if (++pendingCount >= colSamples)
                {
                    pushColumn ({ dbOf (pendingIn), dbOf (pendingOut), gr });
                    pendingIn = pendingOut = 0.0f;
                    pendingCount = 0;
                }
            }
            return;
        }
        if (hasKeyboardFocus (true) || isMouseOver (true)) claimTap();
        // without the tap: the effect's block meters, three columns per refresh
        const auto in = dbOf (liveMeter (2)), out = dbOf (liveMeter (3));
        for (int i = 0; i < 3; ++i) pushColumn ({ in, out, gr });
    }

    // a vertical level meter (peak with a slow fall), from `lo` dB at the bottom to 0 dB at the top
    static void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float db, float lo, juce::Colour c)
    {
        using namespace theme;
        g.setColour (colour::bg1);
        g.fillRect (r);
        const auto f = juce::jlimit (0.0f, 1.0f, (db - lo) / -lo);
        auto lit = r.withTop (r.getBottom() - r.getHeight() * f);
        g.setColour (c);
        g.fillRect (lit);
        g.setColour (colour::bg0.withAlpha (0.55f));
        for (auto y = r.getBottom() - 3.0f; y > r.getY(); y -= 3.0f) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    }

    // --- spectrum analyser (the slot's input / output through the analyser tap) ----------------------
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;
    std::vector<float> specPre, specPost; // dB per column over `specArea`
    juce::Rectangle<float> specArea;
    double specMinHz = 10.0, specMaxHz = 30000.0;

    float xForHz (double f, juce::Rectangle<float> a) const
    {
        return a.getX() + a.getWidth() * (float) (std::log (juce::jlimit (specMinHz, specMaxHz, f) / specMinHz) / std::log (specMaxHz / specMinHz));
    }
    double hzForX (float x, juce::Rectangle<float> a) const
    {
        return specMinHz * std::pow (specMaxHz / specMinHz, juce::jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth())));
    }
    static double tiltDb (double f) { return 4.5 * std::log2 (juce::jmax (10.0, f) / 1000.0); }

    void updateSpectrum()
    {
        const auto a = specArea;
        if (a.isEmpty()) return;
        const auto columns = juce::jmax (64, (int) a.getWidth() / 3);
        if ((int) specPre.size() != columns)
        {
            specPre.assign ((std::size_t) columns, -120.0f);
            specPost.assign ((std::size_t) columns, -120.0f);
        }
        if (demo)
        {
            for (int i = 0; i < columns; ++i)
            {
                const auto f = hzForX (a.getX() + a.getWidth() * (float) i / (float) (columns - 1), a);
                specPre[(std::size_t) i] = specPost[(std::size_t) i] = (float) demoSpectrumDb (f);
            }
            return;
        }
        auto* times = engine.getProcessTimes();
        if (times == nullptr) return;
        auto& tap = times->analyzer;
        if (tap.key.load (std::memory_order_relaxed) != tapKey())
        {
            if (hasKeyboardFocus (true) || isMouseOver (true)) claimTap();
            return;
        }
        const auto written = tap.written.load (std::memory_order_acquire);
        if (written == lastSpectrum)
        {
            for (auto* s : { &specPre, &specPost })
                for (auto& x : *s) x = juce::jmax (-120.0f, x - 1.5f);
            return;
        }
        lastSpectrum = written;
        const auto sr = tap.sampleRate.load (std::memory_order_relaxed);
        for (int which = 0; which < 2; ++which)
        {
            auto& ring = which == 0 ? tap.pre : tap.post;
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
                fftData[(std::size_t) i] = ring[(std::size_t) ((written - (std::uint32_t) fftSize + (std::uint32_t) i) & (ProcessTimes::AnalyzerTap::size - 1))]
                                               .load (std::memory_order_relaxed) * window[(std::size_t) i];
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
            auto& out = which == 0 ? specPre : specPost;
            const auto norm = 4.0f / (float) fftSize;
            for (int i = 0; i < columns; ++i)
            {
                const auto f0 = hzForX (a.getX() + a.getWidth() * ((float) i - 0.5f) / (float) (columns - 1), a);
                const auto f1 = hzForX (a.getX() + a.getWidth() * ((float) i + 0.5f) / (float) (columns - 1), a);
                auto k0 = (int) std::floor (f0 * fftSize / sr), k1 = (int) std::ceil (f1 * fftSize / sr);
                k0 = juce::jlimit (1, fftSize / 2 - 1, k0);
                k1 = juce::jlimit (k0, fftSize / 2 - 1, k1);
                float m = 0.0f;
                for (int k = k0; k <= k1; ++k) m = juce::jmax (m, fftData[(std::size_t) k]);
                const auto f = hzForX (a.getX() + a.getWidth() * (float) i / (float) (columns - 1), a);
                const auto db = (float) (20.0 * std::log10 (m * norm + 1e-9) + tiltDb (f));
                auto& x = out[(std::size_t) i];
                x = db > x ? db : juce::jmax (db, x - 1.5f);
            }
        }
    }

    // the analyser curve as a filled path (dB → y by `yOf`)
    void paintSpectrum (juce::Graphics& g, const std::vector<float>& spec, const std::function<float (float)>& yOf, juce::Colour fill, juce::Colour line) const
    {
        const auto a = specArea;
        if (spec.size() < 2) return;
        juce::Path p;
        p.startNewSubPath (a.getX(), a.getBottom());
        for (std::size_t i = 0; i < spec.size(); ++i)
            p.lineTo (a.getX() + a.getWidth() * (float) i / (float) (spec.size() - 1), juce::jlimit (a.getY(), a.getBottom(), yOf (spec[i])));
        p.lineTo (a.getRight(), a.getBottom());
        p.closeSubPath();
        g.setColour (fill);
        g.fillPath (p);
        g.setColour (line);
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    void frequencyLabels (juce::Graphics& g, juce::Rectangle<float> a, float y) const
    {
        using namespace theme;
        g.setFont (font (type::caption));
        for (const auto hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const auto x = xForHz (hz, a);
            g.setColour (colour::gridBeat);
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (colour::textDisabled);
            g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                        juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x, y }), juce::Justification::centred, false);
        }
    }

    AudioEngine& engine;
    int track, slot;
    EffectKind kind;
    bool demo = false;
    juce::Point<float> mouse { -1.0f, -1.0f };
    int dragCtl = -1;
    Ctl pressed;
    ui::ValueDrag valueDrag;

private:
    int tapKey() const { return track * maxEffectSlots + slot; }
    void claimTap()
    {
        if (auto* times = engine.getProcessTimes()) times->analyzer.key.store (tapKey());
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    void timerCallback() override
    {
        if (usesHistory()) pullHistory();
        if (usesSpectrum()) updateSpectrum();
        tick();
        if (isShowing()) repaint();
    }

    juce::dsp::FFT fft;
    std::vector<float> fftData, window;
    std::uint32_t lastSpectrum = 0;
};
} // namespace lynx::pro
