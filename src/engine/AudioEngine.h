#pragma once

#include <functional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "core/RackRenderer.h"
#include "core/SnapshotExchange.h"
#include "core/Song.h"
#include "engine/Transport.h"

namespace daw
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
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    Transport& getTransport() noexcept { return transport; }

    void play() noexcept;
    void stop() noexcept;
    void rewind() noexcept;

    // Documento (thread da UI)
    const Song& getSong() const noexcept { return song; }
    void setSong (Song newSong);
    void editSong (const std::function<void (Song&)>& edit);

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

    Song song;
    SnapshotExchange<RackState> rackExchange;
    RackState lastPublishedShape; // canais + instrumentos do último snapshot, para reaproveitar vozes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
} // namespace daw
