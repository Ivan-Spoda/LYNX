#include "core/NoteEdit.h"

#include <algorithm>

namespace lynx::noteedit
{
namespace
{
bool valid (const Pattern& p, int i)
{
    return i >= 0 && i < (int) p.notes.size();
}
} // namespace

void sortTracking (Pattern& pattern, std::vector<int>& tracked)
{
    sortByStartTracking (pattern.notes, tracked);
}

void remove (Pattern& pattern, std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());

    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        if (valid (pattern, *it))
            pattern.notes.erase (pattern.notes.begin() + *it);
}

std::vector<Note> copy (const Pattern& pattern, const std::vector<int>& indices)
{
    std::vector<Note> out;

    for (auto i : indices)
        if (valid (pattern, i))
            out.push_back (pattern.notes[(std::size_t) i]);

    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

std::vector<int> paste (Pattern& pattern, const std::vector<Note>& notes, std::int64_t offset, int channel)
{
    std::vector<int> added;

    for (auto n : notes)
    {
        n.start = std::max<std::int64_t> (0, n.start + offset);

        if (channel >= 0)
            n.channel = channel;

        added.push_back ((int) pattern.notes.size());
        pattern.notes.push_back (n);
    }

    return added;
}

void span (const std::vector<Note>& notes, std::int64_t& start, std::int64_t& end)
{
    start = end = 0;

    if (notes.empty())
        return;

    start = notes.front().start;
    end = 0;

    for (const auto& n : notes)
    {
        start = std::min (start, n.start);
        end = std::max (end, n.start + n.length);
    }
}

std::vector<int> duplicate (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid)
{
    const auto notes = copy (pattern, indices);

    if (notes.empty())
        return {};

    std::int64_t start = 0, end = 0;
    span (notes, start, end);
    grid = std::max<std::int64_t> (1, grid);
    const auto offset = std::max<std::int64_t> (grid, (end - start + grid - 1) / grid * grid);
    return paste (pattern, notes, offset);
}

void quantize (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid)
{
    grid = std::max<std::int64_t> (1, grid);

    for (auto i : indices)
        if (valid (pattern, i))
        {
            auto& n = pattern.notes[(std::size_t) i];
            n.start = (n.start + grid / 2) / grid * grid;
        }
}

void move (Pattern& pattern, const std::vector<int>& indices, std::int64_t& deltaTicks, int& deltaKeys)
{
    std::int64_t minStart = INT64_MAX;
    int minKey = 127, maxKey = 0;

    for (auto i : indices)
        if (valid (pattern, i))
        {
            const auto& n = pattern.notes[(std::size_t) i];
            minStart = std::min (minStart, n.start);
            minKey = std::min (minKey, n.key);
            maxKey = std::max (maxKey, n.key);
        }

    if (minStart == INT64_MAX)
    {
        deltaTicks = 0;
        deltaKeys = 0;
        return;
    }

    deltaTicks = std::max (deltaTicks, -minStart);
    deltaKeys = std::clamp (deltaKeys, -minKey, 127 - maxKey);

    for (auto i : indices)
        if (valid (pattern, i))
        {
            auto& n = pattern.notes[(std::size_t) i];
            n.start += deltaTicks;
            n.key += deltaKeys;
        }
}
} // namespace lynx::noteedit
