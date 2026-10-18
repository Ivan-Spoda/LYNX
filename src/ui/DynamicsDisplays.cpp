#include "ui/DynamicsDisplays.h"

#include <cmath>
#include <cstring>

#include "core/DynamicsSuite.h"
#include "core/FacetEq.h"
#include "core/MixSuite.h"
#include "ui/I18n.h"
#include "ui/ParametricEqEditor.h"
#include "ui/Theme.h"

namespace lynx::dynui
{
namespace
{
using namespace theme;

float levelY (float db, juce::Rectangle<float> a, float lo = -60.0f)
{
    return a.getBottom() - a.getHeight() * juce::jlimit (0.0f, 1.0f, (db - lo) / -lo);
}

float peakDb (float linear) { return linear > 1e-6f ? 20.0f * std::log10 (linear) : -120.0f; }

void grid (juce::Graphics& g, juce::Rectangle<float> a, int columns, int rows)
{
    g.setColour (colour::gridBeat);
    for (int i = 1; i < columns; ++i)
        g.drawVerticalLine ((int) (a.getX() + a.getWidth() * (float) i / (float) columns), a.getY(), a.getBottom());
    for (int i = 1; i < rows; ++i)
        g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * (float) i / (float) rows), a.getX(), a.getRight());
}

// an input → output transfer curve over lo … 0 dB on both axes, with the live input level as a dot on it
void transferCurve (juce::Graphics& g, juce::Rectangle<float> a, float s, bool enabled, float lo,
                    const std::function<double (double)>& gainAt, float liveInPeak)
{
    grid (g, a, 6, 6);
    g.setColour (colour::gridBar);
    g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f); // unity
    juce::Path p, fill;
    const auto steps = juce::jmax (32, (int) a.getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const auto in = lo + (0.0f - lo) * (float) i / (float) steps;
        const auto out = in + (float) gainAt (in);
        const juce::Point<float> pt { a.getX() + a.getWidth() * (float) i / (float) steps, levelY (out, a, lo) };
        if (i == 0)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
    }
    fill = p;
    fill.lineTo (a.getRight(), a.getBottom());
    fill.lineTo (a.getX(), a.getBottom());
    fill.closeSubPath();
    const auto c = enabled ? colour::accent : colour::textDisabled;
    g.setColour (c.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * s)));
    const auto in = peakDb (liveInPeak);
    if (enabled && in > lo)
    {
        const auto x = a.getX() + a.getWidth() * (in - lo) / -lo;
        const auto y = levelY (in + (float) gainAt (in), a, lo);
        g.setColour (colour::textPrimary);
        g.fillEllipse (juce::Rectangle<float> (7.0f * s, 7.0f * s).withCentre ({ x, y }));
    }
    g.setColour (colour::textDisabled);
    g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
    g.drawText (tr ("Out"), a.withHeight (12.0f * s).toNearestInt(), juce::Justification::topLeft, false);
    g.drawText (tr ("In"), a.withTop (a.getBottom() - 12.0f * s).toNearestInt(), juce::Justification::bottomRight, false);
}

// log frequency axis 20 Hz … 20 kHz
float freqX (double hz, juce::Rectangle<float> a)
{
    return a.getX() + a.getWidth() * (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0));
}

void freqLabels (juce::Graphics& g, juce::Rectangle<float> a, float s)
{
    g.setColour (colour::textDisabled);
    g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
    for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const auto x = freqX (hz, a);
        g.setColour (colour::gridBeat);
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (colour::textDisabled);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (30.0f * s, 12.0f * s).withCentre ({ x, a.getBottom() - 6.0f * s }).toNearestInt(),
                    juce::Justification::centred, false);
    }
}
} // namespace

bool isLive (const char* id) noexcept
{
    if (id == nullptr)
        return false;
    for (const auto* live : { "grhistory", "levels", "prismbands", "crestcurve", "sentrycurve", "crestmeters" })
        if (std::strcmp (id, live) == 0)
            return true;
    return false;
}

bool paint (juce::Graphics& g, const juce::String& id, juce::Rectangle<float> r, float s, const EffectSlot& fx,
            const std::function<float (int)>& meter, const History& h)
{
    const auto a = r.reduced (6.0f * s);
    const auto enabled = fx.enabled;
    const auto kind = fx.kind;
    const auto& p = fx.params;

    if (id == "crestcurve")
    {
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        transferCurve (g, a, s, enabled, -60.0f, [&p] (double x) { return dyn::crest::staticGainDb (p, x); }, meter (2));
        return true;
    }
    if (id == "sentrycurve")
    {
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        transferCurve (g, a, s, enabled, -80.0f, [&p] (double x) { return std::max (-80.0, dyn::sentry::staticGainDb (p, x)); }, meter (2));
        return true;
    }
    if (id == "grhistory") // the last 6 s: input level (grey), output level (white line), gain reduction (red, from the top)
    {
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        grid (g, a, 6, 4);
        const auto n = History::length;
        const auto xAt = [&] (int i) { return a.getX() + a.getWidth() * (float) i / (float) (n - 1); };
        juce::Path in, out, gr;
        for (int i = 0; i < n; ++i)
        {
            const auto k = (std::size_t) ((h.pos + i) % n);
            const auto yi = levelY (peakDb (h.in[k]), a), yo = levelY (peakDb (h.out[k]), a);
            const auto yg = a.getY() + a.getHeight() * juce::jlimit (0.0f, 1.0f, h.gr[k] * 30.0f / 24.0f);
            if (i == 0)
            {
                in.startNewSubPath (xAt (i), a.getBottom());
                out.startNewSubPath (xAt (i), yo);
                gr.startNewSubPath (xAt (i), a.getY());
            }
            in.lineTo (xAt (i), yi);
            if (i > 0)
                out.lineTo (xAt (i), yo);
            gr.lineTo (xAt (i), yg);
        }
        in.lineTo (a.getRight(), a.getBottom());
        in.closeSubPath();
        gr.lineTo (a.getRight(), a.getY());
        gr.closeSubPath();
        g.setColour (colour::textSecondary.withAlpha (0.22f));
        g.fillPath (in);
        g.setColour (colour::textPrimary.withAlpha (enabled ? 0.85f : 0.3f));
        g.strokePath (out, juce::PathStrokeType (juce::jmax (1.0f, 1.2f * s)));
        g.setColour ((enabled ? colour::accent : colour::textDisabled).withAlpha (0.55f));
        g.fillPath (gr);
        // the threshold across the levels
        const auto thr = kind == EffectKind::crestCompressor ? (float) dyn::crest::thresholdDb (p[dyn::crest::threshold])
                         : kind == EffectKind::sentryGate ? (float) dyn::sentry::thresholdDb (p[dyn::sentry::threshold]) : 1.0f;
        if (thr < 0.5f)
        {
            const auto y = levelY (thr, a);
            g.setColour (colour::textSecondary.withAlpha (0.7f));
            const float dashes[] { 4.0f * s, 3.0f * s };
            g.drawDashedLine (juce::Line<float> (a.getX(), y, a.getRight(), y), dashes, 2, 1.0f);
            g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
            g.drawText (juce::String (thr, 1) + " dB", juce::Rectangle<float> (a.getX() + 4.0f * s, y - 13.0f * s, 60.0f * s, 12.0f * s).toNearestInt(),
                        juce::Justification::centredLeft, false);
        }
        g.setColour (colour::textDisabled);
        g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
        g.drawText (tr ("Gain reduction"), a.reduced (2.0f * s).toNearestInt(), juce::Justification::topRight, false);
        return true;
    }
    if (id == "crestmeters") // tall input / output / gain-reduction bars with a dB scale
    {
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        const auto scaleW = 24.0f * s;
        const auto bars = a.withTrimmedRight (scaleW);
        const auto w = bars.getWidth() / 3.0f;
        const char* names[3] { TR_NOOP ("In"), TR_NOOP ("Out"), TR_NOOP ("GR") };
        const auto labelH = 12.0f * s;
        const auto area = bars.withTrimmedTop (labelH);
        for (int k = 0; k < 3; ++k)
        {
            const auto col = juce::Rectangle<float> (bars.getX() + w * (float) k, area.getY(), w, area.getHeight()).reduced (2.0f * s, 0.0f);
            g.setColour (colour::bg2);
            g.fillRect (col);
            if (k < 2)
            {
                const auto db = peakDb (meter (2 + k));
                const auto top = levelY (db, col);
                g.setColour (db > -0.5f ? colour::meterClip : (db > -12.0f ? colour::meterMid : colour::meterLow));
                g.fillRect (col.withTop (top));
            }
            else // reduction hangs from the top, 0 … 30 dB over the full height
            {
                const auto hgt = col.getHeight() * juce::jlimit (0.0f, 1.0f, meter (0));
                g.setColour (enabled ? colour::accent : colour::textDisabled);
                g.fillRect (col.withHeight (hgt));
            }
            g.setColour (colour::textSecondary);
            g.setFont (font (juce::jmax (7.0f, 9.0f * s), true));
            g.drawText (caps (tr (names[k])), juce::Rectangle<float> (col.getX(), bars.getY(), col.getWidth(), labelH).toNearestInt(),
                        juce::Justification::centred, false);
        }
        g.setFont (font (juce::jmax (7.0f, 8.0f * s)));
        for (int db = 0; db >= -60; db -= 12)
        {
            const auto y = levelY ((float) db, area);
            g.setColour (colour::textDisabled);
            g.drawText (juce::String (db), juce::Rectangle<float> (bars.getRight() + 2.0f * s, y - 6.0f * s, scaleW - 2.0f * s, 12.0f * s).toNearestInt(),
                        juce::Justification::centredLeft, false);
        }
        return true;
    }
    if (id == "levels") // input and output peak, −60 … 0 dBFS
    {
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        const auto rows = a.reduced (0.0f, 1.0f * s);
        const auto h2 = rows.getHeight() * 0.5f;
        const char* names[2] { TR_NOOP ("In"), TR_NOOP ("Out") };
        for (int k = 0; k < 2; ++k)
        {
            const auto row = juce::Rectangle<float> (rows.getX(), rows.getY() + h2 * (float) k, rows.getWidth(), h2 - 2.0f * s);
            const auto label = row.withWidth (26.0f * s);
            const auto bar = row.withTrimmedLeft (28.0f * s);
            const auto db = peakDb (meter (2 + k));
            const auto f = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            g.setColour (colour::bg2);
            g.fillRect (bar);
            g.setColour (db > -0.5f ? colour::meterClip : (db > -12.0f ? colour::meterMid : colour::meterLow));
            g.fillRect (bar.withWidth (bar.getWidth() * f));
            g.setColour (colour::textSecondary);
            g.setFont (font (juce::jmax (7.0f, juce::jmin (9.0f * s, row.getHeight())), true));
            g.drawText (caps (tr (names[k])), label.toNearestInt(), juce::Justification::centredLeft, false);
        }
        return true;
    }
    if (id == "crestsceq") // the side-chain EQ's curve (Facet EQ prototypes), ±24 dB
    {
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        freqLabels (g, a, s);
        EffectParams fp {};
        for (int b = 0; b < dyn::crest::scBands; ++b)
            for (int f = 0; f < 5; ++f)
                fp[(std::size_t) facet::param (b, f)] = p[(std::size_t) dyn::crest::scParam (b, f)];
        fp[(std::size_t) facet::outputParam] = 0.5f;
        fp[(std::size_t) facet::scaleParam] = 0.5f;
        juce::Path c;
        for (int i = 0; i <= 200; ++i)
        {
            const auto hz = 20.0 * std::pow (1000.0, i / 200.0);
            const auto db = (float) facet::responseDb (fp, 48000.0, hz);
            const juce::Point<float> pt { freqX (hz, a), a.getCentreY() - a.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / 24.0f) };
            if (i == 0)
                c.startNewSubPath (pt);
            else
                c.lineTo (pt);
        }
        g.setColour (colour::gridBar);
        g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
        g.setColour (enabled ? colour::info : colour::textDisabled);
        g.strokePath (c, juce::PathStrokeType (juce::jmax (1.5f, 1.8f * s)));
        g.setColour (colour::textDisabled);
        g.setFont (font (juce::jmax (8.0f, 9.0f * s)));
        g.drawText (tr ("Detector EQ"), a.reduced (2.0f * s).toNearestInt(), juce::Justification::topLeft, false);
        return true;
    }
    if (id == "prismbands") // the four bands, their live gain change and the effect's response with those gains
    {
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        using namespace dyn::prism;
        const double xo[3] { crossoverHz (p[xover1]), crossoverHz (p[xover2]), crossoverHz (p[xover3]) };
        const float edges[5] { a.getX(), freqX (xo[0], a), freqX (xo[1], a), freqX (xo[2], a), a.getRight() };
        double live[bands] {};
        for (int b = 0; b < bands; ++b)
        {
            const auto on = p[(std::size_t) param (b, fEnabled)] >= 0.5f;
            live[b] = on ? (double) meter (b) * 30.0 + levelDb (p[(std::size_t) param (b, fLevel)]) : 0.0;
            const auto area = juce::Rectangle<float> (edges[b], a.getY(), juce::jmax (1.0f, edges[b + 1] - edges[b]), a.getHeight());
            const auto c = peqview::bandColour (b);
            g.setColour (c.withAlpha (on && enabled ? 0.10f : 0.03f));
            g.fillRect (area);
            // the live gain change: a bar from the middle line (up = boost, down = reduction), ±18 dB
            const auto mid = a.getCentreY();
            const auto gainChange = on ? (float) meter (b) * 30.0f : 0.0f;
            const auto hBar = a.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, gainChange / 18.0f);
            const auto bar = juce::Rectangle<float> (area.getCentreX() - 10.0f * s, hBar < 0 ? mid : mid - hBar, 20.0f * s, std::abs (hBar));
            g.setColour (c.withAlpha (enabled ? 0.75f : 0.25f));
            g.fillRect (bar);
            g.setColour (on ? colour::textSecondary : colour::textDisabled);
            g.setFont (font (juce::jmax (8.0f, 10.0f * s), true));
            g.drawText (juce::String (tr ("Band")) + " " + juce::String (b + 1) + "  " + juce::String (gainChange, 1) + " dB",
                        area.reduced (3.0f * s).withHeight (14.0f * s).toNearestInt(), juce::Justification::centred, false);
        }
        freqLabels (g, a, s);
        g.setColour (colour::textSecondary);
        for (int k = 1; k <= 3; ++k)
            g.drawVerticalLine ((int) edges[k], a.getY(), a.getBottom());
        g.setColour (colour::gridBar);
        g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
        juce::Path c;
        for (int i = 0; i <= 240; ++i)
        {
            const auto hz = 20.0 * std::pow (1000.0, i / 240.0);
            const auto db = (float) (responseDb (p, live, 48000.0, hz) - trimDb (p[output]));
            const juce::Point<float> pt { freqX (hz, a), a.getCentreY() - a.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / 18.0f) };
            if (i == 0)
                c.startNewSubPath (pt);
            else
                c.lineTo (pt);
        }
        g.setColour (enabled ? colour::accent : colour::textDisabled);
        g.strokePath (c, juce::PathStrokeType (juce::jmax (1.5f, 2.0f * s)));
        return true;
    }
    if (id == "emberbands" && kind == EffectKind::ember) // Ember's multiband split
    {
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, radius::md * s);
        using namespace mixsuite::ember;
        const auto nb = bandCount (p[bands]);
        const double xo[3] { crossoverHz (p[xover1]), crossoverHz (p[xover2]), crossoverHz (p[xover3]) };
        std::array<float, 5> edges { a.getX(), a.getRight(), a.getRight(), a.getRight(), a.getRight() };
        for (int k = 1; k < nb; ++k)
            edges[(std::size_t) k] = freqX (xo[k - 1], a);
        edges[(std::size_t) nb] = a.getRight();
        static const char* styles[] { TR_NOOP ("Tube"), TR_NOOP ("Tape"), TR_NOOP ("Transistor"), TR_NOOP ("Fuzz") };
        for (int b = 0; b < nb; ++b)
        {
            const auto area = juce::Rectangle<float> (edges[(std::size_t) b], a.getY(), juce::jmax (1.0f, edges[(std::size_t) b + 1] - edges[(std::size_t) b]), a.getHeight());
            const auto c = peqview::bandColour (b);
            const auto drive = (float) driveDb (p[(std::size_t) bandParam (b, 1)]);
            g.setColour (c.withAlpha (enabled ? 0.08f + 0.25f * drive / 36.0f : 0.03f));
            g.fillRect (area);
            g.setColour (colour::textSecondary);
            g.setFont (font (juce::jmax (7.0f, 9.0f * s), true));
            g.drawFittedText (caps (tr (styles[(int) styleOf (p[(std::size_t) bandParam (b, 0)])])) + "\n" + juce::String (drive, 1) + " dB",
                              area.reduced (2.0f * s).toNearestInt(), juce::Justification::centred, 2, 0.6f);
        }
        g.setColour (colour::textSecondary);
        for (int k = 1; k < nb; ++k)
            g.drawVerticalLine ((int) edges[(std::size_t) k], a.getY(), a.getBottom());
        return true;
    }
    return false;
}
namespace
{
// the crossover parameters and their frequencies of a band display's effect
int crossoverParams (const EffectSlot& fx, int* params, double* hz)
{
    if (fx.kind == EffectKind::prismMultiband)
    {
        using namespace dyn::prism;
        const int ps[3] { xover1, xover2, xover3 };
        for (int k = 0; k < 3; ++k)
        {
            params[k] = ps[k];
            hz[k] = crossoverHz (fx.params[(std::size_t) ps[k]]);
        }
        return 3;
    }
    if (fx.kind == EffectKind::ember)
    {
        using namespace mixsuite::ember;
        const int ps[3] { xover1, xover2, xover3 };
        const auto n = bandCount (fx.params[bands]) - 1;
        for (int k = 0; k < n; ++k)
        {
            params[k] = ps[k];
            hz[k] = crossoverHz (fx.params[(std::size_t) ps[k]]);
        }
        return n;
    }
    return 0;
}
} // namespace

int crossoverNear (const EffectSlot& fx, juce::Rectangle<float> r, float x, float tolerance)
{
    const auto a = r.reduced (6.0f);
    int params[3];
    double hz[3];
    const auto n = crossoverParams (fx, params, hz);
    int best = -1;
    float bestDist = tolerance;
    for (int k = 0; k < n; ++k)
        if (const auto d = std::abs (freqX (hz[k], a) - x); d <= bestDist)
        {
            bestDist = d;
            best = params[k];
        }
    return best;
}

int bandAt (const EffectSlot& fx, juce::Rectangle<float> r, float x)
{
    const auto a = r.reduced (6.0f);
    int params[3];
    double hz[3];
    const auto n = crossoverParams (fx, params, hz);
    int band = 0;
    for (int k = 0; k < n; ++k)
        if (x > freqX (hz[k], a))
            band = k + 1;
    return band;
}

float crossoverValueAt (const EffectSlot& fx, juce::Rectangle<float> r, float x)
{
    const auto a = r.reduced (6.0f);
    const auto hz = 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - a.getX()) / a.getWidth())));
    return fx.kind == EffectKind::ember ? mixsuite::ember::crossoverValue (hz) : dyn::prism::crossoverValue (hz);
}
} // namespace lynx::dynui
