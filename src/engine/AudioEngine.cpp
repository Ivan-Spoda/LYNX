#include "engine/AudioEngine.h"

#include "engine/Text.h"

#include "core/FlpImport.h"
#include "core/SoftClip.h"
#include "engine/ChannelRackProcessor.h"
#include "engine/Metronome.h"
#include "flp/Flp.h"

namespace daw
{
using IONode = juce::AudioProcessorGraph::AudioGraphIOProcessor;

namespace
{
// Procura a instalação mais recente do FL Studio para resolver %FLStudio...%.
FlPathRoots findFlStudioRoots()
{
    FlPathRoots roots;
    const auto imageLine = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Image-Line");
    juce::File best;

    for (const auto& dir : imageLine.findChildFiles (juce::File::findDirectories, false, "FL Studio*"))
        if (dir.getChildFile ("Data").isDirectory() && dir.getFileName() > best.getFileName())
            best = dir;

    if (best.exists())
    {
        roots.factoryData = best.getFullPathName().toStdString();
        roots.data = best.getChildFile ("Data").getFullPathName().toStdString();
    }

    const auto user = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Image-Line/FL Studio");

    if (user.isDirectory())
        roots.userData = user.getFullPathName().toStdString();

    return roots;
}
} // namespace

AudioEngine::AudioEngine (bool openAudioDevice)
    : song (Song::makeDefault())
{
    publishSong();
    buildInitialGraph();

    if (openAudioDevice)
    {
        player.setProcessor (&graph);

        // 0 entradas, 2 saídas, sem estado salvo ainda, usa o dispositivo padrão.
        deviceManager.initialise (0, 2, nullptr, true);
        deviceManager.addAudioCallback (this);
    }

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
    // O nó de saída herda o número de canais do grafo no momento em que é criado.
    // Sem isto o grafo ainda tem 0 saídas e o JUCE recusa as conexões em silêncio.
    graph.setPlayConfigDetails (0, 2, transport.sampleRate.load(), 512);

    auto output = graph.addNode (std::make_unique<IONode> (IONode::audioOutputNode));
    auto rack = graph.addNode (std::make_unique<ChannelRackProcessor> (transport, rackExchange, liveEvents));
    auto metronome = graph.addNode (std::make_unique<Metronome> (transport));

    for (int channel = 0; channel < 2; ++channel)
    {
        graph.addConnection ({ { rack->nodeID, channel }, { output->nodeID, channel } });
        graph.addConnection ({ { metronome->nodeID, channel }, { output->nodeID, channel } });
    }

    jassert (graph.getConnections().size() == 4);
}

float AudioEngine::renderOffline (double seconds, double sampleRate, juce::AudioFormatWriter* output)
{
    constexpr int blockSize = 512;
    transport.sampleRate.store (sampleRate);
    graph.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    graph.prepareToPlay (sampleRate, blockSize);
    play();

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    float peak = 0.0f;

    for (int done = 0; done < (int) (seconds * sampleRate); done += blockSize)
    {
        buffer.clear();
        graph.processBlock (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            softClip (buffer.getWritePointer (ch), blockSize);

        peak = juce::jmax (peak, buffer.getMagnitude (0, blockSize));

        if (output != nullptr)
            output->writeFromAudioSampleBuffer (buffer, 0, blockSize);

        transport.positionTicks.store (transport.positionTicks.load() + blockSize * transport.ticksPerSample());
    }

    stop();
    graph.releaseResources();
    return peak;
}

void AudioEngine::play() noexcept { transport.playing.store (true); }
void AudioEngine::stop() noexcept { transport.playing.store (false); }
void AudioEngine::rewind() noexcept { transport.rewindRequested.store (true); }

void AudioEngine::sendLiveNote (int channel, int key, float velocity, bool on)
{
    NoteEvent e;
    e.type = on ? NoteEvent::Type::on : NoteEvent::Type::off;
    e.channel = channel;
    e.key = key;
    e.velocity = velocity;
    liveEvents.push (e);
}

void AudioEngine::setSong (Song newSong)
{
    song = std::move (newSong);
    selectedChannel = 0;
    publishSong();
}

void AudioEngine::editSong (const std::function<void (Song&)>& edit)
{
    edit (song);
    publishSong();
}

void AudioEngine::setSelectedChannel (int channel)
{
    channel = juce::jlimit (0, juce::jmax (0, (int) song.channels.size() - 1), channel);

    if (channel != selectedChannel)
    {
        selectedChannel = channel;
        sendChangeMessage();
    }
}

bool AudioEngine::importFlpFile (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
    {
        error = utf8 ("não foi possível ler o arquivo");
        return false;
    }

    try
    {
        const auto project = flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() });

        FlpImportOptions options;
        options.roots = findFlStudioRoots();
        options.fileExists = [] (const std::string& path) { return juce::File (juce::String::fromUTF8 (path.c_str())).existsAsFile(); };

        stop();
        rewind();
        setSong (importFlp (project, options));
        return true;
    }
    catch (const flp::Error& e)
    {
        error = juce::String::fromUTF8 (e.what());
        return false;
    }
}

void AudioEngine::publishSong()
{
    song.currentPattern = juce::jlimit (0, juce::jmax (0, (int) song.patterns.size() - 1), song.currentPattern);
    selectedChannel = juce::jlimit (0, juce::jmax (0, (int) song.channels.size() - 1), selectedChannel);
    transport.bpm.store (song.bpm);
    transport.beatsPerBar.store (song.beatsPerBar);

    auto state = RackState::build (song, &lastPublishedShape, transport.sampleRate.load(),
                                   [this] (const std::string& path) { return samples.load (juce::String::fromUTF8 (path.c_str())); });

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

    for (int ch = 0; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            softClip (outputChannelData[ch], numSamples);

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
