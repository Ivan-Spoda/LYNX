#pragma once

#include <algorithm>

// Display ballistics of a peak meter (mixer strips): a fast-rising bar with a smooth falloff, a
// peak-hold line that stays put for a while before it falls, and a clip latch that stays lit until
// the user clicks the meter. Pure logic, ticked by the UI timer; independent of JUCE.
namespace lynx::meters
{
struct PeakHold
{
    float level = 0.0f;  // the bar (linear gain)
    float peak = 0.0f;   // the hold line (linear gain)
    int holdLeft = 0;    // ticks before the hold line starts to fall
    bool clipped = false;

    // One display tick with the loudest sample since the previous tick (linear gain).
    // decay: bar falloff per tick; holdTicks: how long the hold line stays; peakFall: its falloff per tick.
    void tick (float blockPeak, float decay = 0.86f, int holdTicks = 45, float peakFall = 0.92f) noexcept
    {
        blockPeak = std::max (0.0f, blockPeak);
        level = std::max (blockPeak, level * decay);
        if (level < 0.001f)
            level = 0.0f;

        if (blockPeak >= peak)
        {
            peak = blockPeak;
            holdLeft = holdTicks;
        }
        else if (holdLeft > 0)
            --holdLeft;
        else
        {
            peak = std::max (level, peak * peakFall);
            if (peak < 0.001f)
                peak = 0.0f;
        }

        if (blockPeak > 1.0f)
            clipped = true;
    }

    void resetClip() noexcept
    {
        clipped = false;
        peak = level;
        holdLeft = 0;
    }
};
} // namespace lynx::meters
