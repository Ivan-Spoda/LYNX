#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// The Settings window (FL Studio's Settings pages, interpreted): Project (tempo, time signature, swing, master
// pitch), Info (title, author, genre, web link, comments) and Files (the folders LYNX searches for missing samples,
// the Browser's folders and the plugin folders). Project and Info edit the song: every change is an undo step.
// Files edits the app's settings at once.
enum class SettingsPage { project, info, files };

// `onBrowserFoldersChanged` runs after the Browser folder list changed (the Browser reloads it).
void showSettings (AudioEngine& engine, SettingsPage page, std::function<void()> onBrowserFoldersChanged = {});

// The content, for --snapshot mode (`persistent` false: the Files page shows sample folders and saves nothing).
std::unique_ptr<juce::Component> makeSettingsPanel (AudioEngine& engine, SettingsPage page, bool persistent = false);
} // namespace lynx
