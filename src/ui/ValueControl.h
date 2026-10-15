#pragma once

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

// LYNX's control contract (docs/WORKFLOW_FL.md, P1 items 7 and 8). Every knob, fader, route box and
// number display behaves the same way:
//   - drag up/down to change, Ctrl+drag for fine steps;
//   - mouse wheel = small step (Ctrl+wheel finer);
//   - Alt+click or middle-click resets to the default (double-click also resets);
//   - right-click opens one menu: Type value..., Reset, then Create automation clip / Link to MIDI
//     controller / Unlink when the control can be automated.
// Custom-painted controls (rack knobs, mixer faders, effect knobs) use ValueSpec + ValueDrag +
// showValueMenu(); plain juce::Sliders get the same contract from installSliderContract().
//
// The hint strip reads `hintFor()`: a HintSource component describes what is under the mouse, other
// components use their hint property or tooltip.
namespace lynx::ui
{
struct ValueSpec
{
    juce::String name;                 // "Kick - Volume"
    double minimum = 0.0, maximum = 1.0, defaultValue = 0.0;
    double interval = 0.0;             // > 0: integer-like controls (route box)
    double pixelsForRange = 150.0;     // drag distance that sweeps the whole range
    // shown value <-> stored value (e.g. 0..1 volume shown as "78%"). Both optional.
    std::function<juce::String (double)> toText;
    std::function<std::optional<double> (const juce::String&)> fromText;
    std::optional<AutomationTarget> target; // automatable: adds the automation / MIDI items

    juce::String text (double value) const;
    std::optional<double> parse (const juce::String& typed) const;
};

// The usual specs.
ValueSpec percentSpec (const juce::String& name, double defaultValue);          // 0..1 shown as 0..100%
ValueSpec panSpec (const juce::String& name);                                   // -1..1 shown as L50 / C / R50
ValueSpec faderSpec (const juce::String& name, double defaultValue);            // 0..1 fader shown in dB

// A type-in parser for any display function: finds the value whose shown number is closest to the
// typed one (searches the range in fine steps, so it also works for non-linear curves).
std::function<std::optional<double> (const juce::String&)> inverseOf (std::function<juce::String (double)> toText, double minimum,
                                                                      double maximum);

// One drag gesture on a custom-painted control.
class ValueDrag
{
public:
    void begin (const juce::MouseEvent& e, double value);
    // New value for the current mouse position (Ctrl held = fine; the switch is seamless mid-drag).
    double update (const juce::MouseEvent& e, const ValueSpec& spec);
    bool isActive() const noexcept { return active; }
    void end() noexcept { active = false; }

private:
    double startValue = 0.0;
    float anchorY = 0.0f;
    bool fine = false, active = false;
};

// Alt+left-click or middle-click.
bool isResetClick (const juce::MouseEvent& e) noexcept;

// Value after a wheel move over the control (nothing if the wheel did not move vertically).
std::optional<double> wheelValue (double value, const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel, const ValueSpec& spec);

// The right-click menu. `set` receives the new value (typed or reset). `engine` may be null when the
// control cannot be automated.
void showValueMenu (juce::Component& target, AudioEngine* engine, const ValueSpec& spec, double current, std::function<void (double)> set);
void addValueItems (juce::PopupMenu& menu, juce::Component& target, AudioEngine* engine, const ValueSpec& spec, double current,
                    std::function<void (double)> set);

// "Type value" dialog (asynchronous).
void typeValue (juce::Component& target, const ValueSpec& spec, double current, std::function<void (double)> set);

// --- hints -----------------------------------------------------------------------------------------

struct Hint
{
    juce::String text;         // what the control does
    juce::String value;        // its current value ("" if none)
    bool automatable = false;  // can be automated / linked to MIDI
    bool hasMenu = false;      // right-click opens a menu

    bool isEmpty() const noexcept { return text.isEmpty() && value.isEmpty(); }
};

// Components that paint several controls themselves describe the one at `position`.
class HintSource
{
public:
    virtual ~HintSource() = default;
    virtual Hint hintAt (juce::Point<int> position) = 0;
};

// A fixed hint for a component (stored in its properties; wins over the tooltip).
void setHint (juce::Component& c, const juce::String& text, bool hasMenu = false, bool automatable = false);

// The hint for the component under the mouse (walks up to the parents).
Hint hintFor (juce::Component* component, juce::Point<int> positionInComponent);

// Hint for a value control described by a spec.
Hint valueHint (const ValueSpec& spec, double value, const juce::String& gesture = {});

// Gives every juce::Slider in the app the contract (installed once, by the main window): right-click
// menu, middle-click reset, Ctrl-drag fine, finer Ctrl+wheel. Sliders with the property
// "lynxOwnContract" set are left alone.
void installSliderContract();
void uninstallSliderContract();
} // namespace lynx::ui
