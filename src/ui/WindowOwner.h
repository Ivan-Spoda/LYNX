#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
// Secondary windows (plugin, channel settings, EQ, automation) always stay above the LYNX main window,
// without staying above other programs: on Windows, the main window becomes their "owner".
void setMainWindowForOwnership (juce::Component* mainWindow);
void keepAboveMainWindow (juce::Component& window); // call after the window is visible; sets the LYNX icon
// (it stays owned when JUCE recreates the native window later, e.g. setResizable after it is shown)

// True when `window` is currently owned by the main window (Windows; elsewhere: keepAboveMainWindow was called).
bool isOwnedByMainWindow (juce::Component& window);
// True when `window` is above the main window in the z-order (Windows; elsewhere: isOwnedByMainWindow).
bool isAboveMainWindow (juce::Component& window);

// --windowtest only: makes `window` the foreground window even when LYNX was started in the background.
void forceForegroundForTest (juce::Component& window);

// Native window chrome in the LYNX colours (dark title bar on Windows). keepAboveMainWindow calls it.
void applyWindowChrome (juce::Component& window);

// The LYNX logo (256 px, with transparency).
juce::Image logoImage();

// The CPU time this process has used so far, in seconds (user + kernel; --editorcpu).
double processCpuSeconds();
} // namespace lynx
