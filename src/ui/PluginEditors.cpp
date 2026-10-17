#include "ui/PluginEditors.h"

#include <map>

#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/ParametricEqFl.h"
#include "core/PluginLayout.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ParametricEqEditor.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
using plugui::Item;
using plugui::LayoutItem;

constexpr float headerHeight = 26.0f; // the presets bar (not scaled with the body)

bool hasStyle (const LayoutItem& item, std::uint16_t flag) noexcept
{
    return (item.style & flag) != 0;
}

// --- the editor ----------------------------------------------------------------------------------

class NativeEffectEditor final : public juce::Component,
                                 public ui::HintSource,
                                 private juce::ChangeListener
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
        }

        page = initialPage;
        engine.addChangeListener (this);
        const auto& l = activeLayout();
        setSize ((int) std::ceil ((float) l.width * initialScale), (int) std::ceil ((float) l.height * initialScale + headerHeight));
    }

    ~NativeEffectEditor() override
    {
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
        return fx != nullptr && plugui::isShown (kind, item, page, fx->params);
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
            if (item.type == Item::display && rectOf (item).contains (p) && juce::String (item.display) == "eq")
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

    float valueOf (int param) const
    {
        const auto* fx = current();
        return fx != nullptr && param >= 0 && param < maxEffectParams ? fx->params[(std::size_t) param] : 0.0f;
    }

    void setValue (int param, float value)
    {
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
        return utf8 (formatEffectParam (kind, param, value, engine.getSong().bpm, fx->params).c_str());
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
        const auto name = utf8 (effectInfo (kind).name);
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
                if (hasStyle (item, plugui::boxed))
                {
                    g.setColour (colour::bg3);
                    g.fillRoundedRectangle (r, radius::sm * s);
                }
                caption (g, item, r, hasStyle (item, plugui::captionLeft | plugui::alignLeft) ? juce::Justification::centredLeft : juce::Justification::centred);
                break;

            case Item::value:
                g.setColour (hot ? colour::accent : (fx.enabled ? colour::textPrimary : colour::textDisabled));
                g.setFont (font (juce::jmax (8.0f, 11.0f * s)));
                g.drawFittedText (valueText (param, value), r.toNearestInt(),
                                  hasStyle (item, plugui::alignLeft) ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.7f);
                break;

            case Item::knob:
            {
                const auto circle = knobCircle (item);
                drawKnob (g, circle, value, hasStyle (item, plugui::bipolar), arcColour (fx, item.colour), hot && item.colour < 0);
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
                const auto rowH = r.getHeight() / (float) juce::jmax (1, (int) labels.size());
                for (int o = 0; o < (int) labels.size(); ++o)
                {
                    const auto row = juce::Rectangle<float> (r.getX(), r.getY() + rowH * (float) o, r.getWidth(), rowH);
                    const auto dot = juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre ({ row.getX() + 5.0f * s, row.getCentreY() });
                    g.setColour (o == chosen ? (fx.enabled ? colour::accent : colour::textSecondary) : colour::ledOff);
                    g.fillEllipse (dot);
                    g.setColour (o == chosen ? colour::textPrimary : (hot ? colour::textSecondary : colour::textDisabled));
                    g.setFont (font (juce::jmax (8.0f, 9.5f * s)));
                    g.drawFittedText (tr (juce::String::fromUTF8 (labels[(std::size_t) o].c_str())), row.withTrimmedLeft (12.0f * s).toNearestInt(),
                                      juce::Justification::centredLeft, 1, 0.7f);
                }
                break;
            }

            case Item::tabs:
            {
                const auto labels = plugui::optionLabels (item);
                const auto chosen = hasStyle (item, plugui::pageTabs) ? page : plugui::optionAt (item, value);
                const auto w = r.getWidth() / (float) juce::jmax (1, (int) labels.size());
                g.setColour (colour::bg0);
                g.fillRoundedRectangle (r, radius::sm);
                for (int o = 0; o < (int) labels.size(); ++o)
                {
                    const auto cell = juce::Rectangle<float> (r.getX() + w * (float) o, r.getY(), w, r.getHeight()).reduced (1.0f);
                    if (o == chosen)
                    {
                        g.setColour (colour::bg6);
                        g.fillRoundedRectangle (cell, radius::sm);
                        g.setColour (colour::accent);
                        g.fillRect (cell.withTop (cell.getBottom() - juce::jmax (1.5f, 2.0f * s)).reduced (3.0f * s, 0.0f));
                    }
                    g.setColour (o == chosen ? colour::textPrimary : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
                    g.drawFittedText (caps (tr (juce::String::fromUTF8 (labels[(std::size_t) o].c_str()))), cell.toNearestInt(),
                                      juce::Justification::centred, 1, 0.7f);
                }
                break;
            }

            case Item::toggle:
            {
                const auto on = value >= 0.5f;
                if (hasStyle (item, plugui::boxed)) // a small lit button with its letter (FL's T / L buttons)
                {
                    g.setColour (on ? (fx.enabled ? colour::accent : colour::textSecondary) : (hot ? colour::bg6 : colour::bg5));
                    g.fillRoundedRectangle (r, radius::sm);
                    g.setColour (on ? colour::onAccent : colour::textSecondary);
                    g.setFont (font (juce::jmax (8.0f, r.getHeight() * 0.8f), true));
                    if (item.label != nullptr)
                        g.drawText (tr (item.label), r, juce::Justification::centred, false);
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

    void paintDisplay (juce::Graphics& g, const LayoutItem& item, juce::Rectangle<float> r, const EffectSlot& fx)
    {
        using namespace theme;
        const auto s = scale();
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        const juce::String id (item.display != nullptr ? item.display : "");
        const auto curveColour = fx.enabled ? colour::accent : colour::textDisabled;
        const auto inner = r.reduced (6.0f * s);

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
        else if (id == "eq")
        {
            paintEq (g, r, fx);
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
            page = juce::jlimit (0, n - 1, (int) ((e.position.x - r.getX()) / r.getWidth() * (float) n));
            hoverItem = -1;
            repaint();
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
            juce::PopupMenu menu;
            const auto labels = plugui::optionLabels (item);
            const auto chosen = plugui::optionAt (item, valueOf (param));
            for (int o = 0; o < (int) labels.size(); ++o)
                menu.addItem (tr (juce::String::fromUTF8 (labels[(std::size_t) o].c_str())), true, o == chosen,
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
                const auto o = item.type == Item::radio ? (int) ((e.position.y - r.getY()) / r.getHeight() * (float) n)
                                                        : (int) ((e.position.x - r.getX()) / r.getWidth() * (float) n);
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
        if (eqDragging)
        {
            eqMouseDrag (e);
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
        if (roots.data.empty())
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
            engine.editSong ([t = track, s = slot, info] (Song& song) {
                auto& params = song.mixer[(std::size_t) t].effects[(std::size_t) s].params;
                for (std::size_t i = 0; i < info.params.size() && i < params.size(); ++i)
                    params[i] = info.params[i].defaultValue;
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

    AudioEngine& engine;
    int track, slot;
    EffectKind kind = EffectKind::eq;
    const plugui::Layout* layout = nullptr;
    int page = 0;
    int hoverItem = -1, hoverHeader = 0, dragItem = -1;
    ui::ValueDrag drag;
    juce::String presetName;
    int eqSelected = -1;
    bool eqDragging = false;
    juce::Point<float> eqDragOffset;
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
}
} // namespace NativeEffectWindows
} // namespace lynx
