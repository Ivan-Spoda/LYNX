#include "core/Slicer.h"

#include <algorithm>
#include <cmath>

namespace lynx
{
std::vector<float> detectSlices (const SampleData& sample, float sensitivity, int maxSlices)
{
    const auto frames = (long) sample.frames();
    std::vector<float> slices { 0.0f };

    if (frames < 2048)
        return slices;

    // energy in ~5 ms windows; attack = strong rise relative to the recent average
    const auto hop = std::max (64L, (long) (sample.sampleRate * 0.005));
    const auto* l = sample.leftData();
    const auto* r = sample.rightData();
    std::vector<float> energy;

    for (long start = 0; start + hop <= frames; start += hop)
    {
        double e = 0.0;
        for (long i = start; i < start + hop; ++i)
        {
            const auto m = 0.5f * (l[i] + r[i]);
            e += (double) m * m;
        }
        energy.push_back ((float) std::sqrt (e / (double) hop));
    }

    const auto peak = *std::max_element (energy.begin(), energy.end());

    if (peak <= 1e-6f)
        return slices;

    const auto ratio = 1.6f + (1.0f - std::clamp (sensitivity, 0.0f, 1.0f)) * 3.0f; // how much larger than the average
    const auto floor = peak * (0.04f + (1.0f - sensitivity) * 0.1f);                  // ignores low-level noise
    const auto minGap = (long) std::ceil (0.06 * sample.sampleRate / (double) hop);   // 60 ms between slices
    long last = 0;

    for (long i = 4; i < (long) energy.size(); ++i)
    {
        float average = 0.0f;
        for (long k = i - 4; k < i; ++k)
            average += energy[(std::size_t) k];
        average /= 4.0f;

        if (energy[(std::size_t) i] > floor && energy[(std::size_t) i] > average * ratio && i - last >= minGap)
        {
            slices.push_back ((float) ((double) (i * hop) / (double) frames));
            last = i;

            if ((int) slices.size() >= maxSlices)
                break;
        }
    }

    return slices;
}
} // namespace lynx
