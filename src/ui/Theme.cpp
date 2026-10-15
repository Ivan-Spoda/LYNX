#include "ui/Theme.h"

#include "ui/Icons.h"

namespace lynx::theme
{
using namespace colour;

namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (font (size, bold));
}

void fillRounded (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c)
{
    g.setColour (c);
    g.fillRoundedRectangle (r, corner);
}

void outlineRounded (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c, float thickness = 1.0f)
{
    g.setColour (c);
    g.drawRoundedRectangle (r.reduced (thickness * 0.5f), corner, thickness);
}

// Window caption buttons (only used for non-native title bars).
class CaptionButton final : public juce::Button
{
public:
    CaptionButton (const juce::String& name, Icon i, bool isClose) : juce::Button (name), icon (i), close (isClose) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down)
            g.fillAll (close ? (down ? accentPressed : accent) : (down ? bg6 : bg5));

        icons::draw (g, icon, getLocalBounds().toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                     highlighted || down ? textPrimary : textSecondary);
    }

private:
    Icon icon;
    bool close;
};
} // namespace

void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, bool bipolar, juce::Colour arcColour, bool hover)
{
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    if (size <= 4.0f)
        return;

    const auto area = bounds.withSizeKeepingCentre (size, size);
    const auto centre = area.getCentre();
    const auto thickness = juce::jlimit (2.0f, 4.0f, size * 0.09f);
    const auto arcRadius = size / 2.0f - thickness / 2.0f;
    constexpr auto start = -pi * 0.75f;
    constexpr auto range = pi * 1.5f;
    const auto value = juce::jlimit (0.0f, 1.0f, value01);

    // track: lighter than every panel surface, so a knob at rest (a centred pan has no value arc) is
    // still visible on the dark rows
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, start + range, true);
    g.setColour (hover ? knobTrack.brighter (0.25f) : knobTrack);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const auto from = bipolar ? start + range * 0.5f : start;
    const auto to = start + range * value;

    if (bipolar) // the centre (default) mark at the top of the ring
    {
        g.setColour (textSecondary);
        g.fillEllipse (juce::Rectangle<float> (thickness, thickness).withCentre ({ centre.x, centre.y - arcRadius }));
    }

    if (std::abs (to - from) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setColour (hover && arcColour == knobArc ? accent : arcColour);
        g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body: a flat cap with a hairline rim, and a short pointer near the edge
    const auto bodyRadius = juce::jmin (arcRadius - thickness * 0.5f - juce::jmax (1.5f, size * 0.07f), 22.0f);
    if (bodyRadius > 2.0f)
    {
        const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
        g.setColour (hover ? bg5 : bg4);
        g.fillEllipse (body);
        g.setColour (bg6.withAlpha (0.7f));
        g.drawEllipse (body.reduced (0.5f), 1.0f);

        const auto inner = bodyRadius * (bodyRadius > 8.0f ? 0.42f : 0.15f);
        const auto outer = bodyRadius * 0.86f;
        g.setColour (textPrimary);
        g.drawLine (centre.x + std::sin (to) * inner, centre.y - std::cos (to) * inner, centre.x + std::sin (to) * outer,
                    centre.y - std::cos (to) * outer, juce::jlimit (1.5f, 2.5f, size * 0.05f));
    }
}

LookAndFeel::LookAndFeel()
    : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme { bg1, bg2, bg3, borderStrong, textPrimary, bg4, onAccent, accent, textPrimary })
{
    // Segoe UI on Windows (the JUCE default there is Verdana, wider and dated)
   #if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI");
   #endif

    setColour (juce::ResizableWindow::backgroundColourId, bg1);
    setColour (juce::DocumentWindow::textColourId, textPrimary);

    setColour (juce::TextButton::buttonColourId, bg4);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, textPrimary);
    setColour (juce::TextButton::textColourOnId, onAccent);
    setColour (juce::ToggleButton::textColourId, textPrimary);
    setColour (juce::ToggleButton::tickColourId, onAccent);
    setColour (juce::ToggleButton::tickDisabledColourId, textDisabled);

    setColour (juce::ComboBox::backgroundColourId, bg4);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, textPrimary);
    setColour (juce::ComboBox::arrowColourId, textSecondary);
    setColour (juce::ComboBox::buttonColourId, bg4);
    setColour (juce::ComboBox::focusedOutlineColourId, accent);

    setColour (juce::PopupMenu::backgroundColourId, bg3);
    setColour (juce::PopupMenu::textColourId, textPrimary);
    setColour (juce::PopupMenu::headerTextColourId, textSecondary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, bg5);
    setColour (juce::PopupMenu::highlightedTextColourId, textPrimary);

    setColour (juce::Label::textColourId, textPrimary);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textWhenEditingColourId, textPrimary);
    setColour (juce::Label::backgroundWhenEditingColourId, bg0);
    setColour (juce::Label::outlineWhenEditingColourId, accent);

    setColour (juce::TextEditor::backgroundColourId, bg0);
    setColour (juce::TextEditor::textColourId, textPrimary);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, textPrimary);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId, accent);

    setColour (juce::Slider::backgroundColourId, bg0);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::thumbColourId, textPrimary);
    setColour (juce::Slider::rotarySliderFillColourId, knobArc);
    setColour (juce::Slider::rotarySliderOutlineColourId, bg0);
    setColour (juce::Slider::textBoxTextColourId, textPrimary);
    setColour (juce::Slider::textBoxBackgroundColourId, bg0);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.35f));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour (juce::ScrollBar::thumbColourId, bg6);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedComponent::backgroundColourId, bg2);
    setColour (juce::TabbedButtonBar::tabTextColourId, textSecondary);
    setColour (juce::TabbedButtonBar::frontTextColourId, textPrimary);

    setColour (juce::TooltipWindow::backgroundColourId, bg3);
    setColour (juce::TooltipWindow::textColourId, textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, borderStrong);

    setColour (juce::AlertWindow::backgroundColourId, bg3);
    setColour (juce::AlertWindow::textColourId, textPrimary);
    setColour (juce::AlertWindow::outlineColourId, borderStrong);

    setColour (juce::TreeView::backgroundColourId, bg2);
    setColour (juce::TreeView::linesColourId, border);
    setColour (juce::TreeView::selectedItemBackgroundColourId, accentMuted);
    setColour (juce::TreeView::oddItemsColourId, juce::Colours::transparentBlack);
    setColour (juce::TreeView::evenItemsColourId, juce::Colours::transparentBlack);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, accentMuted);
    setColour (juce::DirectoryContentsDisplayComponent::textColourId, textPrimary);
    setColour (juce::DirectoryContentsDisplayComponent::highlightedTextColourId, textPrimary);
    setColour (juce::ListBox::backgroundColourId, bg2);
    setColour (juce::ListBox::outlineColourId, border);
    setColour (juce::ListBox::textColourId, textPrimary);

    setColour (juce::GroupComponent::outlineColourId, border);
    setColour (juce::GroupComponent::textColourId, textSecondary);
    setColour (juce::ProgressBar::backgroundColourId, bg0);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::HyperlinkButton::textColourId, info);
    setColour (juce::BubbleComponent::backgroundColourId, bg3);
    setColour (juce::BubbleComponent::outlineColourId, borderStrong);
}

// --- buttons ---------------------------------------------------------------------------------------

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour, bool highlighted,
                                        bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = button.getToggleState();
    auto base = backgroundColour; // already buttonOnColour when toggled

    if (down)
        base = on ? accentPressed : (base == bg4 ? bg6 : base.brighter (0.18f));
    else if (highlighted)
        base = on ? accentHover : (base == bg4 ? bg5 : base.brighter (0.1f));

    if (! button.isEnabled())
        base = base.withMultipliedAlpha (0.5f);

    const auto flatLeft = button.isConnectedOnLeft(), flatRight = button.isConnectedOnRight();
    juce::Path p;
    p.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius::md, radius::md, ! flatLeft,
                           ! flatRight, ! flatLeft, ! flatRight);
    g.setColour (base);
    g.fillPath (p);

    if (button.hasKeyboardFocus (false) && button.getWantsKeyboardFocus())
    {
        g.setColour (accent.withAlpha (0.6f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return uiFont (juce::jmin ((float) type::body, (float) buttonHeight * 0.6f));
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);
    const auto fg = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f);
    g.setColour (fg);

    auto area = button.getLocalBounds().reduced (juce::jmin (8, button.getWidth() / 6), 0);
    const auto iconId = (int) button.getProperties().getWithDefault (iconProperty, 0);

    if (iconId != 0)
    {
        const auto iconSize = juce::jmin (16.0f, (float) button.getHeight() * 0.6f);
        const auto textWidth = button.getButtonText().isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (font, button.getButtonText()) + 6.0f;
        auto content = area.toFloat().withSizeKeepingCentre (juce::jmin ((float) area.getWidth(), iconSize + textWidth), (float) area.getHeight());
        icons::draw (g, (Icon) iconId, content.removeFromLeft (iconSize), fg);
        g.setColour (fg);
        content.removeFromLeft (6.0f);
        g.drawFittedText (button.getButtonText(), content.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
        return;
    }

    g.drawFittedText (button.getButtonText(), area, juce::Justification::centred, 2, 0.85f);
}

void LookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component, float x, float y, float w, float h, bool ticked, bool enabled,
                               bool highlighted, bool)
{
    juce::ignoreUnused (component);
    const auto box = juce::Rectangle<float> (x, y, w, h).reduced (0.5f);

    if (ticked)
        fillRounded (g, box, radius::sm, enabled ? (highlighted ? accentHover : accent) : accentMuted);
    else
    {
        fillRounded (g, box, radius::sm, bg0);
        outlineRounded (g, box, radius::sm, highlighted ? textSecondary : borderStrong);
    }

    if (ticked)
        icons::draw (g, Icon::check, box.reduced (box.getWidth() * 0.05f), enabled ? onAccent : textDisabled);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto fontSize = juce::jmin ((float) type::body, (float) button.getHeight() * 0.75f);
    const auto tickSize = juce::jmin (16.0f, (float) button.getHeight() - 4.0f);

    drawTickBox (g, button, 2.0f, ((float) button.getHeight() - tickSize) * 0.5f, tickSize, tickSize, button.getToggleState(),
                 button.isEnabled(), highlighted, down);

    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.setFont (uiFont (fontSize));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft (juce::roundToInt (tickSize) + 8).withTrimmedRight (2),
                      juce::Justification::centredLeft, 2, 0.85f);
}

// --- combo boxes and menus -------------------------------------------------------------------------

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const auto hover = box.isMouseOver (true) && box.isEnabled();
    fillRounded (g, bounds, radius::md, hover ? bg5 : box.findColour (juce::ComboBox::backgroundColourId));

    if (box.hasKeyboardFocus (true) && box.getWantsKeyboardFocus())
        outlineRounded (g, bounds, radius::md, accent.withAlpha (0.7f));
    else if (! box.findColour (juce::ComboBox::outlineColourId).isTransparent())
        outlineRounded (g, bounds, radius::md, box.findColour (juce::ComboBox::outlineColourId));

    const auto arrowSize = juce::jmin (14.0f, (float) height * 0.6f);
    const auto arrowArea = juce::Rectangle<float> ((float) width - arrowSize - 7.0f, ((float) height - arrowSize) * 0.5f, arrowSize, arrowSize);
    icons::draw (g, Icon::chevronDown, arrowArea, (hover ? textPrimary : textSecondary).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return uiFont (juce::jmin ((float) type::body, (float) box.getHeight() * 0.62f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const auto arrowSize = juce::jmin (14, juce::roundToInt ((float) box.getHeight() * 0.6f));
    label.setBounds (2, 1, box.getWidth() - arrowSize - 12, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, box.findColour (juce::ComboBox::textColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.fillAll (bg3);
    g.setColour (borderStrong);
    g.drawRect (bounds, 1.0f);
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return uiFont (type::body);
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth,
                                             int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 9;
        return;
    }

    const auto font = getPopupMenuFont();
    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : 26;
    idealWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, text)) + idealHeight * 2 + 8;
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                     bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                     const juce::Drawable* icon, const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        g.setColour (border.brighter (0.1f));
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (4, 1);

    if (isHighlighted && isActive)
    {
        fillRounded (g, r.toFloat(), radius::sm, bg5);
        g.setColour (accent);
        g.fillRoundedRectangle (r.toFloat().withWidth (3.0f).reduced (0.0f, 4.0f), 1.5f);
    }

    auto textColour = textColourToUse != nullptr ? *textColourToUse : textPrimary;
    if (! isActive)
        textColour = textDisabled;

    auto content = r.reduced (8, 0);
    auto iconArea = content.removeFromLeft (juce::roundToInt ((float) r.getHeight() * 0.8f)).toFloat();

    if (icon != nullptr)
        icon->drawWithin (g, iconArea.withSizeKeepingCentre (14.0f, 14.0f), juce::RectanglePlacement::centred, isActive ? 1.0f : 0.4f);
    else if (isTicked)
        icons::draw (g, Icon::check, iconArea.withSizeKeepingCentre (16.0f, 16.0f), isActive ? accent : textDisabled);

    if (hasSubMenu)
    {
        const auto arrow = content.removeFromRight (14).toFloat().withSizeKeepingCentre (12.0f, 12.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-pi * 0.5f, arrow.getCentreX(), arrow.getCentreY()));
        icons::draw (g, Icon::chevronDown, arrow, isHighlighted ? textPrimary : textSecondary);
    }

    content.removeFromLeft (4);
    const auto font = getPopupMenuFont();
    g.setFont (font);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (textSecondary.withMultipliedAlpha (isActive ? 1.0f : 0.6f));
        g.setFont (uiFont (type::small));
        g.drawText (shortcutKeyText, content.removeFromRight (juce::roundToInt (juce::GlyphArrangement::getStringWidth (uiFont (type::small), shortcutKeyText)) + 4),
                    juce::Justification::centredRight, false);
        g.setFont (font);
    }

    g.setColour (textColour);
    g.drawFittedText (text, content, juce::Justification::centredLeft, 1, 0.9f);
}

void LookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setFont (uiFont (type::small, true));
    g.setColour (textSecondary);
    g.drawFittedText (caps (sectionName), area.reduced (14, 0).withTrimmedTop (4), juce::Justification::centredLeft, 1);
}

void LookAndFeel::drawMenuBarBackground (juce::Graphics&, int, int, bool, juce::MenuBarComponent&)
{
    // transparent: the menu sits inside the main toolbar, which paints the band behind it
}

juce::Font LookAndFeel::getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&)
{
    return uiFont (type::body);
}

int LookAndFeel::getMenuBarItemWidth (juce::MenuBarComponent& bar, int itemIndex, const juce::String& itemText)
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (getMenuBarFont (bar, itemIndex, itemText), itemText)) + 16;
}

void LookAndFeel::drawMenuBarItem (juce::Graphics& g, int width, int height, int itemIndex, const juce::String& itemText, bool isMouseOverItem,
                                   bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent& bar)
{
    juce::ignoreUnused (isMouseOverBar);
    const auto active = isMenuOpen || isMouseOverItem;

    if (active)
        fillRounded (g, juce::Rectangle<float> (0.0f, 2.0f, (float) width, (float) height - 4.0f).reduced (1.0f, 0.0f), radius::sm,
                     isMenuOpen ? bg4 : bg3);

    g.setColour (active ? textPrimary : textSecondary.brighter (0.2f));
    g.setFont (getMenuBarFont (bar, itemIndex, itemText));
    g.drawFittedText (itemText, 0, 0, width, height, juce::Justification::centred, 1);
}

// --- sliders ---------------------------------------------------------------------------------------

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float, float, juce::Slider& slider)
{
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                         && std::abs (slider.getMinimum() + slider.getMaximum()) < 1e-6 * (slider.getMaximum() - slider.getMinimum());
    auto arc = slider.findColour (juce::Slider::rotarySliderFillColourId);
    if (! slider.isEnabled())
        arc = textDisabled;
    drawKnob (g, juce::Rectangle<int> (x, y, width, height).toFloat(), pos, bipolar, arc, slider.isMouseOverOrDragging());
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                                    float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (slider.isBar())
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        fillRounded (g, bounds, radius::sm, bg0);
        auto fill = slider.isHorizontal() ? bounds.withRight (sliderPos) : bounds.withTop (sliderPos);
        fillRounded (g, fill, radius::sm, slider.findColour (juce::Slider::trackColourId).withAlpha (0.7f));
        return;
    }

    if (style == juce::Slider::TwoValueHorizontal || style == juce::Slider::TwoValueVertical || style == juce::Slider::ThreeValueHorizontal
        || style == juce::Slider::ThreeValueVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto vertical = slider.isVertical();
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto trackWidth = 4.0f;
    const auto active = slider.isMouseOverOrDragging();
    const auto trackColour = slider.isEnabled() ? slider.findColour (juce::Slider::trackColourId) : textDisabled;

    if (vertical)
    {
        const auto track = juce::Rectangle<float> (bounds.getCentreX() - trackWidth / 2, bounds.getY(), trackWidth, bounds.getHeight());
        fillRounded (g, track, trackWidth / 2, bg0);
        fillRounded (g, track.withTop (sliderPos), trackWidth / 2, trackColour.withAlpha (0.85f));

        // fader cap
        const auto capWidth = juce::jmin (bounds.getWidth() - 2.0f, 26.0f);
        const auto cap = juce::Rectangle<float> (capWidth, 12.0f).withCentre ({ bounds.getCentreX(), sliderPos });
        fillRounded (g, cap.translated (0.0f, 1.5f), radius::sm, juce::Colours::black.withAlpha (0.35f));
        fillRounded (g, cap, radius::sm, active ? juce::Colour (0xfff4f5f7) : juce::Colour (0xffd4d7dd));
        g.setColour (bg0.withAlpha (0.8f));
        g.fillRect (cap.withSizeKeepingCentre (capWidth - 8.0f, 1.5f));
    }
    else
    {
        const auto track = juce::Rectangle<float> (bounds.getX(), bounds.getCentreY() - trackWidth / 2, bounds.getWidth(), trackWidth);
        fillRounded (g, track, trackWidth / 2, bg0);
        fillRounded (g, track.withRight (sliderPos), trackWidth / 2, trackColour.withAlpha (0.85f));

        const auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, bounds.getCentreY() });
        g.setColour (active ? juce::Colour (0xfff4f5f7) : juce::Colour (0xffd4d7dd));
        g.fillEllipse (thumb);
    }
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (uiFont (type::small));
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::focusedOutlineColourId, accent);
    label->setColour (juce::Label::backgroundColourId,
                      slider.isBar() ? juce::Colours::transparentBlack : slider.findColour (juce::Slider::textBoxBackgroundColourId));
    return label;
}

juce::Button* LookAndFeel::createSliderButton (juce::Slider&, bool isIncrement)
{
    auto* b = new IconButton (isIncrement ? "+" : "-", isIncrement ? Icon::plus : Icon::minimise);
    return b;
}

juce::Slider::SliderLayout LookAndFeel::getSliderLayout (juce::Slider& slider)
{
    auto layout = juce::LookAndFeel_V4::getSliderLayout (slider);

    // rotary with a text box below: leave a small gap between the knob and the value
    if (slider.isRotary() && slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
        layout.sliderBounds = layout.sliderBounds.withTrimmedBottom (2);

    return layout;
}

// --- scrollbars ------------------------------------------------------------------------------------

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical, int thumbStart,
                                 int thumbSize, bool isMouseOver, bool isMouseDown)
{
    if (thumbSize <= 0)
        return;

    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    const auto thickness = isMouseOver || isMouseDown ? 6.0f : 4.0f;
    auto r = thumb.toFloat().reduced (vertical ? 0.0f : 2.0f, vertical ? 2.0f : 0.0f);
    r = vertical ? r.withSizeKeepingCentre (thickness, r.getHeight()) : r.withSizeKeepingCentre (r.getWidth(), thickness);
    fillRounded (g, r, thickness / 2, isMouseDown ? textSecondary : isMouseOver ? bg6.brighter (0.25f) : bg6);
}

// --- tabs ------------------------------------------------------------------------------------------

int LookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth)
{
    const auto hasIcon = (int) button.getProperties().getWithDefault (iconProperty, 0) != 0;
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (uiFont (type::body), button.getButtonText())) + (hasIcon ? tabDepth : 0) + 24;
}

void LookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.fillAll (bg1);
    g.setColour (border);
    g.fillRect (0, bar.getHeight() - 1, bar.getWidth(), 1);
}

void LookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) {}

void LookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown)
{
    const auto front = button.isFrontTab();
    auto area = button.getActiveArea().toFloat();

    if (front)
        fillRounded (g, area.reduced (2.0f, 4.0f), radius::md, bg3);
    else if (isMouseOver || isMouseDown)
        fillRounded (g, area.reduced (2.0f, 4.0f), radius::md, bg2);

    if (front)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (area.removeFromBottom (2.0f).reduced (10.0f, 0.0f), 1.0f);
    }

    const auto fg = front ? textPrimary : isMouseOver ? textPrimary.withAlpha (0.85f) : textSecondary;
    const auto iconId = (int) button.getProperties().getWithDefault (iconProperty, 0);
    const auto font = uiFont (type::body, false);
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, button.getButtonText());
    const auto iconSize = 16.0f;
    auto content = button.getActiveArea().toFloat();
    content = content.withSizeKeepingCentre (juce::jmin (content.getWidth() - 8.0f, textWidth + (iconId != 0 ? iconSize + 7.0f : 0.0f)),
                                             content.getHeight());

    if (iconId != 0)
        icons::draw (g, (Icon) iconId, content.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize), front ? accent : fg);

    content.removeFromLeft (iconId != 0 ? 7.0f : 0.0f);
    g.setColour (fg);
    g.setFont (font);
    g.drawFittedText (button.getButtonText(), content.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
}

// --- text ------------------------------------------------------------------------------------------

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (dynamic_cast<juce::AlertWindow*> (editor.getParentComponent()) != nullptr)
    {
        fillRounded (g, juce::Rectangle<int> (width, height).toFloat(), radius::md, bg0);
        return;
    }

    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), radius::sm);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const auto r = juce::Rectangle<int> (width, height).toFloat();

    if (editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
        outlineRounded (g, r, radius::sm, editor.findColour (juce::TextEditor::focusedOutlineColourId));
    else if (! editor.findColour (juce::TextEditor::outlineColourId).isTransparent())
        outlineRounded (g, r, radius::sm, editor.findColour (juce::TextEditor::outlineColourId));
}

void LookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    const auto background = label.findColour (juce::Label::backgroundColourId);
    if (! background.isTransparent())
        fillRounded (g, label.getLocalBounds().toFloat(), radius::sm, background);

    if (! label.isBeingEdited())
    {
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const auto font = getLabelFont (label);
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (font);
        const auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                          juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight())), label.getMinimumHorizontalScale());
    }

    const auto outline = label.findColour (juce::Label::outlineColourId);
    if (! outline.isTransparent())
        outlineRounded (g, label.getLocalBounds().toFloat(), radius::sm, outline);
}

// --- tooltips, alerts, windows ---------------------------------------------------------------------

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    juce::TextLayout layout;
    juce::AttributedString s;
    s.append (tipText, uiFont (type::small), textPrimary);
    layout.createLayoutWithBalancedLineLengths (s, 360.0f);
    const auto w = (int) (layout.getWidth() + 18.0f), h = (int) (layout.getHeight() + 12.0f);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.fillAll (bg3);
    g.setColour (borderStrong);
    g.drawRect (bounds, 1.0f);

    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, uiFont (type::small), textPrimary);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 360.0f);
    layout.draw (g, bounds.reduced (9.0f, 6.0f));
}

juce::Font LookAndFeel::getAlertWindowTitleFont() { return uiFont (type::title, true); }
juce::Font LookAndFeel::getAlertWindowMessageFont() { return uiFont (type::body); }
juce::Font LookAndFeel::getAlertWindowFont() { return uiFont (type::body); }

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout)
{
    auto bounds = alert.getLocalBounds().toFloat();
    g.fillAll (bg3);
    g.setColour (borderStrong);
    g.drawRect (bounds, 1.0f);
    g.setColour (accent);
    g.fillRect (bounds.removeFromTop (3.0f));

    auto iconSpace = 0;
    const auto type = alert.getAlertType();

    if (type != juce::MessageBoxIconType::NoIcon)
    {
        iconSpace = 46;
        const auto iconArea = juce::Rectangle<float> ((float) textArea.getX() - 2.0f, (float) textArea.getY() + 2.0f, 30.0f, 30.0f);
        const auto tint = type == juce::MessageBoxIconType::WarningIcon ? warning : type == juce::MessageBoxIconType::InfoIcon ? info : accent;
        g.setColour (tint.withAlpha (0.16f));
        g.fillEllipse (iconArea);
        g.setColour (tint);
        g.setFont (uiFont (18.0f, true));
        g.drawText (type == juce::MessageBoxIconType::QuestionIcon ? "?" : type == juce::MessageBoxIconType::InfoIcon ? "i" : "!",
                    iconArea, juce::Justification::centred);
    }

    g.setColour (alert.findColour (juce::AlertWindow::textColourId));
    textLayout.draw (g, textArea.withTrimmedLeft (iconSpace).toFloat());
}

void LookAndFeel::drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g, int w, int h, int titleSpaceX, int titleSpaceW,
                                              const juce::Image* icon, bool drawTitleTextOnLeft)
{
    if (w * h == 0)
        return;

    g.fillAll (bg1);
    g.setColour (border);
    g.fillRect (0, h - 1, w, 1);

    const auto font = uiFont (type::body);
    g.setFont (font);
    auto textW = juce::GlyphArrangement::getStringWidthInt (font, window.getName());
    auto iconW = 0;

    if (icon != nullptr)
        iconW = h - 10 + 6;

    textW = juce::jmin (titleSpaceW, textW + iconW);
    auto textX = drawTitleTextOnLeft ? titleSpaceX + 4 : juce::jmax (titleSpaceX, (w - textW) / 2);

    if (icon != nullptr)
    {
        g.setOpacity (window.isActiveWindow() ? 1.0f : 0.6f);
        g.drawImageWithin (*icon, textX, 5, h - 10, h - 10, juce::RectanglePlacement::centred, false);
        textX += iconW;
        textW -= iconW;
    }

    g.setColour (window.isActiveWindow() ? textPrimary : textSecondary);
    g.drawText (window.getName(), textX, 0, textW, h, juce::Justification::centredLeft, true);
}

juce::Button* LookAndFeel::createDocumentWindowButton (int buttonType)
{
    if (buttonType == juce::DocumentWindow::closeButton)
        return new CaptionButton ("close", Icon::close, true);
    if (buttonType == juce::DocumentWindow::minimiseButton)
        return new CaptionButton ("minimise", Icon::minimise, false);
    return new CaptionButton ("maximise", Icon::maximise, false);
}

void LookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    g.setColour (isMouseOver || isMouseDragging ? textSecondary : textDisabled);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w * i, (float) h + 1.0f, (float) w + 1.0f, (float) h * i, 1.0f);
}

void LookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar, int width, int height, double progress, const juce::String& textToShow)
{
    juce::ignoreUnused (bar);
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    fillRounded (g, r, r.getHeight() / 2, bg0);

    if (progress >= 0.0 && progress <= 1.0)
        fillRounded (g, r.withWidth (juce::jmax (r.getHeight(), (float) (r.getWidth() * progress))), r.getHeight() / 2, accent);
    else // indeterminate: a sliding segment
    {
        const auto t = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() / 1200.0, 1.0);
        fillRounded (g, r.withWidth (r.getWidth() * 0.3f).withX (r.getX() + (r.getWidth() * 1.3f) * t - r.getWidth() * 0.3f), r.getHeight() / 2,
                     accent.withAlpha (0.8f));
    }

    if (textToShow.isNotEmpty())
    {
        g.setColour (textPrimary);
        g.setFont (uiFont (type::small));
        g.drawText (textToShow, r, juce::Justification::centred, false);
    }
}

void LookAndFeel::drawGroupComponentOutline (juce::Graphics& g, int w, int h, const juce::String& text, const juce::Justification&,
                                             juce::GroupComponent&)
{
    const auto r = juce::Rectangle<float> (0.5f, 8.5f, (float) w - 1.0f, (float) h - 9.0f);
    outlineRounded (g, r, radius::lg, border);

    if (text.isNotEmpty())
    {
        const auto font = uiFont (type::small, true);
        const auto tw = juce::GlyphArrangement::getStringWidth (font, text) + 10.0f;
        g.setColour (bg2);
        g.fillRect (12.0f, 0.0f, tw, 16.0f);
        g.setColour (textSecondary);
        g.setFont (font);
        g.drawText (text, juce::Rectangle<float> (12.0f, 0.0f, tw, 16.0f), juce::Justification::centred, false);
    }
}

// --- file browser ----------------------------------------------------------------------------------

void LookAndFeel::drawFileBrowserRow (juce::Graphics& g, int width, int height, const juce::File& file, const juce::String& filename,
                                      juce::Image*, const juce::String&, const juce::String&, bool isDirectory, bool isItemSelected, int,
                                      juce::DirectoryContentsDisplayComponent&)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat();

    if (isItemSelected)
    {
        fillRounded (g, r.reduced (1.0f, 0.5f), radius::sm, accentMuted);
        g.setColour (accent);
        g.fillRoundedRectangle (r.withWidth (2.5f).reduced (0.0f, 3.0f), 1.0f);
    }

    auto iconKind = Icon::file;
    if (isDirectory)
        iconKind = Icon::folder;
    else if (file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;wv"))
        iconKind = Icon::audioFile;
    else if (file.hasFileExtension ("lynx;flp;dawproj"))
        iconKind = Icon::project;
    else if (file.hasFileExtension ("mid;midi"))
        iconKind = Icon::pianoRoll;
    else if (file.hasFileExtension ("fst;lynxpreset"))
        iconKind = Icon::preset;

    const auto iconSize = juce::jmin (16.0f, (float) height - 4.0f);
    icons::draw (g, iconKind, juce::Rectangle<float> (4.0f, ((float) height - iconSize) / 2.0f, iconSize, iconSize),
                 isDirectory ? textSecondary.brighter (0.15f) : (iconKind == Icon::audioFile ? info : textSecondary));

    g.setColour (isItemSelected ? textPrimary : (isDirectory ? textPrimary : textPrimary.withAlpha (0.88f)));
    g.setFont (uiFont (type::body, false));
    g.drawFittedText (filename, juce::Rectangle<int> (juce::roundToInt (iconSize) + 10, 0, width - juce::roundToInt (iconSize) - 12, height),
                      juce::Justification::centredLeft, 1, 0.9f);
}

void LookAndFeel::drawTreeviewPlusMinusBox (juce::Graphics& g, const juce::Rectangle<float>& area, juce::Colour, bool isOpen, bool isMouseOver)
{
    const auto r = area.withSizeKeepingCentre (12.0f, 12.0f);
    juce::Graphics::ScopedSaveState state (g);
    if (! isOpen)
        g.addTransform (juce::AffineTransform::rotation (-pi * 0.5f, r.getCentreX(), r.getCentreY()));
    icons::draw (g, Icon::chevronDown, r, isMouseOver ? textPrimary : textSecondary);
}
} // namespace lynx::theme
