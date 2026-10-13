#pragma once

// Dither para exportar em menos bits: ruído TPDF de ±1 LSB com noise shaping de 2ª ordem
// (empurra o ruído de quantização para os agudos), como o export em 16 bits do FL
// (docs/analise/REA_rodada2.md §5). Quantiza no lugar; o writer só repete a mesma conta.

#include <cmath>
#include <cstdint>

namespace lynx
{
class Dither
{
public:
    explicit Dither (int bits) : scale ((float) (1 << (bits - 1))) {}

    void process (float* samples, int n, int channel) noexcept
    {
        auto& e1 = error1[channel & 1];
        auto& e2 = error2[channel & 1];

        for (int i = 0; i < n; ++i)
        {
            const auto shaped = samples[i] - (2.0f * e1 - e2);
            const auto tpdf = (random() - random()) / scale; // triangular, ±1 LSB
            auto q = std::round ((shaped + tpdf) * scale) / scale;
            q = q > 1.0f - 1.0f / scale ? 1.0f - 1.0f / scale : (q < -1.0f ? -1.0f : q);
            e2 = e1;
            e1 = q - shaped;
            samples[i] = q;
        }
    }

private:
    float random() noexcept // 0..1, xorshift (determinístico)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state >> 8) * (1.0f / 16777216.0f);
    }

    float scale;
    float error1[2] {}, error2[2] {};
    std::uint32_t state = 0x9E3779B9u;
};
} // namespace lynx
