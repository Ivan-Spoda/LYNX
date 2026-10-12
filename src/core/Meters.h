#pragma once

#include <array>
#include <atomic>

#include "core/Song.h"

namespace lynx
{
// Picos por trilha do mixer, escritos pela thread de áudio e lidos pela UI.
// O áudio acumula o máximo; a UI lê e zera (takePeak), então nenhum pico se perde
// entre dois quadros da tela.
class MeterBank
{
public:
    void accumulate (int track, float left, float right) noexcept
    {
        if (track < 0 || track >= maxMixerTracks)
            return;

        raise (peaks[(std::size_t) track * 2], left);
        raise (peaks[(std::size_t) track * 2 + 1], right);
    }

    // UI: pico desde a última leitura. channel 0 = esquerda, 1 = direita.
    float takePeak (int track, int channel) noexcept
    {
        if (track < 0 || track >= maxMixerTracks)
            return 0.0f;

        return peaks[(std::size_t) track * 2 + (std::size_t) channel].exchange (0.0f, std::memory_order_relaxed);
    }

private:
    static void raise (std::atomic<float>& slot, float value) noexcept
    {
        auto current = slot.load (std::memory_order_relaxed);

        while (value > current && ! slot.compare_exchange_weak (current, value, std::memory_order_relaxed))
        {
        }
    }

    std::array<std::atomic<float>, maxMixerTracks * 2> peaks {};
};
} // namespace lynx
