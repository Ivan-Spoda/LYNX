#pragma once

#include <atomic>

#include "core/Song.h"

namespace lynx
{
// Estado do transporte compartilhado entre a UI e a thread de áudio.
// Só atomics: a thread de áudio nunca trava esperando a UI.
// A posição (em ticks, PPQ do core) é escrita apenas pela thread de áudio.
struct Transport
{
    std::atomic<bool> playing { false };
    std::atomic<bool> metronome { false };
    std::atomic<bool> recording { false };
    std::atomic<double> bpm { 130.0 };
    std::atomic<int> beatsPerBar { 4 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> positionTicks { 0.0 };
    std::atomic<bool> rewindRequested { false };
    std::atomic<double> seekRequest { -1.0 }; // < 0 = nenhum; a thread de áudio aplica no próximo bloco

    // Região de loop da Playlist (só no modo song). end <= start = desligada.
    std::atomic<bool> songMode { false };
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };

    double ticksPerSample() const noexcept
    {
        return bpm.load (std::memory_order_relaxed) * ppq / (60.0 * sampleRate.load (std::memory_order_relaxed));
    }

    double positionInBeats() const noexcept
    {
        return positionTicks.load (std::memory_order_relaxed) / ppq;
    }
};
} // namespace lynx
