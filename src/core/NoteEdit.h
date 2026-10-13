#pragma once

// Note editing operations used by the Piano Roll (selection, copy/paste,
// quantize...). They work with indices into Pattern::notes. No JUCE, testable.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::noteedit
{
// Sorts items with a `start` field (notes, clips) and updates `tracked` (indices) to the new positions.
template <typename T>
void sortByStartTracking (std::vector<T>& items, std::vector<int>& tracked)
{
    std::vector<int> order (items.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = (int) i;

    std::stable_sort (order.begin(), order.end(),
                      [&] (int a, int b) { return items[(std::size_t) a].start < items[(std::size_t) b].start; });

    std::vector<int> newIndex (order.size());
    std::vector<T> sorted;
    sorted.reserve (order.size());

    for (std::size_t j = 0; j < order.size(); ++j)
    {
        newIndex[(std::size_t) order[j]] = (int) j;
        sorted.push_back (items[(std::size_t) order[j]]);
    }

    items = std::move (sorted);

    for (auto& i : tracked)
        if (i >= 0 && i < (int) newIndex.size())
            i = newIndex[(std::size_t) i];
}

// Sorts the notes by start and updates `tracked` (indices) to the new positions.
void sortTracking (Pattern& pattern, std::vector<int>& tracked);

// Removes the notes at the given indices (in any order, duplicates ignored).
void remove (Pattern& pattern, std::vector<int> indices);

// Copies of the notes at the given indices, in start order.
std::vector<Note> copy (const Pattern& pattern, const std::vector<int>& indices);

// Appends `notes` shifted by `offset` ticks; channel >= 0 forces the channel.
// Returns the indices of the new notes (the vector is not re-sorted).
std::vector<int> paste (Pattern& pattern, const std::vector<Note>& notes, std::int64_t offset, int channel = -1);

// Start of the first note and end of the last (0, 0 if empty).
void span (const std::vector<Note>& notes, std::int64_t& start, std::int64_t& end);

// Duplicates the notes right after themselves (like FL's Ctrl+B): the offset is the
// span length rounded up to a multiple of `grid`. Returns the new indices.
std::vector<int> duplicate (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// Rounds the notes' start to the nearest multiple of `grid` (length kept).
void quantize (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// Moves the notes by (deltaTicks, deltaKeys), clamping so none leaves [0, ...) and [0, 127].
// Returns the offset actually applied.
void move (Pattern& pattern, const std::vector<int>& indices, std::int64_t& deltaTicks, int& deltaKeys);
} // namespace lynx::noteedit
