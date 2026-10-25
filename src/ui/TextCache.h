#pragma once

// Text drawn again and again with the same words, font and box (mixer strip names, dB readouts, clip names): JUCE 8
// shapes the text (HarfBuzz) on every drawText / drawFittedText, which costs ~0.05-0.1 ms a call on Windows. A busy
// Mixer or Playlist draws hundreds of them per frame (REA round 151: a full Mixer repaint took 12-30 ms). These
// helpers keep the laid-out glyphs, keyed by the text, the font and the box size, and only move them into place.
// Message thread only (the UI's paint calls). Same results as Graphics::drawText / drawFittedText.

#include <juce_graphics/juce_graphics.h>

#include <functional>
#include <unordered_map>

namespace lynx::textcache
{
namespace detail
{
inline std::unordered_map<juce::String, juce::GlyphArrangement>& cache()
{
    static std::unordered_map<juce::String, juce::GlyphArrangement> map;
    return map;
}

inline const juce::GlyphArrangement& get (const juce::String& key, const std::function<void (juce::GlyphArrangement&)>& make)
{
    auto& map = cache();
    if (const auto it = map.find (key); it != map.end())
        return it->second;
    if (map.size() > 6000) // a project switch or a long session: start again rather than grow
        map.clear();
    juce::GlyphArrangement glyphs;
    make (glyphs);
    return map.emplace (key, std::move (glyphs)).first->second;
}

inline juce::String fontKey (const juce::Font& f)
{
    return f.getTypefaceName() + "|" + f.getTypefaceStyle() + "|" + juce::String (f.getHeight(), 3) + "|" + juce::String (f.getHorizontalScale(), 3)
           + (f.isUnderlined() ? "u" : "");
}
} // namespace detail

// Graphics::drawText (text, area, justification, ellipsis) with the current font and colour.
inline void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification justification,
                      bool useEllipsesIfTooBig = true)
{
    if (text.isEmpty() || area.getWidth() <= 0.0f || area.getHeight() <= 0.0f || ! g.clipRegionIntersects (area.getSmallestIntegerContainer()))
        return;
    const auto font = g.getCurrentFont();
    const auto key = "t" + juce::String (area.getWidth(), 2) + "x" + juce::String (area.getHeight(), 2) + "|" + juce::String (justification.getFlags())
                     + (useEllipsesIfTooBig ? "e|" : "|") + detail::fontKey (font) + "|" + text;
    const auto& glyphs = detail::get (key, [&] (juce::GlyphArrangement& arr) {
        // what Graphics::drawText does, in a box at the origin
        arr.addCurtailedLineOfText (font, text, 0.0f, 0.0f, area.getWidth(), useEllipsesIfTooBig);
        arr.justifyGlyphs (0, arr.getNumGlyphs(), 0.0f, 0.0f, area.getWidth(), area.getHeight(), justification);
    });
    glyphs.draw (g, juce::AffineTransform::translation (area.getX(), area.getY()));
}

inline void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification justification,
                      bool useEllipsesIfTooBig = true)
{
    drawText (g, text, area.toFloat(), justification, useEllipsesIfTooBig);
}

inline void drawText (juce::Graphics& g, const juce::String& text, int x, int y, int w, int h, juce::Justification justification,
                      bool useEllipsesIfTooBig = true)
{
    drawText (g, text, juce::Rectangle<int> { x, y, w, h }, justification, useEllipsesIfTooBig);
}

// Graphics::drawFittedText (text, area, justification, maximumLines, minimumHorizontalScale).
inline void drawFittedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification justification,
                            int maximumLines, float minimumHorizontalScale = 0.0f)
{
    if (text.isEmpty() || area.getWidth() <= 0 || area.getHeight() <= 0 || ! g.clipRegionIntersects (area))
        return;
    const auto font = g.getCurrentFont();
    const auto key = "f" + juce::String (area.getWidth()) + "x" + juce::String (area.getHeight()) + "|" + juce::String (justification.getFlags()) + "|"
                     + juce::String (maximumLines) + "|" + juce::String (minimumHorizontalScale, 3) + "|" + detail::fontKey (font) + "|" + text;
    const auto& glyphs = detail::get (key, [&] (juce::GlyphArrangement& arr) {
        arr.addFittedText (font, text, 0.0f, 0.0f, (float) area.getWidth(), (float) area.getHeight(), justification, maximumLines,
                           minimumHorizontalScale);
    });
    glyphs.draw (g, juce::AffineTransform::translation ((float) area.getX(), (float) area.getY()));
}

inline void drawFittedText (juce::Graphics& g, const juce::String& text, int x, int y, int w, int h, juce::Justification justification,
                            int maximumLines, float minimumHorizontalScale = 0.0f)
{
    drawFittedText (g, text, juce::Rectangle<int> { x, y, w, h }, justification, maximumLines, minimumHorizontalScale);
}
} // namespace lynx::textcache
