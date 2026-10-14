#pragma once

// Pattern and Channel Rack operations used by the workflow keys and menus (docs/WORKFLOW_FL.md,
// P1 items 4 and 5). Pure functions over the Song model: no JUCE, unit-tested in tests/tests.cpp.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// LYNX's colour palette for patterns, channels and inserts (0xFFRRGGBB). Original values.
const std::array<std::uint32_t, 12>& lynxPalette();

// A palette colour different from `current`, chosen from `seed` (any integer, e.g. a counter or time).
std::uint32_t randomPaletteColour (std::uint32_t current, std::uint32_t seed);

// --- patterns ----------------------------------------------------------------------------------

// True when the pattern has no notes (its name and colour don't count).
bool isPatternEmpty (const Pattern& pattern) noexcept;

// Index of the next/previous pattern (clamped to the list).
int stepPattern (const Song& song, int delta) noexcept;

// The first empty pattern after `from` (wrapping around, never `from` itself); -1 if there is none.
int findNextEmptyPattern (const Song& song, int from) noexcept;

// Selects the next empty pattern, appending a new one ("Pattern N") if every pattern has notes.
// Returns its index.
int gotoNextEmptyPattern (Song& song);

// Inserts an empty pattern before `index` (Playlist clips keep pointing at the same patterns)
// and selects it. Returns the new index.
int insertPattern (Song& song, int index, const std::string& name);

// Creates one pattern per channel that has notes in pattern `index`, named "<pattern> - <channel>"
// and coloured after the channel, inserted right after it. The original pattern is unchanged.
// Returns how many patterns were created.
int splitPatternByChannel (Song& song, int index);

// Default name for a new pattern: "Pattern N" with the lowest N not in use.
std::string newPatternName (const Song& song);

// --- channel selection -------------------------------------------------------------------------

// Multi-selection of Channel Rack rows: always sorted, unique and inside [0, count).
struct ChannelSelection
{
    std::vector<int> rows;
    int anchor = 0; // start of Shift+click ranges

    bool contains (int row) const noexcept;
    void selectOnly (int row);
    void toggle (int row);         // Ctrl+click / right-click on the selector
    void extendTo (int row);       // Shift+click: anchor..row
    void selectAll (int count);
    void clamp (int count);        // after channels were removed
};

// --- channel operations ------------------------------------------------------------------------

// Reorders the channels: newOrder[i] = old index of the channel that ends up at i (a permutation).
// Notes, audio clips, automation/MIDI targets and controller sources follow their channel.
void permuteChannels (Song& song, const std::vector<int>& newOrder);

// Moves the selected channels up (delta < 0) or down by one row, as a block that stops at the
// edges. `selection` is updated to the new rows. Returns false when nothing moved.
bool moveChannels (Song& song, std::vector<int>& selection, int delta);

// Copies channel `index` (settings, not notes) to a new channel right below it, named "<name> #2"
// (or the next free number). Returns the new index.
int cloneChannel (Song& song, int index);

// Inserts `channel` at `index` (shifting the others down). Returns the index.
int insertChannel (Song& song, int index, ChannelDef channel);

// Removes several channels (any order). Returns how many were removed.
int removeChannels (Song& song, std::vector<int> channels);

// An insert nobody uses yet: no channel routes to it, it has no effects and no insert sends into it.
bool isInsertFree (const Song& song, int track) noexcept;

// Routes each channel to its own free insert (adding inserts at the end if needed, up to
// maxMixerTracks), named and coloured after the channel. Returns the insert of each channel
// (0 = could not get one).
std::vector<int> sendChannelsToFreeInserts (Song& song, const std::vector<int>& channels);
} // namespace lynx
