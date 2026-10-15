#pragma once

// Piano Roll note tool dialogs (docs/WORKFLOW_FL.md P2, R23): Strum, Arpeggiate, Flip, Randomise,
// Scale levels and Limit. Each is a small window whose controls preview live on the pattern (not in the
// history); Accept makes the change one undo step, Revert / closing puts the notes back.
// The note maths lives in core/NoteTools.

#include <cstdint>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
enum class NoteTool { strum, arpeggiate, flip, randomise, scaleLevels, limit };

const char* noteToolTitle (NoteTool tool); // English, for tr()

// The panel (for --snapshot and tests); `indices` are notes of the current pattern, `grid` the editor's grid.
std::unique_ptr<juce::Component> makeNoteToolPanel (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid);

// Opens the tool in its own window. `onFinished` runs after Accept or Revert (the note indices changed).
void showNoteTool (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid, std::function<void()> onFinished = {});
} // namespace lynx
