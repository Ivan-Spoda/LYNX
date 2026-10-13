#pragma once

// Junta Song + instrumentos + scheduler: dado o estado do transporte, gera o
// áudio estéreo de um bloco com as notas disparadas no sample exato.

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Effects.h"
#include "core/Instruments.h"
#include "core/LiveEventQueue.h"
#include "core/Meters.h"
#include "core/Pdc.h"
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
    std::vector<std::vector<std::shared_ptr<Effect>>> effects; // [trilha do mixer][slot]
    double sampleRate = 44100.0; // taxa com que os instrumentos novos foram preparados

    // Automação, pré-processada para a thread de áudio só consultar tabelas.
    // Índices em song.automations, ou -1 = sem automação.
    static constexpr int maxAutomations = 1024;
    std::vector<std::vector<int>> automationClips; // por automação: clips na Playlist
    std::vector<int> channelVolumeAuto, channelPanAuto;
    std::vector<int> mixerVolumeAuto, mixerPanAuto;
    std::vector<std::vector<std::array<int, maxEffectParams>>> effectParamAuto; // [trilha][slot][param] (internos)

    struct PluginParamBinding
    {
        int automation = 0;
        int channel = -1;       // instrumento do canal, ou
        int track = 0, slot = 0; // efeito no slot
        int param = 0;
    };
    std::vector<PluginParamBinding> pluginParams;
    bool hasAutomation = false;

    // PDC: atrasos por aresta (nullptr = sem atraso) e as latências usadas para montar o plano.
    PdcPlan pdc;
    std::vector<std::shared_ptr<DelayLine>> channelDelays;
    std::vector<std::vector<std::shared_ptr<DelayLine>>> sendDelays; // [trilha][k] como PdcPlan::sendDelay
    std::vector<int> latencies; // canais, depois slots de todas as trilhas

    // Latências atuais dos instrumentos e efeitos, no mesmo formato de `latencies`.
    std::vector<int> currentLatencies() const;
    void buildPdc (const RackState* previous);

    // Mixer: ordem de processamento dos inserts e quem soa com o solo atual.
    std::vector<int> mixerOrder;
    std::vector<char> mixerAudible;

    void buildAutomationTables();

    // Carrega (ou pega do cache) o áudio de um arquivo; nullptr se não conseguir.
    using SampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path)>;

    // O app cria instrumentos/efeitos de plugin (o núcleo não conhece JUCE). nullptr = não deu
    // (plugin ausente): o canal fica mudo / o slot vira bypass.
    using PluginInstrumentFactory = std::function<std::shared_ptr<Instrument> (const PluginRef&, double sampleRate)>;
    using PluginEffectFactory = std::function<std::shared_ptr<Effect> (const PluginRef&, double sampleRate)>;

    struct Factories
    {
        SampleProvider samples;
        PluginInstrumentFactory pluginInstruments;
        PluginEffectFactory pluginEffects;
    };

    // Monta o próximo estado reaproveitando os instrumentos de `previous` quando o
    // canal não mudou de instrumento nem de sample, e os efeitos quando o slot tem o
    // mesmo tipo. Roda na thread da UI (aloca).
    static std::unique_ptr<const RackState> build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const SampleProvider& samples = {});
    static std::unique_ptr<const RackState> build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const Factories& factories);
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
                  MeterBank* meters = nullptr,
                  LiveEventQueue* moreLive = nullptr) noexcept;

    // Eventos do último bloco (para testes e diagnóstico).
    const EventBuffer& lastEvents() const noexcept { return events; }

private:
    void processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                       float* left, float* right, MeterBank* meters) noexcept;
    void renderSegment (const RackState& state, int offset, int n) noexcept;
    void runEffects (const RackState& state, int track, int n) noexcept;
    void updateAutomation (const RackState& state, double startTick) noexcept;
    float automated (int automation, float base) const noexcept;
    float* trackBuffer (int track, int channel) noexcept;

    double sampleRate = 44100.0;
    bool wasPlaying = false;
    double expectedTick = -1.0; // onde o próximo bloco deveria começar, para detectar saltos
    bool resumeAudio = false;
    const RackState* lastState = nullptr;
    EventBuffer events;
    std::vector<float> mixBuffers; // maxMixerTracks * 2 * blockCapacity
    std::array<float, RackState::maxAutomations> automationValues {}; // NaN = ainda não avaliada
    std::vector<float> scratch; // 2 * blockCapacity, para atrasar um canal ou um envio
    std::array<float, maxMixerTracks * 2> lastTrackGain {}; // ganho L/R do bloco anterior, para a rampa
    int blockCapacity = 0;
};
} // namespace lynx
