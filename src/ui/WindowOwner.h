#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
// Secondary windows (plugin, channel settings, EQ, automation) always stay above the LYNX main window,
// without staying above other programs: on Windows, the main window becomes their "owner".
void setMainWindowForOwnership (juce::Component* mainWindow);
void keepAboveMainWindow (juce::Component& window); // call after the window is visible; sets the LYNX icon

// The LYNX logo (256 px, with transparency).
juce::Image logoImage();
} // namespace lynx
