#pragma once

// Plugin delay compensation (PDC), in the "per-edge" model FL uses
// (docs/analise/REA_rodada2.md §1): in routing order,
//   inAlign[T] = max(latency of the instruments feeding T, outLat[S] of each S -> T)
//   outLat[T]  = inAlign[T] + sum of the latencies of T's enabled effects
// and each edge (instrument -> track, track -> track) delays by what's missing to reach
// the destination's inAlign. The longest path is never delayed.

#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct PdcPlan
{
    std::vector<int> channelDelay;             // per channel, in samples
    std::vector<std::vector<int>> sendDelay;   // [track][k]: k = 0 Master, k = 1 + i for sends[i]
    std::vector<int> inAlign, outLat;          // per track
    int total = 0;                              // latency at the Master output

    bool any() const noexcept;
};

// `channelLatency[c]` = latency of the channel's instrument; `slotLatency[t][s]` = of the slot's
// effect (0 if disabled). Vectors smaller than the Song count as 0.
PdcPlan computePdc (const Song& song, const std::vector<int>& channelLatency, const std::vector<std::vector<int>>& slotLatency);

// Stereo delay line with an integer delay, allocated on creation. process() does not allocate.
class DelayLine
{
public:
    explicit DelayLine (int delaySamples);

    int length() const noexcept { return delay; }
    void process (float* left, float* right, int n) noexcept; // in place

    // Delays `source * level` and adds it into `dest`, in a single pass. With a null source (silent
    // source) pushes silence; when the line is already all silence, does nothing.
    void mixInto (const float* sourceLeft, const float* sourceRight, float level, float* destLeft, float* destRight, int n) noexcept;

private:
    int delay;
    std::vector<float> left, right; // planar ring
    int position = 0;
    int quiet = 0; // consecutive silent samples already written (up to delay)
};
} // namespace lynx
