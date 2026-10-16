#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Window with a plugin's own UI. Keeps the plugin alive while
// it is open (even if the channel is removed), and only one window per plugin.
namespace PluginWindows
{
// `owner` is the adapter (PluginInstrument/PluginEffect) that holds the plugin.
void open (std::shared_ptr<void> owner, juce::AudioPluginInstance& plugin, const juce::String& title);
void closeAll();
bool isOpen (const juce::AudioPluginInstance& plugin);
void close (const juce::AudioPluginInstance& plugin);
} // namespace PluginWindows

// Scans the VST3 folders with a progress window. `onDone(newCount)` runs on the UI thread.
void scanPluginsWithProgress (AudioEngine& engine, std::function<void (int)> onDone);

// Before an .flp opens: the plugins it uses that LYNX doesn't know yet get a targeted scan of just their files
// (the ones FL Studio's database or the project names, and same-named files in the plugin folders), in the
// background with a small progress window; a running background scan pauses meanwhile. `then()` runs on the UI
// thread afterwards (right away when nothing needs scanning).
void findProjectPlugins (AudioEngine& engine, std::vector<FlpPluginUse> uses, std::function<void()> then);

// Menus of known plugins (instruments or effects). `onChosen` receives the reference.
void addPluginItems (juce::PopupMenu& menu, AudioEngine& engine, bool instruments, std::function<void (PluginRef)> onChosen);
} // namespace lynx
