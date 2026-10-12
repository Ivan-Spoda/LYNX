#pragma once

#include <functional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "core/LiveEventQueue.h"
#include "core/RackRenderer.h"
#include "core/SnapshotExchange.h"
#include "core/Song.h"
#include "engine/SampleLibrary.h"
#include "engine/Transport.h"

namespace lynx
{
// Dono do dispositivo de áudio, do grafo de processamento e do documento (Song).
// Fica entre o driver e o AudioProcessorPlayer para avançar o transporte
// depois de cada bloco, assim todos os nós veem a mesma posição de início.
//
// O Song só é tocado pela thread da UI; cada edição publica um snapshot
// imutável (RackState) para a thread de áudio.
class AudioEngine final : private juce::AudioIODeviceCallback,
                          public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    explicit AudioEngine (bool openAudioDevice = true);
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    Transport& getTransport() noexcept { return transport; }
    SampleLibrary& getSampleLibrary() noexcept { return samples; }

    void play() noexcept;
    void stop() noexcept;
    void rewind() noexcept;

    // Nota tocada ao vivo (teclado do PC, preview). Thread da UI.
    void sendLiveNote (int channel, int key, float velocity, bool on);

    // Documento (thread da UI)
    const Song& getSong() const noexcept { return song; }
    void setSong (Song newSong);
    void editSong (const std::function<void (Song&)>& edit);

    // Canal selecionado no Channel Rack (alvo do Piano Roll e do teclado). Thread da UI.
    int getSelectedChannel() const noexcept { return selectedChannel; }
    void setSelectedChannel (int channel);

    // Importa um .flp localizando a instalação do FL para achar os samples.
    // Retorna false e preenche `error` se não conseguir.
    bool importFlpFile (const juce::File& file, juce::String& error);

    // Renderiza sem dispositivo de áudio. Grava em `output` se não for nullptr.
    // Retorna o pico (usado pelo autoteste).
    float renderOffline (double seconds, double sampleRate, juce::AudioFormatWriter* output = nullptr);

    int getNumGraphConnections() const { return (int) graph.getConnections().size(); }

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

    void buildInitialGraph();
    void publishSong();

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    Transport transport;
    SampleLibrary samples;
    LiveEventQueue liveEvents;

    Song song;
    int selectedChannel = 0;
    SnapshotExchange<RackState> rackExchange;
    RackState lastPublishedShape; // canais + instrumentos do último snapshot, para reaproveitar vozes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
} // namespace lynx
