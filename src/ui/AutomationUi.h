#pragma once

#include "engine/AudioEngine.h"

namespace lynx
{
// Readable name of what an automation controls ("Kick - Volume", "Insert 3 - Pan"...).
juce::String describeAutomationTarget (AudioEngine& engine, const AutomationTarget& target);

// Creates an automation for the target, starting at the current value, plus a clip of it in the Playlist
// (from the start of the song to its end, on a free track), like FL's "Create automation clip".
void createAutomationClip (AudioEngine& engine, const AutomationTarget& target);

// Opens (or brings to front) the point editor window of an automation.
void openAutomationEditor (AudioEngine& engine, int automation);
void closeAutomationEditors();

// Just the editor (for --snapshot mode).
std::unique_ptr<juce::Component> makeAutomationEditor (AudioEngine& engine, int automation);

// "Create automation clip" item for knob menus.
void addAutomationItem (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target);

// Submenu with a plugin's parameters (channel instrument or slot effect).
void addPluginParameterItems (juce::PopupMenu& menu, AudioEngine& engine, juce::AudioPluginInstance& plugin,
                              const AutomationTarget& base);
} // namespace lynx
