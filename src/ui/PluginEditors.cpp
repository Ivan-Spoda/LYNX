#include "ui/PluginEditors.h"

#include <map>

#include "core/Effects.h"
#include "core/FlEffects.h"
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

    juce::Colour arcColour (const EffectSlot& fx) const { return fx.enabled ? theme::colour::knobArc : theme::colour::textDisabled; }

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
                g.setColour (colour::bg2);
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
                drawKnob (g, circle, value, hasStyle (item, plugui::bipolar), arcColour (fx), hot);
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
                        caption (g, item, area.removeFromBottom (13.0f * s));
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
                g.setColour (hot ? colour::accent : arcColour (fx));
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
        }
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
        else if (id == "shaper") // a soft/hard transfer shape from a hardness / threshold control
        {
            grid (4, 4);
            const auto k = 0.2f + 6.0f * param01 (item.param != nullptr ? item.param : "Hardness");
            strokeCurve ([k] (float x) { const auto u = x * 2.0f - 1.0f; return 0.5f + 0.5f * std::tanh (k * u) / std::tanh (k); }, inner, false);
        }
        else if (id == "multiband")
        {
            // the three bands between the crossovers
            grid (8, 4);
            const auto low = param01 ("Low crossover"), high = param01 ("High crossover");
            const juce::Colour bandColours[] = { juce::Colour (0xfff2767b), juce::Colour (0xffe8cf6a), juce::Colour (0xff7aa9f5) };
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
            strokeCurve ([t, soft] (float x) { return soft ? t * std::tanh (x / t) : juce::jmin (x, t); }, inner, false);
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
            drawParametricEqCurve (g, r, fx.params, juce::jmax (8000.0, engine.getTransport().sampleRate.load()), fx.enabled);
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
            g.drawText (caps (tr (item.label)), inner.removeFromTop (12.0f * s), juce::Justification::topLeft);
        }
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

        const auto param = paramOf (item);
        if (param < 0)
            return;

        const auto spec = specFor (item);

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
        if (item.type == Item::radio || item.type == Item::tabs || item.type == Item::toggle || paramOf (item) < 0)
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
        {
            const auto& info = effectInfo (kind);
            std::vector<bool> placed (info.params.size(), false);
            for (const auto& item : activeLayout().items)
                if (const auto p = paramOf (item); p >= 0 && p < (int) placed.size())
                    placed[(std::size_t) p] = true;
            bool first = true;
            for (int p = 0; *activeLayout().fl != 0 && p < (int) placed.size(); ++p)
                if (! placed[(std::size_t) p])
                {
                    if (first)
                        menu.addSeparator();
                    first = false;
                    const auto on = valueOf (p) >= 0.5f;
                    menu.addItem (utf8 (info.params[(std::size_t) p].name), true, on, [this, p, on] { setValue (p, on ? 0.0f : 1.0f); });
                }
            if (! first)
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
