#include "core/EventEdit.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace lynx::eventedit
{
namespace
{
void sortLane (PatternEvents& lane)
{
    std::stable_sort (lane.events.begin(), lane.events.end(), [] (const PatternEvent& a, const PatternEvent& b) { return a.tick < b.tick; });
}

float clamp01 (float v) noexcept
{
    return std::clamp (v, 0.0f, 1.0f);
}
} // namespace

PatternEvents* findLane (Pattern& pattern, const AutomationTarget& target) noexcept
{
    for (auto& l : pattern.events)
        if (l.target == target)
            return &l;
    return nullptr;
}

const PatternEvents* findLane (const Pattern& pattern, const AutomationTarget& target) noexcept
{
    for (const auto& l : pattern.events)
        if (l.target == target)
            return &l;
    return nullptr;
}

PatternEvents& laneFor (Pattern& pattern, const AutomationTarget& target)
{
    if (auto* l = findLane (pattern, target))
        return *l;

    pattern.events.push_back ({ target, {} });
    return pattern.events.back();
}

void removeEmptyLanes (Pattern& pattern)
{
    pattern.events.erase (std::remove_if (pattern.events.begin(), pattern.events.end(),
                                          [] (const PatternEvents& l) { return l.events.empty() || l.target.kind == AutomationTargetKind::none; }),
                          pattern.events.end());
}

void setEvent (PatternEvents& lane, std::int64_t tick, float value)
{
    tick = std::max<std::int64_t> (0, tick);
    auto it = std::lower_bound (lane.events.begin(), lane.events.end(), tick, [] (const PatternEvent& e, std::int64_t t) { return e.tick < t; });

    if (it != lane.events.end() && it->tick == tick)
        it->value = clamp01 (value);
    else
        lane.events.insert (it, { tick, clamp01 (value) });
}

void erase (PatternEvents& lane, std::int64_t from, std::int64_t to)
{
    lane.events.erase (std::remove_if (lane.events.begin(), lane.events.end(), [from, to] (const PatternEvent& e) { return e.tick >= from && e.tick < to; }),
                       lane.events.end());
}

void fill (PatternEvents& lane, std::int64_t from, std::int64_t to, float value, std::int64_t step)
{
    line (lane, from, to, value, value, step);
}

void line (PatternEvents& lane, std::int64_t from, std::int64_t to, float v0, float v1, std::int64_t step)
{
    step = std::max<std::int64_t> (1, step);
    from = std::max<std::int64_t> (0, from);

    if (to <= from)
        to = from + step;

    erase (lane, from, to);
    const auto span = (double) std::max<std::int64_t> (1, to - from - step);

    for (auto t = from; t < to; t += step)
    {
        const auto f = to - from > step ? (double) (t - from) / span : 0.0;
        lane.events.push_back ({ t, clamp01 ((float) (v0 + (v1 - v0) * std::min (1.0, f))) });
    }

    sortLane (lane);
}

void scale (PatternEvents& lane, std::int64_t from, std::int64_t to, float factor, float offset, bool bipolar)
{
    const auto centre = bipolar ? 0.5f : 0.0f;

    for (auto& e : lane.events)
        if (e.tick >= from && e.tick < to)
            e.value = clamp01 (centre + (e.value - centre) * factor + offset);
}

void randomize (PatternEvents& lane, std::int64_t from, std::int64_t to, float amount, std::uint32_t seed)
{
    std::mt19937 random (seed);
    std::uniform_real_distribution<float> spread (-1.0f, 1.0f);

    for (auto& e : lane.events)
        if (e.tick >= from && e.tick < to)
            e.value = clamp01 (e.value + spread (random) * amount);
}

void applyLfo (PatternEvents& lane, std::int64_t from, std::int64_t to, const prtools::LfoOptions& options, std::int64_t step)
{
    step = std::max<std::int64_t> (1, step);
    from = std::max<std::int64_t> (0, from);

    if (to <= from)
        return;

    const auto curve = prtools::lfoCurve (options, to - from);
    erase (lane, from, to);

    for (auto t = from; t < to; t += step)
        lane.events.push_back ({ t, curve[(std::size_t) (t - from)] });

    sortLane (lane);
}
} // namespace lynx::eventedit
