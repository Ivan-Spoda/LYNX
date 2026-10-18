#include "ui/PluginEditors.h"

#include <cstring>
#include <map>
#include <array>
#include <optional>

#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/FruityShapers.h"
#include "core/GrossBeat.h"
#include "core/LynxTune.h"
#include "core/MaximusFx.h"
#include "core/MixSuite.h"
#include "core/EchoSuite.h"
#include "core/ParametricEqFl.h"
#include "core/VocalPitch.h"
#include "core/FruityBasicFx.h"
#include "core/PluginLayout.h"
#include "core/StateParams.h"
#include "core/ConvolverFx.h"
#include "core/EquoFx.h"
#include "core/MultibandDelayFx.h"
#include "core/Vocodex.h"
#include "engine/Text.h"
#include "ui/DynamicsDisplays.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/FacetEqEditor.h"
#include "ui/ParametricEqEditor.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
bool demoDisplays = false; // --plugui: live displays show a synthetic signal

using plugui::Item;
using plugui::LayoutItem;

constexpr float headerHeight = 26.0f; // the presets bar (not scaled with the body)

bool hasStyle (const LayoutItem& item, std::uint16_t flag) noexcept
{
    return (item.style & flag) != 0;
}

// a row of tabs drawn as a column (taller than wide: Maximus' band selector)
bool isVertical (const LayoutItem& item) noexcept
{
    return item.type == Item::tabs && item.h > item.w * 1.2f;
}

// --- the editor ----------------------------------------------------------------------------------

class NativeEffectEditor final : public juce::Component,
                                 public ui::HintSource,
                                 private juce::ChangeListener,
                                 private juce::Timer
{
public:
    NativeEffectEditor (AudioEngine& e, int t, int s, float initialScale, int initialPage)
        : engine (e),
          track (t),
          slot (s)
    {
        if (const auto* fx = current())
        {
            kind = fx->kind;
            layout = &plugui::layoutFor (kind);
            if (kind == EffectKind::maximus) // Soundgoodizer runs on the Maximus engine: its own page
                if (const auto blob = maximusfx::unpack (fx->state); blob && blob->build == hlfx::MaximusBuild::soundgoodizer)
                    layout = &plugui::soundgoodizerLayout();
        }

        page = initialPage;
        if (kind == EffectKind::equo && initialPage == 0)
            page = 3; // bank 1, volume (FL opens EQUO on its first bank)
        if (kind == EffectKind::maximus && stateBacked() && initialPage == 0)
            if (const auto* fx = current())
                page = maximusfx::selectedBand (fx->state); // FL reopens Maximus on the band selected when it was saved
        engine.addChangeListener (this);
        const auto& l = activeLayout();
        for (const auto& item : l.items) // live meters and graphs (mixing suite, LYNX Tune's pitch, round 108's dynamics): 30 Hz
            if (item.type == Item::display && item.display != nullptr
                && (std::strcmp (item.display, "grmeter") == 0 || std::strcmp (item.display, "pitchgraph") == 0 || dynui::isLive (item.display)))
            {
                startTimerHz (30);
                break;
            }
        setSize ((int) std::ceil ((float) l.width * initialScale), (int) std::ceil ((float) l.height * initialScale + headerHeight));
    }

    ~NativeEffectEditor() override
    {
        stopTimer();
        engine.removeChangeListener (this);
        ui::hideValueTip();
    }

    // the design size (for the window's aspect ratio)
    juce::Point<int> designSize() const { return { activeLayout().width, activeLayout().height }; }

    const EffectSlot* current() const
    {
        const auto& mixer = engine.getSong().mixer;

        if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
            return nullptr;

        const auto& fx = mixer[(std::size_t) track].effects[(std::size_t) slot];
        return layout == nullptr || fx.kind == kind ? &fx : nullptr;
    }

    // --- geometry ----------------------------------------------------------------------------------

    const plugui::Layout& activeLayout() const
    {
        static const plugui::Layout empty { "", 360, 120, {} };
        return layout != nullptr ? *layout : empty;
    }

    float scale() const
    {
        const auto& l = activeLayout();
        return juce::jmax (0.25f, juce::jmin ((float) getWidth() / (float) l.width, ((float) getHeight() - headerHeight) / (float) l.height));
    }

    juce::Rectangle<float> rectOf (const LayoutItem& item) const
    {
        const auto s = scale();
        return { item.x * s, headerHeight + item.y * s, item.w * s, item.h * s };
    }

    bool visible (const LayoutItem& item) const
    {
        const auto* fx = current();
        return fx != nullptr && plugui::isShown (kind, item, page, fx->params, &fx->state);
    }

    // the knob's circle inside its cell (the caption takes the rest)
    juce::Rectangle<float> knobCircle (const LayoutItem& item) const
    {
        auto r = rectOf (item);
        const auto s = scale();
        if (hasStyle (item, plugui::valueAbove))
            r.removeFromTop (16.0f * s);
        if (! hasStyle (item, plugui::noCaption))
            r.removeFromBottom ((hasStyle (item, plugui::showValue) ? 26.0f : 14.0f) * s);
        const auto d = juce::jmin (r.getWidth(), r.getHeight());
        return r.withSizeKeepingCentre (d, d).withY (r.getY());
    }

    int itemAt (juce::Point<float> p) const
    {
        const auto& items = activeLayout().items;

        for (int i = (int) items.size(); --i >= 0;)
        {
            const auto& item = items[(std::size_t) i];
            if (! visible (item) || item.type == Item::section || item.type == Item::label)
                continue;
            if (item.type == Item::display && rectOf (item).contains (p) && (juce::String (item.display) == "eq" || juce::String (item.display) == "xy"
                                                                    || juce::String (item.display) == "sgamount" || juce::String (item.display) == "sgpresets"
                                                                    || juce::String (item.display) == "prismbands" || juce::String (item.display) == "emberbands"
                                                                    || isBars (item)))
                return i;
            if (item.type == Item::display || (item.param == nullptr && ! hasStyle (item, plugui::pageTabs)))
                continue;
            if (rectOf (item).contains (p))
                return i;
        }
        return -1;
    }

    int paramOf (const LayoutItem& item) const { return item.param != nullptr ? plugui::paramIndex (kind, item.param) : -1; }

    // --- values --------------------------------------------------------------------------------------

    // Controls stored in the slot's state (core/StateParams.h: Maximus' bands, Delay Bank's banks, band tables...)
    // edit those bytes instead of EffectSlot::params
    bool stateBacked() const { return plugui::usesStateParams (kind) && layout != &plugui::soundgoodizerLayout(); }
    bool isStateParam (int param) const { return stateBacked() && plugui::stateIndex (kind, param) >= 0; }
    const stateparams::Table& stateTable() const { return *stateparams::tableFor (kind); }

    float valueOf (int param) const
    {
        const auto* fx = current();
        if (isStateParam (param))
            return fx != nullptr ? plugui::stateValue01 (kind, param, fx->state) : 0.0f;
        return fx != nullptr && param >= 0 && param < maxEffectParams ? fx->params[(std::size_t) param] : 0.0f;
    }

    void setValue (int param, float value)
    {
        if (isStateParam (param))
        {
            const auto* fx = current();
            if (fx == nullptr)
                return;
            const auto s = plugui::stateIndex (kind, param);
            const auto raw = plugui::stateRaw (kind, param, value);
            if (stateTable().get (fx->state, s) == raw)
                return;
            engine.editSong ([t = track, sl = slot, bytes = stateTable().with (fx->state, s, raw)] (Song& song) {
                song.mixer[(std::size_t) t].effects[(std::size_t) sl].state = bytes;
            });
            return;
        }
        if (current() == nullptr || param < 0 || param >= maxEffectParams)
            return;
        engine.editSong ([t = track, s = slot, param, v = juce::jlimit (0.0f, 1.0f, value)] (Song& song) {
            song.mixer[(std::size_t) t].effects[(std::size_t) s].params[(std::size_t) param] = v;
        });
    }

    juce::String valueText (int param, float value) const
    {
        const auto* fx = current();
        if (fx == nullptr || param < 0)
            return {};
        if (isStateParam (param))
            return utf8 (stateTable().text (fx->state, plugui::stateIndex (kind, param), plugui::stateRaw (kind, param, value)).c_str());
        if (kind == EffectKind::fruitySend && param == 3)
            return flbasic::sendTrack (value) > 0 ? insertName (flbasic::sendTrack (value)) : tr ("None");
        return utf8 (formatEffectParam (kind, param, value, engine.getSong().bpm, fx->params).c_str());
    }

    // an insert's name as the mixer shows it
    juce::String insertName (int t) const
    {
        const auto& mixer = engine.getSong().mixer;
        const auto name = t > 0 && t < (int) mixer.size() ? juce::String::fromUTF8 (mixer[(std::size_t) t].name.c_str()) : juce::String();
        return name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (t) });
    }

    juce::String trackName() const
    {
        const auto& mixer = engine.getSong().mixer;
        if (track == 0)
            return tr ("Master");
        const auto name = track < (int) mixer.size() ? juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str()) : juce::String();
        return name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) });
    }

    ui::ValueSpec specFor (const LayoutItem& item) const
    {
        ui::ValueSpec spec;
        const auto param = paramOf (item);
        const auto& info = effectInfo (kind);

        if (isStateParam (param))
        {
            // not automatable (the values live in the state): drag, wheel, reset and type-in only
            const auto& p = stateTable().params[(std::size_t) plugui::stateIndex (kind, param)];
            spec.name = trackName() + " - " + utf8 (info.name) + " - " + utf8 (p.name.c_str());
            spec.defaultValue = p.maximum > p.minimum ? (double) (p.defaultValue - p.minimum) / (double) (p.maximum - p.minimum) : 0.0;
            spec.pixelsForRange = item.type == Item::hslider ? juce::jmax (60.0, (double) rectOf (item).getWidth())
                                  : item.type == Item::vslider ? juce::jmax (60.0, (double) rectOf (item).getHeight())
                                                               : 180.0;
            if (item.steps > 0)
                spec.interval = 1.0 / item.steps;
            else if (p.maximum - p.minimum <= 16)
                spec.interval = 1.0 / juce::jmax (1, p.maximum - p.minimum);
            spec.toText = [this, param] (double v) { return valueText (param, (float) v); };
            spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
            return spec;
        }

        if (param < 0 || param >= (int) info.params.size())
            return spec;

        spec.name = trackName() + " - " + utf8 (info.name) + " - " + utf8 (info.params[(std::size_t) param].name);
        spec.defaultValue = info.params[(std::size_t) param].defaultValue;
        spec.pixelsForRange = item.type == Item::hslider ? juce::jmax (60.0, (double) rectOf (item).getWidth())
                              : item.type == Item::vslider ? juce::jmax (60.0, (double) rectOf (item).getHeight())
                                                           : 180.0;
        if (item.steps > 0)
            spec.interval = 1.0 / item.steps;
        spec.toText = [this, param] (double v) { return valueText (param, (float) v); };
        spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
        spec.target = AutomationTarget { AutomationTargetKind::effectParam, track, slot, param };
        return spec;
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

        paintHeader (g, *fx);

        const auto& items = activeLayout().items;
        for (int i = 0; i < (int) items.size(); ++i)
            if (visible (items[(std::size_t) i]))
                paintItem (g, items[(std::size_t) i], i, *fx);
    }

    juce::Rectangle<float> headerArea() const { return { 0.0f, 0.0f, (float) getWidth(), headerHeight }; }
    juce::Rectangle<float> menuButton() const { return { 4.0f, 3.0f, 22.0f, 20.0f }; }
    juce::Rectangle<float> nextButton() const { return { (float) getWidth() - 26.0f, 3.0f, 20.0f, 20.0f }; }
    juce::Rectangle<float> prevButton() const { return nextButton().translated (-22.0f, 0.0f); }
    juce::Rectangle<float> presetBox() const
    {
        const auto w = juce::jlimit (90.0f, 220.0f, (float) getWidth() * 0.32f);
        return { prevButton().getX() - w - 4.0f, 3.0f, w, 20.0f };
    }

    void paintHeader (juce::Graphics& g, const EffectSlot& fx)
    {
        using namespace theme;
        const auto area = headerArea();
        g.setColour (colour::bg3);
        g.fillRect (area);
        g.setColour (colour::border);
        g.drawHorizontalLine ((int) area.getBottom() - 1, 0.0f, area.getRight());

        // options menu (FL's arrow at the left of the plugin's title)
        drawArrow (g, menuButton(), 0.0f, hoverHeader == 1 ? colour::textPrimary : colour::textSecondary);

        // bypass LED + name + where it is
        const auto led = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ menuButton().getRight() + 9.0f, area.getCentreY() });
        g.setColour (fx.enabled ? colour::ledOn : colour::ledOff);
        g.fillEllipse (led);
        auto text = area.withLeft (led.getRight() + 8.0f).withRight (presetBox().getX() - 6.0f);
        const auto name = layout == &plugui::soundgoodizerLayout() ? juce::String ("Soundgoodizer") : utf8 (effectInfo (kind).name);
        g.setFont (font (type::body, true));
        g.setColour (colour::textPrimary);
        const auto nameWidth = juce::jmin (text.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 2.0f);
        g.drawText (name, text.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
        g.setFont (font (type::small));
        g.setColour (colour::textSecondary);
        g.drawText ("(" + trackName() + ")", text.withTrimmedLeft (6.0f), juce::Justification::centredLeft, true);

        // presets: name, previous, next
        const auto box = presetBox();
        g.setColour (hoverHeader == 2 ? colour::bg5 : colour::bg2);
        g.fillRoundedRectangle (box, radius::sm);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawText (presetName.isNotEmpty() ? presetName : tr ("(no preset)"), box.reduced (8.0f, 0.0f), juce::Justification::centredRight, true);
        drawArrow (g, prevButton(), juce::MathConstants<float>::pi, hoverHeader == 3 ? colour::accent : colour::textSecondary);
        drawArrow (g, nextButton(), 0.0f, hoverHeader == 4 ? colour::accent : colour::textSecondary);
    }

    static void drawArrow (juce::Graphics& g, juce::Rectangle<float> r, float angle, juce::Colour c)
    {
        juce::Path p;
        p.addTriangle (-3.0f, -5.0f, -3.0f, 5.0f, 4.0f, 0.0f);
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (r.getCentreX(), r.getCentreY()));
        g.setColour (c);
        g.fillPath (p);
    }

    juce::Colour arcColour (const EffectSlot& fx, int colourIndex = -1) const
    {
        if (! fx.enabled)
            return theme::colour::textDisabled;
        return colourIndex >= 0 ? peqview::bandColour (colourIndex) : theme::colour::knobArc;
    }

    void caption (juce::Graphics& g, const LayoutItem& item, juce::Rectangle<float> r, juce::Justification j = juce::Justification::centred)
    {
        using namespace theme;
        if (item.label == nullptr || *item.label == 0)
            return;
        g.setColour (hasStyle (item, plugui::dim) ? colour::textDisabled : colour::textSecondary);
        g.setFont (font (juce::jmax (8.0f, 9.5f * scale()), true));
        g.drawFittedText (caps (tr (item.label)), r.toNearestInt(), j, 1, 0.7f);
    }

    void paintItem (juce::Graphics& g, const LayoutItem& item, int index, const EffectSlot& fx)
    {
        using namespace theme;
        const auto r = rectOf (item);
        const auto s = scale();
        const auto param = paramOf (item);
        const auto value = valueOf (param);
        const auto hot = index == hoverItem || index == dragItem;

        switch (item.type)
        {
            case Item::section:
            {
                g.setColour (hasStyle (item, plugui::raised) ? colour::bg3 : colour::bg2);
                g.fillRoundedRectangle (r, radius::md * s);
                if (item.label != nullptr && *item.label != 0 && ! hasStyle (item, plugui::boxed))
                {
                    auto head = r.withHeight (16.0f * s);
                    g.setColour (colour::bg3);
                    g.fillRoundedRectangle (head, radius::md * s);
                    g.fillRect (head.withTrimmedTop (head.getHeight() * 0.5f));
                    g.setColour (colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, 10.5f * s), true));
                    g.drawFittedText (caps (tr (item.label)), head.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                }
                break;
            }

            case Item::label:
                if (hasStyle (item, plugui::dim) && ! hasStyle (item, plugui::boxed) && item.h > 30.0f && item.label != nullptr)
                {
                    // the plugin's name, big and quiet (FL's logos are not reproduced)
                    g.setColour (colour::bg4);
                    g.setFont (font (r.getHeight() * 0.62f, true));
                    g.drawFittedText (item.label, r.toNearestInt(),
                                      hasStyle (item, plugui::alignLeft) ? juce::Justification::centredLeft : juce::Justification::centredRight, 1, 0.8f);
                    break;
                }
                if (hasStyle (item, plugui::boxed))
                {
                    g.setColour (colour::bg3);
                    g.fillRoundedRectangle (r, radius::sm * s);
                }
                caption (g, item, r, hasStyle (item, plugui::captionLeft | plugui::alignLeft) ? juce::Justification::centredLeft : juce::Justification::centred);
                break;

            case Item::value:
                if (hasStyle (item, plugui::boxed)) // a read-out plate (FL's value boxes)
                {
                    g.setColour (colour::bg0);
                    g.fillRoundedRectangle (r, radius::sm * s);
                }
                g.setColour (hot ? colour::accent : (fx.enabled ? colour::textPrimary : colour::textDisabled));
                g.setFont (font (juce::jmax (8.0f, 11.0f * s)));
                g.drawFittedText (valueText (param, value), r.toNearestInt(),
                                  hasStyle (item, plugui::alignLeft) ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.7f);
                break;

            case Item::knob:
            {
                const auto circle = knobCircle (item);
                drawKnob (g, circle, value, hasStyle (item, plugui::bipolar), arcColour (fx, item.colour), hot && item.colour < 0);
                if (circle.getWidth() > 56.0f) // big knobs: a body that fills the ring (the theme's body stops at 22 px)
                {
                    const auto size = circle.getWidth() - 2.0f;
                    const auto thickness = juce::jlimit (2.0f, 4.0f, size * 0.09f);
                    const auto radius = size * 0.5f - thickness * 2.2f;
                    const auto centre = circle.getCentre();
                    const auto body = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
                    g.setGradientFill (juce::ColourGradient (hot ? colour::bg6 : colour::bg5, centre.x, body.getY(), colour::bg4, centre.x, body.getBottom(), false));
                    g.fillEllipse (body);
                    g.setColour (colour::borderStrong);
                    g.drawEllipse (body, 1.0f);
                    const auto angle = -juce::MathConstants<float>::pi * 0.75f + juce::MathConstants<float>::pi * 1.5f * juce::jlimit (0.0f, 1.0f, value);
                    const juce::Point<float> dir { std::sin (angle), -std::cos (angle) };
                    g.setColour (hot ? colour::accent : colour::textPrimary);
                    g.drawLine ({ centre + dir * (radius * 0.55f), centre + dir * (radius * 0.88f) }, juce::jmax (2.0f, size * 0.035f));
                }
                auto below = r.withTop (circle.getBottom() + 1.0f * s);
                if (hasStyle (item, plugui::valueAbove))
                {
                    const auto box = r.withHeight (13.0f * s).reduced (juce::jmax (0.0f, (r.getWidth() - 40.0f * s) * 0.5f), 0.0f);
                    g.setColour (colour::bg0);
                    g.fillRoundedRectangle (box, radius::sm);
                    g.setColour (hot ? colour::textPrimary : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
                    g.drawFittedText (valueText (param, value), box.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                }
                if (! hasStyle (item, plugui::noCaption))
                {
                    caption (g, item, below.withHeight (12.0f * s));
                    if (hasStyle (item, plugui::showValue))
                    {
                        g.setColour (hot ? colour::accent : colour::textPrimary);
                        g.setFont (font (juce::jmax (8.0f, 10.5f * s)));
                        g.drawFittedText (valueText (param, value), below.withTrimmedTop (12.0f * s).withHeight (13.0f * s).toNearestInt(),
                                          juce::Justification::centred, 1, 0.7f);
                    }
                }
                break;
            }

            case Item::hslider:
            case Item::vslider:
            {
                const auto vertical = item.type == Item::vslider;
                auto area = r;
                if (item.label != nullptr && ! hasStyle (item, plugui::noCaption))
                {
                    if (hasStyle (item, plugui::captionLeft))
                        caption (g, item, area.removeFromLeft (area.getWidth() * 0.4f), juce::Justification::centredLeft);
                    else if (vertical)
                        caption (g, item, area.removeFromBottom (13.0f * s).expanded (18.0f * s, 0.0f));
                    else
                        caption (g, item, area.removeFromBottom (12.0f * s));
                }
                const auto thick = juce::jmax (3.0f, 4.0f * s);
                const auto trackRect = vertical ? area.withSizeKeepingCentre (thick, area.getHeight() - 10.0f * s)
                                                : area.withSizeKeepingCentre (area.getWidth() - 8.0f * s, thick);
                g.setColour (colour::bg0);
                g.fillRoundedRectangle (trackRect, thick * 0.5f);
                const auto bip = hasStyle (item, plugui::bipolar);
                juce::Rectangle<float> fill;
                juce::Point<float> thumb;
                if (vertical)
                {
                    const auto y = trackRect.getBottom() - trackRect.getHeight() * value;
                    const auto from = bip ? trackRect.getCentreY() : trackRect.getBottom();
                    fill = { trackRect.getX(), juce::jmin (y, from), trackRect.getWidth(), std::abs (from - y) };
                    thumb = { trackRect.getCentreX(), y };
                }
                else
                {
                    const auto x = trackRect.getX() + trackRect.getWidth() * value;
                    const auto from = bip ? trackRect.getCentreX() : trackRect.getX();
                    fill = { juce::jmin (x, from), trackRect.getY(), std::abs (from - x), trackRect.getHeight() };
                    thumb = { x, trackRect.getCentreY() };
                }
                g.setColour (hot && item.colour < 0 ? colour::accent : arcColour (fx, item.colour));
                g.fillRoundedRectangle (fill, thick * 0.5f);
                const auto thumbRect = vertical ? juce::Rectangle<float> (juce::jmin (area.getWidth(), 20.0f * s), 9.0f * s).withCentre (thumb)
                                                : juce::Rectangle<float> (9.0f * s, juce::jmin (area.getHeight(), 16.0f * s)).withCentre (thumb);
                g.setColour (hot ? colour::bg6 : colour::bg5);
                g.fillRoundedRectangle (thumbRect, radius::sm);
                g.setColour (hot ? colour::accent : colour::textSecondary);
                if (vertical)
                    g.drawHorizontalLine ((int) thumb.y, thumbRect.getX() + 3.0f, thumbRect.getRight() - 3.0f);
                else
                    g.drawVerticalLine ((int) thumb.x, thumbRect.getY() + 3.0f, thumbRect.getBottom() - 3.0f);
                break;
            }

            case Item::radio:
            {
                const auto labels = plugui::optionLabels (item);
                const auto chosen = plugui::optionAt (item, value);
                const auto texts = optionTexts (item);
                if (item.columns > 0) // a grid of named cells (Gross Beat's slots)
                {
                    const auto accent = item.colour >= 0 ? colour::info : colour::accent;
                    g.setFont (font (juce::jmax (8.0f, 10.0f * s)));
                    for (int o = 0; o < (int) texts.size(); ++o)
                    {
                        const auto cell = gridCell (item, o, (int) texts.size()).reduced (0.5f);
                        g.setColour (o == chosen ? accent.withAlpha (0.22f) : ((o / item.columns) % 2 == 0 ? colour::bg0 : colour::bg1));
                        g.fillRect (cell);
                        if (o == chosen)
                        {
                            g.setColour (fx.enabled ? accent : colour::textSecondary);
                            g.drawRect (cell, 1.0f);
                        }
                        g.setColour (o == chosen ? colour::textPrimary : colour::textSecondary);
                        if (! drawWaveGlyph (g, cell.reduced (cell.getWidth() * 0.25f, cell.getHeight() * 0.28f), juce::String::fromUTF8 (labels[(std::size_t) o].c_str())))
                            g.drawFittedText (texts[(std::size_t) o], cell.reduced (2.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
                    }
                    break;
                }
                const auto rowH = r.getHeight() / (float) juce::jmax (1, (int) labels.size());
                for (int o = 0; o < (int) labels.size(); ++o)
                {
                    const auto row = juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) o, r.getWidth(), rowH);
                    const auto dot = juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre ({ row.getX() + 5.0f * s, row.getCentreY() });
                    g.setColour (o == chosen ? (fx.enabled ? colour::accent : colour::textSecondary) : colour::ledOff);
                    g.fillEllipse (dot);
                    g.setColour (o == chosen ? colour::textPrimary : (hot ? colour::textSecondary : colour::textDisabled));
                    g.setFont (font (juce::jmax (8.0f, 9.5f * s)));
                    g.drawFittedText (texts[(std::size_t) o], row.withTrimmedLeft (12.0f * s).toNearestInt(),
                                      juce::Justification::centredLeft, 1, 0.7f);
                }
                break;
            }

            case Item::tabs:
            {
                const auto labels = plugui::optionLabels (item);
                const auto texts = optionTexts (item);
                const auto chosen = hasStyle (item, plugui::pageTabs) ? (item.steps > 0 ? (page / item.steps) % juce::jmax (1, (int) labels.size()) : page)
                                                                      : plugui::optionAt (item, value);
                const auto vertical = isVertical (item);
                const auto count = (float) juce::jmax (1, (int) labels.size());
                const auto w = vertical ? r.getWidth() : r.getWidth() / count;
                const auto h = vertical ? r.getHeight() / count : r.getHeight();
                g.setColour (colour::bg0);
                g.fillRoundedRectangle (r, radius::sm);
                for (int o = 0; o < (int) labels.size(); ++o)
                {
                    const auto cell = (vertical ? juce::Rectangle<float> (r.getX(), r.getY() + h * (float) o, w, h)
                                                : juce::Rectangle<float> (r.getX() + w * (float) o, r.getY(), w, h)).reduced (1.0f);
                    if (vertical) // a band selector (Maximus): the band's colour on the left edge
                    {
                        g.setColour (peqview::bandColour (o).withAlpha (o == chosen ? 1.0f : 0.45f));
                        g.fillRect (cell.withWidth (juce::jmax (2.0f, 3.0f * s)));
                    }
                    if (o == chosen)
                    {
                        g.setColour (colour::bg6);
                        g.fillRoundedRectangle (vertical ? cell.withTrimmedLeft (juce::jmax (2.0f, 3.0f * s)) : cell, radius::sm);
                        g.setColour (colour::accent);
                        if (! vertical)
                            g.fillRect (cell.withTop (cell.getBottom() - juce::jmax (1.5f, 2.0f * s)).reduced (3.0f * s, 0.0f));
                    }
                    g.setColour (o == chosen ? colour::textPrimary : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
                    g.drawFittedText (caps (texts[(std::size_t) o]), cell.toNearestInt(),
                                      juce::Justification::centred, 1, 0.7f);
                }
                break;
            }

            case Item::toggle:
            {
                const auto on = (value >= 0.5f) != hasStyle (item, plugui::inverted);
                if (hasStyle (item, plugui::boxed)) // a small lit button with its letter (FL's T / L buttons)
                {
                    g.setColour (on ? (fx.enabled ? colour::accent : colour::textSecondary) : (hot ? colour::bg6 : colour::bg5));
                    g.fillRoundedRectangle (r, radius::sm);
                    g.setColour (on ? colour::onAccent : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, r.getHeight() * 0.8f), true));
                    if (item.label != nullptr)
                        g.drawText (caps (tr (item.label)), r, juce::Justification::centred, false);
                    break;
                }
                const auto box = juce::Rectangle<float> (16.0f * s, 9.0f * s).withCentre ({ r.getX() + 9.0f * s, r.getCentreY() });
                g.setColour (on ? (fx.enabled ? colour::accent : colour::textSecondary) : colour::bg0);
                g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
                g.setColour (on ? colour::onAccent : colour::bg5);
                const auto knobD = box.getHeight() - 2.0f;
                g.fillEllipse (juce::Rectangle<float> (knobD, knobD).withCentre ({ on ? box.getRight() - knobD * 0.5f - 1.0f : box.getX() + knobD * 0.5f + 1.0f,
                                                                                   box.getCentreY() }));
                auto text = r.withTrimmedLeft (21.0f * s);
                g.setColour (on ? colour::textPrimary : (hot ? colour::textSecondary : colour::textDisabled));
                g.setFont (font (juce::jmax (8.0f, 9.5f * s), true));
                if (item.label != nullptr)
                    g.drawFittedText (caps (tr (item.label)), text.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
                break;
            }

            case Item::stepper:
            {
                auto box = r;
                if (item.label != nullptr && ! hasStyle (item, plugui::noCaption))
                    caption (g, item, box.removeFromTop (12.0f * s));
                g.setColour (hot ? colour::bg5 : colour::bg0);
                g.fillRoundedRectangle (box, radius::sm);
                auto arrows = box.removeFromRight (juce::jmin (12.0f * s, box.getWidth() * 0.4f));
                g.setColour (colour::textSecondary);
                drawSmallArrow (g, arrows.removeFromTop (arrows.getHeight() * 0.5f), true);
                drawSmallArrow (g, arrows, false);
                g.setColour (colour::textPrimary);
                g.setFont (font (juce::jmax (8.0f, 11.0f * s), true));
                g.drawFittedText (valueText (param, value), box.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                break;
            }

            case Item::display:
                paintDisplay (g, item, r, fx);
                break;

            case Item::menu:
            {
                if (kind == EffectKind::fruitySend) // the destination insert (FL's tinted box; "…" = none)
                {
                    const auto target = flbasic::sendTrack (value);
                    g.setColour ((target > 0 ? colour::accent : colour::accentMuted).withAlpha (hot ? 1.0f : 0.85f));
                    g.fillRoundedRectangle (r, radius::sm * s);
                    g.setColour (target > 0 ? colour::onAccent : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, 9.0f * s), true));
                    g.drawFittedText (target > 0 ? insertName (target) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")),
                                      r.reduced (2.0f * s, 0.0f).toNearestInt(), juce::Justification::centred, 2, 0.6f);
                    break;
                }
                const auto c = item.colour >= 0 ? peqview::bandColour (item.colour) : colour::bg5;
                const auto chosen = plugui::optionAt (item, value);
                const auto off = chosen == 0 && kind == EffectKind::parametricEq;
                g.setColour ((fx.enabled && ! off ? c : c.withSaturation (0.0f).darker (0.6f)).withAlpha (hot ? 1.0f : 0.85f));
                g.fillRoundedRectangle (r, radius::sm * s);
                g.setColour (colour::bg0);
                if (kind == EffectKind::parametricEq)
                    drawBandShape (g, r.reduced (2.0f * s, 4.0f * s), chosen);
                else
                {
                    g.setFont (font (juce::jmax (8.0f, 9.0f * s), true));
                    const auto labels = plugui::optionLabels (item);
                    if (chosen < (int) labels.size())
                        g.drawFittedText (tr (juce::String::fromUTF8 (labels[(std::size_t) chosen].c_str())), r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                }
                break;
            }
        }
    }

    // a small drawing of an EQ band's response (peak, shelves, cuts, band-pass, notch)
    static void drawBandShape (juce::Graphics& g, juce::Rectangle<float> r, int type)
    {
        juce::Path p;
        const auto x0 = r.getX(), x1 = r.getRight(), xm = r.getCentreX(), y0 = r.getY(), y1 = r.getBottom(), ym = r.getCentreY();
        switch (type)
        {
            case 1: p.startNewSubPath (x0, ym); p.quadraticTo (xm, y0 - (y1 - y0) * 0.3f, x1, ym); break;            // peak
            case 2: p.startNewSubPath (x0, y0); p.lineTo (xm - 2, y0); p.lineTo (xm + 2, ym); p.lineTo (x1, ym); break; // low shelf
            case 3: p.startNewSubPath (x0, ym); p.lineTo (xm - 2, ym); p.lineTo (xm + 2, y0); p.lineTo (x1, y0); break; // high shelf
            case 4: p.startNewSubPath (x0 + 2, y1); p.quadraticTo (xm - 1, y0, x1, y0); break;                          // low cut
            case 5: p.startNewSubPath (x0, y0); p.quadraticTo (xm + 1, y0, x1 - 2, y1); break;                          // high cut
            case 6: p.startNewSubPath (x0, y1); p.quadraticTo (xm, y0 - (y1 - y0) * 0.6f, x1, y1); break;                // band-pass
            case 7: p.startNewSubPath (x0, y0); p.quadraticTo (xm, y1 + (y1 - y0) * 0.6f, x1, y0); break;                // notch
            default: p.startNewSubPath (x0, ym); p.lineTo (x1, ym); break;                                             // off
        }
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // a small drawing of an LFO / oscillator wave named in English (false: not a wave name)
    static bool drawWaveGlyph (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& name)
    {
        if (name != "Sine" && name != "Triangle" && name != "Square" && name != "Saw")
            return false;
        juce::Path p;
        constexpr int n = 32;
        for (int i = 0; i <= n; ++i)
        {
            const auto t = (float) i / (float) n;
            const auto y = name == "Sine" ? std::sin (t * juce::MathConstants<float>::twoPi)
                           : name == "Triangle" ? (t < 0.25f ? 4.0f * t : (t < 0.75f ? 2.0f - 4.0f * t : 4.0f * t - 4.0f))
                           : name == "Square" ? (t < 0.5f ? 1.0f : -1.0f)
                                              : 2.0f * t - 1.0f;
            const juce::Point<float> pt { r.getX() + r.getWidth() * t, r.getCentreY() - r.getHeight() * 0.5f * y };
            if (i == 0)
                p.startNewSubPath (pt);
            else
                p.lineTo (pt);
        }
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return true;
    }

    static void drawSmallArrow (juce::Graphics& g, juce::Rectangle<float> r, bool up)
    {
        const auto c = r.getCentre();
        const auto w = juce::jmin (r.getWidth(), r.getHeight() * 2.0f) * 0.32f;
        juce::Path p;
        if (up)
            p.addTriangle (c.x - w, c.y + w * 0.5f, c.x + w, c.y + w * 0.5f, c.x, c.y - w * 0.5f);
        else
            p.addTriangle (c.x - w, c.y - w * 0.5f, c.x + w, c.y - w * 0.5f, c.x, c.y + w * 0.5f);
        g.fillPath (p);
    }

    // --- displays (graphs drawn from the parameters; FL's live analysers are not reproduced) -------------

    float param01 (const char* name) const { return valueOf (plugui::paramIndex (kind, name)); }

    // LYNX Tune's live display: the read-out on the left (note, cents, Hz, target, correction meter), the last 8 s of
    // sung (white) and corrected (accent) pitch on the right over the key's note lines
    void paintPitchDisplay (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        auto noteText = [] (int n) { return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1); };

        std::vector<PitchPoint> history = pitchHistory;
        if (demoDisplays && history.empty()) // a sung phrase with vibrato and scoops, corrected toward the scale
            for (std::size_t i = 0; i < pitchHistoryLength; ++i)
            {
                const auto t = (float) i / 30.0f;
                const auto phrase = std::fmod (t, 2.7f);
                const auto base = t < 2.7f ? 64.0f : t < 5.4f ? 67.0f : 69.0f;
                const auto sung = phrase < 0.15f ? 0.0f : base - 0.6f * std::exp (-phrase * 5.0f) + 0.35f * std::sin (t * 20.0f) * juce::jmin (1.0f, phrase) + 0.18f;
                const auto shift = sung > 0.0f ? (base - sung) * (1.0f - std::exp (-phrase * 12.0f)) : 0.0f;
                history.push_back ({ sung, sung > 0.0f ? base : 0.0f, shift });
            }

        const auto last = history.empty() ? PitchPoint { 0.0f, 0.0f, 0.0f } : history.back();
        const auto voiced = last.sung > 0.5f;
        const auto ref = lynxtune::referenceHz (fx.params[(std::size_t) lynxtune::reference]);

        // --- read-out
        auto left = r.removeFromLeft (juce::jmin (176.0f * s, r.getWidth() * 0.34f)).reduced (8.0f * s, 6.0f * s);
        const auto noteArea = left.removeFromTop (left.getHeight() * 0.42f);
        g.setFont (font (juce::jmax (12.0f, 34.0f * s), true));
        g.setColour (voiced ? colour::textPrimary : colour::textDisabled);
        const auto nearest = (int) std::lround (last.sung);
        g.drawText (voiced ? noteText (nearest) : juce::String ("--"), noteArea.withTrimmedRight (noteArea.getWidth() * 0.4f), juce::Justification::centredLeft);
        g.setFont (font (juce::jmax (8.0f, 12.0f * s)));
        g.setColour (colour::textSecondary);
        if (voiced)
        {
            const auto cents = (int) std::lround ((last.sung - (float) nearest) * 100.0f);
            const auto hzNow = ref * std::exp2 ((last.sung - 69.0f) / 12.0f);
            const auto side = noteArea.withTrimmedLeft (noteArea.getWidth() * 0.6f);
            g.drawText ((cents > 0 ? "+" : "") + juce::String (cents) + " c", side.withHeight (side.getHeight() * 0.5f), juce::Justification::bottomLeft);
            g.drawText (juce::String (hzNow, 1) + " Hz", side.withTrimmedTop (side.getHeight() * 0.5f), juce::Justification::topLeft);
        }
        auto row = [&] (const juce::String& caption, const juce::String& value, juce::Colour c) {
            auto line = left.removeFromTop (juce::jmax (12.0f, 18.0f * s));
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (7.0f, 10.0f * s), true));
            g.drawText (caption, line, juce::Justification::centredLeft);
            g.setColour (c);
            g.setFont (font (juce::jmax (8.0f, 12.0f * s), true));
            g.drawText (value, line, juce::Justification::centredRight);
        };
        row (caps (tr ("Target")), voiced && last.target > 0.5f ? noteText ((int) std::lround (last.target)) : juce::String ("--"), colour::accent);
        const auto corrCents = (int) std::lround (last.shift * 100.0f);
        row (caps (tr ("Correction")), voiced ? (corrCents > 0 ? "+" : "") + juce::String (corrCents) + " c" : juce::String ("--"), colour::textPrimary);
        // the correction meter: ±100 cents from the centre
        auto bar = left.removeFromTop (juce::jmax (6.0f, 9.0f * s)).reduced (0.0f, 1.0f);
        g.setColour (colour::bg3);
        g.fillRoundedRectangle (bar, radius::sm);
        g.setColour (colour::gridBar);
        g.drawVerticalLine ((int) bar.getCentreX(), bar.getY() - 2.0f, bar.getBottom() + 2.0f);
        if (voiced)
        {
            const auto amount = juce::jlimit (-1.0f, 1.0f, last.shift);
            const auto x0 = bar.getCentreX(), x1 = bar.getCentreX() + bar.getWidth() * 0.5f * amount;
            g.setColour ((fx.enabled ? colour::accent : colour::textDisabled).withAlpha (0.9f));
            g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (x0, x1), bar.getY(), std::abs (x1 - x0) + 1.0f, bar.getHeight()), radius::sm);
        }

        // --- graph
        auto graph = r.reduced (6.0f * s, 6.0f * s);
        g.setColour (colour::bg1);
        g.fillRoundedRectangle (graph, radius::sm * s);
        // vertical range: ±7 semitones around the recent pitch (follows the voice)
        float centre = 0.0f;
        int count = 0;
        for (auto i = history.size() > 60 ? history.size() - 60 : 0; i < history.size(); ++i)
            if (history[i].sung > 0.5f) { centre += history[i].sung; ++count; }
        centre = count > 0 ? std::round (centre / (float) count) : (displayCentre > 0.0f ? displayCentre : 60.0f);
        displayCentre = displayCentre <= 0.0f ? centre : displayCentre + 0.15f * (centre - displayCentre);
        const auto span = 7.5f;
        auto yFor = [&] (float note) { return graph.getCentreY() - (note - displayCentre) / span * graph.getHeight() * 0.5f; };
        // note lanes: the scale's notes brighter, C labelled
        const auto mask = vocal::scaleMask ((vocal::Scale) lynxtune::scaleIndex (fx.params[(std::size_t) lynxtune::scale]),
                                            lynxtune::keyIndex (fx.params[(std::size_t) lynxtune::key]), 0xfff);
        g.saveState();
        g.reduceClipRegion (graph.toNearestInt());
        g.setFont (font (juce::jmax (7.0f, 9.0f * s)));
        for (int n = (int) std::floor (displayCentre - span) - 1; n <= (int) std::ceil (displayCentre + span) + 1; ++n)
        {
            const auto inScale = ((mask >> (((n % 12) + 12) % 12)) & 1) != 0;
            const auto on = fx.params[(std::size_t) (lynxtune::noteC + ((n % 12) + 12) % 12)] >= 0.5f;
            const auto y = yFor ((float) n);
            g.setColour (inScale && on ? colour::gridBar : colour::gridStep);
            g.drawHorizontalLine ((int) y, graph.getX(), graph.getRight());
            if (inScale && on)
            {
                g.setColour (colour::textDisabled);
                g.drawText (noteText (n), juce::Rectangle<float> (graph.getX() + 3.0f, y - 7.0f, 30.0f * s, 12.0f), juce::Justification::centredLeft);
            }
        }
        // the curves, newest on the right
        const auto stepX = graph.getWidth() / (float) (pitchHistoryLength - 1);
        const auto x0 = graph.getRight() - stepX * (float) (history.size() - 1);
        for (int which = 0; which < 2; ++which)
        {
            juce::Path p;
            bool open = false;
            for (std::size_t i = 0; i < history.size(); ++i)
            {
                const auto& h = history[i];
                if (h.sung <= 0.5f) { open = false; continue; }
                const auto x = x0 + stepX * (float) i;
                const auto y = yFor (which == 0 ? h.sung : h.sung + h.shift);
                if (! open) p.startNewSubPath (x, y);
                else p.lineTo (x, y);
                open = true;
            }
            if (which == 0)
            {
                g.setColour (colour::textSecondary.withAlpha (0.8f));
                g.strokePath (p, juce::PathStrokeType (1.2f * s));
            }
            else
            {
                g.setColour (colour::accent.withAlpha (0.25f));
                g.strokePath (p, juce::PathStrokeType (5.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (fx.enabled ? colour::accent : colour::textDisabled);
                g.strokePath (p, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        g.restoreState();
        if (history.empty() || std::none_of (history.begin(), history.end(), [] (const PitchPoint& h) { return h.sung > 0.5f; }))
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 12.0f * s)));
            g.drawText (tr ("Play a voice or an instrument through the insert to see its pitch"), graph, juce::Justification::centred, true);
        }
        // legend
        g.setFont (font (juce::jmax (7.0f, 9.0f * s)));
        g.setColour (colour::textSecondary);
        g.drawText (tr ("input"), graph.removeFromTop (14.0f * s).removeFromRight (120.0f * s).withTrimmedRight (6.0f), juce::Justification::centredLeft);
        g.setColour (colour::accent);
        g.drawText (tr ("corrected"), graph.withHeight (14.0f * s).withY (graph.getY() - 14.0f * s).removeFromRight (64.0f * s), juce::Justification::centredLeft);
    }

    // the live meter `output` of this slot's effect (published by the audio thread; 0 when not playing)
    float liveMeter (int output) const
    {
        const auto* times = engine.getProcessTimes();
        const auto key = (std::size_t) (track * maxEffectSlots + slot) * ProcessTimes::meterOutputs + (std::size_t) juce::jlimit (0, ProcessTimes::meterOutputs - 1, output);
        return times != nullptr && key < times->effectMeters.size() ? times->effectMeters[key].load (std::memory_order_relaxed) : 0.0f;
    }

    // the captions of a radio's options (Gross Beat: the slot names saved in the state)
    std::vector<juce::String> optionTexts (const LayoutItem& item) const
    {
        std::vector<juce::String> out;
        const auto labels = plugui::optionLabels (item);
        // the whole list is one translation ("Mono|Stereo|Ping pong|Off"); single words are translated alone
        const auto whole = juce::StringArray::fromTokens (tr (juce::String::fromUTF8 (item.options != nullptr ? item.options : "")), "|", "");
        for (std::size_t o = 0; o < labels.size(); ++o)
            out.push_back (whole.size() == (int) labels.size() ? whole[(int) o] : tr (juce::String::fromUTF8 (labels[o].c_str())));
        if (kind == EffectKind::grossBeat && item.columns > 0)
            if (const auto* fx = current())
            {
                const auto names = grossbeat::slotNames (fx->state, std::strcmp (item.param, "Volume slot") == 0);
                for (std::size_t i = 0; i < names.size() && i < out.size(); ++i)
                    if (! names[i].empty())
                        out[i] = juce::String::fromUTF8 (names[i].c_str());
            }
        return out;
    }

    // the cell of option `o` in a radio grid
    juce::Rectangle<float> gridCell (const LayoutItem& item, int o, int count) const
    {
        const auto r = rectOf (item);
        const auto rows = (count + item.columns - 1) / item.columns;
        const auto w = r.getWidth() / (float) item.columns, h = r.getHeight() / (float) juce::jmax (1, rows);
        return { r.getX() + w * (float) (o % item.columns), r.getY() + h * (float) (o / item.columns), w, h };
    }

    void paintDisplay (juce::Graphics& g, const LayoutItem& item, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        const juce::String id (item.display != nullptr ? item.display : "");
        const auto curveColour = fx.enabled ? colour::accent : colour::textDisabled;
        const auto inner = r.reduced (6.0f * s);
        if (dynui::paint (g, id, r, s, fx, [this] (int k) { return liveMeter (k); }, dynHistory)) // the dynamics suite
            return;

        auto grid = [&] (int columns, int rows) {
            g.setColour (colour::gridBeat);
            for (int i = 1; i < columns; ++i)
                g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * (float) i / (float) columns), inner.getY(), inner.getBottom());
            for (int i = 1; i < rows; ++i)
                g.drawHorizontalLine ((int) (inner.getY() + inner.getHeight() * (float) i / (float) rows), inner.getX(), inner.getRight());
        };

        auto strokeCurve = [&] (const std::function<float (float)>& f, juce::Rectangle<float> area, bool fillBelow) {
            juce::Path p;
            const auto steps = juce::jmax (16, (int) area.getWidth() / 2);
            for (int i = 0; i <= steps; ++i)
            {
                const auto x = (float) i / (float) steps;
                const auto y = juce::jlimit (0.0f, 1.0f, f (x));
                const juce::Point<float> pt { area.getX() + area.getWidth() * x, area.getBottom() - area.getHeight() * y };
                if (i == 0)
                    p.startNewSubPath (pt);
                else
                    p.lineTo (pt);
            }
            if (fillBelow)
            {
                auto fill = p;
                fill.lineTo (area.getRight(), area.getBottom());
                fill.lineTo (area.getX(), area.getBottom());
                fill.closeSubPath();
                g.setColour (curveColour.withAlpha (0.14f));
                g.fillPath (fill);
            }
            g.setColour (curveColour);
            g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * s)));
        };

        if (id == "limiter" || id == "compressor" || id == "dynamics")
        {
            // static transfer curve: input level (x, −60…0 dB) to output level (y)
            grid (6, 4);
            const auto threshold01 = id == "limiter" ? param01 ("Ceiling") : param01 (id == "compressor" ? "Threshold" : "Comp. threshold");
            const auto ratio01 = id == "limiter" ? 1.0f : param01 ("Ratio");
            const auto t = juce::jlimit (0.05f, 1.0f, threshold01);
            const auto ratio = 1.0f + ratio01 * 19.0f;
            strokeCurve ([t, ratio] (float x) { return x <= t ? x : t + (x - t) / ratio; }, inner, true);
            g.setColour (colour::gridBar);
            g.drawHorizontalLine ((int) (inner.getBottom() - inner.getHeight() * t), inner.getX(), inner.getRight());
        }
        else if (id == "shaper") // the feedback saturation: drive from the level, knee and symmetry
        {
            grid (4, 4);
            const auto drive = 1.0f + 5.0f * param01 (item.param != nullptr ? item.param : "Distortion");
            const auto knee = 0.5f + 3.0f * param01 ("Hardness");
            const auto bias = (param01 ("Symmetry") - 0.5f) * 0.6f;
            strokeCurve ([drive, knee, bias] (float x) {
                const auto u = (x * 2.0f - 1.0f) * drive + bias;
                const auto y = u / std::pow (1.0f + std::pow (std::abs (u), knee * 2.0f), 1.0f / (knee * 2.0f));
                return 0.5f + 0.5f * y;
            }, inner, false);
        }
        else if (id == "multiband")
        {
            // the three bands between the crossovers
            grid (8, 4);
            const auto low = param01 ("Low crossover"), high = param01 ("High crossover");
            const juce::Colour bandColours[] = { peqview::bandColour (0), peqview::bandColour (2), peqview::bandColour (5) };
            const float edges[] = { 0.0f, juce::jlimit (0.05f, 0.95f, low), juce::jlimit (0.05f, 0.95f, juce::jmax (low + 0.02f, high)), 1.0f };
            for (int b = 0; b < 3; ++b)
            {
                const auto band = juce::Rectangle<float> (inner.getX() + inner.getWidth() * edges[b], inner.getY() + inner.getHeight() * 0.35f,
                                                          inner.getWidth() * (edges[b + 1] - edges[b]), inner.getHeight() * 0.65f);
                g.setColour (bandColours[b].withAlpha (fx.enabled ? 0.18f : 0.06f));
                g.fillRect (band);
                g.setColour (bandColours[b].withAlpha (fx.enabled ? 0.9f : 0.3f));
                g.drawHorizontalLine ((int) band.getY(), band.getX(), band.getRight());
            }
        }
        else if (id == "envelope") // decay shape of a reverb / delay tail
        {
            grid (8, 4);
            const auto decay = 0.15f + param01 (item.param != nullptr ? item.param : "Decay") * 3.0f;
            strokeCurve ([decay] (float x) { return std::exp (-x * 4.0f / decay) * 0.9f; }, inner, true);
        }
        else if (id == "meter") // level meter well (FL's live meters; LYNX shows the levels in the mixer)
        {
            g.setColour (colour::gridBeat);
            for (int i = 1; i < 12; ++i)
                g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 12.0f), r.getX() + 2.0f, r.getRight() - 2.0f);
        }
        else if (id == "clip") // hard / soft clipping at the threshold
        {
            grid (4, 4);
            const auto t = juce::jlimit (0.02f, 1.0f, param01 (item.param != nullptr ? item.param : "Threshold"));
            const auto soft = param01 ("Type") >= 0.5f;
            const auto pre = 1.0f + 30.0f * param01 ("Pre amp") * param01 ("Pre amp");
            const auto post = 0.25f + 0.75f * param01 ("Post gain");
            strokeCurve ([t, soft, pre, post] (float x) {
                const auto u = x * pre;
                return post * (soft ? t * std::tanh (u / t) : juce::jmin (u, t));
            }, inner, false);
        }
        else if (id == "softclip") // linear up to the threshold, then a soft knee into the ceiling
        {
            grid (4, 4);
            const auto t = juce::jlimit (0.05f, 0.98f, 0.3f + 0.7f * param01 ("Threshold"));
            const auto post = 0.4f + 0.6f * param01 ("Post");
            strokeCurve ([t, post] (float x) {
                const auto y = x <= t ? x : t + (1.0f - t) * std::tanh ((x - t) / (1.0f - t));
                return y * post;
            }, inner, false);
        }
        else if (id == "sgamount" || id == "sgpresets")
        {
            paintSoundgoodizer (g, id, r, fx);
            return;
        }
        else if (id == "xy") // a pad: `param` across, `param2` up (drag the point)
        {
            grid (4, 4);
            const auto x = inner.getX() + inner.getWidth() * param01 (item.param);
            const auto y = inner.getBottom() - inner.getHeight() * param01 (item.param2);
            g.setColour (curveColour);
            g.drawEllipse (juce::Rectangle<float> (10.0f * s, 10.0f * s).withCentre ({ x, y }), juce::jmax (1.5f, 2.0f * s));
        }
        else if (id == "voices") // the chorus voices spread across the stereo field (a static picture)
        {
            g.setColour (colour::gridBeat);
            g.drawVerticalLine ((int) inner.getCentreX(), inner.getY(), inner.getBottom());
            const auto spread = 0.2f + 0.8f * param01 ("Movement");
            for (int i = 0; i < 8; ++i)
            {
                const auto t = (float) i / 7.0f;
                const auto x = inner.getCentreX() + (t - 0.5f) * inner.getWidth() * spread;
                g.setColour (curveColour.withAlpha (0.35f + 0.5f * (1.0f - std::abs (t - 0.5f))));
                g.fillRoundedRectangle (juce::Rectangle<float> (3.0f * s, inner.getHeight() * (0.3f + 0.4f * std::abs (std::sin ((float) i * 1.7f)))).withCentre ({ x, inner.getCentreY() }), 1.5f);
            }
        }
        else if (id == "panpad") // pan (x) and volume (y) as a crosshair
        {
            grid (6, 6);
            const auto x = inner.getX() + inner.getWidth() * param01 ("Pan");
            const auto y = inner.getBottom() - inner.getHeight() * param01 ("Volume");
            g.setColour (curveColour);
            g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
            g.fillEllipse (juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre ({ x, y }));
        }
        else if (id == "waveshaper") // the saved shaping curve (FL's point editor is not reproduced)
        {
            grid (8, 8);
            std::vector<std::int16_t> table;
            auto unipolar = false;
            if (const auto st = flshape::parseWaveShaper (fx.state))
            {
                unipolar = st->unipolar;
                table = flshape::shaperTable (st->points, st->unipolar);
            }
            if (table.size() > 4)
                strokeCurve ([&table, unipolar] (float x) {
                    const auto i = juce::jlimit<std::size_t> (1, table.size() - 2, (std::size_t) (1.0f + x * (float) (table.size() - 3)));
                    const auto v = (float) table[i] / 32767.0f;
                    return unipolar ? v : 0.5f + 0.5f * v;
                }, inner, false);
            else
                strokeCurve ([] (float x) { return x; }, inner, false);
        }
        else if (id == "room") // nested walls: the room size and the decay
        {
            const auto size = 0.25f + 0.7f * param01 (item.param != nullptr ? item.param : "Room size");
            const auto c = inner.getCentre();
            for (int i = 0; i < 6; ++i)
            {
                const auto k = size * (1.0f - 0.14f * (float) i);
                const auto box = juce::Rectangle<float> (inner.getWidth() * k, inner.getHeight() * k * 0.8f).withCentre (c);
                g.setColour (curveColour.withAlpha (0.75f - 0.11f * (float) i));
                g.drawRoundedRectangle (box, 4.0f * s, juce::jmax (1.0f, 1.5f * s));
            }
        }
        else if (id == "maxcurve") // Maximus: the selected band's drawn compression curve (input → output, 0.5 = 0 dBFS)
        {
            grid (4, 4);
            const auto band = juce::jlimit (0, 3, page);
            const auto c = fx.enabled ? peqview::bandColour (band) : colour::textDisabled;
            g.setColour (colour::gridBar);
            g.drawLine ({ inner.getBottomLeft(), inner.getTopRight() }, 1.0f);
            g.drawVerticalLine ((int) inner.getCentreX(), inner.getY(), inner.getBottom());
            g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
            const auto pts = maximusfx::curvePoints (fx.state, band);
            if (pts.size() >= 2)
            {
                juce::Path p;
                auto at = [&] (const std::pair<float, float>& q) { return juce::Point<float> (inner.getX() + inner.getWidth() * q.first, inner.getBottom() - inner.getHeight() * q.second); };
                p.startNewSubPath (at (pts.front()));
                for (std::size_t i = 1; i < pts.size(); ++i)
                    p.lineTo (at (pts[i]));
                auto fill = p;
                fill.lineTo (at (pts.back()).withY (inner.getBottom()));
                fill.lineTo (at (pts.front()).withY (inner.getBottom()));
                fill.closeSubPath();
                g.setColour (c.withAlpha (0.14f));
                g.fillPath (fill);
                g.setColour (c);
                g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * s)));
                for (const auto& q : pts)
                    g.fillEllipse (juce::Rectangle<float> (5.0f * s, 5.0f * s).withCentre (at (q)));
            }
            g.setColour (colour::textSecondary);
            g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
            g.drawText (tr ("Out"), inner.withHeight (12.0f * s).toNearestInt(), juce::Justification::topLeft, false);
            g.drawText (tr ("In"), inner.withTop (inner.getBottom() - 12.0f * s).toNearestInt(), juce::Justification::bottomRight, false);
        }
        else if (id == "maxbands") // Maximus: the three bands over 20 Hz…20 kHz with their crossovers and the low cut
        {
            const auto raw = [&] (int p) { return maximusfx::stateParam (fx.state, p).value_or (maximusfx::stateParams()[(std::size_t) p].defaultValue); };
            const auto xOf = [&] (double hz) { return inner.getX() + inner.getWidth() * (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0)); };
            auto lo = 30.0 * std::pow (533.33, raw (0) / 65536.0), hi = 30.0 * std::pow (533.33, raw (2) / 65536.0);
            if (lo > hi)
                std::swap (lo, hi);
            const float edges[4] { inner.getX(), xOf (lo), xOf (hi), inner.getRight() };
            static const char* names[3] { "Low", "Mid", "High" };
            for (int b = 0; b < 3; ++b)
            {
                const auto mode = raw (7 + 13 * b);
                const auto area = juce::Rectangle<float> (edges[b], inner.getY(), edges[b + 1] - edges[b], inner.getHeight());
                const auto c = peqview::bandColour (b);
                g.setColour (c.withAlpha (! fx.enabled || mode >= 2 ? 0.05f : (b == page ? 0.24f : 0.12f)));
                g.fillRect (area);
                g.setColour (b == page ? colour::textPrimary : colour::textSecondary);
                g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
                g.drawFittedText (caps (tr (names[b])) + (mode == 2 ? " · " + tr ("Muted") : mode == 3 ? " · " + tr ("Off") : juce::String()),
                                  area.reduced (2.0f * s).withHeight (14.0f * s).toNearestInt(), juce::Justification::centred, 1, 0.6f);
            }
            g.setColour (colour::textSecondary);
            for (int k = 1; k <= 2; ++k)
                g.drawVerticalLine ((int) edges[k], inner.getY(), inner.getBottom());
            if (raw (6) >= 0) // low cut
            {
                const auto x = xOf (5.0 * std::pow (16.0, raw (6) / 65536.0));
                g.setColour (colour::accent.withAlpha (0.6f));
                g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            }
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
            for (const auto hz : { 100.0, 1000.0, 10000.0 })
                g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (30.0f * s, 12.0f * s).withCentre ({ xOf (hz), inner.getBottom() - 6.0f * s }).toNearestInt(),
                            juce::Justification::centred, false);
        }
        else if (id == "eq")
        {
            paintEq (g, r, fx);
            return;
        }
        else if (id == "grossbeat" || id == "gbinfo" || id == "gbjog")
        {
            paintGrossBeat (g, id, r, fx);
            return;
        }
        else if (id == "eqinfo") // the selected band, or how to use the graph
        {
            g.setColour (colour::bg1);
            g.fillRect (r);
            g.setFont (font (juce::jmax (9.0f, 11.0f * s)));
            if (eqSelected >= 0)
            {
                g.setColour (peqview::bandColour (eqSelected));
                g.drawFittedText (peqview::bandText (fx.params, eqSelected), r.reduced (4.0f * s, 0.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
            }
            else
            {
                g.setColour (colour::textSecondary);
                g.drawFittedText (tr ("Drag a handle (frequency and gain); mouse wheel = Q; right-click = type; double-click = 0 dB"),
                                  r.reduced (4.0f * s, 0.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
            }
            return;
        }
        else if (id == "filter")
        {
            grid (8, 4);
            const auto cutoff = param01 ("Cutoff");
            const auto reso = param01 ("Resonance");
            strokeCurve ([cutoff, reso] (float x) {
                const auto d = (x - cutoff) * 8.0f;
                const auto bump = reso * 0.35f * std::exp (-d * d);
                return 0.6f + bump - (x > cutoff ? (x - cutoff) * 2.2f : 0.0f);
            }, inner, true);
        }
        else if (id == "pitchgraph") // LYNX Tune: detected pitch, target, correction and the scrolling pitch graph
        {
            paintPitchDisplay (g, r, fx);
            return;
        }
        else if (id == "grmeter") // live gain reduction of the mixing suite (0..30 dB, from the right like a VU's GR)
        {
            const auto amount = juce::jlimit (0.0f, 1.0f, liveMeter (item.steps));
            const auto bar = r.reduced (1.5f * s);
            g.setColour (colour::gridBeat);
            for (int db = 3; db < 30; db += 3)
                g.drawVerticalLine ((int) (bar.getRight() - bar.getWidth() * (float) db / 30.0f), bar.getY(), bar.getBottom());
            g.setColour ((fx.enabled ? colour::accent : colour::textDisabled).withAlpha (0.85f));
            g.fillRoundedRectangle (bar.withLeft (bar.getRight() - bar.getWidth() * amount), radius::sm);
            if (item.label != nullptr && *item.label != 0)
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (juce::jmax (7.0f, juce::jmin (10.0f * s, bar.getHeight() * 0.95f)), true));
                g.drawText (caps (tr (item.label)) + "  " + juce::String (amount * 30.0f, 1) + " dB", bar.reduced (3.0f * s, 0.0f),
                            juce::Justification::centredLeft, false);
            }
            return;
        }
        else if (id == "ridgeeq") // the strip's EQ + filters (20 Hz .. 20 kHz, ±20 dB)
        {
            grid (10, 4);
            const auto params = fx.params;
            strokeCurve ([&params] (float x) {
                const auto hz = 20.0 * std::pow (1000.0, (double) x);
                return 0.5f + (float) mixsuite::ridge::responseDb (params, 48000.0, hz) / 40.0f;
            }, inner, false);
        }
        else if (id == "ridgecomp") // compressor's static curve (−60 .. 0 dB in and out)
        {
            grid (6, 4);
            const auto params = fx.params;
            strokeCurve ([&params] (float x) {
                return 1.0f + (float) mixsuite::ridge::compressorCurveDb (params, (double) (x - 1.0f) * 60.0) / 60.0f;
            }, inner, true);
        }
        else if (id == "hushband") // the detector's filter (high-pass or band) at the frequency, 1 .. 20 kHz
        {
            grid (8, 4);
            const auto f0 = mixsuite::hush::hz (param01 ("Frequency"));
            const auto band = param01 ("Band detector") >= 0.5f;
            strokeCurve ([f0, band] (float x) {
                const auto u = 1000.0 * std::pow (20.0, (double) x) / f0;
                const auto u2 = u * u;
                const auto mag = band ? (u / 1.4) / std::sqrt ((1.0 - u2) * (1.0 - u2) + u2 / (1.4 * 1.4))
                                      : u2 / std::sqrt ((1.0 - u2) * (1.0 - u2) + 2.0 * u2);
                return 0.9f + (float) (20.0 * std::log10 (std::max (1e-6, mag))) / 40.0f;
            }, inner, true);
        }
        else if (id == "embercurve") // the style's transfer curve at the drive (x = ±1: the peaks of a −12 dBFS sine)
        {
            grid (4, 4);
            const auto style = mixsuite::ember::styleOf (param01 ("Style"));
            const auto drive = 0.25f * (float) std::pow (10.0, mixsuite::ember::driveDb (param01 ("Drive")) / 20.0);
            const auto norm = 1.0f / juce::jmax (0.05f, std::abs (mixsuite::ember::shape (style, drive)));
            strokeCurve ([style, drive, norm] (float x) {
                return 0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, mixsuite::ember::shape (style, (x * 2.0f - 1.0f) * drive) * norm);
            }, inner, false);
        }
        else if (id == "echotrain" || id == "lattice") // the echoes / taps on a time axis: left up, right down
        {
            grid (8, 2);
            const auto params = fx.params;
            const auto bpm = engine.getSong().bpm > 1.0 ? engine.getSong().bpm : 120.0;
            struct Tick { double ms; double level; bool right; };
            std::vector<Tick> ticks;
            double window = 50.0;
            if (id == "echotrain")
            {
                using namespace echosuite::echoform;
                const auto st = styleOf (params[style]);
                auto tL = sideMs (params, false, bpm), tR = sideMs (params, true, bpm);
                if (st == Style::single || st == Style::quad)
                    tR = tL;
                else if (st == Style::ratio)
                    tR = tL * (0.01 + 0.99 * (double) params[ratio]);
                const auto gain = juce::jlimit (0.0, 1.0, feedbackGain (params[feedback]));
                window = juce::jmax (50.0, 4.5 * juce::jmax (tL, tR));
                if (st == Style::pingPong)
                {
                    auto t = tL, level = 1.0;
                    for (int k = 0; k < 24 && t <= window; ++k)
                    {
                        ticks.push_back ({ t, level, (k % 2) == 1 });
                        t += (k % 2) == 0 ? tR : tL;
                        level *= std::sqrt (gain);
                    }
                }
                else
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto t = c == 0 ? tL : tR;
                        auto level = 1.0;
                        for (int k = 1; k <= 24 && t > 0.0 && t * k <= window; ++k, level *= gain)
                        {
                            if (st == Style::quad)
                                for (int q = 1; q < 4; ++q)
                                    ticks.push_back ({ t * (k - 1 + q / 4.0), 0.5 * level, c == 1 });
                            ticks.push_back ({ t * k, st == Style::quad ? 0.5 * level : level, c == 1 });
                        }
                    }
            }
            else
            {
                using namespace echosuite::lattice;
                const auto n = tapCount (params[taps]);
                double longest = 1.0;
                for (int k = 0; k < n; ++k)
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto t = tapMs (params, k, n, c == 1);
                        longest = juce::jmax (longest, t);
                        ticks.push_back ({ t, std::abs (tapLevel (params, k, n)) * std::sqrt ((double) n), c == 1 });
                    }
                window = longest * 1.15;
            }
            const auto mid = inner.getCentreY();
            g.setColour (colour::gridBar);
            g.drawHorizontalLine ((int) mid, inner.getX(), inner.getRight());
            g.setColour (curveColour);
            for (const auto& t : ticks)
            {
                if (t.level < 0.01)
                    continue;
                const auto x = inner.getX() + inner.getWidth() * (float) (t.ms / window);
                const auto h = inner.getHeight() * 0.48f * (float) juce::jlimit (0.0, 1.0, t.level);
                g.fillRect (juce::Rectangle<float> (x - 1.0f * s, t.right ? mid : mid - h, juce::jmax (1.5f, 2.0f * s), h));
            }
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 9.0f * s), true));
            g.drawText (juce::String (window >= 1000.0 ? juce::String (window / 1000.0, 2) + " s" : juce::String ((int) window) + " ms"),
                        inner.reduced (2.0f * s), juce::Justification::bottomRight);
        }
        else if (paintFxDisplay (g, id, item, inner, fx))
        {
        }
        else // a plain well with the plugin's name (FL's scopes / animations are not reproduced)
        {
            g.setColour (colour::bg2);
            g.setFont (font (juce::jmax (10.0f, juce::jmin (28.0f, r.getHeight() * 0.22f)), true));
            g.drawFittedText (caps (utf8 (effectInfo (kind).name)), r.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }

        if (item.label != nullptr && *item.label != 0)
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 9.0f * s), true));
            g.drawText (caps (tr (item.label)), inner.withHeight (12.0f * s), juce::Justification::topLeft);
        }
    }

#include "ui/PluginEditorsFx.inc"

    // --- Gross Beat: the selected slots' envelopes ------------------------------------------------------

    void paintGrossBeat (juce::Graphics& g, const juce::String& id, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        std::vector<std::uint8_t> flState;
        int ppq = 96;
        std::optional<grossbeat::State> state;
        if (grossbeat::unpack (fx.state, flState, ppq))
            state = grossbeat::parseState (flState.data(), flState.size());
        const auto timeSlot = grossbeat::rawFromParam (0, param01 ("Time slot"));
        const auto volumeSlot = grossbeat::rawFromParam (1, param01 ("Volume slot"));

        if (id == "gbjog") // a dial with the beat positions (FL's scratch indicator)
        {
            const auto c = r.getCentre();
            const auto radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.4f;
            g.setColour (colour::bg3);
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c));
            for (int i = 0; i < 12; ++i)
            {
                const auto a = juce::MathConstants<float>::twoPi * (float) i / 12.0f;
                g.setColour (i == 0 ? (fx.enabled ? colour::accent : colour::textSecondary) : colour::textDisabled);
                g.fillEllipse (juce::Rectangle<float> (4.0f * s, 4.0f * s).withCentre ({ c.x + std::sin (a) * radius * 0.85f, c.y - std::cos (a) * radius * 0.85f }));
            }
            return;
        }

        if (id == "gbinfo")
        {
            juce::String text = tr ("Time: {0}", { "Slot " + juce::String (timeSlot + 1) });
            if (state)
            {
                const auto& slot = state->slots[(std::size_t) timeSlot];
                if (! slot.name.empty())
                    text = tr ("Time: {0}", { juce::String::fromUTF8 (slot.name.c_str()) });
                static const char* const syncs[] = { "off", "1/4 beat", "1/2 beat", "1 beat", "2 beats", "4 beats" };
                const auto sync = [] (int v) { return juce::String (syncs[juce::jlimit (0, 5, v)]); };
                text << "     " << tr ("Hold: {0}", { slot.hold > 0 ? juce::String (slot.hold) : tr ("Off") }) << "   "
                     << tr ("Trig sync: {0}", { sync (slot.triggerSync) }) << "   " << tr ("Pos sync: {0}", { sync (slot.positionSync) });
            }
            g.setColour (colour::textSecondary);
            g.setFont (font (juce::jmax (9.0f, 11.0f * s)));
            g.drawFittedText (text, r.reduced (6.0f * s, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
            return;
        }

        // the editor area: beat grid, then the time slot's envelope (accent) and the volume slot's (blue)
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        const auto inner = r.reduced (8.0f * s);
        g.setColour (colour::gridBeat);
        for (int i = 1; i < 16; ++i)
            g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * (float) i / 16.0f), inner.getY(), inner.getBottom());
        g.setColour (colour::gridBar);
        for (int i = 1; i < 4; ++i)
            g.drawVerticalLine ((int) (inner.getX() + inner.getWidth() * (float) i / 4.0f), inner.getY(), inner.getBottom());
        for (int i = 1; i < 8; ++i)
            g.drawHorizontalLine ((int) (inner.getY() + inner.getHeight() * (float) i / 8.0f), inner.getX(), inner.getRight());
        const auto drawSlot = [&] (const grossbeat::Slot& slot, juce::Colour c, bool time) {
            if (slot.points.empty()) // an empty slot does nothing: time plays straight on, volume stays at full
            {
                g.setColour (c.withAlpha (0.5f));
                if (time)
                    g.drawLine (inner.getX(), inner.getY(), inner.getRight(), inner.getY() + inner.getHeight() * 0.5f, juce::jmax (1.0f, 1.5f * s));
                else
                    g.drawHorizontalLine ((int) inner.getY() + 1, inner.getX(), inner.getRight());
                return;
            }
            double length = 0.0;
            for (const auto& p : slot.points)
                length += p.dx;
            length = juce::jmax (length, 1.0e-6);
            juce::Path path;
            double x = 0.0;
            for (std::size_t i = 0; i < slot.points.size(); ++i)
            {
                x += slot.points[i].dx;
                const juce::Point<float> pt { inner.getX() + inner.getWidth() * (float) (x / length),
                                              inner.getBottom() - inner.getHeight() * juce::jlimit (0.0f, 1.0f, slot.points[i].y) };
                if (i == 0)
                    path.startNewSubPath (pt);
                else
                    path.lineTo (pt);
                g.setColour (c);
                g.fillEllipse (juce::Rectangle<float> (5.0f * s, 5.0f * s).withCentre (pt));
            }
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * s)));
        };
        drawSlot (state ? state->slots[(std::size_t) (grossbeat::slotsPerKind + volumeSlot)] : grossbeat::Slot {}, colour::info.withAlpha (fx.enabled ? 0.9f : 0.3f), false);
        drawSlot (state ? state->slots[(std::size_t) timeSlot] : grossbeat::Slot {}, (fx.enabled ? colour::accent : colour::textDisabled), true);
    }

    // --- the parametric EQ's graph (FL's Parametric EQ 2 arrangement) -----------------------------------

    static constexpr double eqRange = 18.0; // FL's graph spans ±18 dB

    // rows above the graph (octaves, band names), frequencies under it, dB scale at its right
    juce::Rectangle<float> eqGraph (juce::Rectangle<float> r) const
    {
        const auto s = scale();
        return r.withTrimmedTop (29.0f * s).withTrimmedBottom (16.0f * s).withTrimmedRight (22.0f * s);
    }

    void paintEq (juce::Graphics& g, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        const auto graph = eqGraph (r);
        g.setFont (font (juce::jmax (7.5f, 9.0f * s), true));

        // octave marks (C1…C9) and the usual band names, on plates like FL's
        const auto octaveRow = juce::Rectangle<float> (graph.getX(), r.getY() + 1.0f * s, graph.getWidth(), 11.0f * s);
        for (int o = 1; o <= 9; ++o)
        {
            const auto x = peqview::xForHz (graph, 32.703 * std::pow (2.0, o - 1));
            const auto next = peqview::xForHz (graph, 32.703 * std::pow (2.0, o));
            const auto plate = juce::Rectangle<float> (x, octaveRow.getY(), juce::jmin (next, graph.getRight()) - x - 1.0f, octaveRow.getHeight());
            g.setColour (colour::bg2);
            g.fillRect (plate);
            g.setColour (colour::textDisabled);
            g.drawText ("C" + juce::String (o), plate.withTrimmedLeft (2.0f), juce::Justification::centredLeft, false);
        }
        struct Range { double lo, hi; const char* name; };
        static const Range ranges[] = { { 20, 60, TR_NOOP ("Sub") }, { 60, 250, TR_NOOP ("Bass") }, { 250, 500, TR_NOOP ("Low mid") },
                                        { 500, 2000, TR_NOOP ("Mid") }, { 2000, 4000, TR_NOOP ("High mid") }, { 4000, 6000, TR_NOOP ("Prs") },
                                        { 6000, 20000, TR_NOOP ("Treble") } };
        for (const auto& range : ranges)
        {
            const auto x0 = peqview::xForHz (graph, range.lo), x1 = peqview::xForHz (graph, range.hi);
            const auto plate = juce::Rectangle<float> (x0, r.getY() + 15.0f * s, x1 - x0 - 1.0f, 11.0f * s);
            g.setColour (colour::bg3);
            g.fillRect (plate);
            g.setColour (colour::textSecondary);
            g.drawFittedText (caps (tr (range.name)), plate.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }

        drawParametricEqCurve (g, graph, fx.params, juce::jmax (8000.0, engine.getTransport().sampleRate.load()), fx.enabled, eqSelected, eqRange, false);

        // frequencies under the graph, dB at its right
        g.setColour (colour::textDisabled);
        for (auto [hz, text] : { std::pair { 20.0, "20" }, { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
                                 { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } })
            g.drawText (text, juce::Rectangle<float> (peqview::xForHz (graph, hz) + 2.0f, graph.getBottom() + 2.0f * s, 40.0f * s, 11.0f * s),
                        juce::Justification::centredLeft, false);
        for (int db = -18; db <= 18; db += 6)
            g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<float> (graph.getRight() + 1.0f, peqview::yForDb (graph, db, eqRange) - 6.0f * s, 21.0f * s, 12.0f * s),
                        juce::Justification::centredRight, false);

        // the band handles
        for (int b = 0; b < peq::bands; ++b)
        {
            if (peq::type (fx.params[(std::size_t) peq::param (b, 0)]) == peq::BandType::off)
                continue;
            const auto centre = peqview::handlePosition (graph, fx.params, b, eqRange);
            const auto d = juce::jmax (12.0f, 16.0f * s);
            const auto h = juce::Rectangle<float> (d, d).withCentre (centre);
            const auto c = peqview::bandColour (b);
            if (b == eqSelected)
            {
                g.setColour (c.withAlpha (0.25f));
                g.fillEllipse (h.expanded (4.0f));
            }
            g.setColour (colour::bg0.withAlpha (0.7f));
            g.fillEllipse (h);
            g.setColour (c.withAlpha (fx.enabled ? 1.0f : 0.4f));
            g.drawEllipse (h.reduced (1.0f), juce::jmax (1.5f, 2.0f * s));
            g.setFont (font (juce::jmax (8.0f, 9.5f * s), true));
            g.drawText (juce::String (b + 1), h, juce::Justification::centred);
        }
    }

    void setParams (const std::vector<std::pair<int, float>>& changes)
    {
        if (current() == nullptr)
            return;
        engine.editSong ([t = track, sl = slot, changes] (Song& song) {
            auto& params = song.mixer[(std::size_t) t].effects[(std::size_t) sl].params;
            for (auto [p, v] : changes)
                if (p >= 0 && p < maxEffectParams)
                    params[(std::size_t) p] = juce::jlimit (0.0f, 1.0f, v);
        });
    }

    bool eqMouseDown (const juce::MouseEvent& e, const LayoutItem& item)
    {
        const auto* fx = current();
        const auto graph = eqGraph (rectOf (item));
        const auto band = peqview::bandAt (graph, fx->params, e.position, eqRange);
        eqSelected = band;
        repaint();
        if (band < 0)
            return true;
        if (e.mods.isPopupMenu())
        {
            peqview::showBandMenu (*this, fx->params, band, [this] (std::vector<std::pair<int, float>> changes) { setParams (changes); });
            return true;
        }
        eqDragOffset = peqview::handlePosition (graph, fx->params, band, eqRange) - e.position;
        eqDragging = true;
        return true;
    }

    void eqMouseDrag (const juce::MouseEvent& e)
    {
        const auto* fx = current();
        if (fx == nullptr || eqSelected < 0 || dragItem < 0)
            return;
        const auto graph = eqGraph (rectOf (activeLayout().items[(std::size_t) dragItem]));
        const auto p = e.position + eqDragOffset;
        std::vector<std::pair<int, float>> changes { { peq::param (eqSelected, 1), peq::hzValue (peqview::hzForX (graph, p.x)) } };
        if (peqview::hasGain (fx->params, eqSelected))
            changes.push_back ({ peq::param (eqSelected, 2), peq::gainValue (peqview::dbForY (graph, p.y, eqRange)) });
        setParams (changes);
        ui::showValueTip (*this, juce::Rectangle<int> (6, 6).withCentre (e.getPosition()), peqview::bandText (fx->params, eqSelected));
    }

    // --- Soundgoodizer: Amount and the preset A–D live in the slot's state ---------------------------------

    std::optional<maximusfx::Blob> soundgoodizerBlob() const
    {
        const auto* fx = current();
        return fx != nullptr ? maximusfx::unpack (fx->state) : std::nullopt;
    }

    float soundgoodizerAmount() const
    {
        const auto blob = soundgoodizerBlob();
        return blob && blob->mix >= 0 ? (float) blob->mix / 1000.0f : 0.6f;
    }

    void writeSoundgoodizer (const maximusfx::Blob& blob)
    {
        engine.editSong ([t = track, sl = slot, bytes = maximusfx::pack (blob)] (Song& song) {
            song.mixer[(std::size_t) t].effects[(std::size_t) sl].state = bytes;
        });
    }

    void setSoundgoodizerAmount (float v)
    {
        if (auto blob = soundgoodizerBlob())
        {
            blob->mix = juce::roundToInt (v * 1000.0f);
            writeSoundgoodizer (*blob);
        }
    }

    const std::vector<std::uint8_t>& soundgoodizerPresetBytes (int preset)
    {
        auto& bytes = sgPresets[(std::size_t) preset];
        if (bytes.empty())
        {
            const auto roots = findFlStudioRoots();
            juce::MemoryBlock data;
            if (! roots.factoryData.empty()
                && juce::File (juce::String::fromUTF8 (roots.factoryData.c_str()))
                       .getChildFile ("Plugins/Fruity/Effects/Soundgoodizer/Data/Presets")
                       .getChildFile (maximusfx::soundgoodizerPresetFile (preset))
                       .loadFileAsData (data))
            {
                const auto* b = static_cast<const std::uint8_t*> (data.getData());
                bytes.assign (b, b + data.getSize());
            }
        }
        return bytes;
    }

    int soundgoodizerPreset()
    {
        const auto blob = soundgoodizerBlob();
        for (int p = 0; blob && p < 4; ++p)
            if (const auto& bytes = soundgoodizerPresetBytes (p); ! bytes.empty() && bytes == blob->state)
                return p;
        return -1;
    }

    void setSoundgoodizerPreset (int preset)
    {
        auto blob = soundgoodizerBlob();
        const auto& bytes = soundgoodizerPresetBytes (preset);
        if (! blob || bytes.empty())
            return;
        blob->state = bytes;
        writeSoundgoodizer (*blob);
    }

    void paintSoundgoodizer (juce::Graphics& g, const juce::String& id, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        if (id == "sgamount")
        {
            const auto amount = soundgoodizerAmount();
            drawKnob (g, r, amount, false, fx.enabled ? colour::accent : colour::textDisabled, sgDragging);
            const auto body = r.reduced (r.getWidth() * 0.16f);
            g.setGradientFill (juce::ColourGradient (colour::bg6, body.getCentreX(), body.getY(), colour::bg4, body.getCentreX(), body.getBottom(), false));
            g.fillEllipse (body);
            g.setColour (colour::borderStrong);
            g.drawEllipse (body, 1.0f);
            g.setColour (colour::textPrimary);
            g.setFont (font (juce::jmax (10.0f, 18.0f * s), true));
            g.drawText (juce::String (amount * 100.0f, 0) + "%", body, juce::Justification::centred, false);
            return;
        }
        const auto chosen = soundgoodizerPreset();
        for (int p = 0; p < 4; ++p)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + r.getWidth() * (float) p / 4.0f, r.getY(), r.getWidth() / 4.0f, r.getHeight());
            const auto led = juce::Rectangle<float> (8.0f * s, 8.0f * s).withCentre ({ cell.getCentreX(), cell.getY() + 8.0f * s });
            g.setColour (p == chosen ? (fx.enabled ? colour::accent : colour::textSecondary) : colour::ledOff);
            g.fillEllipse (led);
            g.setColour (p == chosen ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (juce::jmax (10.0f, 20.0f * s), true));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + p)), cell.withTrimmedTop (16.0f * s), juce::Justification::centred, false);
        }
    }

    // a pad sets two parameters at once
    void xyMouse (const juce::MouseEvent& e, const LayoutItem& item)
    {
        const auto inner = rectOf (item).reduced (6.0f * scale());
        const auto px = plugui::paramIndex (kind, item.param), py = plugui::paramIndex (kind, item.param2);
        const auto x = juce::jlimit (0.0f, 1.0f, (e.position.x - inner.getX()) / inner.getWidth());
        const auto y = juce::jlimit (0.0f, 1.0f, (inner.getBottom() - e.position.y) / inner.getHeight());
        setParams ({ { px, x }, { py, y } });
        ui::showValueTip (*this, rectOf (item).toNearestInt(), valueText (px, x) + " / " + valueText (py, y));
    }

    // --- mouse -----------------------------------------------------------------------------------------

    int headerItemAt (juce::Point<float> p) const
    {
        if (! headerArea().contains (p))
            return 0;
        if (menuButton().expanded (2.0f).contains (p))
            return 1;
        if (presetBox().contains (p))
            return 2;
        if (prevButton().contains (p))
            return 3;
        if (nextButton().contains (p))
            return 4;
        return 0;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        const auto h = headerItemAt (p);
        const auto i = h == 0 ? itemAt (p) : -1;
        if (h != hoverHeader || i != hoverItem)
        {
            hoverHeader = h;
            hoverItem = i;
            repaint();
        }
        setMouseCursor (i >= 0 || h > 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverHeader = 0;
        hoverItem = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (current() == nullptr)
            return;

        if (const auto h = headerItemAt (e.position); h > 0)
        {
            if (h == 1 || h == 2 || e.mods.isPopupMenu())
                showOptionsMenu (h == 2);
            else
                stepPreset (h == 3 ? -1 : 1);
            return;
        }

        const auto index = itemAt (e.position);
        if (index < 0)
            return;

        const auto& item = activeLayout().items[(std::size_t) index];
        const auto r = rectOf (item);

        if (hasStyle (item, plugui::pageTabs))
        {
            const auto n = juce::jmax (1, (int) plugui::optionLabels (item).size());
            const auto along = isVertical (item) ? (e.position.y - r.getY()) / r.getHeight() : (e.position.x - r.getX()) / r.getWidth();
            const auto o = juce::jlimit (0, n - 1, (int) (along * (float) n));
            if (item.steps > 0) // one digit of the page (EQUO: target ×1, bank ×3)
                page += (o - (page / item.steps) % n) * item.steps;
            else
                page = o;
            hoverItem = -1;
            repaint();
            return;
        }

        if (item.type == Item::display && juce::String (item.display) == "sgpresets")
        {
            const auto r = rectOf (item);
            setSoundgoodizerPreset (juce::jlimit (0, 3, (int) ((e.position.x - r.getX()) / r.getWidth() * 4.0f)));
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "sgamount")
        {
            dragItem = index;
            sgDragging = true;
            sgStart = soundgoodizerAmount();
            ui::showValueTip (*this, rectOf (item).toNearestInt(), juce::String (sgStart * 100.0f, 0) + "%");
            return;
        }
        if (item.type == Item::display && (juce::String (item.display) == "prismbands" || juce::String (item.display) == "emberbands"))
        {
            // band displays (round 108): drag a crossover line, or click a band to edit it (its page)
            const auto* fx = current();
            if (const auto p = dynui::crossoverNear (*fx, r, e.position.x, 6.0f * scale()); p >= 0)
            {
                dragItem = index;
                crossoverDrag = p;
                return;
            }
            if (! activeLayout().pages.empty())
            {
                page = juce::jlimit (0, (int) activeLayout().pages.size() - 1, dynui::bandAt (*fx, r, e.position.x));
                repaint();
            }
            return;
        }
        if (item.type == Item::display && juce::String (item.display) == "xy")
        {
            dragItem = index;
            xyDragging = true;
            xyMouse (e, item);
            return;
        }
        if (isBars (item))
        {
            dragItem = index;
            barsDragging = true;
            barsMouse (e, item, true);
            return;
        }
        if (item.type == Item::display) // the EQ graph
        {
            if (eqMouseDown (e, item) && eqDragging)
                dragItem = index;
            return;
        }

        const auto param = paramOf (item);
        if (param < 0)
            return;

        const auto spec = specFor (item);

        if (item.type == Item::menu && ! ui::isResetClick (e))
        {
            if (kind == EffectKind::parametricEq && item.colour >= 0)
            {
                eqSelected = item.colour;
                repaint();
                peqview::showBandMenu (*this, current()->params, item.colour, [this] (std::vector<std::pair<int, float>> changes) { setParams (changes); });
                return;
            }
            if (kind == EffectKind::fruitySend) // FL's list: no destination, then every other insert by name
            {
                juce::PopupMenu menu;
                const auto chosen = flbasic::sendTrack (valueOf (param));
                menu.addItem (tr ("None"), true, chosen <= 0, [this, param] { setValue (param, 0.0f); });
                const auto& mixer = engine.getSong().mixer;
                for (int t = 1; t < (int) mixer.size(); ++t)
                    if (t != track)
                        menu.addItem (juce::String (t) + "  " + insertName (t), engine.getSong().canRoute (track, t) || t == chosen, t == chosen,
                                      [this, param, t] { setValue (param, flbasic::sendTrackValue (t)); });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (r.toNearestInt())));
                return;
            }
            juce::PopupMenu menu;
            const auto labels = plugui::optionLabels (item);
            const auto chosen = plugui::optionAt (item, valueOf (param));
            for (int o = 0; o < (int) labels.size(); ++o)
                menu.addItem (optionTexts (item)[(std::size_t) o], true, o == chosen,
                              [this, param, v = plugui::optionValue (item, o)] { setValue (param, v); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (r.toNearestInt())));
            return;
        }

        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, spec, valueOf (param), [this, param] (double v) { setValue (param, (float) v); });
            return;
        }

        if (ui::isResetClick (e))
        {
            setValue (param, (float) spec.defaultValue);
            ui::flashValueTip (*this, r.toNearestInt(), spec.text (spec.defaultValue));
            return;
        }

        switch (item.type)
        {
            case Item::radio:
            case Item::tabs:
            {
                const auto n = juce::jmax (1, (int) plugui::optionLabels (item).size());
                auto o = item.type == Item::radio || isVertical (item) ? (int) ((e.position.y - r.getY()) / r.getHeight() * (float) n)
                                                  : (int) ((e.position.x - r.getX()) / r.getWidth() * (float) n);
                if (item.type == Item::radio && item.columns > 0)
                    for (int k = 0; k < n; ++k)
                        if (gridCell (item, k, n).contains (e.position))
                            o = k;
                setValue (param, plugui::optionValue (item, juce::jlimit (0, n - 1, o)));
                return;
            }
            case Item::toggle:
                setValue (param, valueOf (param) >= 0.5f ? 0.0f : 1.0f);
                return;
            case Item::stepper:
                if (e.position.x > r.getRight() - juce::jmin (12.0f * scale(), r.getWidth() * 0.4f))
                {
                    const auto up = e.position.y < r.getY() + (item.label != nullptr ? 12.0f * scale() : 0.0f) + (r.getBottom() - r.getY()) * 0.5f;
                    const auto step = item.steps > 0 ? 1.0f / (float) item.steps : 0.01f;
                    setValue (param, valueOf (param) + (up ? step : -step));
                    ui::flashValueTip (*this, r.toNearestInt(), valueText (param, valueOf (param)));
                    return;
                }
                break;
            case Item::hslider:
            case Item::vslider:
            case Item::knob:
            case Item::value:
            case Item::menu:
            case Item::section:
            case Item::label:
            case Item::display:
                break;
        }

        dragItem = index;
        drag.begin (e, valueOf (param));
        ui::showValueTip (*this, r.toNearestInt(), spec.text (valueOf (param)));
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (crossoverDrag >= 0 && dragItem >= 0)
        {
            const auto r = rectOf (activeLayout().items[(std::size_t) dragItem]);
            const auto v = dynui::crossoverValueAt (*current(), r, e.position.x);
            setValue (crossoverDrag, v);
            ui::showValueTip (*this, r.toNearestInt(), valueText (crossoverDrag, v));
            return;
        }
        if (eqDragging)
        {
            eqMouseDrag (e);
            return;
        }
        if (sgDragging && dragItem >= 0)
        {
            const auto v = juce::jlimit (0.0f, 1.0f, sgStart - (float) e.getDistanceFromDragStartY() / (e.mods.isCtrlDown() ? 800.0f : 200.0f));
            setSoundgoodizerAmount (v);
            ui::showValueTip (*this, rectOf (activeLayout().items[(std::size_t) dragItem]).toNearestInt(), juce::String (v * 100.0f, 0) + "%");
            return;
        }
        if (xyDragging && dragItem >= 0)
        {
            xyMouse (e, activeLayout().items[(std::size_t) dragItem]);
            return;
        }
        if (barsDragging && dragItem >= 0)
        {
            barsMouse (e, activeLayout().items[(std::size_t) dragItem], false);
            return;
        }
        if (dragItem < 0 || ! drag.isActive())
            return;
        const auto& item = activeLayout().items[(std::size_t) dragItem];
        const auto param = paramOf (item);
        const auto spec = specFor (item);
        auto v = drag.update (e, spec);
        if (item.type == Item::hslider) // horizontal sliders follow the mouse sideways too
        {
            const auto r = rectOf (item);
            v = juce::jlimit (0.0, 1.0, (double) ((e.position.x - r.getX() - 4.0f * scale()) / juce::jmax (1.0f, r.getWidth() - 8.0f * scale())));
            if (e.mods.isCtrlDown())
                v = drag.update (e, spec);
        }
        setValue (param, (float) v);
        ui::showValueTip (*this, rectOf (item).toNearestInt(), spec.text (v));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        eqDragging = false;
        if (crossoverDrag >= 0)
        {
            crossoverDrag = -1;
            dragItem = -1;
            ui::hideValueTip();
        }
        if (xyDragging || sgDragging || barsDragging)
        {
            xyDragging = sgDragging = barsDragging = false;
            dragItem = -1;
            ui::hideValueTip();
        }
        if (dragItem >= 0)
        {
            drag.end();
            dragItem = -1;
            ui::hideValueTip();
            repaint();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto index = itemAt (e.position);
        if (index < 0)
            return;
        const auto& item = activeLayout().items[(std::size_t) index];
        if (item.type == Item::display) // EQ graph: the band under the mouse back to 0 dB
        {
            if (const auto* fx = current(); fx != nullptr)
                if (const auto b = peqview::bandAt (eqGraph (rectOf (item)), fx->params, e.position, eqRange); b >= 0)
                    setParams ({ { peq::param (b, 2), 0.5f } });
            return;
        }
        if (item.type == Item::radio || item.type == Item::tabs || item.type == Item::toggle || item.type == Item::menu || paramOf (item) < 0)
            return;
        const auto spec = specFor (item);
        setValue (paramOf (item), (float) spec.defaultValue);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto index = itemAt (e.position);
        if (index < 0)
            return;
        const auto& item = activeLayout().items[(std::size_t) index];
        if (item.type == Item::display) // EQ graph: Q / bandwidth of the band under the mouse (or the selected one)
        {
            const auto* fx = current();
            if (fx == nullptr)
                return;
            auto b = peqview::bandAt (eqGraph (rectOf (item)), fx->params, e.position, eqRange);
            if (b < 0)
                b = eqSelected;
            if (b < 0)
                return;
            eqSelected = b;
            const auto q = fx->params[(std::size_t) peq::param (b, 3)];
            // wheel up = narrower; FL's width grows the other way
            const auto step = peq::flLaw (fx->params) ? -wheel.deltaY * 0.15f : wheel.deltaY * 0.15f;
            setParams ({ { peq::param (b, 3), q + step } });
            return;
        }
        const auto param = paramOf (item);
        if (param < 0)
            return;
        auto spec = specFor (item);
        if (item.type == Item::radio || item.type == Item::tabs)
        {
            const auto n = (int) plugui::optionLabels (item).size();
            const auto o = plugui::optionAt (item, valueOf (param)) + (wheel.deltaY > 0 ? (item.type == Item::radio ? -1 : 1) : (item.type == Item::radio ? 1 : -1));
            setValue (param, plugui::optionValue (item, juce::jlimit (0, n - 1, o)));
            return;
        }
        if (const auto v = ui::wheelValue (valueOf (param), e, wheel, spec))
        {
            setValue (param, (float) *v);
            ui::flashValueTip (*this, rectOf (item).toNearestInt(), spec.text (*v));
        }
    }

    ui::Hint hintAt (juce::Point<int> position) override
    {
        const auto p = position.toFloat();
        switch (headerItemAt (p))
        {
            case 1: return { tr ("Options: presets, reset, bypass"), {}, false, true };
            case 2: return { tr ("Preset: click for the list of FL presets of this plugin"), presetName, false, true };
            case 3: return { tr ("Previous preset"), {}, false, false };
            case 4: return { tr ("Next preset"), {}, false, false };
            default: break;
        }
        const auto index = itemAt (p);
        if (index < 0)
            return {};
        const auto& item = activeLayout().items[(std::size_t) index];
        if (item.type == Item::display && juce::String (item.display) == "sgamount")
            return { tr ("Amount: drag up or down (Ctrl: fine)"), juce::String (soundgoodizerAmount() * 100.0f, 0) + "%", false, false };
        if (item.type == Item::display && juce::String (item.display) == "sgpresets")
            return { tr ("Soundgoodizer preset A to D (read from your FL installation)"), {}, false, false };
        if (item.type == Item::display && juce::String (item.display) == "xy")
            return { tr ("Drag the point: both values at once"), {}, false, false };
        if (item.type == Item::display)
            return { tr ("Drag a handle (frequency and gain); mouse wheel = Q; right-click = type; double-click = 0 dB"), {}, false, true };
        if (hasStyle (item, plugui::pageTabs))
            return { tr ("Page: switch the controls shown"), {}, false, false };
        const auto spec = specFor (item);
        return ui::valueHint (spec, valueOf (paramOf (item)));
    }

    // --- presets ---------------------------------------------------------------------------------------

    juce::Array<juce::File> presetFiles() const
    {
        juce::Array<juce::File> all;
        const auto roots = findFlStudioRoots();
        if (roots.data.empty() || layout == &plugui::soundgoodizerLayout()) // Soundgoodizer: its presets A–D are on the page
            return all;
        const auto base = juce::File (juce::String::fromUTF8 (roots.data.c_str())).getChildFile ("Patches/Plugin presets/Effects");
        for (const auto& plugin : flPluginsForEffect (kind))
        {
            auto files = base.getChildFile (juce::String::fromUTF8 (plugin.c_str())).findChildFiles (juce::File::findFiles, true, "*.fst");
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareNatural (b.getFullPathName()) < 0; });
            all.addArray (files);
        }
        return all;
    }

    void loadPreset (const juce::File& f)
    {
        juce::MemoryBlock data;
        if (! f.loadFileAsData (data))
            return;
        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
        auto fx = effectFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), engine.getSong().bpm);
        if (! fx || fx->kind != kind)
            return;
        presetName = f.getFileNameWithoutExtension();
        engine.editSong ([t = track, s = slot, fx = *fx] (Song& song) {
            auto& target = song.mixer[(std::size_t) t].effects[(std::size_t) s];
            const auto enabled = target.enabled;
            const auto mix = target.mix;
            target = fx;
            target.enabled = enabled;
            target.mix = mix;
        });
        repaint();
    }

    void stepPreset (int delta)
    {
        const auto files = presetFiles();
        if (files.isEmpty())
            return;
        auto at = -1;
        for (int i = 0; i < files.size(); ++i)
            if (files[i].getFileNameWithoutExtension() == presetName)
                at = i;
        at = at < 0 ? (delta > 0 ? 0 : files.size() - 1) : (at + delta + files.size()) % files.size();
        loadPreset (files[at]);
    }

    void showOptionsMenu (bool presetsOnly)
    {
        juce::PopupMenu presets;
        const auto files = presetFiles();
        std::map<juce::String, juce::PopupMenu> folders;
        const auto roots = findFlStudioRoots();
        for (const auto& f : files)
        {
            const auto folder = f.getParentDirectory().getFileName();
            auto& menu = folders[folder];
            menu.addItem (f.getFileNameWithoutExtension(), true, f.getFileNameWithoutExtension() == presetName, [this, f] { loadPreset (f); });
        }
        if (folders.size() == 1)
            presets = folders.begin()->second;
        else
            for (auto& [name, menu] : folders)
                presets.addSubMenu (name, menu);

        if (presetsOnly)
        {
            if (presets.getNumItems() == 0)
                presets.addItem (tr ("No FL presets found for this plugin"), false, false, [] {});
            presets.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (presetBox().toNearestInt())));
            return;
        }

        juce::PopupMenu menu;
        if (presets.getNumItems() > 0)
            menu.addSubMenu (tr ("Presets"), presets);
        menu.addItem (tr ("Reset to defaults"), [this] {
            const auto& info = effectInfo (kind);
            presetName = {};
            auto bytes = current() != nullptr ? current()->state : std::vector<std::uint8_t> {};
            if (stateBacked())
            {
                const auto& table = stateTable();
                for (int p = 0; p < (int) table.params.size(); ++p)
                    bytes = table.with (bytes, p, table.params[(std::size_t) p].defaultValue);
            }
            engine.editSong ([t = track, s = slot, info, bytes, state = stateBacked()] (Song& song) {
                auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) s];
                for (std::size_t i = 0; i < info.params.size() && i < fx.params.size(); ++i)
                    fx.params[i] = info.params[i].defaultValue;
                if (state)
                    fx.state = bytes;
            });
        });
        // switches that FL keeps out of the panel (in its own menus): on / off here
        if (! activeLayout().menuSwitches.empty())
        {
            menu.addSeparator();
            for (const auto* name : activeLayout().menuSwitches)
                if (const auto p = plugui::paramIndex (kind, name); p >= 0)
                {
                    const auto on = valueOf (p) >= 0.5f;
                    menu.addItem (utf8 (name), true, on, [this, p, on] { setValue (p, on ? 0.0f : 1.0f); });
                }
            menu.addSeparator();
        }
        const auto* fx = current();
        menu.addItem (tr ("Bypass"), true, fx != nullptr && ! fx->enabled, [this] {
            engine.editSong ([t = track, s = slot] (Song& song) {
                auto& target = song.mixer[(std::size_t) t].effects[(std::size_t) s];
                target.enabled = ! target.enabled;
            });
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (menuButton().toNearestInt())));
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    void timerCallback() override
    {
        bool history = false;
        for (const auto& item : activeLayout().items)
        {
            if (item.type == Item::display && item.display != nullptr && std::strcmp (item.display, "pitchgraph") == 0 && visible (item))
            {
                // LYNX Tune's pitch history (30 points a second)
                pitchHistory.push_back ({ liveMeter (0) * 127.0f, liveMeter (1) * 127.0f, (liveMeter (2) - 0.5f) * 4.0f });
                if (pitchHistory.size() > pitchHistoryLength)
                    pitchHistory.erase (pitchHistory.begin(), pitchHistory.begin() + (std::ptrdiff_t) (pitchHistory.size() - pitchHistoryLength));
                repaint (rectOf (item).getSmallestIntegerContainer());
            }
            if (item.type == Item::display && item.display != nullptr && (std::strcmp (item.display, "grmeter") == 0 || dynui::isLive (item.display)) && visible (item))
            {
                history = history || std::strcmp (item.display, "grhistory") == 0;
                repaint (rectOf (item).getSmallestIntegerContainer());
            }
        }
        if (history)
            dynHistory.push (liveMeter (0), liveMeter (2), liveMeter (3));
    }

    AudioEngine& engine;
    int track, slot;
    EffectKind kind = EffectKind::eq;
    const plugui::Layout* layout = nullptr;
    int page = 0;
    int hoverItem = -1, hoverHeader = 0, dragItem = -1;
    ui::ValueDrag drag;
    juce::String presetName;
    int eqSelected = -1;
    bool eqDragging = false, xyDragging = false, sgDragging = false;
    struct PitchPoint { float sung, target, shift; }; // MIDI note numbers (0 = none) and the applied correction in semitones
    static constexpr std::size_t pitchHistoryLength = 240; // 8 s
    std::vector<PitchPoint> pitchHistory;
    float displayCentre = 0.0f;
    float sgStart = 0.0f;
    std::array<std::vector<std::uint8_t>, 4> sgPresets {}; // the installation's preset files A–D (read on demand)
    juce::Point<float> eqDragOffset;
    dynui::History dynHistory; // the dynamics suite's scrolling gain-reduction graph
    int crossoverDrag = -1;    // the crossover parameter being dragged on a band display
};

// --- the window ------------------------------------------------------------------------------------

class NativeEffectWindow final : public juce::DocumentWindow
{
public:
    NativeEffectWindow (AudioEngine& engine, int track, int slot, const juce::String& title)
        : DocumentWindow (title, theme::colour::bg1, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        auto* editor = new NativeEffectEditor (engine, track, slot, 1.25f, 0);
        const auto design = editor->designSize();
        setContentOwned (editor, true);
        setResizable (true, false);
        // keep FL's proportions while resizing (the presets bar has a fixed height)
        if (auto* c = getConstrainer())
        {
            c->setMinimumSize (design.x / 2, design.y / 2);
            c->setMaximumSize (design.x * 3, design.y * 3 + 200);
        }
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
    }

    void closeButtonPressed() override;
};

std::map<std::pair<int, int>, std::unique_ptr<NativeEffectWindow>>& windows()
{
    static std::map<std::pair<int, int>, std::unique_ptr<NativeEffectWindow>> open;
    return open;
}

void NativeEffectWindow::closeButtonPressed()
{
    for (auto& [key, window] : windows())
        if (window.get() == this)
        {
            juce::MessageManager::callAsync ([k = key] { windows().erase (k); });
            return;
        }
}
} // namespace

void setNativeEditorDemo (bool on)
{
    demoDisplays = on;
}

std::unique_ptr<juce::Component> makeNativeEffectEditor (AudioEngine& engine, int track, int slot, float scale, int page)
{
    return std::make_unique<NativeEffectEditor> (engine, track, slot, scale, page);
}

namespace NativeEffectWindows
{
bool canOpen (EffectKind kind)
{
    return kind != EffectKind::plugin && kind != EffectKind::parallel;
}

void open (AudioEngine& engine, int track, int slot)
{
    const auto& mixer = engine.getSong().mixer;
    if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
        return;

    const auto kind = mixer[(std::size_t) track].effects[(std::size_t) slot].kind;
    if (! canOpen (kind))
        return;
    if (kind == EffectKind::parametricEq) // its own editor (response curve with handles)
    {
        ParametricEqWindows::open (engine, track, slot);
        return;
    }
    if (kind == EffectKind::facetEq) // its own editor (analyser, nodes, dynamics; REA round 103)
    {
        FacetEqWindows::open (engine, track, slot);
        return;
    }

    auto& open = windows();
    const auto key = std::make_pair (track, slot);

    if (auto it = open.find (key); it != open.end())
    {
        if (it->second->getContentComponent() != nullptr && static_cast<NativeEffectEditor*> (it->second->getContentComponent())->current() != nullptr)
        {
            it->second->toFront (true);
            return;
        }
        open.erase (it); // the slot now holds another effect
    }

    const auto name = track == 0 ? tr ("Master") : juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str());
    const auto title = utf8 (effectInfo (kind).name) + " - " + (name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) })) + " ("
                       + tr ("slot {0}", { juce::String (slot + 1) }) + ")";
    open[key] = std::make_unique<NativeEffectWindow> (engine, track, slot, title);
}

void closeAll()
{
    windows().clear();
    FacetEqWindows::closeAll();
}
} // namespace NativeEffectWindows
} // namespace lynx
