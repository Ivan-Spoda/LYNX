#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// Touch keyboard (FL's "Touch keyboard", interpreted; REA_rodada84): an on-screen piano that plays the selected
// channel. How far down a key is touched sets the velocity (or a fixed velocity), several fingers play at once, a
// drag glides from key to key, octave buttons shift it. Every note it starts is stopped when the finger or mouse is
// released, leaves the keys, the selected channel changes or the window closes. Toggled from View.
void toggleTouchKeyboard (AudioEngine& engine);
bool isTouchKeyboardOpen();

// The content for --snapshot mode (`pressed` keys drawn as held, nothing is played).
std::unique_ptr<juce::Component> makeTouchKeyboardPanel (AudioEngine& engine);
} // namespace lynx
