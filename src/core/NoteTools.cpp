#include "core/NoteTools.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace lynx::notetools
{
namespace
{
bool valid (const Pattern& p, int i)
{
    return i >= 0 && i < (int) p.notes.size();
}

std::vector<int> validSorted (const Pattern& p, std::vector<int> indices)
{
    indices.erase (std::remove_if (indices.begin(), indices.end(), [&p] (int i) { return ! valid (p, i); }), indices.end());
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    return indices;
}

// small deterministic generator (the same seed gives the same result on every compiler)
struct Random
{
    std::uint32_t state;
    explicit Random (std::uint32_t seed) : state (seed * 2654435761u + 0x6d2b79f5u) {}

    float next() noexcept // 0..1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state >> 8) / (float) (1u << 24);
    }

    float bipolar (bool both) noexcept { return both ? next() * 2.0f - 1.0f : -next(); }
};
} // namespace

// --- levels -----------------------------------------------------------------------------------

const char* levelName (Level level)
{
    switch (level)
    {
        case Level::velocity: return TR_NOOP ("Velocity");
        case Level::pan: return TR_NOOP ("Pan");
        case Level::release: return TR_NOOP ("Release");
        case Level::finePitch: return TR_NOOP ("Fine pitch");
        case Level::modX: return TR_NOOP ("Mod X");
        case Level::modY: return TR_NOOP ("Mod Y");
    }
    return "";
}

float levelValue (const Note& n, Level level) noexcept
{
    switch (level)
    {
        case Level::velocity: return n.velocity;
        case Level::pan: return (n.pan + 1.0f) * 0.5f;
        case Level::release: return n.release;
        case Level::finePitch: return (n.finePitch + 120.0f) / 240.0f;
        case Level::modX: return n.modX;
        case Level::modY: return n.modY;
    }
    return 0.0f;
}

void setLevelValue (Note& n, Level level, float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);

    switch (level)
    {
        case Level::velocity: n.velocity = v; break;
        case Level::pan: n.pan = v * 2.0f - 1.0f; break;
        case Level::release: n.release = v; break;
        case Level::finePitch: n.finePitch = v * 240.0f - 120.0f; break;
        case Level::modX: n.modX = v; break;
        case Level::modY: n.modY = v; break;
    }
}

float levelDefault (Level level) noexcept
{
    return levelValue (Note {}, level);
}

bool levelIsBipolar (Level level) noexcept
{
    return level == Level::pan || level == Level::finePitch;
}

// --- scales -----------------------------------------------------------------------------------

const std::vector<ScaleInfo>& scales()
{
    static const std::vector<ScaleInfo> list {
        { TR_NOOP ("No scale"), {} },
        { TR_NOOP ("Major"), { 0, 2, 4, 5, 7, 9, 11 } },
        { TR_NOOP ("Natural minor"), { 0, 2, 3, 5, 7, 8, 10 } },
        { TR_NOOP ("Harmonic minor"), { 0, 2, 3, 5, 7, 8, 11 } },
        { TR_NOOP ("Major pentatonic"), { 0, 2, 4, 7, 9 } },
        { TR_NOOP ("Minor pentatonic"), { 0, 3, 5, 7, 10 } },
        { "Blues", { 0, 3, 5, 6, 7, 10 } },
        { TR_NOOP ("Dorian"), { 0, 2, 3, 5, 7, 9, 10 } },
        { TR_NOOP ("Phrygian"), { 0, 1, 3, 5, 7, 8, 10 } },
    };
    return list;
}

bool inScale (int key, int root, int scale) noexcept
{
    if (scale <= 0 || scale >= (int) scales().size())
        return true;

    const auto& intervals = scales()[(std::size_t) scale].intervals;
    const auto degree = ((key - root) % 12 + 12) % 12;
    return std::find (intervals.begin(), intervals.end(), degree) != intervals.end();
}

int snapToScale (int key, int root, int scale) noexcept
{
    key = std::clamp (key, 0, 127);

    if (scale <= 0 || scale >= (int) scales().size())
        return key;

    for (int d = 0; d < 12; ++d)
    {
        if (key - d >= 0 && inScale (key - d, root, scale))
            return key - d;
        if (key + d <= 127 && inScale (key + d, root, scale))
            return key + d;
    }

    return key;
}

void snapNotesToScale (Pattern& pattern, const std::vector<int>& indices, int root, int scale)
{
    for (auto i : validSorted (pattern, indices))
    {
        auto& n = pattern.notes[(std::size_t) i];
        n.key = snapToScale (n.key, root, scale);
    }
}

// --- chords -----------------------------------------------------------------------------------

std::vector<std::vector<int>> chordsOf (const Pattern& pattern, const std::vector<int>& indices)
{
    std::map<std::tuple<std::int64_t, int>, std::vector<int>> groups; // (start, channel)

    for (auto i : validSorted (pattern, indices))
    {
        const auto& n = pattern.notes[(std::size_t) i];
        groups[{ n.start, n.channel }].push_back (i);
    }

    std::vector<std::vector<int>> out;

    for (auto& [key, group] : groups)
    {
        std::stable_sort (group.begin(), group.end(), [&pattern] (int a, int b) {
            return pattern.notes[(std::size_t) a].key < pattern.notes[(std::size_t) b].key;
        });
        out.push_back (std::move (group));
    }

    return out;
}

// --- strum ------------------------------------------------------------------------------------

void strum (Pattern& pattern, const std::vector<int>& indices, const StrumOptions& o)
{
    const auto chords = chordsOf (pattern, indices);
    int chordNumber = 0;
    const auto exponent = std::pow (2.0, (double) std::clamp (o.curve, -1.0f, 1.0f) * 2.0);

    for (const auto& chord : chords)
    {
        if (chord.size() < 2)
            continue;

        auto downward = o.downward;
        if (o.alternate && (chordNumber % 2) == 1)
            downward = ! downward;
        ++chordNumber;

        const auto count = (int) chord.size();
        const auto total = (double) o.spread * (count - 1);

        for (int k = 0; k < count; ++k)
        {
            auto& n = pattern.notes[(std::size_t) chord[(std::size_t) (downward ? count - 1 - k : k)]];
            const auto x = (double) k / (double) (count - 1);
            const auto offset = std::max<std::int64_t> (0, (std::int64_t) std::llround (total * std::pow (x, exponent)));
            const auto end = n.start + n.length;
            n.start += offset;

            if (o.keepEnds)
                n.length = std::max<std::int64_t> (std::min<std::int64_t> (n.length, 10), end - n.start);

            n.velocity = std::clamp (n.velocity + o.velocityStep * (float) k, 0.0f, 1.0f);
        }
    }
}

// --- arpeggiate -------------------------------------------------------------------------------

std::vector<int> arpeggiate (Pattern& pattern, const std::vector<int>& indices, const ArpeggiateOptions& o)
{
    const auto step = std::max<std::int64_t> (1, o.step);
    const auto octaves = std::clamp (o.octaves, 1, 4);
    const auto noteLength = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) step * std::clamp (o.gate, 0.05f, 1.0f)));
    std::vector<int> doomed;
    std::vector<Note> created;

    for (const auto& chord : chordsOf (pattern, indices))
    {
        if (chord.size() < 2 && octaves == 1)
            continue;

        std::vector<Note> base;
        std::int64_t span = 0;

        for (auto i : chord)
        {
            base.push_back (pattern.notes[(std::size_t) i]);
            span = std::max (span, pattern.notes[(std::size_t) i].length);
            doomed.push_back (i);
        }

        // the ascending run over the octaves
        std::vector<Note> run;
        for (int oct = 0; oct < octaves; ++oct)
            for (auto n : base)
                if (n.key + 12 * oct <= 127)
                {
                    n.key += 12 * oct;
                    run.push_back (n);
                }

        std::vector<Note> sequence;
        auto down = run;
        std::reverse (down.begin(), down.end());

        switch (o.direction)
        {
            case ArpeggiateOptions::down: sequence = down; break;
            case ArpeggiateOptions::upDown:
                sequence = run;
                if (down.size() > 2)
                    sequence.insert (sequence.end(), down.begin() + 1, down.end() - 1);
                break;
            case ArpeggiateOptions::downUp:
                sequence = down;
                if (run.size() > 2)
                    sequence.insert (sequence.end(), run.begin() + 1, run.end() - 1);
                break;
            default: sequence = run; break;
        }

        if (sequence.empty())
            continue;

        const auto start = base.front().start;

        for (std::int64_t t = 0, k = 0; t < span; t += step, ++k)
        {
            auto n = sequence[(std::size_t) (k % (std::int64_t) sequence.size())];
            n.start = start + t;
            n.length = std::min (noteLength, span - t);
            created.push_back (n);
        }
    }

    std::sort (doomed.begin(), doomed.end());
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)
        pattern.notes.erase (pattern.notes.begin() + *it);

    std::vector<int> added;
    for (const auto& n : created)
    {
        added.push_back ((int) pattern.notes.size());
        pattern.notes.push_back (n);
    }

    return added;
}

// --- flip -------------------------------------------------------------------------------------

void flip (Pattern& pattern, const std::vector<int>& indices, bool horizontal, bool vertical)
{
    const auto targets = validSorted (pattern, indices);

    if (targets.empty())
        return;

    std::int64_t from = INT64_MAX, to = INT64_MIN;
    int low = 127, high = 0;

    for (auto i : targets)
    {
        const auto& n = pattern.notes[(std::size_t) i];
        from = std::min (from, n.start);
        to = std::max (to, n.start + n.length);
        low = std::min (low, n.key);
        high = std::max (high, n.key);
    }

    for (auto i : targets)
    {
        auto& n = pattern.notes[(std::size_t) i];

        if (horizontal)
            n.start = from + (to - (n.start + n.length));
        if (vertical)
            n.key = std::clamp (low + high - n.key, 0, 127);
    }
}

// --- randomise --------------------------------------------------------------------------------

void randomise (Pattern& pattern, const std::vector<int>& indices, const RandomiseOptions& o)
{
    Random random (o.seed);

    for (auto i : validSorted (pattern, indices))
    {
        auto& n = pattern.notes[(std::size_t) i];
        // every note draws the same number of values, so changing one amount doesn't reshuffle the others
        const auto rv = random.bipolar (o.bipolar), rp = random.bipolar (true), rf = random.bipolar (true);
        const auto rx = random.bipolar (true), ry = random.bipolar (true), rt = random.bipolar (o.bipolar);
        const auto rl = random.bipolar (true);

        n.velocity = std::clamp (n.velocity + o.velocity * rv, 0.0f, 1.0f);
        n.pan = std::clamp (n.pan + 2.0f * o.pan * rp, -1.0f, 1.0f);
        n.finePitch = std::clamp (n.finePitch + 120.0f * o.finePitch * rf, -120.0f, 120.0f);
        n.modX = std::clamp (n.modX + o.modX * rx, 0.0f, 1.0f);
        n.modY = std::clamp (n.modY + o.modY * ry, 0.0f, 1.0f);

        if (o.timing > 0)
            n.start = std::max<std::int64_t> (0, n.start + (std::int64_t) std::llround ((double) o.timing * (o.bipolar ? rt : -rt)));

        if (o.length > 0.0f)
            n.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((double) n.length * (1.0 + (double) o.length * rl)));
    }
}

// --- scale levels -----------------------------------------------------------------------------

void scaleLevels (Pattern& pattern, const std::vector<int>& indices, Level level, float scaleStart, float scaleEnd, float offset)
{
    const auto targets = validSorted (pattern, indices);

    if (targets.empty())
        return;

    std::int64_t first = INT64_MAX, last = INT64_MIN;
    for (auto i : targets)
    {
        first = std::min (first, pattern.notes[(std::size_t) i].start);
        last = std::max (last, pattern.notes[(std::size_t) i].start);
    }

    const auto centre = levelIsBipolar (level) ? 0.5f : 0.0f;

    for (auto i : targets)
    {
        auto& n = pattern.notes[(std::size_t) i];
        const auto t = last > first ? (float) (n.start - first) / (float) (last - first) : 0.0f;
        const auto scale = scaleStart + (scaleEnd - scaleStart) * t;
        setLevelValue (n, level, centre + (levelValue (n, level) - centre) * scale + offset);
    }
}

// --- limit ------------------------------------------------------------------------------------

void limit (Pattern& pattern, const std::vector<int>& indices, const LimitOptions& o)
{
    const auto low = std::clamp (std::min (o.lowKey, o.highKey), 0, 127);
    const auto high = std::clamp (std::max (o.lowKey, o.highKey), 0, 127);
    const auto minV = std::clamp (std::min (o.minVelocity, o.maxVelocity), 0.0f, 1.0f);
    const auto maxV = std::clamp (std::max (o.minVelocity, o.maxVelocity), 0.0f, 1.0f);

    for (auto i : validSorted (pattern, indices))
    {
        auto& n = pattern.notes[(std::size_t) i];

        if (high - low >= 11) // an octave fits: keep the pitch class
        {
            while (n.key < low)
                n.key += 12;
            while (n.key > high)
                n.key -= 12;
        }

        n.key = std::clamp (n.key, low, high);
        n.velocity = std::clamp (n.velocity, minV, maxV);
    }
}
} // namespace lynx::notetools
