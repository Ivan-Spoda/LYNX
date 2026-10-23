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

// An output of a built-in controller that knobs can be linked to (Envelope / Keyboard Controller channels, Formula and
// Peak Controller mixer slots), as FL's "Link to controller" lists them.
struct ControllerOutput
{
    int track = -1, slot = 0, output = 0; // a mixer slot's controller effect, or
    int channel = -1;                     // a controller channel
    juce::String label;
};
std::vector<ControllerOutput> controllerOutputs (const Song& song);
// Drives `target` live from that output (replaces the target's previous controller link).
void linkToController (AudioEngine& engine, const AutomationTarget& target, const ControllerOutput& source);
// "Link to internal controller" submenu.
void addControllerLinkItems (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& target);

// "MIDI CC #64 (Damper pedal (sustain))", "MIDI Channel 2 Aftertouch"… for a MIDI index (core/MidiControls.h).
juce::String midiParamLabel (int param);

// Submenu with the MIDI CCs, channel aftertouch and pitch bend a hosted plugin can be automated with (FL's wrapper).
void addMidiParameterItems (juce::PopupMenu& menu, AudioEngine& engine, const AutomationTarget& base);

// Submenu with a plugin's parameters (channel instrument or slot effect).
void addPluginParameterItems (juce::PopupMenu& menu, AudioEngine& engine, juce::AudioPluginInstance& plugin,
                              const AutomationTarget& base);
} // namespace lynx
