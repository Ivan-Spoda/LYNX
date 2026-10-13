#pragma once

#include <deque>
#include <functional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "core/LiveEventQueue.h"
#include "core/Recorder.h"
#include "core/WorkerPool.h"
#include "core/RackRenderer.h"
#include "core/FlpImport.h"
#include "core/SnapshotExchange.h"
#include "core/Song.h"
#include "engine/PluginHost.h"
#include "engine/SampleLibrary.h"
#include "engine/StreamPrefetcher.h"
#include "engine/Transport.h"

namespace lynx
{
// Dono do dispositivo de áudio, do grafo de processamento e do documento (Song).
// Fica entre o driver e o AudioProcessorPlayer para avançar o transporte
// depois de cada bloco, assim todos os nós veem a mesma posição de início.
//
// O Song só é tocado pela thread da UI; cada edição publica um snapshot
// imutável (RackState) para a thread de áudio.
// Pastas da instalação mais recente do FL Studio (vazias se não houver).
FlPathRoots findFlStudioRoots();

class AudioEngine final : private juce::AudioIODeviceCallback,
                          private juce::MidiInputCallback,
                          public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    explicit AudioEngine (bool openAudioDevice = true);
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    Transport& getTransport() noexcept { return transport; }
    SampleLibrary& getSampleLibrary() noexcept { return samples; }
    MeterBank& getMeters() noexcept { return meters; }
    PluginHost& getPluginHost() noexcept { return plugins; }

    // Plugins de terceiros do último .flp importado que não foram achados entre os escaneados.
    const juce::StringArray& getMissingPlugins() const noexcept { return missingPlugins; }
    // O que o último .flp importado converteu, aproximou ou deixou de fora.
    const FlpImportReport& getImportReport() const noexcept { return importReport; }

    // Instâncias vivas (para abrir a janela do plugin). nullptr se não for plugin.
    std::shared_ptr<Instrument> getChannelInstrument (int channel) const;
    std::shared_ptr<Effect> getEffect (int track, int slot) const;

    // Copia o estado atual dos plugins para o Song (antes de salvar).
    void capturePluginStates();

    void play() noexcept;
    void stop() noexcept;
    void rewind() noexcept;
    void seek (double ticks) noexcept;

    // Prévia de um arquivo de áudio (navegador). Arquivo inválido/vazio para a prévia.
    void previewFile (const juce::File& file);

    // Nota tocada ao vivo (teclado do PC, preview). Thread da UI.
    // `record` = entra na gravação se ela estiver ligada (o preview do Piano Roll não entra).
    void sendLiveNote (int channel, int key, float velocity, bool on, bool record = false);

    // Gravação de notas (MIDI e teclado do PC) na pattern atual enquanto toca. Thread da UI.
    void setRecording (bool shouldRecord);
    bool isRecording() const noexcept { return transport.recording.load(); }

    // Gravação de áudio da entrada da placa: com isto ligado, gravar (Rec + Play) escreve a
    // entrada num WAV e, ao parar, o take vira um clip na Playlist (compensado pela latência).
    // Ligar abre a entrada do dispositivo (só então o Windows mostra o microfone em uso).
    void setAudioRecordingArmed (bool armed);
    bool isAudioRecordingArmed() const noexcept { return audioArmed; }
    juce::String getLastRecordingError() const { return recordingError; }

    // MIDI learn: o próximo CC recebido vira um controlador ligado a `target`.
    void learnMidiController (const AutomationTarget& target);
    bool isLearningMidi() const noexcept { return learning.kind != AutomationTargetKind::none; }

    // Entradas MIDI: todas as disponíveis ficam ligadas e tocam o canal selecionado.
    juce::StringArray getActiveMidiInputs() const;

    // Documento (thread da UI)
    const Song& getSong() const noexcept { return song; }
    void setSong (Song newSong); // projeto novo/aberto: zera o histórico

    // Edita a música e publica para o áudio. Edições `undoable` entram no histórico;
    // todas as edições feitas enquanto o mesmo clique do mouse está pressionado
    // (arrastar uma nota, girar um knob) viram um passo só.
    void editSong (const std::function<void (Song&)>& edit, bool undoable = true);

    // Alterações desde o último salvar/abrir (para o "*" no título e o "salvar alterações?").
    bool isModified() const noexcept { return modified; }
    void markSaved() noexcept { modified = false; sendChangeMessage(); }

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    // Canal selecionado no Channel Rack (alvo do Piano Roll e do teclado). Thread da UI.
    int getSelectedChannel() const noexcept { return selectedChannel; }
    void setSelectedChannel (int channel);

    // Importa um .flp localizando a instalação do FL para achar os samples.
    // Retorna false e preenche `error` se não conseguir.
    bool importFlpFile (const juce::File& file, juce::String& error);

    // Renderiza sem dispositivo de áudio. Grava em `output` se não for nullptr.
    // Depois de `seconds`, continua por até `tailSeconds` enquanto ainda houver som.
    // `progress(0..1)` retorna false para cancelar. Retorna o pico (usado pelo autoteste),
    // ou -1 se cancelado.
    struct RenderOptions
    {
        double seconds = 10.0;
        double sampleRate = 44100.0;
        juce::AudioFormatWriter* output = nullptr;
        std::function<bool (double)> progress; // 0..1; false cancela
        double tailSeconds = 0.0;
        int trimSamples = 0;  // PDC: descarta esse tanto do começo e renderiza o mesmo tanto a mais no fim
        int ditherBits = 0;   // > 0: dither TPDF com noise shaping para essa resolução
    };

    float renderOffline (const RenderOptions& options);

    // tempos do último renderOffline (diagnóstico): o grafo de áudio e a gravação do arquivo
    struct RenderStats
    {
        double graphMs = 0.0, writeMs = 0.0;
    };
    RenderStats getLastRenderStats() const noexcept { return lastRenderStats; }
    float renderOffline (double seconds, double sampleRate, juce::AudioFormatWriter* output = nullptr)
    {
        RenderOptions o;
        o.seconds = seconds;
        o.sampleRate = sampleRate;
        o.output = output;
        return renderOffline (o);
    }

    int getNumGraphConnections() const { return (int) graph.getConnections().size(); }

    // Últimos samples da saída (mono), para o osciloscópio. Thread da UI.
    void copyScope (float* destination, int count) const noexcept;
    static constexpr int scopeSize = 2048;

    // Latência total compensada pelo PDC (na saída do Master), em samples.
    int getLatencySamples() const noexcept { return lastPublishedShape.pdc.total; }

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void enableAllMidiInputs();
    void updateRecording();
    void updateAudioRecording();
    void startAudioTake();
    void finishAudioTake();
    double estimatedPositionNow() const noexcept;

    void buildInitialGraph();
    class PreviewPlayer* preview = nullptr; // nó do grafo (o grafo é o dono)
    void publishSong();

    std::unique_ptr<WorkerPool> workers; // threads extras do renderer (antes do grafo, que as usa)
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    Transport transport;
    SampleLibrary samples;
    LiveEventQueue liveEvents;   // UI -> áudio
    LiveEventQueue midiEvents;   // thread MIDI -> áudio
    SpscQueue<RecordedEvent, 1024> midiRecordQueue; // thread MIDI -> UI (gravação)

    struct ControllerEvent
    {
        int cc = 0, channel = 0, value = 0;
    };
    SpscQueue<ControllerEvent, 1024> controllerQueue; // thread MIDI -> UI (controladores)
    AutomationTarget learning;
    void applyControllers();
    std::vector<RecordedEvent> uiRecorded;           // teclado do PC -> gravação
    NoteRecorder recorder;
    bool takeHasUndoStep = false;
    double lastPlayingTick = 0.0;
    std::atomic<int> liveChannel { 0 };              // canal selecionado, lido pela thread MIDI
    std::atomic<double> blockTimeMs { 0.0 };         // quando positionTicks foi atualizado por último
    std::array<std::atomic<float>, scopeSize> scope {}; // anel com a saída (escrito pelo áudio)
    std::atomic<int> scopeWrite { 0 };
    juce::MidiDeviceListConnection midiDevicesChanged;

    // gravação de áudio
    bool audioArmed = false;
    juce::String recordingError;
    juce::TimeSliceThread recordThread { "LYNX gravação" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> takeWriter;
    juce::SpinLock takeLock;                       // a thread de áudio só tenta (nunca espera)
    juce::File takeFile;
    int takeChannels = 0;
    std::atomic<double> takeStartTick { -1.0 };    // posição do primeiro sample gravado
    std::atomic<juce::int64> takeFrames { 0 };
    double takeSampleRate = 44100.0;
    int takeLatency = 0;                           // entrada + saída, em samples
    MeterBank meters;
    PluginHost plugins;
    juce::StringArray missingPlugins;
    FlpImportReport importReport;
    RenderStats lastRenderStats;
    class ChannelRackProcessor* rackProcessor = nullptr; // nó do grafo (dono: o grafo)

    Song song;
    int selectedChannel = 0;
    bool modified = false;
    std::unique_ptr<StreamPrefetcher> prefetcher; // só com dispositivo de áudio (tocando ao vivo)

    static constexpr std::size_t maxUndoSteps = 100;
    std::deque<Song> undoStack, redoStack;
    juce::Time lastUndoPress; // clique do mouse que já gerou um passo de desfazer
    SnapshotExchange<RackState> rackExchange;
    RackState lastPublishedShape; // canais + instrumentos do último snapshot, para reaproveitar vozes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
} // namespace lynx
