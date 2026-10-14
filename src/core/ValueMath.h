#pragma once

// The arithmetic of LYNX's control contract (docs/WORKFLOW_FL.md, P1 items 7 and 15): how a drag,
// a fine drag or a wheel notch changes a value, the tempo display's digit drag, and tap tempo.
// Header-only, no JUCE.

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "core/Song.h"

namespace lynx::controls
{
constexpr double fineFactor = 0.1;      // Ctrl+drag / Ctrl+wheel move 10x less
constexpr double wheelStep = 1.0 / 50;  // one wheel notch = 2% of the range

// Value after dragging `pixelsUp` pixels (positive = up/right) from `start`; `pixelsForRange` pixels
// sweep the whole range.
inline double dragValue (double start, double pixelsUp, double minimum, double maximum, double pixelsForRange, bool fine) noexcept
{
    const auto perPixel = (maximum - minimum) / std::max (1.0, pixelsForRange) * (fine ? fineFactor : 1.0);
    return std::clamp (start + pixelsUp * perPixel, minimum, maximum);
}

// Value after `notches` wheel notches (positive = up). `interval` > 0: at least one interval per notch.
inline double wheelValue (double value, double notches, double minimum, double maximum, bool fine, double interval = 0.0) noexcept
{
    auto delta = notches * (maximum - minimum) * wheelStep * (fine ? fineFactor : 1.0);

    if (interval > 0.0 && std::abs (delta) < interval && delta != 0.0)
        delta = delta < 0.0 ? -interval : interval;

    return std::clamp (value + delta, minimum, maximum);
}

inline double snapToInterval (double value, double minimum, double interval) noexcept
{
    return interval > 0.0 ? minimum + std::round ((value - minimum) / interval) * interval : value;
}

// --- tempo -------------------------------------------------------------------------------------

inline double clampTempo (double bpm) noexcept
{
    return std::clamp (bpm, minTempo, maxTempo);
}

// The tempo display is dragged by its digits: on the whole number one BPM per `pixelsPerStep` pixels,
// on the decimals 0.01 BPM per pixel (Ctrl: ten times finer on either).
inline double tempoDrag (double startBpm, double pixelsUp, bool onDecimals, bool fine) noexcept
{
    constexpr double pixelsPerStep = 4.0;
    double delta = onDecimals ? pixelsUp * 0.01 : std::trunc (pixelsUp / pixelsPerStep);

    if (fine)
        delta = onDecimals ? pixelsUp * 0.001 : pixelsUp / pixelsPerStep * 0.1;

    auto bpm = startBpm + delta;
    bpm = onDecimals || fine ? std::round (bpm * 1000.0) / 1000.0 : std::round (bpm * 100.0) / 100.0;
    return clampTempo (bpm);
}

inline double halfTempo (double bpm) noexcept { return clampTempo (bpm / 2.0); }
inline double doubleTempo (double bpm) noexcept { return clampTempo (bpm * 2.0); }

// Tap tempo: the tempo follows the average interval of the recent taps. A pause longer than
// `resetMs` starts over. A tempo is available from the second tap on.
class TapTempo
{
public:
    static constexpr double resetMs = 2000.0;
    static constexpr std::size_t maxTaps = 8;

    // `timeMs`: a monotonic clock. Returns the tempo once there are at least two taps.
    std::optional<double> tap (double timeMs)
    {
        if (! taps.empty() && (timeMs - taps.back() > resetMs || timeMs <= taps.back()))
            taps.clear();

        taps.push_back (timeMs);

        if (taps.size() > maxTaps)
            taps.erase (taps.begin());

        if (taps.size() < 2)
            return std::nullopt;

        const auto interval = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        return clampTempo (std::round (60000.0 / interval * 100.0) / 100.0);
    }

    int count() const noexcept { return (int) taps.size(); }
    void reset() { taps.clear(); }

private:
    std::vector<double> taps;
};
} // namespace lynx::controls
