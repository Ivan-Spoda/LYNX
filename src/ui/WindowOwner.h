#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
// Secondary windows (plugin, channel settings, EQ, automation) always stay above the LYNX main window,
// without staying above other programs: on Windows, the main window becomes their "owner".
void setMainWindowForOwnership (juce::Component* mainWindow);
void keepAboveMainWindow (juce::Component& window); // call after the window is visible; sets the LYNX icon

// Native window chrome in the LYNX colours (dark title bar on Windows). keepAboveMainWindow calls it.
void applyWindowChrome (juce::Component& window);

// The LYNX logo (256 px, with transparency).
juce::Image logoImage();

// The CPU time this process has used so far, in seconds (user + kernel; --editorcpu).
double processCpuSeconds();
} // namespace lynx
