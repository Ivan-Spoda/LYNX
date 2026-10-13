#include "engine/AudioEngine.h"

#include "engine/Text.h"

#include "core/FlpImport.h"
#include "core/SoftClip.h"
#include "engine/ChannelRackProcessor.h"
#include "engine/Metronome.h"
#include "flp/Flp.h"

namespace lynx
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

        enableAllMidiInputs();
        deviceManager.addMidiInputDeviceCallback ({}, this); // {} = todas as entradas ligadas
        midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableAllMidiInputs(); });
    }

    startTimerHz (30); // gravação e coleta de snapshots aposentados
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    midiDevicesChanged = {};
    deviceManager.removeMidiInputDeviceCallback ({}, this);
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
    auto rack = graph.addNode (std::make_unique<ChannelRackProcessor> (transport, rackExchange, liveEvents, midiEvents, meters));
    auto metronome = graph.addNode (std::make_unique<Metronome> (transport));

    for (int channel = 0; channel < 2; ++channel)
    {
        graph.addConnection ({ { rack->nodeID, channel }, { output->nodeID, channel } });
        graph.addConnection ({ { metronome->nodeID, channel }, { output->nodeID, channel } });
    }

    jassert (graph.getConnections().size() == 4);
}

float AudioEngine::renderOffline (double seconds, double sampleRate, juce::AudioFormatWriter* output,
                                  const std::function<bool (double)>& progress, double tailSeconds)
{
    constexpr int blockSize = 512;
    constexpr float silence = 3.0e-5f; // ~ -90 dB
    transport.sampleRate.store (sampleRate);
    graph.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    graph.prepareToPlay (sampleRate, blockSize);
    play();

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    const auto musicSamples = (std::int64_t) (seconds * sampleRate);
    const auto totalSamples = musicSamples + (std::int64_t) (juce::jmax (0.0, tailSeconds) * sampleRate);
    std::int64_t quietSamples = 0;
    bool cancelled = false;

    for (std::int64_t done = 0; done < totalSamples; done += blockSize)
    {
        const auto n = (int) juce::jmin<std::int64_t> (blockSize, totalSamples - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        view.clear();
        graph.processBlock (view, midi);

        for (int ch = 0; ch < view.getNumChannels(); ++ch)
            softClip (view.getWritePointer (ch), n);

        const auto blockPeak = view.getMagnitude (0, n);
        peak = juce::jmax (peak, blockPeak);

        if (output != nullptr)
            output->writeFromAudioSampleBuffer (view, 0, n);

        transport.positionTicks.store (transport.positionTicks.load() + n * transport.ticksPerSample());

        // na cauda: meio segundo de silêncio encerra
        if (done + n > musicSamples)
        {
            quietSamples = blockPeak < silence ? quietSamples + n : 0;

            if (quietSamples > (std::int64_t) (sampleRate * 0.5))
                break;
        }

        if (progress && (done / blockSize) % 32 == 0 && ! progress ((double) done / (double) juce::jmax<std::int64_t> (1, totalSamples)))
        {
            cancelled = true;
            break;
        }
    }

    stop();
    graph.releaseResources();
    return cancelled ? -1.0f : peak;
}

void AudioEngine::play() noexcept { transport.playing.store (true); }
void AudioEngine::stop() noexcept { transport.playing.store (false); }
void AudioEngine::rewind() noexcept { transport.rewindRequested.store (true); }
void AudioEngine::seek (double ticks) noexcept { transport.seekRequest.store (juce::jmax (0.0, ticks)); }

void AudioEngine::sendLiveNote (int channel, int key, float velocity, bool on, bool record)
{
    NoteEvent e;
    e.type = on ? NoteEvent::Type::on : NoteEvent::Type::off;
    e.channel = channel;
    e.key = key;
    e.velocity = velocity;
    liveEvents.push (e);

    if (record && transport.recording.load() && transport.playing.load())
        uiRecorded.push_back ({ estimatedPositionNow(), channel, key, velocity, on });
}

double AudioEngine::estimatedPositionNow() const noexcept
{
    // a posição só anda de bloco em bloco; interpola pelo tempo desde o último bloco
    auto tick = transport.positionTicks.load (std::memory_order_relaxed);

    if (transport.playing.load (std::memory_order_relaxed))
    {
        const auto elapsedMs = juce::jlimit (0.0, 100.0, juce::Time::getMillisecondCounterHiRes() - blockTimeMs.load());
        tick += elapsedMs * 0.001 * transport.sampleRate.load() * transport.ticksPerSample();
    }

    return tick;
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // thread MIDI: só filas e atomics
    if (! m.isNoteOnOrOff())
        return;

    const auto on = m.isNoteOn();
    NoteEvent e;
    e.type = on ? NoteEvent::Type::on : NoteEvent::Type::off;
    e.channel = liveChannel.load();
    e.key = m.getNoteNumber();
    e.velocity = on ? m.getFloatVelocity() : 0.0f;
    midiEvents.push (e);

    if (transport.recording.load() && transport.playing.load())
        midiRecordQueue.push ({ estimatedPositionNow(), e.channel, e.key, e.velocity, on });
}

void AudioEngine::enableAllMidiInputs()
{
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (! deviceManager.isMidiInputDeviceEnabled (device.identifier))
            deviceManager.setMidiInputDeviceEnabled (device.identifier, true);
}

juce::StringArray AudioEngine::getActiveMidiInputs() const
{
    juce::StringArray names;

    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier))
            names.add (device.name);

    return names;
}

void AudioEngine::setRecording (bool shouldRecord)
{
    transport.recording.store (shouldRecord);
    updateRecording();
    sendChangeMessage();
}

void AudioEngine::updateRecording()
{
    const auto playing = transport.playing.load();
    const auto active = transport.recording.load() && playing;

    if (playing)
        lastPlayingTick = estimatedPositionNow();

    // a tomada começa ao tocar com a gravação ligada, ou no primeiro evento gravado
    // (que só é enfileirado nessa situação, mesmo que este timer ainda não tenha visto)
    auto beginTake = [this] {
        if (recorder.isActive())
            return;

        const auto& pattern = song.patterns[(std::size_t) song.currentPattern];
        recorder.begin (pattern.effectiveLength (song.beatsPerBar));
        takeHasUndoStep = false;
    };

    if (active)
        beginTake();

    RecordedEvent e;

    while (midiRecordQueue.pop (e))
    {
        beginTake();
        recorder.add (song, e);
    }

    for (const auto& ui : uiRecorded)
    {
        beginTake();
        recorder.add (song, ui);
    }

    uiRecorded.clear();

    if (! active && recorder.isActive())
        recorder.end (song, lastPlayingTick);

    auto notes = recorder.takeFinished();

    if (notes.empty())
        return;

    // a tomada inteira é um passo só no desfazer
    editSong ([&notes] (Song& s) {
        auto& p = s.patterns[(std::size_t) s.currentPattern];
        p.notes.insert (p.notes.end(), notes.begin(), notes.end());
        p.sortNotes();
    }, ! takeHasUndoStep);
    takeHasUndoStep = true;
}

void AudioEngine::setSong (Song newSong)
{
    undoStack.clear();
    redoStack.clear();
    song = std::move (newSong);
    selectedChannel = 0;
    publishSong();
}

void AudioEngine::editSong (const std::function<void (Song&)>& edit, bool undoable)
{
    if (undoable)
    {
        // Durante um arraste chegam dezenas de edições; só a primeira do clique guarda o estado.
        bool samePress = false;

        if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        {
            const auto press = juce::Desktop::getInstance().getMainMouseSource().getLastMouseDownTime();
            samePress = press == lastUndoPress;
            lastUndoPress = press;
        }
        else
        {
            lastUndoPress = {};
        }

        if (! samePress)
        {
            undoStack.push_back (song);
            redoStack.clear();

            if (undoStack.size() > maxUndoSteps)
                undoStack.pop_front();
        }
    }

    edit (song);
    publishSong();
}

void AudioEngine::undo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (std::move (song));
    song = std::move (undoStack.back());
    undoStack.pop_back();
    lastUndoPress = {};
    publishSong();
}

void AudioEngine::redo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (std::move (song));
    song = std::move (redoStack.back());
    redoStack.pop_back();
    lastUndoPress = {};
    publishSong();
}

void AudioEngine::setSelectedChannel (int channel)
{
    channel = juce::jlimit (0, juce::jmax (0, (int) song.channels.size() - 1), channel);

    liveChannel.store (channel);

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
    liveChannel.store (selectedChannel);
    transport.bpm.store (song.bpm);
    transport.beatsPerBar.store (song.beatsPerBar);
    transport.songMode.store (song.mode == PlayMode::song);
    transport.loopStart.store ((double) song.loopStart);
    transport.loopEnd.store ((double) song.loopEnd);

    RackState::Factories factories;
    factories.samples = [this] (const std::string& path) { return samples.load (juce::String::fromUTF8 (path.c_str())); };
    factories.pluginInstruments = [this] (const PluginRef& ref, double sr) { return plugins.makeInstrument (ref, sr); };
    factories.pluginEffects = [this] (const PluginRef& ref, double sr) { return plugins.makeEffect (ref, sr); };

    auto state = RackState::build (song, &lastPublishedShape, transport.sampleRate.load(), factories);

    // o que o próximo build precisa para reaproveitar instrumentos e efeitos
    lastPublishedShape.song.channels = song.channels;
    lastPublishedShape.song.mixer = song.mixer;
    lastPublishedShape.instruments = state->instruments;
    lastPublishedShape.effects = state->effects;
    lastPublishedShape.sampleRate = state->sampleRate;

    rackExchange.publish (std::move (state));
    sendChangeMessage();
}

void AudioEngine::timerCallback()
{
    updateRecording();
    rackExchange.collectGarbage();

    // Plugins são preparados numa taxa fixa; se o dispositivo mudou de taxa, recria.
    if (std::abs (transport.sampleRate.load() - lastPublishedShape.sampleRate) > 0.5)
        publishSong();
}

std::shared_ptr<Instrument> AudioEngine::getChannelInstrument (int channel) const
{
    if (channel < 0 || channel >= (int) lastPublishedShape.instruments.size())
        return nullptr;

    return lastPublishedShape.instruments[(std::size_t) channel];
}

std::shared_ptr<Effect> AudioEngine::getEffect (int track, int slot) const
{
    const auto& effects = lastPublishedShape.effects;

    if (track < 0 || track >= (int) effects.size() || slot < 0 || slot >= (int) effects[(std::size_t) track].size())
        return nullptr;

    return effects[(std::size_t) track][(std::size_t) slot];
}

void AudioEngine::capturePluginStates()
{
    // Muda só o estado salvo (não recria nada nem entra no histórico de desfazer).
    for (std::size_t i = 0; i < song.channels.size(); ++i)
        if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument ((int) i).get()))
            song.channels[i].plugin.state = capturePluginState (p->getInstance());

    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < song.mixer[t].effects.size(); ++s)
            if (auto* p = dynamic_cast<PluginEffect*> (getEffect ((int) t, (int) s).get()))
                song.mixer[t].effects[s].plugin.state = capturePluginState (p->getInstance());

    lastPublishedShape.song.channels = song.channels;
    lastPublishedShape.song.mixer = song.mixer;
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

    if (const auto target = transport.seekRequest.exchange (-1.0); target >= 0.0)
        transport.positionTicks.store (target);

    // Com a região de loop ligada, o bloco é dividido no fim do loop e a posição volta ao
    // início dele (os nós veem um salto e soltam as notas, como no FL).
    std::array<const float*, 64> inputs {};
    std::array<float*, 64> outputs {};
    numInputChannels = juce::jmin (numInputChannels, (int) inputs.size());
    numOutputChannels = juce::jmin (numOutputChannels, (int) outputs.size());

    for (int done = 0; done < numSamples;)
    {
        auto n = numSamples - done;
        const auto playing = transport.playing.load (std::memory_order_relaxed);
        const auto tps = transport.ticksPerSample();
        const auto loopStart = transport.loopStart.load (std::memory_order_relaxed);
        const auto loopEnd = transport.loopEnd.load (std::memory_order_relaxed);
        const auto looping = playing && transport.songMode.load (std::memory_order_relaxed) && loopEnd > loopStart && tps > 0.0;

        if (looping)
        {
            // antes do loop toca normalmente até chegar nele; depois do fim volta ao início
            auto position = transport.positionTicks.load (std::memory_order_relaxed);

            if (position >= loopEnd)
            {
                position = loopStart;
                transport.positionTicks.store (position, std::memory_order_relaxed);
            }

            n = juce::jlimit (1, n, (int) std::ceil ((loopEnd - position) / tps));
        }

        for (int ch = 0; ch < numInputChannels; ++ch)
            inputs[(std::size_t) ch] = inputChannelData[ch] != nullptr ? inputChannelData[ch] + done : nullptr;

        for (int ch = 0; ch < numOutputChannels; ++ch)
            outputs[(std::size_t) ch] = outputChannelData[ch] != nullptr ? outputChannelData[ch] + done : nullptr;

        player.audioDeviceIOCallbackWithContext (inputs.data(), numInputChannels, outputs.data(), numOutputChannels, n, context);

        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputs[(std::size_t) ch] != nullptr)
                softClip (outputs[(std::size_t) ch], n);

        if (playing)
        {
            auto position = transport.positionTicks.load (std::memory_order_relaxed) + n * tps;

            if (looping && position >= loopEnd - 1e-6)
                position = loopStart;

            transport.positionTicks.store (position, std::memory_order_relaxed);
        }

        done += n;
    }

    blockTimeMs.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
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
} // namespace lynx
