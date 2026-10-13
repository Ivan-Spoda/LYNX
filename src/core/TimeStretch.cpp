#include "core/TimeStretch.h"

#include <algorithm>
#include <cmath>

namespace lynx
{
std::shared_ptr<SampleData> timeStretch (const SampleData& in, double ratio)
{
    auto out = std::make_shared<SampleData>();
    out->sampleRate = in.sampleRate;
    const auto frames = (long) in.frames();
    const auto stereo = in.isStereo();

    if (frames < 4 || ! (ratio > 0.0))
    {
        out->left.assign (in.leftData(), in.leftData() + frames);
        if (stereo)
            out->right.assign (in.rightData(), in.rightData() + frames);
        return out;
    }

    ratio = std::clamp (ratio, 0.05, 20.0);

    // ~46 ms window, 50% overlap; ±12 ms search
    const auto window = (long) std::max (256.0, std::pow (2.0, std::round (std::log2 (in.sampleRate * 0.046))));
    const auto synthesisHop = window / 2;
    const auto analysisHop = (double) synthesisHop / ratio;
    const auto tolerance = (long) (in.sampleRate * 0.012);
    const auto outFrames = (long) std::llround ((double) frames * ratio);

    out->left.assign ((std::size_t) outFrames + (std::size_t) window, 0.0f);
    if (stereo)
        out->right.assign ((std::size_t) outFrames + (std::size_t) window, 0.0f);

    std::vector<float> hann ((std::size_t) window);
    for (long i = 0; i < window; ++i)
        hann[(std::size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * 3.141592653589793 * (double) i / (double) window);

    const auto* l = in.leftData();
    const auto* r = in.rightData();
    auto mono = [&] (long i) { return i < 0 || i >= frames ? 0.0f : (stereo ? 0.5f * (l[i] + r[i]) : l[i]); };

    long previous = 0; // where the previous window started in the input

    for (long k = 0; k * synthesisHop < outFrames; ++k)
    {
        const auto nominal = (long) std::llround ((double) k * analysisHop);
        auto best = nominal;

        if (k > 0)
        {
            // searches, near the nominal position, for the segment most similar to the natural
            // continuation of the previous window (previous + synthesisHop): avoids phase cancellation
            const auto natural = previous + synthesisHop;
            double bestScore = -1e300;

            for (long d = -tolerance; d <= tolerance; d += 2)
            {
                const auto candidate = std::clamp (nominal + d, 0L, std::max (0L, frames - 1));
                double score = 0.0;

                for (long i = 0; i < synthesisHop; i += 4)
                    score += (double) mono (natural + i) * (double) mono (candidate + i);

                if (score > bestScore)
                {
                    bestScore = score;
                    best = candidate;
                }
            }
        }

        previous = best;
        const auto outStart = k * synthesisHop;

        for (long i = 0; i < window; ++i)
        {
            const auto src = best + i;

            if (src >= frames)
                break;

            out->left[(std::size_t) (outStart + i)] += l[src] * hann[(std::size_t) i];
            if (stereo)
                out->right[(std::size_t) (outStart + i)] += r[src] * hann[(std::size_t) i];
        }
    }

    out->left.resize ((std::size_t) outFrames);
    if (stereo)
        out->right.resize ((std::size_t) outFrames);

    return out;
}
} // namespace lynx
