#include "engine/AudioEngine.h"

#include "engine/ChannelRackProcessor.h"
#include "engine/Metronome.h"

namespace daw
{
using IONode = juce::AudioProcessorGraph::AudioGraphIOProcessor;

AudioEngine::AudioEngine()
    : song (Song::makeDefault())
{
    publishSong();
    buildInitialGraph();
    player.setProcessor (&graph);

    // 0 entradas, 2 saídas, sem estado salvo ainda, usa o dispositivo padrão.
    deviceManager.initialise (0, 2, nullptr, true);
    deviceManager.addAudioCallback (this);

    startTimerHz (10); // coleta de snapshots aposentados
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    player.setProcessor (nullptr);
    graph.clear(); // os nós referenciam rackExchange e transport
}

void AudioEngine::buildInitialGraph()
{
    auto output = graph.addNode (std::make_unique<IONode> (IONode::audioOutputNode));
    auto rack = graph.addNode (std::make_unique<ChannelRackProcessor> (transport, rackExchange));
    auto metronome = graph.addNode (std::make_unique<Metronome> (transport));

    for (int channel = 0; channel < 2; ++channel)
    {
        graph.addConnection ({ { rack->nodeID, channel }, { output->nodeID, channel } });
        graph.addConnection ({ { metronome->nodeID, channel }, { output->nodeID, channel } });
    }
}

void AudioEngine::play() noexcept { transport.playing.store (true); }
void AudioEngine::stop() noexcept { transport.playing.store (false); }
void AudioEngine::rewind() noexcept { transport.rewindRequested.store (true); }

void AudioEngine::setSong (Song newSong)
{
    song = std::move (newSong);
    publishSong();
}

void AudioEngine::editSong (const std::function<void (Song&)>& edit)
{
    edit (song);
    publishSong();
}

void AudioEngine::publishSong()
{
    song.currentPattern = juce::jlimit (0, juce::jmax (0, (int) song.patterns.size() - 1), song.currentPattern);
    transport.bpm.store (song.bpm);
    transport.beatsPerBar.store (song.beatsPerBar);

    auto state = RackState::build (song, &lastPublishedShape, transport.sampleRate.load());

    lastPublishedShape.song.channels = song.channels;
    lastPublishedShape.instruments = state->instruments;

    rackExchange.publish (std::move (state));
    sendChangeMessage();
}

void AudioEngine::timerCallback()
{
    rackExchange.collectGarbage();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext& context)
{
    if (transport.rewindRequested.exchange (false))
        transport.positionTicks.store (0.0);

    player.audioDeviceIOCallbackWithContext (inputChannelData, numInputChannels,
                                             outputChannelData, numOutputChannels,
                                             numSamples, context);

    if (transport.playing.load (std::memory_order_relaxed))
        transport.positionTicks.store (transport.positionTicks.load (std::memory_order_relaxed)
                                           + numSamples * transport.ticksPerSample(),
                                       std::memory_order_relaxed);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    transport.sampleRate.store (device->getCurrentSampleRate());
    player.audioDeviceAboutToStart (device);
}

void AudioEngine::audioDeviceStopped()
{
    player.audioDeviceStopped();
}
} // namespace daw
