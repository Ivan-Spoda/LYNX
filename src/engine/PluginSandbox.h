#pragma once

#include <memory>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

namespace lynx
{
// Optional out-of-process plugins ("sandbox"): each plugin runs in a child LYNX.exe
// (`--plugin-host`). Audio, MIDI and parameters go through shared memory every block; state and
// editor window, through messages. If the child hangs or exits, the plugin goes silent and LYNX keeps running.
// Windows only; on other systems `createSandboxedPlugin` fails and the plugin runs in-process.

constexpr const char* pluginSandboxId = "lynx-plugin-host";

// On/off (file sandbox.txt in %APPDATA%\LYNX; default: off).
bool pluginSandboxEnabled();
void setPluginSandboxEnabled (bool enabled);

// Plugins that always run in a separate process, by name (round 157a): e.g. one whose editor crashed LYNX (a GPU
// driver failing inside the plugin's OpenGL window takes the host down with it; in its own process only the plugin
// goes). File separate-process.txt in the settings folder, one name per line.
bool pluginAlwaysSeparate (const juce::String& name);
void setPluginAlwaysSeparate (const juce::String& name, bool on);

// Opens the plugin in a child process, already prepared (rate, block, base64 state). nullptr + `error` on
// failure (child didn't start, plugin didn't load, timeout).
std::unique_ptr<juce::AudioPluginInstance> createSandboxedPlugin (const juce::PluginDescription& description, double sampleRate,
                                                                  int blockSize, const std::string& stateBase64, juce::String& error);

// True if the plugin is a sandbox proxy and its process died (hung, exited or stopped responding).
bool isSandboxedPluginDead (const juce::AudioPluginInstance& plugin);

// Child mode: if the command line belongs to a sandbox process, connects to the parent and returns true (the app
// must not open a window or audio). The process exits on its own when the parent goes away.
bool runPluginSandboxWorker (const juce::String& commandLine);
void shutdownPluginSandboxWorker();

// Tests (--sandboxtest): no alert box when a plugin dies; and a request that makes the child
// hang on its next audio call (to check the timeout).
void setPluginSandboxAlerts (bool show);
void hangSandboxedPluginForTest (juce::AudioPluginInstance& plugin);
} // namespace lynx
