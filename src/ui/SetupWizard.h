#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// First-run setup (small and skippable; Options > Setup wizard reopens it): language, audio device with a test
// sound, plugin folders (the standard VST3/CLAP folders plus the ones FL Studio uses on this PC), and a plugin
// scan that runs in the background (it keeps going after the window closes).
// `onLanguage(id)` runs when another language was chosen (the caller saves it and offers a restart).
void showSetupWizard (AudioEngine& engine, std::function<void (const juce::String&)> onLanguage);

// The wizard's content, for --snapshot mode.
std::unique_ptr<juce::Component> makeSetupWizardContent (AudioEngine& engine, std::function<void (const juce::String&)> onLanguage,
                                                         std::function<void()> onClose);

// Whether the wizard was finished or skipped once (%APPDATA%\LYNX\setup-done.txt).
bool setupWizardDone();
void markSetupWizardDone();

// "Scan for new plugins at every start" (on by default; set in the wizard).
bool backgroundScanAtStart();
} // namespace lynx
