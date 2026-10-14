#include "ui/Icons.h"

#include <map>

#include "ui/Theme.h"

namespace lynx
{
namespace icons
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

void line (juce::Path& p, float x1, float y1, float x2, float y2)
{
    p.startNewSubPath (x1, y1);
    p.lineTo (x2, y2);
}

void roundRect (juce::Path& p, float x, float y, float w, float h, float r)
{
    p.addRoundedRectangle (x, y, w, h, r);
}

void polyline (juce::Path& p, std::initializer_list<juce::Point<float>> points, bool close = false)
{
    bool first = true;
    for (auto pt : points)
    {
        if (first)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
        first = false;
    }
    if (close)
        p.closeSubPath();
}

Shape make (Icon icon)
{
    Shape s;
    auto& st = s.stroke;
    auto& fi = s.fill;

    switch (icon)
    {
        case Icon::none:
            break;

        // --- transport (solid) ---
        case Icon::play:
        {
            juce::Path tri;
            tri.addTriangle (7.5f, 5.0f, 7.5f, 19.0f, 19.0f, 12.0f);
            fi = tri.createPathWithRoundedCorners (1.6f);
            break;
        }
        case Icon::pause:
            fi.addRoundedRectangle (6.5f, 5.0f, 4.0f, 14.0f, 1.2f);
            fi.addRoundedRectangle (13.5f, 5.0f, 4.0f, 14.0f, 1.2f);
            break;
        case Icon::stop:
            fi.addRoundedRectangle (6.0f, 6.0f, 12.0f, 12.0f, 2.0f);
            break;
        case Icon::record:
            fi.addEllipse (6.0f, 6.0f, 12.0f, 12.0f);
            break;

        // --- small glyphs ---
        case Icon::chevronDown:
            polyline (st, { { 7.0f, 10.0f }, { 12.0f, 15.0f }, { 17.0f, 10.0f } });
            break;
        case Icon::more:
            for (float x : { 6.0f, 12.0f, 18.0f })
                fi.addEllipse (x - 1.6f, 10.4f, 3.2f, 3.2f);
            break;
        case Icon::plus:
            line (st, 12.0f, 6.0f, 12.0f, 18.0f);
            line (st, 6.0f, 12.0f, 18.0f, 12.0f);
            break;
        case Icon::check:
            polyline (st, { { 6.0f, 12.5f }, { 10.0f, 16.5f }, { 18.0f, 8.0f } });
            break;
        case Icon::close:
            line (st, 7.0f, 7.0f, 17.0f, 17.0f);
            line (st, 17.0f, 7.0f, 7.0f, 17.0f);
            break;
        case Icon::minimise:
            line (st, 7.0f, 12.5f, 17.0f, 12.5f);
            break;
        case Icon::maximise:
            roundRect (st, 7.0f, 7.0f, 10.0f, 10.0f, 1.5f);
            break;

        // --- modes ---
        case Icon::pattern: // a 2x2 step block, one lit
            roundRect (st, 5.0f, 5.0f, 6.0f, 6.0f, 1.2f);
            roundRect (st, 13.0f, 13.0f, 6.0f, 6.0f, 1.2f);
            roundRect (st, 5.0f, 13.0f, 6.0f, 6.0f, 1.2f);
            fi.addRoundedRectangle (13.0f, 5.0f, 6.0f, 6.0f, 1.2f);
            break;
        case Icon::song: // arrangement lanes
            line (st, 4.5f, 7.0f, 13.0f, 7.0f);
            line (st, 9.0f, 12.0f, 19.5f, 12.0f);
            line (st, 4.5f, 17.0f, 16.0f, 17.0f);
            break;
        case Icon::metronome:
        {
            juce::Path body;
            body.startNewSubPath (9.5f, 4.5f);
            body.lineTo (14.5f, 4.5f);
            body.lineTo (18.5f, 19.5f);
            body.lineTo (5.5f, 19.5f);
            body.closeSubPath();
            st.addPath (body.createPathWithRoundedCorners (1.5f));
            line (st, 12.0f, 16.0f, 17.5f, 7.0f);
            line (st, 7.0f, 15.5f, 17.0f, 15.5f);
            break;
        }

        // --- file ---
        case Icon::newFile:
        case Icon::file:
        {
            juce::Path page;
            polyline (page, { { 6.0f, 3.5f }, { 13.5f, 3.5f }, { 18.0f, 8.0f }, { 18.0f, 20.5f }, { 6.0f, 20.5f } }, true);
            st.addPath (page.createPathWithRoundedCorners (1.5f));
            polyline (st, { { 13.5f, 3.5f }, { 13.5f, 8.0f }, { 18.0f, 8.0f } });
            if (icon == Icon::newFile)
            {
                line (st, 12.0f, 11.0f, 12.0f, 17.0f);
                line (st, 9.0f, 14.0f, 15.0f, 14.0f);
            }
            break;
        }
        case Icon::open:
        case Icon::folder:
        {
            juce::Path f;
            polyline (f, { { 3.5f, 6.0f }, { 9.0f, 6.0f }, { 11.0f, 8.0f }, { 20.5f, 8.0f }, { 20.5f, 18.5f }, { 3.5f, 18.5f } }, true);
            st.addPath (f.createPathWithRoundedCorners (1.5f));
            if (icon == Icon::open)
                line (st, 3.5f, 11.5f, 20.5f, 11.5f);
            break;
        }
        case Icon::save:
        {
            juce::Path body;
            polyline (body, { { 4.5f, 4.5f }, { 16.0f, 4.5f }, { 19.5f, 8.0f }, { 19.5f, 19.5f }, { 4.5f, 19.5f } }, true);
            st.addPath (body.createPathWithRoundedCorners (1.5f));
            polyline (st, { { 8.0f, 4.5f }, { 8.0f, 9.0f }, { 15.0f, 9.0f }, { 15.0f, 4.5f } });
            roundRect (st, 8.0f, 13.5f, 8.0f, 6.0f, 1.0f);
            break;
        }
        case Icon::exportAudio: // waveform leaving a box
            polyline (st, { { 12.0f, 3.5f }, { 12.0f, 14.0f } });
            polyline (st, { { 8.0f, 7.5f }, { 12.0f, 3.5f }, { 16.0f, 7.5f } });
            polyline (st, { { 4.5f, 13.0f }, { 4.5f, 19.5f }, { 19.5f, 19.5f }, { 19.5f, 13.0f } });
            break;
        case Icon::settings: // three sliders
            line (st, 5.0f, 7.0f, 19.0f, 7.0f);
            line (st, 5.0f, 12.0f, 19.0f, 12.0f);
            line (st, 5.0f, 17.0f, 19.0f, 17.0f);
            fi.addEllipse (7.0f, 4.8f, 4.4f, 4.4f);
            fi.addEllipse (13.6f, 9.8f, 4.4f, 4.4f);
            fi.addEllipse (9.0f, 14.8f, 4.4f, 4.4f);
            break;
        case Icon::help:
            st.addCentredArc (12.0f, 9.5f, 3.5f, 3.5f, 0.0f, -pi * 0.5f, pi * 0.75f, true);
            st.lineTo (12.0f, 14.5f);
            fi.addEllipse (10.9f, 17.0f, 2.2f, 2.2f);
            break;
        case Icon::undo:
        case Icon::redo:
        {
            juce::Path p;
            p.startNewSubPath (6.0f, 10.0f);
            p.lineTo (14.0f, 10.0f);
            p.addCentredArc (14.0f, 14.0f, 4.0f, 4.0f, 0.0f, 0.0f, pi, false);
            p.lineTo (9.0f, 18.0f);
            polyline (p, { { 9.5f, 6.5f }, { 6.0f, 10.0f }, { 9.5f, 13.5f } });
            if (icon == Icon::redo)
                p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 12.0f, 12.0f));
            st.addPath (p);
            break;
        }

        // --- views ---
        case Icon::channelRack: // rows of steps
            for (int r = 0; r < 3; ++r)
            {
                const auto y = 5.5f + (float) r * 5.0f;
                fi.addRoundedRectangle (4.0f, y, 4.0f, 3.0f, 0.8f);
                for (int c = 0; c < 3; ++c)
                {
                    juce::Path cell;
                    cell.addRoundedRectangle (10.0f + (float) c * 3.6f, y, 2.6f, 3.0f, 0.6f);
                    if ((r + c) % 2 == 0)
                        fi.addPath (cell);
                    else
                        st.addPath (cell);
                }
            }
            break;
        case Icon::pianoRoll: // keys + notes
            roundRect (st, 4.0f, 4.5f, 16.0f, 15.0f, 1.5f);
            line (st, 8.5f, 4.5f, 8.5f, 19.5f);
            fi.addRoundedRectangle (10.5f, 7.0f, 4.0f, 2.4f, 0.8f);
            fi.addRoundedRectangle (13.0f, 11.0f, 5.0f, 2.4f, 0.8f);
            fi.addRoundedRectangle (11.0f, 15.0f, 3.5f, 2.4f, 0.8f);
            break;
        case Icon::playlist: // clips on lanes
            fi.addRoundedRectangle (4.0f, 5.0f, 9.0f, 3.5f, 1.0f);
            fi.addRoundedRectangle (9.0f, 10.3f, 11.0f, 3.5f, 1.0f);
            fi.addRoundedRectangle (4.0f, 15.6f, 6.0f, 3.5f, 1.0f);
            fi.addRoundedRectangle (12.0f, 15.6f, 6.0f, 3.5f, 1.0f);
            break;
        case Icon::mixer: // three faders
            for (int i = 0; i < 3; ++i)
            {
                const auto x = 6.5f + (float) i * 5.5f;
                line (st, x, 4.5f, x, 19.5f);
            }
            fi.addRoundedRectangle (4.5f, 12.0f, 4.0f, 3.0f, 0.8f);
            fi.addRoundedRectangle (10.0f, 7.0f, 4.0f, 3.0f, 0.8f);
            fi.addRoundedRectangle (15.5f, 14.5f, 4.0f, 3.0f, 0.8f);
            break;
        case Icon::browser: // sidebar
            roundRect (st, 4.0f, 4.5f, 16.0f, 15.0f, 1.5f);
            line (st, 9.5f, 4.5f, 9.5f, 19.5f);
            line (st, 12.5f, 9.0f, 17.0f, 9.0f);
            line (st, 12.5f, 12.5f, 17.0f, 12.5f);
            line (st, 12.5f, 16.0f, 15.0f, 16.0f);
            break;

        // --- content ---
        case Icon::audioFile:
            for (int i = 0; i < 5; ++i)
            {
                const float heights[] = { 5.0f, 11.0f, 15.0f, 9.0f, 4.0f };
                const auto x = 6.0f + (float) i * 3.0f;
                line (st, x, 12.0f - heights[i] / 2.0f, x, 12.0f + heights[i] / 2.0f);
            }
            break;
        case Icon::plugin: // a module with a jack
            roundRect (st, 5.0f, 6.5f, 14.0f, 11.0f, 2.0f);
            line (st, 9.0f, 3.5f, 9.0f, 6.5f);
            line (st, 15.0f, 3.5f, 15.0f, 6.5f);
            fi.addEllipse (10.0f, 10.0f, 4.0f, 4.0f);
            break;
        case Icon::preset: // a knob
            st.addCentredArc (12.0f, 12.0f, 7.0f, 7.0f, 0.0f, -pi * 0.75f, pi * 0.75f, true);
            line (st, 12.0f, 12.0f, 15.5f, 8.5f);
            break;
        case Icon::project: // the LYNX "spark": a diamond
        {
            juce::Path d;
            polyline (d, { { 12.0f, 3.5f }, { 19.0f, 12.0f }, { 12.0f, 20.5f }, { 5.0f, 12.0f } }, true);
            st.addPath (d.createPathWithRoundedCorners (2.0f));
            fi.addEllipse (10.0f, 10.0f, 4.0f, 4.0f);
            break;
        }

        // --- tools ---
        case Icon::draw: // pencil
        {
            juce::Path p;
            polyline (p, { { 15.5f, 4.5f }, { 19.5f, 8.5f }, { 9.0f, 19.0f }, { 4.5f, 19.5f }, { 5.0f, 15.0f } }, true);
            st.addPath (p.createPathWithRoundedCorners (1.0f));
            line (st, 13.0f, 7.0f, 17.0f, 11.0f);
            break;
        }
        case Icon::erase:
        {
            juce::Path p;
            polyline (p, { { 13.0f, 4.5f }, { 19.5f, 11.0f }, { 11.5f, 19.0f }, { 7.5f, 19.0f }, { 4.5f, 16.0f } }, true);
            st.addPath (p.createPathWithRoundedCorners (1.0f));
            line (st, 9.0f, 9.0f, 15.0f, 15.0f);
            line (st, 11.5f, 19.0f, 19.5f, 19.0f);
            break;
        }
        case Icon::select:
        {
            const float dash[] = { 2.5f, 2.0f };
            juce::Path box;
            box.addRectangle (4.5f, 4.5f, 15.0f, 15.0f);
            juce::PathStrokeType (1.0f).createDashedStroke (fi, box, dash, 2);
            break;
        }
        case Icon::slice:
        case Icon::scissors:
            st.addEllipse (4.5f, 13.5f, 5.0f, 5.0f);
            st.addEllipse (14.5f, 13.5f, 5.0f, 5.0f);
            line (st, 8.5f, 14.5f, 17.0f, 4.5f);
            line (st, 15.5f, 14.5f, 7.0f, 4.5f);
            break;
        case Icon::mute: // speaker with a cross
        {
            juce::Path sp;
            polyline (sp, { { 4.5f, 9.5f }, { 8.0f, 9.5f }, { 12.0f, 5.5f }, { 12.0f, 18.5f }, { 8.0f, 14.5f }, { 4.5f, 14.5f } }, true);
            st.addPath (sp.createPathWithRoundedCorners (1.0f));
            line (st, 15.5f, 9.5f, 20.0f, 14.5f);
            line (st, 20.0f, 9.5f, 15.5f, 14.5f);
            break;
        }
        case Icon::link:
            st.addRoundedRectangle (3.5f, 9.0f, 10.0f, 6.0f, 3.0f);
            st.addRoundedRectangle (10.5f, 9.0f, 10.0f, 6.0f, 3.0f);
            break;
        case Icon::magnet:
        {
            juce::Path m;
            m.startNewSubPath (6.0f, 5.0f);
            m.lineTo (6.0f, 12.0f);
            m.addCentredArc (12.0f, 12.0f, 6.0f, 6.0f, 0.0f, -pi * 0.5f, pi * 0.5f, false);
            m.lineTo (18.0f, 5.0f);
            st.addPath (m);
            line (st, 6.0f, 8.0f, 9.5f, 8.0f);
            line (st, 14.5f, 8.0f, 18.0f, 8.0f);
            break;
        }
    }

    return s;
}
} // namespace

const Shape& get (Icon icon)
{
    static std::map<Icon, Shape> cache;
    auto it = cache.find (icon);
    if (it == cache.end())
        it = cache.emplace (icon, make (icon)).first;
    return it->second;
}

void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    if (icon == Icon::none || area.isEmpty())
        return;

    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    const auto box = area.withSizeKeepingCentre (size, size);
    const auto scale = size / 24.0f;
    const auto transform = juce::AffineTransform::scale (scale).translated (box.getX(), box.getY());
    const auto& shape = get (icon);

    g.setColour (colour);

    if (! shape.fill.isEmpty())
        g.fillPath (shape.fill, transform);

    if (! shape.stroke.isEmpty())
        g.strokePath (shape.stroke,
                      juce::PathStrokeType (juce::jlimit (1.2f, 2.4f, 1.7f * scale), juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      transform);
}
} // namespace icons

IconButton::IconButton (const juce::String& name, Icon i, const juce::String& t)
    : juce::Button (name), icon (i), text (t), onColour (theme::colour::accent), iconOnColour (theme::colour::onAccent)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::setIcon (Icon newIcon)
{
    if (icon != newIcon)
    {
        icon = newIcon;
        repaint();
    }
}

void IconButton::setText (const juce::String& newText)
{
    if (text != newText)
    {
        text = newText;
        repaint();
    }
}

void IconButton::setPrimary (bool b) { primary = b; repaint(); }
void IconButton::setOnColour (juce::Colour c) { onColour = c; repaint(); }
void IconButton::setIconColourWhenOn (juce::Colour c) { iconOnColour = c; repaint(); }
void IconButton::setFlat (bool b) { flat = b; repaint(); }

void IconButton::setCorners (bool left, bool right)
{
    roundLeft = left;
    roundRight = right;
    repaint();
}

int IconButton::getIdealWidth (int height) const
{
    const auto iconSize = icon == Icon::none ? 0 : juce::roundToInt (height * 0.55f);
    const auto textWidth = text.isEmpty() ? 0
                                          : juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (theme::type::body)), text));
    const auto gap = iconSize > 0 && textWidth > 0 ? 6 : 0;
    return juce::jmax (height, iconSize + gap + textWidth + (textWidth > 0 ? 20 : 0));
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace theme;
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();
    const auto enabled = isEnabled();

    juce::Colour bg;
    if (on || primary)
        bg = down ? colour::accentPressed : highlighted ? onColour.brighter (0.12f) : onColour;
    else if (down)
        bg = colour::bg6;
    else if (highlighted)
        bg = colour::bg5;
    else
        bg = flat ? juce::Colours::transparentBlack : colour::bg4;

    if (! enabled)
        bg = bg.withMultipliedAlpha (0.5f);

    if (! bg.isTransparent())
    {
        juce::Path p;
        p.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius::md, radius::md,
                               roundLeft, roundRight, roundLeft, roundRight);
        g.setColour (bg);
        g.fillPath (p);
    }

    auto fg = (on || primary) ? iconOnColour : (highlighted ? colour::textPrimary : colour::textSecondary.brighter (0.25f));
    if (! enabled)
        fg = colour::textDisabled;

    auto area = getLocalBounds().toFloat();
    const auto iconSize = juce::jmin (area.getHeight() * 0.6f, 20.0f);

    if (text.isEmpty())
    {
        icons::draw (g, icon, area.withSizeKeepingCentre (iconSize, iconSize), fg);
        return;
    }

    const auto font = juce::Font (theme::font (type::body));
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, text);
    const auto gap = icon == Icon::none ? 0.0f : 6.0f;
    const auto contentWidth = juce::jmin (area.getWidth() - 12.0f, (icon == Icon::none ? 0.0f : iconSize) + gap + textWidth);
    auto content = area.withSizeKeepingCentre (contentWidth, area.getHeight());

    if (icon != Icon::none)
        icons::draw (g, icon, content.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize), fg);

    content.removeFromLeft (gap);
    g.setColour (fg);
    g.setFont (font);
    g.drawFittedText (text, content.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
}
} // namespace lynx
