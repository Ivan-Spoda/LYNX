#pragma once

// Names for undo steps (docs/WORKFLOW_FL.md P2: history panel). The undo stack keeps whole Song
// snapshots; this compares two of them and says what changed, in a few words the UI translates.

#include <string>

#include "core/Song.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx
{
struct ChangeLabel
{
    const char* text = "Edit"; // English, may contain {0}; translated by the UI
    std::string arg;           // what {0} stands for (a pattern, channel or insert name)
};

// What turned `before` into `after` (the first difference found, from the most specific).
ChangeLabel describeChange (const Song& before, const Song& after);

// Same, already formatted in English ("Notes: Pattern 1"); used by the tests and logs.
std::string describeChangeText (const Song& before, const Song& after);
} // namespace lynx
