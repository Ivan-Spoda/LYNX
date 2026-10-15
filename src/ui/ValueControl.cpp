#include "ui/ValueControl.h"

#include <cmath>

#include "core/ValueMath.h"
#include "ui/AutomationUi.h"
#include "ui/I18n.h"
#include "ui/WindowOwner.h"

namespace lynx::ui
{
namespace
{
constexpr const char* hintProperty = "lynxHint";
constexpr const char* hintMenuProperty = "lynxHintMenu";
constexpr const char* hintAutoProperty = "lynxHintAuto";
constexpr const char* ownContractProperty = "lynxOwnContract";
constexpr const char* configuredProperty = "lynxContract";
constexpr const char* sensitivityProperty = "lynxDragPixels";

juce::String number (double v, int decimals)
{
    auto s = juce::String (v, decimals);
    if (s.containsChar ('.'))
        s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
    return s == "-0" ? juce::String ("0") : s;
}

std::optional<double> leadingNumber (const juce::String& typed)
{
    const auto t = typed.trim().replaceCharacter (',', '.');
    const auto digits = t.initialSectionContainingOnly ("+-0123456789.");

    if (digits.isEmpty() || digits.containsOnly ("+-."))
        return std::nullopt;

    return digits.getDoubleValue();
}
} // namespace

// --- specs -------------------------------------------------------------------------------------

juce::String ValueSpec::text (double value) const
{
    if (toText)
        return toText (value);

    return number (value, interval >= 1.0 ? 0 : 3);
}

std::optional<double> ValueSpec::parse (const juce::String& typed) const
{
    auto value = fromText ? fromText (typed) : leadingNumber (typed);

    if (! value || ! std::isfinite (*value))
        return std::nullopt;

    auto v = juce::jlimit (minimum, maximum, *value);
    return controls::snapToInterval (v, minimum, interval);
}

ValueSpec percentSpec (const juce::String& name, double defaultValue)
{
    ValueSpec s;
    s.name = name;
    s.defaultValue = defaultValue;
    s.toText = [] (double v) { return number (v * 100.0, 0) + "%"; };
    s.fromText = [] (const juce::String& t) -> std::optional<double> {
        if (const auto v = leadingNumber (t))
            return *v / 100.0;
        return std::nullopt;
    };
    return s;
}

ValueSpec panSpec (const juce::String& name)
{
    ValueSpec s;
    s.name = name;
    s.minimum = -1.0;
    s.maximum = 1.0;
    s.defaultValue = 0.0;
    s.toText = [] (double v) {
        const auto percent = juce::roundToInt (std::abs (v) * 100.0);
        return percent == 0 ? tr ("Centered") : (v < 0 ? tr ("{0}% left", { juce::String (percent) }) : tr ("{0}% right", { juce::String (percent) }));
    };
    // typed: -50 / 50, or "50 L" / "50 R"
    s.fromText = [] (const juce::String& t) -> std::optional<double> {
        const auto lower = t.toLowerCase();
        auto v = leadingNumber (lower);

        if (! v)
        {
            if (lower.trim().startsWith ("c"))
                return 0.0;
            v = leadingNumber (lower.retainCharacters ("0123456789.,"));
        }

        if (! v)
            return std::nullopt;

        auto pan = *v / 100.0;
        const auto trimmed = lower.trim();
        if (trimmed.contains ("left") || trimmed.contains ("esq") || trimmed.startsWithChar ('l') || trimmed.endsWithChar ('l')) // "50 L", "50% left", "esquerda"
            pan = -std::abs (pan);
        return pan;
    };
    return s;
}

ValueSpec faderSpec (const juce::String& name, double defaultValue)
{
    ValueSpec s;
    s.name = name;
    s.defaultValue = defaultValue;
    s.pixelsForRange = 200.0;
    s.toText = [] (double v) {
        const auto gain = faderGain ((float) v);
        return gain <= 1.0e-5f ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dB";
    };
    s.fromText = [] (const juce::String& t) -> std::optional<double> {
        if (t.trim().startsWithIgnoreCase ("-inf"))
            return 0.0;

        const auto db = leadingNumber (t);
        if (! db)
            return std::nullopt;

        // invert the fader curve (monotonic) by bisection
        const auto target = juce::Decibels::decibelsToGain (*db, -200.0);
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 40; ++i)
        {
            const auto mid = (lo + hi) / 2.0;
            (faderGain ((float) mid) < target ? lo : hi) = mid;
        }
        return (lo + hi) / 2.0;
    };
    return s;
}

std::function<std::optional<double> (const juce::String&)> inverseOf (std::function<juce::String (double)> toText, double minimum,
                                                                      double maximum)
{
    return [toText, minimum, maximum] (const juce::String& typed) -> std::optional<double> {
        const auto wanted = leadingNumber (typed);

        if (! wanted)
            return std::nullopt;

        std::optional<double> best;
        double bestDistance = 0.0;
        constexpr int steps = 2000;

        for (int i = 0; i <= steps; ++i)
        {
            const auto v = minimum + (maximum - minimum) * i / steps;
            if (const auto shown = leadingNumber (toText (v)))
                if (const auto d = std::abs (*shown - *wanted); ! best || d < bestDistance)
                {
                    best = v;
                    bestDistance = d;
                }
        }

        return best;
    };
}

// --- gestures ----------------------------------------------------------------------------------

void ValueDrag::begin (const juce::MouseEvent& e, double value)
{
    startValue = value;
    anchorY = e.position.y;
    fine = e.mods.isCtrlDown();
    active = true;
}

double ValueDrag::update (const juce::MouseEvent& e, const ValueSpec& spec)
{
    const auto nowFine = e.mods.isCtrlDown();

    if (nowFine != fine) // switching mid-drag: continue from where the value is now
    {
        startValue = controls::dragValue (startValue, anchorY - e.position.y, spec.minimum, spec.maximum, spec.pixelsForRange, fine);
        anchorY = e.position.y;
        fine = nowFine;
    }

    const auto v = controls::dragValue (startValue, anchorY - e.position.y, spec.minimum, spec.maximum, spec.pixelsForRange, fine);
    return controls::snapToInterval (v, spec.minimum, spec.interval);
}

bool isResetClick (const juce::MouseEvent& e) noexcept
{
    return e.mods.isMiddleButtonDown() || (e.mods.isLeftButtonDown() && e.mods.isAltDown() && ! e.mods.isCtrlDown());
}

std::optional<double> wheelValue (double value, const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel, const ValueSpec& spec)
{
    const auto delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;

    if (delta == 0.0f)
        return std::nullopt;

    const auto notches = (wheel.isReversed ? -1.0 : 1.0) * (delta > 0.0f ? 1.0 : -1.0) * juce::jmax (1.0, std::abs (delta) / 0.12);
    return controls::wheelValue (value, notches, spec.minimum, spec.maximum, e.mods.isCtrlDown(), spec.interval);
}

// --- menu --------------------------------------------------------------------------------------

void addValueItems (juce::PopupMenu& menu, juce::Component& target, AudioEngine* engine, const ValueSpec& spec, double current,
                    std::function<void (double)> set)
{
    juce::Component::SafePointer<juce::Component> safe (&target);
    menu.addItem (tr ("Type value..."), [safe, spec, current, set] {
        if (safe != nullptr)
            typeValue (*safe, spec, current, set);
    });
    menu.addItem (tr ("Reset ({0})", { spec.text (spec.defaultValue) }), [spec, set] { set (spec.defaultValue); });

    if (engine != nullptr && spec.target)
    {
        menu.addSeparator();
        addAutomationItem (menu, *engine, *spec.target);
    }
}

void showValueMenu (juce::Component& target, AudioEngine* engine, const ValueSpec& spec, double current, std::function<void (double)> set)
{
    juce::PopupMenu menu;

    if (spec.name.isNotEmpty())
        menu.addSectionHeader (spec.name + ": " + spec.text (current));

    addValueItems (menu, target, engine, spec, current, std::move (set));
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMousePosition());
}

void typeValue (juce::Component& target, const ValueSpec& spec, double current, std::function<void (double)> set)
{
    juce::ignoreUnused (target);
    auto* window = new juce::AlertWindow (tr ("Type value"), spec.name, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("value", spec.text (current));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (auto* editor = window->getTextEditor ("value"))
        editor->selectAll();

    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, spec, set] (int result) {
                                 if (result != 1)
                                     return;

                                 if (const auto v = spec.parse (window->getTextEditorContents ("value")))
                                     set (*v);
                             }),
                             true);
    keepAboveMainWindow (*window);
}

// --- hints -------------------------------------------------------------------------------------

void setHint (juce::Component& c, const juce::String& text, bool hasMenu, bool automatable)
{
    c.getProperties().set (hintProperty, text);
    c.getProperties().set (hintMenuProperty, hasMenu);
    c.getProperties().set (hintAutoProperty, automatable);
}

Hint valueHint (const ValueSpec& spec, double value, const juce::String& gesture)
{
    Hint h;
    h.text = spec.name + (gesture.isNotEmpty() ? " - " + gesture : juce::String());
    h.value = spec.text (value);
    h.automatable = spec.target.has_value();
    h.hasMenu = true;
    return h;
}

namespace
{
juce::String sliderName (juce::Slider& s)
{
    if (s.getTooltip().isNotEmpty())
        return s.getTooltip().upToFirstOccurrenceOf ("\n", false, false);

    // a label attached to the slider, or the closest label above it
    if (auto* parent = s.getParentComponent())
    {
        juce::Label* best = nullptr;
        int bestDistance = 60;

        for (auto* child : parent->getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
            {
                if (label->getAttachedComponent() == &s)
                    return label->getText();

                const auto d = std::abs (label->getBounds().getCentreX() - s.getBounds().getCentreX())
                               + std::abs (label->getBottom() - s.getY());
                if (label->isVisible() && label->getBottom() <= s.getY() + 4 && d < bestDistance)
                {
                    best = label;
                    bestDistance = d;
                }
            }

        if (best != nullptr)
            return best->getText();
    }

    return s.getName().isNotEmpty() ? s.getName() : tr ("Knob");
}

ValueSpec sliderSpec (juce::Slider& s)
{
    ValueSpec spec;
    spec.name = sliderName (s);
    spec.minimum = s.getMinimum();
    spec.maximum = s.getMaximum();
    spec.interval = s.getInterval();
    spec.defaultValue = s.isDoubleClickReturnEnabled() ? s.getDoubleClickReturnValue() : s.getMinimum();
    juce::Component::SafePointer<juce::Slider> safe (&s);
    spec.toText = [safe] (double v) { return safe != nullptr ? safe->getTextFromValue (v) : juce::String (v); };
    spec.fromText = [safe] (const juce::String& t) -> std::optional<double> {
        if (safe == nullptr || ! leadingNumber (t))
            return std::nullopt;
        return safe->getValueFromText (t);
    };
    return spec;
}

juce::Slider* sliderOf (juce::Component* c)
{
    if (auto* s = dynamic_cast<juce::Slider*> (c))
        return s;

    return c != nullptr ? c->findParentComponentOfClass<juce::Slider>() : nullptr;
}

bool takesContract (juce::Slider* s)
{
    return s != nullptr && ! s->getProperties().contains (ownContractProperty) && s->getSliderStyle() != juce::Slider::IncDecButtons
           && s->getSliderStyle() != juce::Slider::TwoValueHorizontal && s->getSliderStyle() != juce::Slider::TwoValueVertical
           && s->getSliderStyle() != juce::Slider::ThreeValueHorizontal && s->getSliderStyle() != juce::Slider::ThreeValueVertical;
}

void configure (juce::Slider& s)
{
    if (s.getProperties().contains (configuredProperty))
        return;

    s.getProperties().set (configuredProperty, true);
    s.getProperties().set (sensitivityProperty, s.getMouseDragSensitivity());
    s.setVelocityModeParameters (1.0, 1, 0.0, false); // Ctrl means "fine", not "velocity mode"
    s.setScrollWheelEnabled (false);                  // the wheel is handled below (small steps, Ctrl finer)
    s.setPopupMenuEnabled (false);
}

void setFine (juce::Slider& s, bool fine)
{
    const auto base = (int) s.getProperties().getWithDefault (sensitivityProperty, 250);
    const auto wanted = fine ? (int) (base / controls::fineFactor) : base;

    if (s.getMouseDragSensitivity() != wanted)
        s.setMouseDragSensitivity (wanted);
}

struct SliderContract final : juce::MouseListener
{
    void mouseMove (const juce::MouseEvent& e) override
    {
        if (auto* s = sliderOf (e.eventComponent); takesContract (s))
            configure (*s);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* s = sliderOf (e.eventComponent);

        if (! takesContract (s) || ! s->isEnabled())
            return;

        configure (*s);
        setFine (*s, e.mods.isCtrlDown());
        juce::Component::SafePointer<juce::Slider> safe (s);
        auto set = [safe] (double v) {
            if (safe != nullptr)
                safe->setValue (v, juce::sendNotificationSync);
        };

        if (e.mods.isPopupMenu())
            showValueMenu (*s, nullptr, sliderSpec (*s), s->getValue(), set);
        else if (e.mods.isMiddleButtonDown())
            set (sliderSpec (*s).defaultValue);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (auto* s = sliderOf (e.eventComponent); takesContract (s))
            setFine (*s, e.mods.isCtrlDown());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (auto* s = sliderOf (e.eventComponent); takesContract (s))
            setFine (*s, false);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        auto* s = sliderOf (e.eventComponent);

        if (! takesContract (s) || ! s->isEnabled() || ! s->getProperties().contains (configuredProperty))
            return;

        if (const auto v = wheelValue (s->getValue(), e, wheel, sliderSpec (*s)))
            s->setValue (*v, juce::sendNotificationSync);
    }
};

std::unique_ptr<SliderContract>& contract()
{
    static std::unique_ptr<SliderContract> instance;
    return instance;
}
} // namespace

Hint hintFor (juce::Component* component, juce::Point<int> position)
{
    for (auto* c = component; c != nullptr; c = c->getParentComponent())
    {
        if (auto* source = dynamic_cast<HintSource*> (c))
        {
            auto h = source->hintAt (position);
            if (! h.isEmpty())
                return h;
        }

        if (auto* s = dynamic_cast<juce::Slider*> (c))
        {
            Hint h;
            h.text = c->getProperties().contains (hintProperty) ? c->getProperties()[hintProperty].toString() : sliderName (*s);
            h.value = s->getTextFromValue (s->getValue());
            h.hasMenu = true;
            h.automatable = (bool) c->getProperties().getWithDefault (hintAutoProperty, false);
            return h;
        }

        if (c->getProperties().contains (hintProperty))
        {
            Hint h;
            h.text = c->getProperties()[hintProperty].toString();
            h.hasMenu = c->getProperties()[hintMenuProperty];
            h.automatable = c->getProperties()[hintAutoProperty];
            return h;
        }

        if (auto* client = dynamic_cast<juce::TooltipClient*> (c))
            if (const auto tip = client->getTooltip(); tip.isNotEmpty())
            {
                Hint h;
                h.text = tip.replace ("\n", "   ");
                return h;
            }

        if (auto* parent = c->getParentComponent())
            position = parent->getLocalPoint (c, position);
    }

    return {};
}

void installSliderContract()
{
    if (contract() == nullptr)
    {
        contract() = std::make_unique<SliderContract>();
        juce::Desktop::getInstance().addGlobalMouseListener (contract().get());
    }
}

void uninstallSliderContract()
{
    if (contract() != nullptr)
    {
        juce::Desktop::getInstance().removeGlobalMouseListener (contract().get());
        contract().reset();
    }
}
} // namespace lynx::ui
