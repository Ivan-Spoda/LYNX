#pragma once

// FL's wrapper option "Use fixed size buffers" (REA_rodada118 Part B): the plugin is always called with the same
// block length, whatever the host's chunk sizes are (LYNX splits its blocks at every event, so without this a plugin
// sees variable, sometimes tiny, blocks). Input is collected until a whole block is there; the processed block is
// played back while the next one fills, so the output is the input delayed by exactly `block` samples — reported to
// PDC as extra latency. JUCE-free; real-time safe after construction (no allocation in `run`).

#include <algorithm>
#include <cstddef>
#include <vector>

namespace lynx
{
class FixedBlock
{
public:
    FixedBlock (int blockSize, int channels)
        : block (std::max (1, blockSize)), numChannels (std::max (1, channels)),
          input ((std::size_t) (block * numChannels), 0.0f), output ((std::size_t) (block * numChannels), 0.0f)
    {
        for (int c = 0; c < numChannels; ++c)
            pointers.push_back (input.data() + (std::size_t) (c * block));
    }

    int blockSize() const noexcept { return block; }
    int latency() const noexcept { return block; }
    // where the next input sample goes inside the block being collected (MIDI offsets of events arriving now)
    int position() const noexcept { return pos; }

    // `io[c]` (c < channels given at construction, `n` samples each) is replaced with the delayed output. `hasInput`
    // false: blocks start silent (instruments). `process (float* const* channels, int block)` works in place on a
    // whole block.
    template <typename Process>
    void run (float* const* io, int n, bool hasInput, Process&& process) noexcept
    {
        for (int done = 0; done < n;)
        {
            const auto count = std::min (n - done, block - pos);

            for (int c = 0; c < numChannels; ++c)
            {
                auto* in = input.data() + (std::size_t) (c * block + pos);
                auto* out = output.data() + (std::size_t) (c * block + pos);
                auto* x = io[c] + done;

                for (int i = 0; i < count; ++i)
                {
                    const auto v = x[i];
                    x[i] = out[i];
                    in[i] = hasInput ? v : 0.0f;
                }
            }

            pos += count;
            done += count;

            if (pos == block)
            {
                process (pointers.data(), block);
                std::copy (input.begin(), input.end(), output.begin());
                pos = 0;
            }
        }
    }

    void clear() noexcept
    {
        std::fill (input.begin(), input.end(), 0.0f);
        std::fill (output.begin(), output.end(), 0.0f);
        pos = 0;
    }

private:
    int block, numChannels, pos = 0;
    std::vector<float> input, output;
    std::vector<float*> pointers;
};
} // namespace lynx
