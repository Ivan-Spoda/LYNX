#include "ui/PluginSkin.h"

#include "ui/WindowOwner.h"

namespace lynx::skin
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;
constexpr float arcStart = -pi * 0.75f;
constexpr float arcRange = pi * 1.5f;

juce::Path arcPath (juce::Point<float> c, float r, float from, float to)
{
    juce::Path p;
    p.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
    return p;
}

juce::PathStrokeType roundStroke (float w)
{
    return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
}
} // namespace

juce::Colour accentFor (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::reverb: case EffectKind::reeverb2: case EffectKind::reeverb1: case EffectKind::luxeVerb: case EffectKind::convolver:
        case EffectKind::epochVerb: case EffectKind::atrium: case EffectKind::foilPlate: case EffectKind::nebula: case EffectKind::halo:
        case EffectKind::horizon:
            return colour::violet;
        case EffectKind::delay: case EffectKind::fruityDelay2: case EffectKind::fruityDelay3: case EffectKind::fruityDelayBank:
        case EffectKind::multibandDelay: case EffectKind::echoform: case EffectKind::spiralEcho: case EffectKind::geminiDoubler:
        case EffectKind::newtime:
            return colour::azure;
        case EffectKind::chorus: case EffectKind::fruityChorus: case EffectKind::hyperChorus: case EffectKind::vintageChorus:
        case EffectKind::fruityFlanger: case EffectKind::fruityFlangus: case EffectKind::fruityPhaser: case EffectKind::vintagePhaser:
        case EffectKind::orbit: case EffectKind::latticeMod: case EffectKind::autoPan: case EffectKind::fruityPanOMatic:
        case EffectKind::effector: case EffectKind::freqShift: case EffectKind::grossBeat: case EffectKind::volumeGate: case EffectKind::rhythmGain:
            return colour::teal;
        case EffectKind::compressor: case EffectKind::limiter: case EffectKind::fruityLimiter: case EffectKind::fruityCompressor:
        case EffectKind::fruityMultiband: case EffectKind::crestCompressor: case EffectKind::prismMultiband: case EffectKind::sentryGate:
        case EffectKind::apexLimiter: case EffectKind::glowLeveler: case EffectKind::hushDeEsser: case EffectKind::transientProcessor:
        case EffectKind::maximus: case EffectKind::peakController:
            return colour::amber;
        case EffectKind::distortion: case EffectKind::fruityBloodOverdrive: case EffectKind::fruityFastDist: case EffectKind::ember:
        case EffectKind::hardcore: case EffectKind::distructor: case EffectKind::fruityWaveShaper: case EffectKind::fruitySoftClipper:
        case EffectKind::lofi: case EffectKind::squeeze:
            return colour::ember;
        case EffectKind::lynxTune: case EffectKind::pitchCorrect: case EffectKind::pitchShift: case EffectKind::flPitchShifter:
        case EffectKind::vocoder: case EffectKind::vocodex: case EffectKind::quellDenoiser: case EffectKind::aether: case EffectKind::newtone:
            return colour::rose;
        case EffectKind::utility: case EffectKind::stereoShaper: case EffectKind::stereoEnhancer: case EffectKind::spreader:
        case EffectKind::fruityBalance: case EffectKind::fruitySend:
            return colour::lime;
        default:
            return colour::crimson;
    }
}

juce::Colour accentForInstrument (const juce::String& name)
{
    if (name.containsIgnoreCase ("Keys") || name.containsIgnoreCase ("Piano")) return colour::azure;
    if (name.containsIgnoreCase ("Bass")) return colour::ember;
    if (name.containsIgnoreCase ("Harmor") || name.containsIgnoreCase ("Sytrus")) return colour::violet;
    return colour::crimson;
}

// --- surfaces ------------------------------------------------------------------------------------

void canvas (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour accent)
{
    g.setGradientFill (juce::ColourGradient (colour::canvasTop, 0.0f, a.getY(), colour::canvasBottom, 0.0f, a.getBottom(), false));
    g.fillRect (a);
    // a wide, faint pool of the accent under the header
    juce::ColourGradient glow (accent.withAlpha (0.075f), a.getCentreX(), a.getY(), accent.withAlpha (0.0f), a.getCentreX(),
                               a.getY() + juce::jmin (a.getHeight(), a.getWidth() * 0.45f), true);
    g.setGradientFill (glow);
    g.fillRect (a.withHeight (juce::jmin (a.getHeight(), a.getWidth() * 0.45f)));
}

void headerBar (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour accent)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff292c34), 0.0f, a.getY(), juce::Colour (0xff1c1e24), 0.0f, a.getBottom(), false));
    g.fillRect (a);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) a.getY(), a.getX(), a.getRight());
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawHorizontalLine ((int) a.getBottom() - 1, a.getX(), a.getRight());
    juce::ColourGradient line (accent.withAlpha (0.9f), a.getX(), 0.0f, accent.withAlpha (0.0f), a.getX() + juce::jmin (a.getWidth(), 420.0f), 0.0f, false);
    g.setGradientFill (line);
    g.fillRect (juce::Rectangle<float> (a.getX(), a.getBottom() - 2.0f, juce::jmin (a.getWidth(), 420.0f), 1.0f));
}

void brandMark (juce::Graphics& g, juce::Rectangle<float> a)
{
    const auto d = juce::jmin (a.getWidth(), a.getHeight());
    if (d < 6.0f) return;
    // the logo resampled once per size (×2 for high-DPI)
    static std::map<int, juce::Image> cache;
    const auto px = juce::jlimit (8, 128, (int) std::ceil (d * 2.0f));
    auto& img = cache[px];
    if (img.isNull())
    {
        const auto logo = logoImage();
        if (logo.isNull()) return;
        img = logo.rescaled (px, px, juce::Graphics::highResamplingQuality);
    }
    const auto r = a.withSizeKeepingCentre (d, d);
    g.setColour (theme::colour::accent.withAlpha (0.22f));
    g.fillEllipse (r.expanded (1.5f));
    g.setOpacity (1.0f);
    g.drawImage (img, r, juce::RectanglePlacement::centred);
}

float titleText (juce::Graphics& g, juce::Rectangle<float> a, const juce::String& name, juce::Colour accent, bool underline)
{
    g.setFont (theme::font (theme::type::title, true));
    const auto w = juce::jmin (a.getWidth(), juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawText (name, a.withWidth (w).translated (0.0f, 1.0f), juce::Justification::centredLeft, true);
    g.setColour (theme::colour::textPrimary);
    g.drawText (name, a.withWidth (w), juce::Justification::centredLeft, true);
    if (underline)
    {
        const auto line = juce::Rectangle<float> (a.getX(), a.getBottom() - 3.0f, w, 2.0f);
        g.setGradientFill (juce::ColourGradient (accent, line.getX(), 0.0f, accent.withAlpha (0.15f), line.getRight(), 0.0f, false));
        g.fillRoundedRectangle (line, 1.0f);
    }
    return w;
}

void powerLed (juce::Graphics& g, juce::Point<float> c, float d, bool on)
{
    const auto r = juce::Rectangle<float> (d, d).withCentre (c);
    if (on)
    {
        juce::ColourGradient halo (theme::colour::ledOn.withAlpha (0.45f), c.x, c.y, theme::colour::ledOn.withAlpha (0.0f), c.x + d * 1.4f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (r.expanded (d * 0.9f));
    }
    g.setColour (on ? theme::colour::ledOn : theme::colour::ledOff);
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.55f : 0.12f));
    g.fillEllipse (r.reduced (d * 0.3f).translated (-d * 0.1f, -d * 0.12f));
}

void section (juce::Graphics& g, juce::Rectangle<float> a, float corner, bool raised)
{
    const auto top = raised ? juce::Colour (0xff2a2d35) : juce::Colour (0xff1f2127);
    const auto bottom = raised ? juce::Colour (0xff22252c) : juce::Colour (0xff18191e);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (a.translated (0.0f, 1.5f), corner);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, a.getY(), bottom, 0.0f, a.getBottom(), false));
    g.fillRoundedRectangle (a, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.09f), 0.0f, a.getY(), juce::Colours::white.withAlpha (0.015f), 0.0f,
                                             a.getY() + juce::jmin (40.0f, a.getHeight()), false));
    g.drawRoundedRectangle (a.reduced (0.5f), corner, 1.0f);
}

void sectionTitle (juce::Graphics& g, juce::Rectangle<float> strip, const juce::String& text, juce::Colour accent, float fontSize)
{
    g.setFont (theme::font (fontSize, true));
    const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
    const auto dotD = juce::jmax (3.0f, fontSize * 0.42f);
    const auto x0 = strip.getCentreX() - (w + dotD + 6.0f) * 0.5f;
    g.setColour (accent.withAlpha (0.9f));
    g.fillEllipse (juce::Rectangle<float> (dotD, dotD).withCentre ({ x0 + dotD * 0.5f, strip.getCentreY() }));
    g.setColour (juce::Colour (0xffb9bdc6));
    g.drawText (text, juce::Rectangle<float> (x0 + dotD + 6.0f, strip.getY(), w + 4.0f, strip.getHeight()), juce::Justification::centredLeft, false);
    // hairlines to the edges
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    const auto y = (int) strip.getCentreY();
    if (x0 - 8.0f > strip.getX() + 8.0f) g.drawHorizontalLine (y, strip.getX() + 8.0f, x0 - 6.0f);
    if (x0 + dotD + w + 14.0f < strip.getRight() - 8.0f) g.drawHorizontalLine (y, x0 + dotD + w + 14.0f, strip.getRight() - 8.0f);
}

void well (juce::Graphics& g, juce::Rectangle<float> a, float corner)
{
    g.setGradientFill (juce::ColourGradient (colour::wellTop, 0.0f, a.getY(), colour::wellBottom, 0.0f, a.getBottom(), false));
    g.fillRoundedRectangle (a, corner);
    // inner shadow along the top edge
    const auto sh = juce::jmin (8.0f, a.getHeight() * 0.3f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, a.getY(), juce::Colours::transparentBlack, 0.0f, a.getY() + sh, false));
    g.fillRoundedRectangle (a.withHeight (sh), corner);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (a.reduced (0.5f), corner, 1.0f);
    // the lip catching light at the bottom
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine ((int) a.getBottom(), a.getX() + corner, a.getRight() - corner);
}

void floatingPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour edge, float corner)
{
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 18, { 0, 6 }).drawForRectangle (g, r.toNearestInt());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xf6262930), 0.0f, r.getY(), juce::Colour (0xf61a1c21), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), 0.0f, r.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f,
                                             r.getY() + juce::jmin (30.0f, r.getHeight()), false));
    g.drawRoundedRectangle (r.reduced (1.5f), corner - 1.0f, 1.0f);
    g.setColour (edge.withAlpha (0.65f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.2f);
}

void bottomBar (juce::Graphics& g, juce::Rectangle<float> a)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1e2026), 0.0f, a.getY(), juce::Colour (0xff17181d), 0.0f, a.getBottom(), false));
    g.fillRect (a);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawHorizontalLine ((int) a.getY(), a.getX(), a.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine ((int) a.getY() + 1, a.getX(), a.getRight());
}

// --- controls ------------------------------------------------------------------------------------

void knob (juce::Graphics& g, juce::Rectangle<float> bounds, const KnobLook& k)
{
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    if (size <= 4.0f) return;
    const auto area = bounds.withSizeKeepingCentre (size, size);
    const auto c = area.getCentre();
    const auto small = size < 26.0f;
    const auto thickness = juce::jlimit (2.0f, 5.0f, size * 0.075f);
    const auto arcR = size * 0.5f - thickness * (small ? 0.6f : 1.05f);
    const auto value = juce::jlimit (0.0f, 1.0f, k.value);
    const auto accent = k.enabled ? (k.active ? k.accent.brighter (0.25f) : k.accent) : theme::colour::textDisabled;
    const auto hover = juce::jlimit (0.0f, 1.0f, k.active ? 1.0f : k.hover);

    // the track: a dark groove with the unlit ring in it
    const auto track = arcPath (c, arcR, arcStart, arcStart + arcRange);
    g.setColour (juce::Colour (0xff0a0b0e));
    g.strokePath (track, roundStroke (thickness + 2.0f));
    g.setColour (theme::colour::knobTrack.interpolatedWith (juce::Colour (0xff59606e), hover * 0.6f));
    g.strokePath (track, roundStroke (thickness));

    // the lit value arc with its glow
    const auto from = k.bipolar ? arcStart + arcRange * 0.5f : arcStart;
    const auto to = arcStart + arcRange * value;
    if (std::abs (to - from) > 0.002f)
    {
        const auto arc = arcPath (c, arcR, from, to);
        if (k.enabled && ! small)
        {
            g.setColour (accent.withAlpha (0.10f + 0.10f * hover));
            g.strokePath (arc, roundStroke (thickness * 3.0f));
            g.setColour (accent.withAlpha (0.20f + 0.12f * hover));
            g.strokePath (arc, roundStroke (thickness * 1.8f));
        }
        g.setGradientFill (juce::ColourGradient (accent.darker (0.25f), area.getX(), area.getBottom(), accent.brighter (0.2f), area.getRight(), area.getY(), false));
        g.strokePath (arc, roundStroke (thickness));
        if (! small)
        {
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.strokePath (arc, roundStroke (thickness * 0.3f));
        }
    }
    if (k.bipolar) // the centre mark
    {
        g.setColour (theme::colour::textSecondary);
        const auto m = juce::jmax (2.0f, thickness * 0.55f);
        g.fillEllipse (juce::Rectangle<float> (m, m).withCentre ({ c.x, c.y - arcR - thickness * (small ? 0.9f : 1.3f) }));
    }

    // the cap: a soft shadow, a machined body lit from above, a rim
    const auto capR = juce::jmax (2.0f, arcR - thickness * (small ? 1.3f : 1.6f));
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    if (! small)
    {
        juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.55f), c.x, c.y + capR * 0.18f, juce::Colours::transparentBlack, c.x + capR * 1.25f,
                                     c.y + capR * 0.18f, true);
        g.setGradientFill (shadow);
        g.fillEllipse (cap.expanded (capR * 0.25f).translated (0.0f, capR * 0.12f));
    }
    const auto lift = hover * 0.08f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff434853).brighter (lift), c.x, cap.getY(), juce::Colour (0xff1d1f25), c.x, cap.getBottom(), false));
    g.fillEllipse (cap);
    if (capR > 9.0f) // the inner face, slightly concave
    {
        const auto face = cap.reduced (capR * 0.13f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff272a31).brighter (lift), c.x, face.getY(), juce::Colour (0xff353943).brighter (lift), c.x,
                                                 face.getBottom(), false));
        g.fillEllipse (face);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), c.x, cap.getY(), juce::Colours::black.withAlpha (0.5f), c.x, cap.getBottom(), false));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);

    // the pointer
    const juce::Point<float> dir { std::sin (to), -std::cos (to) };
    const auto inner = capR * (capR > 8.0f ? 0.35f : 0.1f);
    const auto outer = capR * 0.84f;
    const auto pw = juce::jlimit (1.5f, 3.0f, size * 0.05f);
    juce::Path pointer;
    pointer.startNewSubPath (c + dir * inner);
    pointer.lineTo (c + dir * outer);
    if (k.enabled && (k.active || hover > 0.01f) && ! small)
    {
        g.setColour (accent.withAlpha (0.35f * hover));
        g.strokePath (pointer, roundStroke (pw * 2.6f));
    }
    g.setColour (k.enabled ? (k.active ? accent.brighter (0.6f) : theme::colour::textPrimary) : theme::colour::textSecondary);
    g.strokePath (pointer, roundStroke (pw));
}

void slider (juce::Graphics& g, juce::Rectangle<float> t, float value01, bool vertical, bool bipolar, juce::Colour accent, bool hot, bool enabled,
             float thumbLength, float thumbThickness)
{
    const auto v = juce::jlimit (0.0f, 1.0f, value01);
    const auto thick = vertical ? t.getWidth() : t.getHeight();
    g.setColour (juce::Colour (0xff08090b));
    g.fillRoundedRectangle (t.expanded (1.0f), thick * 0.5f + 1.0f);
    g.setColour (theme::colour::knobTrack.darker (0.35f));
    g.fillRoundedRectangle (t, thick * 0.5f);
    juce::Rectangle<float> fill;
    juce::Point<float> thumb;
    if (vertical)
    {
        const auto y = t.getBottom() - t.getHeight() * v;
        const auto from = bipolar ? t.getCentreY() : t.getBottom();
        fill = { t.getX(), juce::jmin (y, from), t.getWidth(), std::abs (from - y) };
        thumb = { t.getCentreX(), y };
    }
    else
    {
        const auto x = t.getX() + t.getWidth() * v;
        const auto from = bipolar ? t.getCentreX() : t.getX();
        fill = { juce::jmin (x, from), t.getY(), std::abs (from - x), t.getHeight() };
        thumb = { x, t.getCentreY() };
    }
    const auto c = enabled ? accent : theme::colour::textDisabled;
    if (enabled && ! fill.isEmpty())
    {
        g.setColour (c.withAlpha (hot ? 0.3f : 0.18f));
        g.fillRoundedRectangle (fill.expanded (thick * 0.8f), thick);
    }
    g.setGradientFill (vertical ? juce::ColourGradient (c.brighter (0.2f), 0.0f, fill.getY(), c.darker (0.3f), 0.0f, fill.getBottom(), false)
                                : juce::ColourGradient (c.darker (0.3f), fill.getX(), 0.0f, c.brighter (0.2f), fill.getRight(), 0.0f, false));
    g.fillRoundedRectangle (fill, thick * 0.5f);

    // the thumb: a small raised cap with a lit line
    const auto tr = vertical ? juce::Rectangle<float> (thumbLength, thumbThickness).withCentre (thumb)
                             : juce::Rectangle<float> (thumbThickness, thumbLength).withCentre (thumb);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (tr.translated (0.0f, 1.5f), 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff545a66).brighter (hot ? 0.15f : 0.0f), 0.0f, tr.getY(), juce::Colour (0xff2b2e36), 0.0f,
                                             tr.getBottom(), false));
    g.fillRoundedRectangle (tr, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawRoundedRectangle (tr.reduced (0.5f), 3.0f, 1.0f);
    g.setColour (hot && enabled ? c.brighter (0.4f) : theme::colour::textPrimary.withAlpha (0.85f));
    if (vertical) g.fillRect (juce::Rectangle<float> (tr.getX() + 3.0f, thumb.y - 0.75f, tr.getWidth() - 6.0f, 1.5f));
    else g.fillRect (juce::Rectangle<float> (thumb.x - 0.75f, tr.getY() + 3.0f, 1.5f, tr.getHeight() - 6.0f));
}

void button (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool hover, juce::Colour accent, float corner)
{
    if (lit)
    {
        g.setColour (accent.withAlpha (hover ? 0.28f : 0.18f));
        g.drawRoundedRectangle (r.expanded (1.5f), corner + 1.5f, 2.0f);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.42f), 0.0f, r.getY(), accent.withAlpha (0.22f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, corner);
        g.setColour (accent.brighter (0.15f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        return;
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (hover ? 0xff3c404a : 0xff33363e), 0.0f, r.getY(), juce::Colour (hover ? 0xff2d3038 : 0xff26292f), 0.0f,
                                             r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (hover ? 0.12f : 0.07f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + corner, r.getRight() - corner);
}

void switchPill (juce::Graphics& g, juce::Rectangle<float> box, bool on, juce::Colour accent, bool enabled)
{
    const auto c = enabled ? accent : theme::colour::textSecondary;
    if (on)
    {
        g.setColour (c.withAlpha (0.25f));
        g.fillRoundedRectangle (box.expanded (1.5f), box.getHeight() * 0.5f + 1.5f);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.15f), 0.0f, box.getY(), c.darker (0.2f), 0.0f, box.getBottom(), false));
    }
    else
        g.setColour (juce::Colour (0xff0b0c0f));
    g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, 1.0f);
    const auto d = box.getHeight() - 2.0f;
    const auto knobR = juce::Rectangle<float> (d, d).withCentre ({ on ? box.getRight() - d * 0.5f - 1.0f : box.getX() + d * 0.5f + 1.0f, box.getCentreY() });
    g.setGradientFill (juce::ColourGradient (on ? juce::Colours::white : juce::Colour (0xff6a707c), 0.0f, knobR.getY(), on ? juce::Colour (0xffd9dce2) : juce::Colour (0xff474c56),
                                             0.0f, knobR.getBottom(), false));
    g.fillEllipse (knobR);
}

void selector (juce::Graphics& g, juce::Rectangle<float> r, bool hover, float corner)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (hover ? 0xff40444f : 0xff363a43), 0.0f, r.getY(), juce::Colour (hover ? 0xff2f323a : 0xff292c33), 0.0f,
                                             r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (hover ? 0.14f : 0.08f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + corner, r.getRight() - corner);
}

void meter (juce::Graphics& g, juce::Rectangle<float> r, float level01, float peak01, bool vertical)
{
    g.setColour (juce::Colour (0xff08090b));
    g.fillRoundedRectangle (r, 2.0f);
    const auto lv = juce::jlimit (0.0f, 1.0f, level01);
    const auto grad = vertical ? juce::ColourGradient (theme::colour::meterLow, 0.0f, r.getBottom(), theme::colour::meterClip, 0.0f, r.getY(), false)
                               : juce::ColourGradient (theme::colour::meterLow, r.getX(), 0.0f, theme::colour::meterClip, r.getRight(), 0.0f, false);
    auto gr = grad;
    gr.addColour (0.72, theme::colour::meterMid);
    if (lv > 0.0f)
    {
        const auto lit = vertical ? r.withTop (r.getBottom() - r.getHeight() * lv) : r.withWidth (r.getWidth() * lv);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (lit, 2.0f);
        // segment gaps
        g.setColour (juce::Colour (0xff08090b).withAlpha (0.6f));
        if (vertical)
            for (auto y = r.getBottom() - 3.0f; y > lit.getY(); y -= 3.0f) g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        else
            for (auto x = r.getX() + 3.0f; x < lit.getRight(); x += 3.0f) g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
    }
    const auto pk = juce::jlimit (0.0f, 1.0f, peak01);
    if (pk > 0.01f)
    {
        g.setColour (pk > 0.97f ? theme::colour::meterClip : juce::Colours::white.withAlpha (0.85f));
        if (vertical) g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom() - r.getHeight() * pk - 1.0f, r.getWidth(), 2.0f));
        else g.fillRect (juce::Rectangle<float> (r.getX() + r.getWidth() * pk - 1.0f, r.getY(), 2.0f, r.getHeight()));
    }
}

void meterSolid (juce::Graphics& g, juce::Rectangle<float> r, float level01, juce::Colour c, bool fromTop)
{
    g.setColour (juce::Colour (0xff08090b));
    g.fillRoundedRectangle (r, 2.0f);
    const auto lv = juce::jlimit (0.0f, 1.0f, level01);
    if (lv <= 0.0f) return;
    const auto lit = fromTop ? r.withHeight (r.getHeight() * lv) : r.withTop (r.getBottom() - r.getHeight() * lv);
    g.setGradientFill (juce::ColourGradient (c.brighter (0.2f), 0.0f, lit.getY(), c.darker (0.3f), 0.0f, lit.getBottom(), false));
    g.fillRoundedRectangle (lit, 2.0f);
}

void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float thickness, float glow)
{
    if (glow > 0.0f)
    {
        g.setColour (c.withAlpha (0.10f * glow));
        g.strokePath (p, roundStroke (thickness * 5.0f));
        g.setColour (c.withAlpha (0.20f * glow));
        g.strokePath (p, roundStroke (thickness * 2.4f));
    }
    g.setColour (c);
    g.strokePath (p, roundStroke (thickness));
}

void valuePlate (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff08090c), 0.0f, r.getY(), juce::Colour (0xff111217), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine ((int) r.getBottom(), r.getX() + corner, r.getRight() - corner);
}
} // namespace lynx::skin
