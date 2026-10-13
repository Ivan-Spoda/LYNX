#pragma once

// Junta Song + instrumentos + scheduler: dado o estado do transporte, gera o
// áudio estéreo de um bloco com as notas disparadas no sample exato.

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Arpeggiator.h"
#include "core/Biquad.h"
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
    ScheduleIndex schedule; // índice para agendar a Playlist sem varrer todas as notas
    int tempoAutomation = -1; // automação do andamento, se houver

    // PDC: atrasos por aresta (nullptr = sem atraso) e as latências usadas para montar o plano.
    PdcPlan pdc;
    std::vector<std::shared_ptr<DelayLine>> channelDelays;
    std::vector<std::vector<std::shared_ptr<DelayLine>>> sendDelays; // [trilha][k] como PdcPlan::sendDelay
    std::vector<int> latencies; // canais, depois slots de todas as trilhas

    // Latências atuais dos instrumentos e efeitos, no mesmo formato de `latencies`.
    std::vector<int> currentLatencies() const;
    void buildPdc (const RackState* previous);

    // Arpejadores dos canais com arpejo ligado (nullptr nos outros); reaproveitados entre snapshots.
    std::vector<std::shared_ptr<Arpeggiator>> arpeggiators;

    // Cut groups: canais que cada canal corta ao tocar (cutBy == cutGroup dele).
    std::vector<std::vector<int>> cutTargets;
    bool hasCutGroups = false;

    // Mixer: ordem de processamento dos inserts e quem soa com o solo atual.
    std::vector<int> mixerOrder;
    std::vector<char> mixerAudible;

    // Modelo "puxar": cada trilha soma as saídas (pós-fader) das que enviam para ela.
    struct MixerInput
    {
        int source = 0;
        int delay = 0; // índice em sendDelays[source]
        float level = 1.0f;
    };
    std::vector<std::vector<MixerInput>> mixerInputs; // [trilha]; [0] = Master
    std::vector<std::vector<int>> mixerLayers;        // inserts sem dependência entre si, em ordem
    bool parallel = false;                            // vale a pena dividir entre threads
    void buildMixerGraph();

    void buildAutomationTables();

    // Carrega (ou pega do cache) o áudio de um arquivo; nullptr se não conseguir.
    using SampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path)>;

    // O app cria instrumentos/efeitos de plugin (o núcleo não conhece JUCE). nullptr = não deu
    // (plugin ausente): o canal fica mudo / o slot vira bypass.
    using PluginInstrumentFactory = std::function<std::shared_ptr<Instrument> (const PluginRef&, double sampleRate)>;
    using PluginEffectFactory = std::function<std::shared_ptr<Effect> (const PluginRef&, double sampleRate)>;

    // Sample esticado sem mudar a altura (razão = duração nova / original); com cache no app.
    using StretchedSampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path, double ratio)>;

    struct Factories
    {
        SampleProvider samples;
        StretchedSampleProvider stretchedSamples;
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

class WorkerPool;

class RackRenderer
{
public:
    // Threads extras para canais e camadas do mixer (nullptr = tudo na thread de áudio).
    void setWorkerPool (WorkerPool* pool) noexcept { workers = pool; }

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

    static void applyStereoForTest (const MixerTrack& t, float* l, float* r, int n) noexcept { applyStereo (t, l, r, n); }

    // Andamento usado no último bloco (muda com automação de tempo). O transporte avança com ele.
    double currentBpm() const noexcept { return bpm; }

    // Eventos do último bloco (para testes e diagnóstico).
    const EventBuffer& lastEvents() const noexcept { return events; }

private:
    void processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                       float* left, float* right, MeterBank* meters) noexcept;
    void renderChannel (const RackState& state, int channel, int n) noexcept;
    void processTrack (const RackState& state, int track, int n, MeterBank* meters) noexcept;
    void pullInputs (const RackState& state, int track, int n) noexcept;
    static void applyStereo (const MixerTrack& track, float* l, float* r, int n) noexcept;
    float* channelBuffer (int channel, int side) noexcept;
    float* trackScratch (int track, int side) noexcept;
    void runEffects (const RackState& state, int track, int n) noexcept;
    void updateAutomation (const RackState& state, double startTick) noexcept;
    float automated (int automation, float base) const noexcept;
    float* trackBuffer (int track, int channel) noexcept;

    double sampleRate = 44100.0;
    double bpm = 120.0;
    bool wasPlaying = false;
    double expectedTick = -1.0; // onde o próximo bloco deveria começar, para detectar saltos
    bool resumeAudio = false;
    const RackState* lastState = nullptr;
    EventBuffer events;
    std::vector<float> mixBuffers; // maxMixerTracks * 2 * blockCapacity
    std::array<float, RackState::maxAutomations> automationValues {}; // NaN = ainda não avaliada
    std::array<float, maxMixerTracks * 2> lastTrackGain {}; // ganho L/R do bloco anterior, para a rampa
    std::array<char, maxMixerTracks> trackActive {};        // a saída da trilha tem som neste bloco
    static constexpr int maxChannels = 1024;
    static constexpr int chunkCapacity = 512;               // trechos processados de uma vez
    std::vector<float> channelBuffers;                      // maxChannels * 2 * chunkCapacity
    std::vector<float> trackScratchBuffers;                 // maxMixerTracks * 2 * chunkCapacity
    double chunkStartTick = 0.0, chunkTps = 1.0;            // do trecho sendo renderizado (para o arpejador)
    WorkerPool* workers = nullptr;

    struct SmartDisableState
    {
        const Effect* effect = nullptr;
        int silentSamples = 0;
    };
    std::vector<SmartDisableState> smartDisable; // [trilha * maxEffectSlots + slot]

    struct TrackEqState
    {
        TrackEq settings;
        std::array<Biquad, 3> bands;
        bool running = false;
    };
    std::vector<TrackEqState> trackEqs; // EQ embutido de cada trilha
    double chunkTick = 0.0;             // começo do bloco (para os LFOs globais)
    void applyTrackEq (const MixerTrack& track, int t, float* l, float* r, int n) noexcept;
    static constexpr float silenceLevel = 1.5849e-5f;  // -96 dBFS
    int blockCapacity = 0;
};
} // namespace lynx
