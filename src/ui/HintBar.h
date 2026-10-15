#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/ValueControl.h"

// The hint bar (docs/WORKFLOW_FL.md, G12 and section 5.1): the lower line of the main toolbar's first
// panel says what the control under the mouse does, with its value, and shows the value live while it
// is dragged.
//
// How a panel feeds it (any of these; the first that gives text wins, walking up from the component
// under the mouse):
//   1. a ui::HintSource component describes the control at a point (custom-painted controls);
//   2. ui::setHint (component, text) gives a fixed hint;
//   3. otherwise the tooltip is shown.
// While a gesture is in progress (moving notes, dragging a clip, rubber-band selection...), a panel can
// show what the gesture does right now with ui::postHint(); the bar shows the posted hint instead of the
// hover hint until shortly after the last post.
//
// Hint texts may use the markup of core/HintMarkup.h, drawn as mouse-button and key glyphs:
//   [LMB] click/drag  [2xLMB] double-click  [RMB] right-click  [MMB] middle-click  [Wheel] wheel
//   [Ctrl] [Shift] [Alt] [F5] [L]... (any other short key name: a key cap)
// e.g. tr ("Note: [LMB] drag moves  [Shift][LMB] duplicates  [RMB] deletes  [Alt] no snap").
// Tooltips should stay plain text (they are shown as is); use ui::plainHint() to derive one.
namespace lynx::ui
{
// Shows `hint` in the bar until `holdMs` after the last call (call it on every drag step).
void postHint (const Hint& hint, int holdMs = 1200);
void clearPostedHint();
std::optional<Hint> postedHint(); // the posted hint while it is fresh (the main window polls it)

// The standard gesture line of a value control (knob, fader, number): drag, fine, wheel, reset, menu.
juce::String valueGestures();
// The hint text with the markup turned into words ("[Ctrl][LMB]" -> "Ctrl+click"), for tooltips.
juce::String plainHint (const juce::String& markup);
// Hints written before the markup existed say "drag (Ctrl: fine), wheel, Alt+click resets"; this swaps
// such known phrases for their markup (applied by the bar to every hint).
juce::String withMarkup (const juce::String& text);

class HintBar final : public juce::Component
{
public:
    // `live`: the value is changing under the mouse (drawn in the accent colour).
    void setHint (const Hint& hint, bool live = false);
    const Hint& getHint() const noexcept { return current; }
    void paint (juce::Graphics& g) override;

private:
    Hint current;
    bool live = false;
};
} // namespace lynx::ui
