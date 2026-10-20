#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
// Opening an FL Studio project behind a progress panel (round 112, docs/analise/REA_rodada112.md).
//
// Before, the open ran in one block on the message thread after a "Finding this project's plugins..." window that
// sat at 0 % while it listed the plugin folders and while each plugin file was described; then the window froze while
// the samples were decoded and every hosted plugin was created. Now the stages are:
//   1. targeted plugin scan, only for plugins not known and not already searched for (a thread);
//   2. read, parse and import the project, decode its samples in parallel (a thread);
//   3. create the hosted plugins one per message-loop turn ("Loading Auto-Tune Pro... 3/12");
//   4. build the rack (message thread, everything is ready by then).
// The panel covers the main window (input goes nowhere meanwhile), its bar only moves forward and it says what is
// loading. Phase times are written to %APPDATA%\LYNX\load.log (core/LoadTrace.h).
//
// `done (ok, error)` runs on the message thread once the project is in (or failed). The panel deletes itself.
void openFlpWithProgress (juce::Component& parent, AudioEngine& engine, const juce::File& file,
                          std::function<void (bool ok, const juce::String& error)> done);

// True while an open started by openFlpWithProgress is running (a second open is refused meanwhile).
bool projectLoadRunning();

// Drops an open in progress (the main window is closing): its thread is joined before the engine goes away.
void cancelProjectLoad();

// Writes the finished trace to %APPDATA%\LYNX\load.log (also used by the synchronous opens).
void writeLoadLog();
} // namespace lynx
