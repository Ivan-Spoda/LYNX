#pragma once

// Junta Song + instrumentos + scheduler: dado o estado do transporte, gera o
// áudio estéreo de um bloco com as notas disparadas no sample exato.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"
#include "core/LiveEventQueue.h"
#include "core/Meters.h"
#include "core/Scheduler.h"
#include "core/Song.h"

namespace lynx
{
// Snapshot imutável publicado pela UI. Os instrumentos são compartilhados entre
// snapshots consecutivos para as vozes não serem cortadas quando a música é editada.
struct RackState
{
    Song song;
    std::vector<std::shared_ptr<Instrument>> instruments; // um por canal
    double sampleRate = 44100.0; // taxa com que os instrumentos novos foram preparados

    // Carrega (ou pega do cache) o áudio de um arquivo; nullptr se não conseguir.
    using SampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path)>;

    // Monta o próximo estado reaproveitando os instrumentos de `previous` quando o
    // canal não mudou de instrumento nem de sample. Roda na thread da UI (aloca).
    static std::unique_ptr<const RackState> build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const SampleProvider& samples = {});
};

class RackRenderer
{
public:
    // Aloca os buffers do mixer: chamar fora da thread de áudio.
    // Blocos maiores que maxBlockSize são processados em pedaços.
    void prepare (double sampleRate, int maxBlockSize = 2048);

    // Thread de áudio. left/right são sobrescritos. `live` (opcional) traz notas
    // tocadas ao vivo, aplicadas no início do bloco.
    void process (const RackState* state,
                  bool playing,
                  double startTick,
                  int numSamples,
                  float* left,
                  float* right,
                  LiveEventQueue* live = nullptr,
                  MeterBank* meters = nullptr) noexcept;

    // Eventos do último bloco (para testes e diagnóstico).
    const EventBuffer& lastEvents() const noexcept { return events; }

private:
    void processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                       float* left, float* right, MeterBank* meters) noexcept;
    void renderSegment (const RackState& state, int offset, int n) noexcept;
    float* trackBuffer (int track, int channel) noexcept;

    double sampleRate = 44100.0;
    bool wasPlaying = false;
    double expectedTick = -1.0; // onde o próximo bloco deveria começar, para detectar saltos
    const RackState* lastState = nullptr;
    EventBuffer events;
    std::vector<float> mixBuffers; // maxMixerTracks * 2 * blockCapacity
    int blockCapacity = 0;
};
} // namespace lynx
