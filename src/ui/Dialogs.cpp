#include "ui/Dialogs.h"

#include "core/RackOps.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
// a row of swatches: "automatic" first, then the palette
class Swatches final : public juce::Component
{
public:
    explicit Swatches (std::uint32_t c) : colour (c) { setSize (13 * (size + gap), size + 4); }

    std::uint32_t colour;

    void paint (juce::Graphics& g) override
    {
        const auto& palette = lynxPalette();

        for (int i = 0; i <= (int) palette.size(); ++i)
        {
            const auto r = cell (i).toFloat();
            const auto value = i == 0 ? 0u : palette[(std::size_t) i - 1];

            if (i == 0)
            {
                g.setColour (theme::colour::bg4);
                g.fillRoundedRectangle (r, theme::radius::sm);
                g.setColour (theme::colour::textSecondary);
                g.drawLine (r.getX() + 5.0f, r.getBottom() - 5.0f, r.getRight() - 5.0f, r.getY() + 5.0f, 1.2f);
            }
            else
            {
                g.setColour (juce::Colour (value));
                g.fillRoundedRectangle (r, theme::radius::sm);
            }

            if (value == colour)
            {
                g.setColour (theme::colour::textPrimary);
                g.drawRoundedRectangle (r.expanded (1.5f), theme::radius::md, 2.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& palette = lynxPalette();

        for (int i = 0; i <= (int) palette.size(); ++i)
            if (cell (i).expanded (1).contains (e.getPosition()))
            {
                colour = i == 0 ? 0u : palette[(std::size_t) i - 1];
                repaint();
            }
    }

    void pickRandom()
    {
        colour = randomPaletteColour (colour, (std::uint32_t) juce::Time::getMillisecondCounter());
        repaint();
    }

private:
    static constexpr int size = 22, gap = 6;
    juce::Rectangle<int> cell (int i) const { return { 2 + i * (size + gap), 2, size, size }; }
};

class NameAndColourWindow final : public juce::AlertWindow
{
public:
    NameAndColourWindow (const juce::String& title, const juce::String& name, std::uint32_t colour)
        : juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon), swatches (colour)
    {
        addTextEditor ("name", name, tr ("Name"));
        addCustomComponent (&swatches);
        hint.setText (tr ("Click a color, or press F2 for a random one"), juce::dontSendNotification);
        hint.setFont (theme::font (theme::type::small));
        hint.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        hint.setSize (swatches.getWidth(), 18);
        addCustomComponent (&hint);
        addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

        if (auto* editor = getTextEditor ("name"))
            editor->selectAll();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::F2Key)
        {
            swatches.pickRandom();
            return true;
        }

        return juce::AlertWindow::keyPressed (key);
    }

    std::uint32_t colour() const { return swatches.colour; }

private:
    Swatches swatches;
    juce::Label hint;
};
} // namespace

std::unique_ptr<juce::AlertWindow> makeNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour)
{
    return std::make_unique<NameAndColourWindow> (title, name, colour);
}

void showNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour,
                              std::function<void (const juce::String&, std::uint32_t)> onDone)
{
    auto* window = new NameAndColourWindow (title, name, colour);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onDone] (int result) {
                                 const auto typed = window->getTextEditorContents ("name").trim();

                                 if (result == 1 && typed.isNotEmpty())
                                     onDone (typed, window->colour());
                             }),
                             true);
    keepAboveMainWindow (*window);
}
} // namespace lynx
