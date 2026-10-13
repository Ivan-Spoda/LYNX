#include "engine/AudioEngine.h"

#include "engine/Text.h"

#include "core/FlpImport.h"
#include "core/Dither.h"
#include "core/SoftClip.h"
#include "engine/ChannelRackProcessor.h"
#include "engine/Metronome.h"
#include "engine/PreviewPlayer.h"
#include "flp/Flp.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

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

namespace
{
// Workers do áudio com prioridade de tempo real (MMCSS "Pro Audio" no Windows, como o driver).
void raiseAudioThreadPriority()
{
   #if JUCE_WINDOWS
    using AvSet = void* (__stdcall*) (const wchar_t*, unsigned long*);
    static const auto avSet = [] () -> AvSet {
        if (auto* avrt = LoadLibraryW (L"avrt.dll"))
            return reinterpret_cast<AvSet> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadCharacteristicsW")));
        return nullptr;
    }();
    unsigned long task = 0;

    if (avSet == nullptr || avSet (L"Pro Audio", &task) == nullptr)
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
   #endif
}

int workerCount()
{
    // a thread de áudio também trabalha; deixa um núcleo livre para a interface
    const auto cores = (int) std::thread::hardware_concurrency();
    return juce::jlimit (0, 7, cores - 2);
}
} // namespace

AudioEngine::AudioEngine (bool openAudioDevice)
    : workers (std::make_unique<WorkerPool> (workerCount(), raiseAudioThreadPriority)),
      song (Song::makeDefault())
{
    publishSong();
    buildInitialGraph();

    if (openAudioDevice)
    {
        player.setProcessor (&graph);

        // 0 entradas, 2 saídas, sem estado salvo ainda, usa o dispositivo padrão.
        deviceManager.initialise (0, 2, nullptr, true);
        deviceManager.addAudioCallback (this);

        prefetcher = std::make_unique<StreamPrefetcher> (transport);
        prefetcher->update (song, samples);
        enableAllMidiInputs();
        deviceManager.addMidiInputDeviceCallback ({}, this); // {} = todas as entradas ligadas
        midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableAllMidiInputs(); });
    }

    startTimerHz (30); // gravação e coleta de snapshots aposentados
}

AudioEngine::~AudioEngine()
{
    stopTimer();

    {
        const juce::SpinLock::ScopedLockType lock (takeLock);
        takeWriter = nullptr; // fecha o WAV do take em andamento (fica na pasta de gravações)
    }

    recordThread.stopThread (2000);
    prefetcher = nullptr;
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
    auto rack = graph.addNode (std::make_unique<ChannelRackProcessor> (transport, rackExchange, liveEvents, midiEvents, meters, workers.get()));
    auto metronome = graph.addNode (std::make_unique<Metronome> (transport));
    auto previewOwned = std::make_unique<PreviewPlayer>();
    preview = previewOwned.get();
    auto previewNode = graph.addNode (std::move (previewOwned));

    for (int channel = 0; channel < 2; ++channel)
    {
        graph.addConnection ({ { rack->nodeID, channel }, { output->nodeID, channel } });
        graph.addConnection ({ { metronome->nodeID, channel }, { output->nodeID, channel } });
        graph.addConnection ({ { previewNode->nodeID, channel }, { output->nodeID, channel } });
    }

    jassert (graph.getConnections().size() == 6);
}

float AudioEngine::renderOffline (const RenderOptions& options)
{
    const auto seconds = options.seconds;
    const auto sampleRate = options.sampleRate;
    auto* output = options.output;
    const auto& progress = options.progress;
    const auto tailSeconds = options.tailSeconds;
    const auto trimSamples = options.trimSamples;
    std::optional<Dither> dither;

    if (options.ditherBits > 0)
        dither.emplace (options.ditherBits);

    constexpr int blockSize = 512;
    constexpr float silence = 3.0e-5f; // ~ -90 dB
    transport.sampleRate.store (sampleRate);
    graph.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    graph.prepareToPlay (sampleRate, blockSize);
    play();

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    const auto trim = (std::int64_t) juce::jmax (0, trimSamples);
    const auto musicSamples = (std::int64_t) (seconds * sampleRate) + trim;
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

        if (dither)
            for (int ch = 0; ch < view.getNumChannels(); ++ch)
                dither->process (view.getWritePointer (ch), n, ch);

        // PDC: o começo é o silêncio da latência; o fim da música chega `trim` samples depois
        if (output != nullptr && done + n > trim)
        {
            const auto skip = (int) juce::jmax<std::int64_t> (0, trim - done);
            output->writeFromAudioSampleBuffer (view, skip, n - skip);
        }

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

void AudioEngine::previewFile (const juce::File& file)
{
    if (preview != nullptr)
        preview->play (file.existsAsFile() ? samples.load (file.getFullPathName()) : nullptr);
}

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

void AudioEngine::updateAudioRecording()
{
    const auto wantTake = audioArmed && transport.recording.load();

    // o arquivo fica pronto antes do Play: o primeiro bloco já é gravado
    if (wantTake && takeWriter == nullptr && recordingError.isEmpty())
        startAudioTake();

    // parou de tocar (ou desligou a gravação) com algo gravado: fecha o take
    if (takeWriter != nullptr && (! wantTake || ! transport.playing.load()) && takeFrames.load() > 0)
        finishAudioTake();
    else if (takeWriter != nullptr && ! wantTake)
        finishAudioTake();
}

void AudioEngine::startAudioTake()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    takeChannels = juce::jlimit (0, 2, device->getActiveInputChannels().countNumberOfSetBits());

    if (takeChannels == 0)
    {
        recordingError = utf8 ("nenhuma entrada de áudio ativa");
        return;
    }

    const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (utf8 ("LYNX/Gravações"));
    folder.createDirectory();
    takeFile = folder.getNonexistentChildFile ("Take " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav");
    takeSampleRate = device->getCurrentSampleRate();
    takeLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

    juce::WavAudioFormat wav;
    auto stream = takeFile.createOutputStream();

    if (stream == nullptr)
    {
        recordingError = utf8 ("não foi possível criar ") + takeFile.getFullPathName();
        return;
    }

    auto* writer = wav.createWriterFor (stream.get(), takeSampleRate, (unsigned int) takeChannels, 24, {}, 0);

    if (writer == nullptr)
    {
        stream.reset();
        takeFile.deleteFile();
        recordingError = utf8 ("o WAV não aceita esta taxa/canais");
        return;
    }

    stream.release(); // agora é do writer
    takeStartTick.store (-1.0);
    takeFrames.store (0);
    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, recordThread, 1 << 18);
    const juce::SpinLock::ScopedLockType lock (takeLock);
    takeWriter = std::move (threaded);
}

void AudioEngine::finishAudioTake()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> done;
    {
        const juce::SpinLock::ScopedLockType lock (takeLock);
        done = std::move (takeWriter);
    }
    done.reset(); // esvazia a fila e fecha o WAV

    const auto frames = takeFrames.exchange (0);
    const auto startTick = takeStartTick.exchange (-1.0);

    if (frames <= 0 || startTick < 0.0)
    {
        takeFile.deleteFile();
        return;
    }

    // o que entrou na placa corresponde ao que se ouvia latência atrás
    const auto ticksPerSample = song.bpm * ppq / (60.0 * takeSampleRate);
    auto start = (std::int64_t) std::llround (startTick - takeLatency * ticksPerSample);
    std::int64_t offset = 0;

    if (start < 0)
    {
        offset = -start;
        start = 0;
    }

    const auto length = (std::int64_t) std::llround ((double) frames * ticksPerSample) - offset;

    if (length <= 0)
        return;

    const auto path = takeFile.getFullPathName().toStdString();
    const auto name = takeFile.getFileNameWithoutExtension().toStdString();

    editSong ([=] (Song& s) {
        ChannelDef c;
        c.name = name;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = path;
        c.mixerTrack = s.nextFreeInsert();
        s.channels.push_back (c);

        int track = 0;
        for (const auto& clip : s.playlist)
            track = std::max (track, clip.track + 1);

        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = (int) s.channels.size() - 1;
        clip.track = track;
        clip.start = start;
        clip.length = length;
        clip.offset = offset;
        s.playlist.push_back (clip);
    });
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

void AudioEngine::setAudioRecordingArmed (bool armed)
{
    audioArmed = armed;

    if (armed)
    {
        // abre a entrada padrão (estéreo se der) no mesmo dispositivo/tipo de driver
        auto setup = deviceManager.getAudioDeviceSetup();

        if (auto* type = deviceManager.getCurrentDeviceTypeObject(); type != nullptr && setup.inputDeviceName.isEmpty())
        {
            const auto names = type->getDeviceNames (true);
            const auto index = type->getDefaultDeviceIndex (true);

            if (juce::isPositiveAndBelow (index, names.size()))
                setup.inputDeviceName = names[index];
        }

        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, 2, true);
        recordingError = deviceManager.setAudioDeviceSetup (setup, true);

        if (! recordThread.isThreadRunning())
            recordThread.startThread();
    }

    sendChangeMessage();
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

    updateAudioRecording();

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
    modified = false;
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

    if (undoable)
        modified = true;

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
    modified = true;
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
    modified = true;
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
    lastPublishedShape.latencies = state->latencies;
    lastPublishedShape.pdc = state->pdc;
    lastPublishedShape.channelDelays = state->channelDelays;
    lastPublishedShape.sendDelays = state->sendDelays;

    rackExchange.publish (std::move (state));

    if (prefetcher != nullptr)
        prefetcher->update (song, samples);

    sendChangeMessage();
}

void AudioEngine::timerCallback()
{
    updateRecording();
    rackExchange.collectGarbage();

    // Plugins são preparados numa taxa fixa; se o dispositivo mudou de taxa, recria.
    // Um plugin que mudou de latência (troca de preset, lookahead) pede um novo plano de PDC.
    if (std::abs (transport.sampleRate.load() - lastPublishedShape.sampleRate) > 0.5
        || lastPublishedShape.currentLatencies() != lastPublishedShape.latencies)
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

        // gravação de áudio: a entrada deste trecho vai para o disco (sem esperar lock)
        if (playing && transport.recording.load (std::memory_order_relaxed))
        {
            const juce::SpinLock::ScopedTryLockType lock (takeLock);

            if (lock.isLocked() && takeWriter != nullptr && numInputChannels > 0)
            {
                std::array<const float*, 2> take {};

                for (int ch = 0; ch < takeChannels; ++ch)
                    take[(std::size_t) ch] = inputs[(std::size_t) juce::jmin (ch, numInputChannels - 1)];

                if (take[0] != nullptr && (takeChannels < 2 || take[1] != nullptr))
                {
                    if (takeStartTick.load (std::memory_order_relaxed) < 0.0)
                        takeStartTick.store (transport.positionTicks.load (std::memory_order_relaxed));

                    if (takeWriter->write (take.data(), n))
                        takeFrames.fetch_add (n, std::memory_order_relaxed);
                }
            }
        }

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
