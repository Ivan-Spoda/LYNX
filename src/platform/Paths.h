#pragma once

// Per-OS folders (docs/PLATFORMS.md). Everything platform-specific about *where* LYNX keeps or looks for files
// lives here, so the rest of the code asks for "the settings folder" instead of spelling out %APPDATA%.

#include <juce_core/juce_core.h>

namespace lynx::platform
{
// LYNX's settings folder (created on first use):
//   Windows  %APPDATA%\LYNX
//   macOS    ~/Library/Application Support/LYNX
//   Linux    $XDG_CONFIG_HOME/LYNX (default ~/.config/LYNX)
juce::File settingsFolder();

// A file or sub-folder inside the settings folder, e.g. settingsFile ("autosave").
juce::File settingsFile (const juce::String& relativePath);

// Rebuildable caches (waveform peaks):
//   Windows  %APPDATA%\LYNX\<name>   (unchanged from earlier versions)
//   macOS    ~/Library/Caches/LYNX/<name>
//   Linux    $XDG_CACHE_HOME/LYNX/<name> (default ~/.cache/LYNX/<name>)
juce::File cacheFolder (const juce::String& name);

// The standard plugin folders of this OS, in scan order (they need not exist yet: plugins installed later land
// there): VST3, VST2, CLAP (+ AU components on macOS, LV2 on Linux) and LYNX's own Plugins folder.
juce::FileSearchPath standardPluginFolders();
} // namespace lynx::platform
