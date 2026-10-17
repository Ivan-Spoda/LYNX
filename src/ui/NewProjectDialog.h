#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Song.h"

namespace lynx
{
// The New project window (FL's "New project", interpreted; REA_rodada84): a template (LYNX's default, the built-in
// ones, or the user's own .lynx files in Documents\LYNX\Templates), name and folder, tempo and time signature, the
// project's credits. `onCreate` receives the new song and, when a folder was chosen, the file it should be saved to.
using NewProjectCallback = std::function<void (Song song, juce::File file)>;
void showNewProjectDialog (NewProjectCallback onCreate);

// Whether File › New opens the window (the "Don't show this again" choice), and the user templates' folder.
bool newProjectDialogEnabled();
juce::File userTemplatesFolder();

// Saves `song` as a user template named `name` (File › Save as template...). False with `error` on failure.
bool saveUserTemplate (const Song& song, const juce::String& name, juce::String& error);

// The window's content, for --snapshot mode (sample user templates, nothing saved).
std::unique_ptr<juce::Component> makeNewProjectPanel();
} // namespace lynx
