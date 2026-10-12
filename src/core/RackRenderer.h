#pragma once

// Junta Song + instrumentos + scheduler: dado o estado do transporte, gera o
// áudio estéreo de um bloco com as notas disparadas no sample exato.

#include <memory>
#include <vector>

#include "core/Instruments.h"
#include "core/Scheduler.h"
#include "core/Song.h"

namespace daw
{
// Snapshot imutável publicado pela UI. Os instrumentos são compartilhados entre
// snapshots consecutivos para as vozes não serem cortadas quando a música é editada.
struct RackState
{
    Song song;
    std::vector<std::shared_ptr<Instrument>> instruments; // um por canal

    // Monta o próximo estado reaproveitando os instrumentos de `previous` quando o
    // tipo do canal não mudou. Roda na thread da UI (aloca).
    static std::unique_ptr<const RackState> build (Song song, const RackState* previous, double sampleRate);
};

class RackRenderer
{
public:
    void prepare (double sampleRate) noexcept;

    // Thread de áudio. left/right são sobrescritos.
    void process (const RackState* state,
                  bool playing,
                  double startTick,
                  int numSamples,
                  float* left,
                  float* right) noexcept;

    // Eventos do último bloco (para testes e diagnóstico).
    const EventBuffer& lastEvents() const noexcept { return events; }

private:
    void renderSegment (const RackState& state, float* left, float* right, int n) noexcept;

    double sampleRate = 44100.0;
    bool wasPlaying = false;
    const RackState* lastState = nullptr;
    EventBuffer events;
};
} // namespace daw
