#pragma once

#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
// A small non-modal window that owns `content`, stays above the main window and deletes itself when
// closed (close button or Esc). Used by the note tool dialogs, the undo history and the quick-add palette.
// `key` identifies the window: opening another with the same key closes the previous one.
juce::DocumentWindow* openToolWindow (std::unique_ptr<juce::Component> content, const juce::String& title, const juce::String& key = {});

// Closes the tool window that holds `content` (from inside it, e.g. an Accept button).
void closeToolWindowOf (juce::Component& content);

// The open tool window with this key, or nullptr.
juce::DocumentWindow* findToolWindow (const juce::String& key);
} // namespace lynx
