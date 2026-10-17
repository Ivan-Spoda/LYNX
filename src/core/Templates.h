#pragma once

#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// New project templates (File > New from template).
std::vector<std::string> templateNames();
Song makeTemplate (int index);

// The New project window (FL's "New project", interpreted; REA_rodada84): name, folder, time settings and credits.
struct NewProjectSetup
{
    std::string name;         // the project's name (file name)
    std::string folder;       // where it is saved; empty = not saved yet
    bool ownFolder = true;    // create "<folder>/<name>/" for it
    double bpm = 130.0;
    int beatsPerBar = 4;
    ProjectInfo info;
};

// A name usable as a file name: characters Windows refuses become '_', spaces/dots trimmed at the ends; empty → "Untitled".
std::string legalProjectName (const std::string& name);

// Where the project file goes: "<folder>/<name>/<name><ext>" with ownFolder, else "<folder>/<name><ext>"; empty
// when there is no folder. Uses '/' (the caller converts).
std::string newProjectPath (const NewProjectSetup& setup, const char* extension);

// Applies the time settings (tempo 10..522, beats per bar 1..16) and the credits to a song made from a template.
void applySetup (Song& song, const NewProjectSetup& setup);

// A user template's name from its file name ("Trap kit.lynx" → "Trap kit").
std::string templateDisplayName (const std::string& fileName);
} // namespace lynx
