#pragma once

// Arpejador do canal (como o do Channel settings do FL): pega as notas que estão sendo
// seguradas no canal e toca uma por vez, na grade do transporte. Sem alocação: estado
// e buffers de tamanho fixo, criados fora da thread de áudio.

#include <array>

#include "core/Scheduler.h"
#include "core/Song.h"

namespace lynx
{
class Arpeggiator
{
public:
    static constexpr int maxHeld = 32;
    static constexpr int maxEvents = 512;

    // Processa um trecho: `in` são os eventos do canal (on/off) com offsets no trecho; a saída
    // (on/off do arpejo, mais tudo que não é on/off) vai para `out`, em ordem.
    void process (int channel, const NoteEvent* in, int inCount, double startTick, double tps, int numSamples,
                  const ArpSettings& settings, NoteEvent* out, int& outCount) noexcept;

    void reset() noexcept;

    std::array<NoteEvent, maxEvents> input {}, output {};

private:
    void hold (int key, float velocity) noexcept;
    void release (int key) noexcept;
    int nextIndex (const ArpSettings& settings) noexcept; // na sequência de heldCount * oitavas

    std::array<int, maxHeld> heldKeys {};
    std::array<float, maxHeld> heldVelocity {};
    int heldCount = 0;
    int step = 0;          // posição na sequência
    int repeatCount = 0;
    int sounding = -1;     // tecla tocando agora (-1 = nenhuma)
    double offTick = -1.0; // quando a tecla tocando solta (gate)
    std::uint32_t random = 0x2545F491u;
};
} // namespace lynx
