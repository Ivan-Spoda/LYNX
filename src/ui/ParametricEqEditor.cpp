#include "ui/ParametricEqEditor.h"
#include "ui/PluginEditors.h"
#include "core/ParametricEqFl.h"


#include "core/Effects.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
constexpr double minHz = 20.0, maxHz = 20000.0;

juce::Colour bandColour (int band)
{
    // one hue per band, at the same lightness so none dominates the red curve
    static const juce::Colour colours[] = { juce::Colour (0xfff2767b), juce::Colour (0xfff2a46b), juce::Colour (0xffe8cf6a),
                                            juce::Colour (0xff86d993), juce::Colour (0xff62cfd0), juce::Colour (0xff7aa9f5),
                                            juce::Colour (0xffb59af2) };
    return colours[band % 7];
}

float xForHz (juce::Rectangle<float> area, double hz)
{
    return area.getX() + area.getWidth() * (float) (std::log (hz / minHz) / std::log (maxHz / minHz));
}

double hzForX (juce::Rectangle<float> area, float x)
{
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0, 1.0, (double) ((x - area.getX()) / area.getWidth())));
}

float yForDb (juce::Rectangle<float> area, double db, double rangeDb)
{
    return area.getCentreY() - area.getHeight() * 0.5f * (float) (juce::jlimit (-rangeDb, rangeDb, db) / rangeDb);
}

double dbForY (juce::Rectangle<float> area, float y, double rangeDb)
{
    return juce::jlimit (-18.0, 18.0, (double) ((area.getCentreY() - y) / (area.getHeight() * 0.5f)) * rangeDb);
}

// display names of peq::BandType (core names, translated here)
const char* const bandTypeNames[] = { TR_NOOP ("Off"), TR_NOOP ("Peak"), TR_NOOP ("Low shelf"), TR_NOOP ("High shelf"),
                                      TR_NOOP ("Low cut"), TR_NOOP ("High cut"), TR_NOOP ("Band-pass"), TR_NOOP ("Notch") };

juce::String bandTypeName (peq::BandType t)
{
    const auto i = (int) t;
    return i >= 0 && i < (int) std::size (bandTypeNames) ? tr (bandTypeNames[i]) : juce::String::fromUTF8 (peq::typeName (t));
}

bool hasGain (peq::BandType t)
{
    return t == peq::BandType::peak || t == peq::BandType::lowShelf || t == peq::BandType::highShelf;
}

} // namespace

void drawParametricEqCurve (juce::Graphics& g, juce::Rectangle<float> area, const EffectParams& params, double sampleRate,
                            bool enabled, int highlightBand, double rangeDb, bool labels)
{
    using namespace theme;
    g.setColour (colour::bg0);
    g.fillRoundedRectangle (area, radius::md);

    // grid: frequency decades and ±6/12/18 dB
    g.setColour (colour::gridBeat);
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        g.drawVerticalLine ((int) xForHz (area, hz), area.getY(), area.getBottom());
    for (double db : { -18.0, -12.0, -6.0, 6.0, 12.0, 18.0 })
        if (std::abs (db) < rangeDb)
        g.drawHorizontalLine ((int) yForDb (area, db, rangeDb), area.getX(), area.getRight());
    g.setColour (colour::gridBar);
    g.drawHorizontalLine ((int) yForDb (area, 0.0, rangeDb), area.getX(), area.getRight());

    if (labels && area.getWidth() > 300.0f)
    {
        g.setColour (colour::textDisabled);
        g.setFont (font (10.0f));
        for (auto [hz, label] : { std::pair { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
            g.drawText (label, (int) xForHz (area, hz) + 3, (int) area.getBottom() - 14, 30, 12, juce::Justification::centredLeft);
    }

    // curve
    juce::Path curve;
    const auto steps = juce::jmax (32, (int) area.getWidth() / 2);

    for (int i = 0; i <= steps; ++i)
    {
        const auto x = area.getX() + area.getWidth() * (float) i / (float) steps;
        const auto y = yForDb (area, peq::responseDb (params, sampleRate, hzForX (area, x)), rangeDb);

        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    auto fill = curve;
    fill.lineTo (area.getRight(), area.getCentreY());
    fill.lineTo (area.getX(), area.getCentreY());
    fill.closeSubPath();
    const auto curveColour = enabled ? (highlightBand >= 0 ? bandColour (highlightBand) : colour::accent) : colour::textDisabled;
    g.setGradientFill (juce::ColourGradient (curveColour.withAlpha (0.22f), area.getX(), area.getY(), curveColour.withAlpha (0.04f), area.getX(),
                                             area.getCentreY(), false));
    g.fillPath (fill);
    g.setColour (curveColour);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}

std::unique_ptr<juce::Component> makeParametricEqEditor (AudioEngine& engine, int track, int slot)
{
    return makeNativeEffectEditor (engine, track, slot, 1.25f);
}

namespace ParametricEqWindows
{
void open (AudioEngine& engine, int track, int slot)
{
    NativeEffectWindows::open (engine, track, slot);
}

void closeAll()
{
    NativeEffectWindows::closeAll();
}
} // namespace ParametricEqWindows

namespace peqview
{
juce::Colour bandColour (int band) { return lynx::bandColour (band); }
float xForHz (juce::Rectangle<float> area, double hz) { return lynx::xForHz (area, hz); }
double hzForX (juce::Rectangle<float> area, float x) { return lynx::hzForX (area, x); }
float yForDb (juce::Rectangle<float> area, double db, double rangeDb) { return lynx::yForDb (area, db, rangeDb); }
double dbForY (juce::Rectangle<float> area, float y, double rangeDb) { return lynx::dbForY (area, y, rangeDb); }
juce::String bandTypeName (int type) { return lynx::bandTypeName ((peq::BandType) type); }

bool hasGain (const EffectParams& params, int band)
{
    return lynx::hasGain (peq::type (params[(std::size_t) peq::param (band, 0)]));
}

juce::Point<float> handlePosition (juce::Rectangle<float> area, const EffectParams& params, int band, double rangeDb)
{
    const auto hz = peq::hz (params[(std::size_t) peq::param (band, 1)]);
    const auto db = hasGain (params, band) ? peq::gainDb (params[(std::size_t) peq::param (band, 2)]) : 0.0;
    return { xForHz (area, hz), yForDb (area, db, rangeDb) };
}

int bandAt (juce::Rectangle<float> area, const EffectParams& params, juce::Point<float> p, double rangeDb)
{
    int best = -1;
    float bestDistance = 12.0f;
    for (int b = 0; b < peq::bands; ++b)
        if (peq::type (params[(std::size_t) peq::param (b, 0)]) != peq::BandType::off)
            if (const auto d = handlePosition (area, params, b, rangeDb).getDistanceFrom (p); d < bestDistance)
            {
                best = b;
                bestDistance = d;
            }
    return best;
}

void showBandMenu (juce::Component& target, const EffectParams& params, int band, std::function<void (std::vector<std::pair<int, float>>)> set)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Band {0}", { juce::String (band + 1) }));
    const auto currentType = peq::type (params[(std::size_t) peq::param (band, 0)]);

    for (int t = 0; t < peq::bandTypes; ++t)
        menu.addItem (lynx::bandTypeName ((peq::BandType) t), true, (int) currentType == t, [set, band, t] {
            set ({ { peq::param (band, 0), peq::typeValue ((peq::BandType) t) } });
        });

    if (peq::flLaw (params) && ! (peqfl::lawOf (params) == peqfl::Law::peq1))
    {
        // FL's Parametric EQ 2 orders (Steep 8 … 2 … Gentle 8) apply to every band type
        menu.addSeparator();
        auto p = params;
        for (int order = 0; order < 7; ++order)
        {
            p[(std::size_t) (peq::slopeParam + band)] = (float) order / 6.0f;
            const auto current = (int) std::lround (params[(std::size_t) (peq::slopeParam + band)] * 6.0f) == order;
            menu.addItem (juce::String::fromUTF8 (peq::orderText (p, band).c_str()), true, current,
                          [set, band, order] { set ({ { peq::slopeParam + band, (float) order / 6.0f } }); });
        }
    }
    else if (! peq::flLaw (params) && (currentType == peq::BandType::lowCut || currentType == peq::BandType::highCut))
    {
        menu.addSeparator();
        for (int stages = 1; stages <= 4; ++stages)
            menu.addItem (juce::String (12 * stages) + " dB/oct", true, peq::slopeStages (params[(std::size_t) (peq::slopeParam + band)]) == stages,
                          [set, band, stages] { set ({ { peq::slopeParam + band, peq::slopeValue (stages) } }); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMousePosition());
}

juce::String bandText (const EffectParams& p, int band)
{
    const auto t = peq::type (p[(std::size_t) peq::param (band, 0)]);
    juce::String text;
    text << tr ("Band {0}: {1}", { juce::String (band + 1), lynx::bandTypeName (t) }) << "   "
         << juce::String::fromUTF8 (formatEffectParam (EffectKind::parametricEq, peq::param (band, 1), p[(std::size_t) peq::param (band, 1)], 120.0).c_str());
    if (lynx::hasGain (t))
        text << "   " << juce::String::fromUTF8 (formatEffectParam (EffectKind::parametricEq, peq::param (band, 2), p[(std::size_t) peq::param (band, 2)], 120.0).c_str());
    // FL's modes (imported Parametric EQ 2): bandwidth and FL's order for every band type
    if (peq::flLaw (p))
        text << "   " << juce::String::fromUTF8 (peq::widthText (p, band).c_str()) << "   " << juce::String::fromUTF8 (peq::orderText (p, band).c_str());
    else if (t == peq::BandType::lowCut || t == peq::BandType::highCut)
        text << "   " << juce::String::fromUTF8 (peq::orderText (p, band).c_str());
    else
        text << "   " << juce::String::fromUTF8 (peq::widthText (p, band).c_str());
    return text;
}
} // namespace peqview
} // namespace lynx
