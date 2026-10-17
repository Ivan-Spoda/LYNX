#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;

// The Plugin manager (FL Studio's plugin manager, interpreted): every scanned VST3/CLAP plugin with its vendor,
// type, format, file and status, the plugins the scan couldn't load (retry or forget them), favourites (listed first
// in the plugin menus and the quick-add palette), a search, and scanning (new plugins / everything again).
// `onFolders` opens the folder settings (Settings › Files).
void showPluginManager (AudioEngine& engine, std::function<void()> onFolders);

// The content, for --snapshot mode (`persistent` false: sample plugins, nothing saved).
std::unique_ptr<juce::Component> makePluginManagerPanel (AudioEngine& engine, bool persistent, std::function<void()> onFolders = {});
} // namespace lynx
