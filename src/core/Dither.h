#pragma once

// Dither for exporting at lower bit depths: ±1 LSB TPDF noise with 2nd-order noise shaping
// (pushes quantization noise into the highs), like FL's 16-bit export
// (docs/analise/REA_rodada2.md §5). Quantizes in place; the writer just repeats the same math.
//
// Round 160: JUCE's writers turn floats into int32 with (2^31 − 1)·x, rounded, and then drop the low bits (truncation).
// For a value already on the k/2^(bits−1) grid that lands just below k·2^(32−bits) once |k| passes half scale, and the
// truncation then writes k − 1: every positive sample above −6 dBFS came out 1 LSB low (an asymmetric error the dither
// can't hide). The output therefore sits a quarter LSB above the grid point; truncation then gives exactly k for any
// k (writerBias). The noise-shaping error uses the grid value itself.

#include <cmath>
#include <cstdint>

namespace lynx
{
class Dither
{
public:
    explicit Dither (int bits, bool noiseShaping = true) : scale ((float) (1 << (bits - 1))), shaping (noiseShaping), bias (0.25f / scale) {}

    // what process() adds to each quantized value so JUCE's float → int conversion keeps it (see above)
    float writerBias() const noexcept { return bias; }

    void process (float* samples, int n, int channel) noexcept
    {
        auto& e1 = error1[channel & 1];
        auto& e2 = error2[channel & 1];

        for (int i = 0; i < n; ++i)
        {
            const auto shaped = shaping ? samples[i] - (2.0f * e1 - e2) : samples[i];
            const auto tpdf = (random() - random()) / scale; // triangular, ±1 LSB
            auto q = std::round ((shaped + tpdf) * scale) / scale;
            q = q > 1.0f - 1.0f / scale ? 1.0f - 1.0f / scale : (q < -1.0f ? -1.0f : q);
            e2 = e1;
            e1 = q - shaped;
            samples[i] = q + bias;
        }
    }

private:
    float random() noexcept // 0..1, xorshift (deterministic)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state >> 8) * (1.0f / 16777216.0f);
    }

    float scale;
    bool shaping = true;
    float bias = 0.0f;
    float error1[2] {}, error2[2] {};
    std::uint32_t state = 0x9E3779B9u;
};
} // namespace lynx
