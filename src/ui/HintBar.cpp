#include "ui/HintBar.h"

#include "core/HintMarkup.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace lynx::ui
{
namespace
{
struct Posted
{
    Hint hint;
    double until = 0.0;
};

Posted& posted()
{
    static Posted p;
    return p;
}

// one drawable piece of a hint line
struct Item
{
    enum class Kind { word, mouse, key } kind = Kind::word;
    juce::String text;  // word, key name, or mouse token
    float gap = 0.0f;   // space before it
    float width = 0.0f;
    bool title = false; // the hint's subject ("Volume" in "Volume: drag...")
};

std::vector<Item> makeItems (const juce::String& text, const juce::Font& font, const juce::Font& keyFont, float glyph)
{
    std::vector<Item> items;
    const auto space = juce::GlyphArrangement::getStringWidth (font, " ");

    // the subject: the words before the first ": " or " - " (when it comes early, before any glyph)
    auto titleEnd = -1;
    {
        const auto colon = text.indexOf (": "), dash = text.indexOf (" - "), bracket = text.indexOf ("[");
        const auto first = colon < 0 ? dash : dash < 0 ? colon : juce::jmin (colon, dash);
        if (first > 0 && first < 48 && (bracket < 0 || first < bracket))
            titleEnd = first + (first == colon ? 1 : 0);
    }

    int consumed = 0; // characters of `text` already turned into items (for the title)
    float pendingGap = 0.0f;

    for (const auto& segment : hintmarkup::parse (text.toStdString()))
    {
        const auto value = juce::String::fromUTF8 (segment.value.c_str());

        if (segment.kind != hintmarkup::Segment::Kind::text)
        {
            Item item;
            item.kind = segment.kind == hintmarkup::Segment::Kind::mouse ? Item::Kind::mouse : Item::Kind::key;
            item.text = value;
            item.gap = pendingGap;
            item.width = item.kind == Item::Kind::mouse
                             ? glyph + (value == "2xLMB" ? juce::GlyphArrangement::getStringWidth (keyFont, "x2") + 1.0f : 0.0f)
                             : juce::GlyphArrangement::getStringWidth (keyFont, value) + 8.0f;
            items.push_back (item);
            pendingGap = 0.0f;
            consumed += value.length() + 2;
            continue;
        }

        // words, keeping how much space separates them (two spaces = a wider break between gestures)
        int i = 0;
        while (i < value.length())
        {
            int spaces = 0;
            while (i < value.length() && value[i] == ' ')
            {
                ++spaces;
                ++i;
            }

            pendingGap += spaces == 0 ? 0.0f : spaces == 1 ? space : space * 2.5f;
            const auto start = i;
            while (i < value.length() && value[i] != ' ')
                ++i;

            if (i > start)
            {
                Item item;
                item.text = value.substring (start, i);
                item.gap = pendingGap;
                item.width = juce::GlyphArrangement::getStringWidth (font, item.text);
                item.title = titleEnd > 0 && consumed + start < titleEnd;
                items.push_back (item);
                pendingGap = 0.0f;
            }
        }

        consumed += value.length();
    }

    return items;
}

// Splits the items into at most `maxLines` lines of `width`; false if they do not fit.
bool layOut (const std::vector<Item>& items, float width, int maxLines, std::vector<std::vector<Item>>& lines)
{
    lines.assign (1, {});
    float x = 0.0f;

    for (auto item : items)
    {
        const auto needed = (lines.back().empty() ? 0.0f : item.gap) + item.width;

        if (! lines.back().empty() && x + needed > width)
        {
            if ((int) lines.size() == maxLines)
                return false;

            lines.emplace_back();
            x = 0.0f;
        }

        if (lines.back().empty())
            item.gap = 0.0f;

        x += item.gap + item.width;
        lines.back().push_back (item);
    }

    return x <= width;
}

void drawLine (juce::Graphics& g, const std::vector<Item>& line, juce::Rectangle<float> area, const juce::Font& font,
               const juce::Font& keyFont, float glyph)
{
    using namespace theme;
    auto x = area.getX();

    for (const auto& item : line)
    {
        x += item.gap;

        if (x >= area.getRight())
            break;

        const auto w = juce::jmin (item.width, area.getRight() - x);
        const auto box = juce::Rectangle<float> (x, area.getY(), w, area.getHeight());

        switch (item.kind)
        {
            case Item::Kind::word:
                g.setFont (font);
                g.setColour (item.title ? colour::textPrimary : colour::textSecondary);
                g.drawText (item.text, box, juce::Justification::centredLeft, true);
                break;

            case Item::Kind::mouse:
            {
                const auto icon = item.text == "RMB" ? Icon::rightClick : item.text == "MMB" ? Icon::mouseMiddle
                                                                          : item.text == "Wheel" ? Icon::mouseWheel
                                                                                                 : Icon::mouseLeft;
                icons::draw (g, icon, box.withWidth (glyph).withSizeKeepingCentre (glyph, glyph), colour::textPrimary);

                if (item.text == "2xLMB")
                {
                    g.setFont (keyFont);
                    g.setColour (colour::textPrimary);
                    g.drawText ("x2", box.withTrimmedLeft (glyph), juce::Justification::centredLeft, false);
                }
                break;
            }

            case Item::Kind::key:
            {
                const auto cap = box.withSizeKeepingCentre (box.getWidth(), juce::jmin (box.getHeight(), keyFont.getHeight() + 5.0f));
                g.setColour (colour::bg4);
                g.fillRoundedRectangle (cap, radius::sm);
                g.setColour (colour::borderStrong);
                g.drawRoundedRectangle (cap.reduced (0.5f), radius::sm, 1.0f);
                g.setColour (colour::textPrimary);
                g.setFont (keyFont);
                g.drawText (item.text, cap, juce::Justification::centred, false);
                break;
            }
        }

        x += item.width;
    }
}
} // namespace

void postHint (const Hint& hint, int holdMs)
{
    posted().hint = hint;
    posted().until = juce::Time::getMillisecondCounterHiRes() + holdMs;
}

void clearPostedHint()
{
    posted() = {};
}

std::optional<Hint> postedHint()
{
    if (posted().until > 0.0 && juce::Time::getMillisecondCounterHiRes() < posted().until && ! posted().hint.isEmpty())
        return posted().hint;

    return std::nullopt;
}

juce::String valueGestures()
{
    return tr ("[LMB] drag  [Ctrl] fine  [Wheel] step  [Alt][LMB] reset  [2xLMB] type  [RMB] menu");
}

juce::String plainHint (const juce::String& markup)
{
    const std::vector<std::string> words { tr ("click").toStdString(), tr ("double-click").toStdString(), tr ("right-click").toStdString(),
                                           tr ("middle-click").toStdString(), tr ("wheel").toStdString() };
    return juce::String::fromUTF8 (hintmarkup::plain (markup.toStdString(), words).c_str());
}

juce::String withMarkup (const juce::String& text)
{
    // the old gesture phrases still used by some panels (kept translated, so they match in both languages)
    static const char* const valuePhrases[] = { TR_NOOP ("drag (Ctrl: fine), wheel, Alt+click resets") };

    for (const auto* phrase : valuePhrases)
        if (const auto translated = tr (phrase); text.contains (translated))
            return text.replace (translated, valueGestures());

    return text;
}

void HintBar::setHint (const Hint& hint, bool isLive)
{
    if (hint.text != current.text || hint.value != current.value || hint.automatable != current.automatable || hint.hasMenu != current.hasMenu
        || isLive != live)
    {
        current = hint;
        live = isLive;
        repaint();
    }
}

void HintBar::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat().reduced (6.0f, 1.0f);

    // capability markers (dim when not applicable, so the text does not jump)
    if (current.automatable || current.hasMenu)
    {
        icons::draw (g, Icon::automate, r.removeFromLeft (16.0f).withSizeKeepingCentre (14.0f, 14.0f),
                     current.automatable ? colour::automation : colour::textDisabled.withAlpha (0.35f));
        r.removeFromLeft (2.0f);
        icons::draw (g, Icon::rightClick, r.removeFromLeft (16.0f).withSizeKeepingCentre (14.0f, 14.0f),
                     current.hasMenu ? colour::textSecondary : colour::textDisabled.withAlpha (0.35f));
        r.removeFromLeft (8.0f);
    }

    // the value, in a pill on the right (accent while it changes under the mouse)
    if (current.value.isNotEmpty())
    {
        const juce::Font valueFont (font (type::small, true));
        const auto w = juce::jmin (r.getWidth() / 3.0f, juce::GlyphArrangement::getStringWidth (valueFont, current.value) + 18.0f);
        const auto valueArea = r.removeFromRight (juce::jmax (44.0f, w)).reduced (0.0f, 3.0f);
        g.setColour (live ? colour::accentMuted : colour::bg3);
        g.fillRoundedRectangle (valueArea, radius::sm);
        if (live)
        {
            g.setColour (colour::accent);
            g.drawRoundedRectangle (valueArea.reduced (0.5f), radius::sm, 1.0f);
        }
        g.setColour (colour::textPrimary);
        g.setFont (valueFont);
        g.drawFittedText (current.value, valueArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        r.removeFromRight (8.0f);
    }

    // the text: one line if it fits, else two smaller lines, else two lines cut at the end
    const auto text = withMarkup (current.text);
    std::vector<std::vector<Item>> lines;

    const juce::Font big (font (type::small)), bigKey (font (type::caption - 1.0f, true));
    if (auto items = makeItems (text, big, bigKey, 15.0f); layOut (items, r.getWidth(), 1, lines))
    {
        drawLine (g, lines.front(), r.withSizeKeepingCentre (r.getWidth(), 17.0f), big, bigKey, 15.0f);
        return;
    }

    const juce::Font smallFont (font (type::caption)), smallKey (font (type::caption - 2.0f, true));
    const auto items = makeItems (text, smallFont, smallKey, 12.0f);
    const auto fits = layOut (items, r.getWidth(), 2, lines);
    const auto lineHeight = juce::jmin (13.0f, r.getHeight() / 2.0f);
    auto area = r.withSizeKeepingCentre (r.getWidth(), lineHeight * 2.0f);

    for (std::size_t i = 0; i < lines.size() && i < 2; ++i)
        drawLine (g, lines[i], area.removeFromTop (lineHeight), smallFont, smallKey, 12.0f);

    if (! fits) // the rest did not fit: a fade at the end of the second line says so
    {
        const auto fade = r.withLeft (r.getRight() - 24.0f).withTrimmedTop (r.getHeight() / 2.0f);
        g.setGradientFill (juce::ColourGradient (colour::bg1.withAlpha (0.0f), fade.getX(), 0.0f, colour::bg1, fade.getRight(), 0.0f, false));
        g.fillRect (fade);
    }
}
} // namespace lynx::ui
