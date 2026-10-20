#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "engine/FlStudio.h"

#include <map>

#include "engine/Text.h"
#include "ui/I18n.h"

#include "core/FlpImport.h"
#include "core/Instruments.h"
#include "core/Dither.h"
#include "core/SoftClip.h"
#include "engine/ChannelRackProcessor.h"
#include "engine/DllAsioDevice.h"
#include "engine/Metronome.h"
#include "core/NoteTools.h"
#include "engine/PreviewPlayer.h"
#include "flp/Flp.h"

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_MAC
 #include <pthread.h>
 #include <sys/qos.h>
#elif JUCE_LINUX || JUCE_BSD
 #include <pthread.h>
 #include <sched.h>
#endif

namespace lynx
{
using IONode = juce::AudioProcessorGraph::AudioGraphIOProcessor;

// The FL Studio installation (registry, else the usual folders) to resolve %FLStudio...%.
FlPathRoots findFlStudioRoots()
{
    FlPathRoots roots;
    const auto fl = findFlStudio();

    if (fl.installDir.isDirectory())
    {
        roots.factoryData = fl.installDir.getFullPathName().toStdString();
        roots.data = fl.installDir.getChildFile ("Data").getFullPathName().toStdString();
    }

    if (fl.userData.isDirectory())
        roots.userData = fl.userData.getFullPathName().toStdString();

    roots.home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString();
    return roots;
}

namespace
{
// Audio workers with real-time priority (MMCSS "Pro Audio" on Windows, like the driver; QoS class on macOS; SCHED_FIFO
// on Linux when allowed).
void raiseAudioThreadPriority()
{
   #if JUCE_WINDOWS
    using AvSet = void* (__stdcall*) (const wchar_t*, unsigned long*);
    static const auto avSet = [] () -> AvSet {
        if (auto* avrt = LoadLibraryW (L"avrt.dll"))
            return reinterpret_cast<AvSet> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadCharacteristicsW")));
        return nullptr;
    }();
    // FL's audio worker threads also raise their MMCSS priority to "high" (FLEngine_x64.dll, REA round 87)
    using AvPriority = int (__stdcall*) (void*, int);
    static const auto avPriority = [] () -> AvPriority {
        if (auto* avrt = LoadLibraryW (L"avrt.dll"))
            return reinterpret_cast<AvPriority> (reinterpret_cast<void*> (GetProcAddress (avrt, "AvSetMmThreadPriority")));
        return nullptr;
    }();
    unsigned long task = 0;

    if (auto* handle = avSet != nullptr ? avSet (L"Pro Audio", &task) : nullptr)
    {
        if (avPriority != nullptr)
            avPriority (handle, 1 /* AVRT_PRIORITY_HIGH */);
    }
    else
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
   #elif JUCE_MAC
    // the highest quality-of-service class: scheduled like the UI's own interactive work, on the performance cores
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);
   #elif JUCE_LINUX || JUCE_BSD
    // real-time FIFO scheduling when the user may have it (audio group / rtkit limits, as JACK and PipeWire set up);
    // below the usual JACK/PipeWire audio thread priorities. Without the permission the thread stays as it is.
    sched_param param {};
    param.sched_priority = juce::jlimit (sched_get_priority_min (SCHED_FIFO), sched_get_priority_max (SCHED_FIFO), 70);
    pthread_setschedparam (pthread_self(), SCHED_FIFO, &param);
   #endif
}

int workerCount()
{
    // diagnostics: LYNX_WORKERS=n fixes the number of extra threads
    if (const auto forced = juce::SystemStats::getEnvironmentVariable ("LYNX_WORKERS", {}); forced.isNotEmpty())
        return juce::jlimit (0, 31, forced.getIntValue());

    // the audio thread works too; leave one core free for the UI
    const auto cores = (int) std::thread::hardware_concurrency();
    return juce::jlimit (0, 7, cores - 2);
}
juce::File midiSettingsFile() { return PluginHost::settingsFolder().getChildFile ("midi-settings.txt"); }
juce::File audioDeviceFile() { return PluginHost::settingsFolder().getChildFile ("audio-device.xml"); }
} // namespace

// Saves the audio device setup whenever it changes (Settings › Audio, the setup wizard, a driver's own panel), so
// the next start opens the same device.
struct AudioEngine::DeviceStateSaver final : public juce::ChangeListener
{
    explicit DeviceStateSaver (AudioEngine& e) : engine (e) { engine.deviceManager.addChangeListener (this); }
    ~DeviceStateSaver() override { engine.deviceManager.removeChangeListener (this); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { engine.saveAudioDeviceState(); }
    AudioEngine& engine;
};

void AudioEngine::saveAudioDeviceState()
{
    if (auto xml = deviceManager.createStateXml())
        xml->writeTo (audioDeviceFile());
}

void AudioEngine::restoreAudioDeviceState (const juce::XmlElement* state)
{
    deviceStateSaver = nullptr;
    if (state != nullptr)
        state->writeTo (audioDeviceFile());
    else
        audioDeviceFile().deleteFile();
}

AudioEngine::AudioEngine (bool openAudioDevice)
    : workers (std::make_unique<WorkerPool> (workerCount(), raiseAudioThreadPriority)),
      song (Song::makeDefault())
{
    inputSlot.fill (-1);
    publishSong();
    buildInitialGraph();

    if (openAudioDevice)
    {
        player.setProcessor (&graph);

        // LYNX ASIO without installation: lynx-asio.dll next to LYNX.exe, when it is not registered (REA round 106)
        if (auto lynxAsio = makeBuiltInLynxAsioType())
        {
            deviceManager.getAvailableDeviceTypes(); // the standard types first
            deviceManager.addAudioDeviceType (std::move (lynxAsio));
        }

        // 0 inputs, 2 outputs; the device of the last session (Settings › Audio) if it is still there, else the default.
        const auto saved = juce::XmlDocument::parse (audioDeviceFile());
        auto fresh = saved == nullptr;
        if (deviceManager.initialise (0, 2, saved.get(), true).isNotEmpty() && saved != nullptr)
        {
            deviceManager.initialise (0, 2, nullptr, true);
            fresh = true;
        }
        if (fresh)
            chooseSafeBufferSize(); // no saved setup: a buffer with room for heavy projects (Settings › Audio changes it)
        deviceManager.addAudioCallback (this);
        if (rackProcessor != nullptr)
            rackProcessor->getRenderer().setProfiling (true); // per-unit CPU for the CPU meter and the glitch log
        deviceStateSaver = std::make_unique<DeviceStateSaver> (*this);
        midiPrefs = midiPrefsShared = prefs::MidiPrefs::fromText (midiSettingsFile().loadFileAsString().toStdString());

        prefetcher = std::make_unique<StreamPrefetcher> (transport);
        prefetcher->update (song, samples);
        enableAllMidiInputs();
        deviceManager.addMidiInputDeviceCallback ({}, this); // {} = all inputs enabled
        midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableAllMidiInputs(); });
    }

    startTimerHz (30); // recording and collection of retired snapshots
}

AudioEngine::~AudioEngine()
{
    stopTimer();

    {
        const juce::SpinLock::ScopedLockType lock (takeLock);
        for (auto& take : takes)
            take.writer = nullptr; // closes the WAVs of the takes in progress (they stay in the recordings folder)
        takeCount = 0;
    }

    recordThread.stopThread (2000);
    prefetcher = nullptr;
    midiDevicesChanged = {};
    deviceStateSaver = nullptr;
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    player.setProcessor (nullptr);
    graph.clear(); // the nodes reference rackExchange and transport
}

void AudioEngine::buildInitialGraph()
{
    // The output node inherits the graph's channel count at the moment it is created.
    // Without this the graph still has 0 outputs and JUCE silently rejects the connections.
    graph.setPlayConfigDetails (0, 2, transport.sampleRate.load(), 512);

    auto output = graph.addNode (std::make_unique<IONode> (IONode::audioOutputNode));
    auto rack = graph.addNode (std::make_unique<ChannelRackProcessor> (transport, rackExchange, liveEvents, midiEvents, meters, workers.get()));
    rackProcessor = dynamic_cast<ChannelRackProcessor*> (rack->getProcessor());
    if (rackProcessor != nullptr)
        rackProcessor->setLiveInput (&liveInput);
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
        dither.emplace (options.ditherBits, options.ditherShaping);

    constexpr int blockSize = 512;
    constexpr float silence = 3.0e-5f; // ~ -90 dB
    setOfflineRendering (true); // instruments with FL render-quality settings use them (Sytrus)
    transport.sampleRate.store (sampleRate);
    graph.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    graph.prepareToPlay (sampleRate, blockSize);

    if (rackProcessor != nullptr)
        rackProcessor->setMetering (false); // nobody looks at the meters during the render

    play();

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    const auto trim = (std::int64_t) juce::jmax (0, trimSamples);
    const auto musicSamples = (std::int64_t) (seconds * sampleRate) + trim;
    const auto totalSamples = musicSamples + (std::int64_t) (juce::jmax (0.0, tailSeconds) * sampleRate);
    std::int64_t quietSamples = 0;
    bool cancelled = false;
    lastRenderStats = {};

    for (std::int64_t done = 0; done < totalSamples; done += blockSize)
    {
        const auto n = (int) juce::jmin<std::int64_t> (blockSize, totalSamples - done);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
        view.clear();
        const auto before = juce::Time::getMillisecondCounterHiRes();
        graph.processBlock (view, midi);
        const auto afterGraph = juce::Time::getMillisecondCounterHiRes();
        lastRenderStats.graphMs += afterGraph - before;

        for (int ch = 0; ch < view.getNumChannels(); ++ch)
            softClip (view.getWritePointer (ch), n);

        const auto blockPeak = view.getMagnitude (0, n);
        peak = juce::jmax (peak, blockPeak);

        if (dither)
            for (int ch = 0; ch < view.getNumChannels(); ++ch)
                dither->process (view.getWritePointer (ch), n, ch);

        // PDC: the start is the latency's silence; the song's end arrives `trim` samples later
        if ((output != nullptr || options.capture) && done + n > trim)
        {
            const auto skip = (int) juce::jmax<std::int64_t> (0, trim - done);
            const auto beforeWrite = juce::Time::getMillisecondCounterHiRes();
            if (output != nullptr)
                output->writeFromAudioSampleBuffer (view, skip, n - skip);
            if (options.capture)
                options.capture (view.getReadPointer (0, skip), view.getReadPointer (1, skip), n - skip);
            lastRenderStats.writeMs += juce::Time::getMillisecondCounterHiRes() - beforeWrite;
        }

        transport.positionTicks.store (transport.positionTicks.load() + n * transport.ticksPerSample());

        // in the tail: half a second of silence ends it
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

    if (rackProcessor != nullptr)
        rackProcessor->setMetering (true);

    graph.releaseResources();
    setOfflineRendering (false);
    return cancelled ? -1.0f : peak;
}

ProcessTimes* AudioEngine::getProcessTimes() noexcept
{
    return rackProcessor != nullptr ? &rackProcessor->processTimes() : nullptr;
}

void AudioEngine::play() noexcept { transport.playing.store (true); }

void AudioEngine::stop() noexcept
{
    waitingForInput.store (false);
    transport.countIn.store (0.0);
    transport.playing.store (false);
}

void AudioEngine::setRecordOptions (const RecordOptions& options)
{
    recordOptions = options;
    recordOptions.countInBars = juce::jlimit (0, 4, options.countInBars);
    sendChangeMessage();
}

void AudioEngine::startPlayback()
{
    if (! transport.recording.load())
    {
        play();
        return;
    }

    if (recordOptions.waitForInput)
    {
        waitingForInput.store (true);
        sendChangeMessage();
        return;
    }

    if (recordOptions.countInBars > 0)
    {
        const auto at = (std::int64_t) transport.positionTicks.load();
        transport.countIn.store ((double) countInLength (song, recordOptions.countInBars, at));
        sendChangeMessage();
        return;
    }

    play();
}

void AudioEngine::startOnInput() noexcept
{
    if (waitingForInput.exchange (false))
        transport.playing.store (true);
}

void AudioEngine::setInsertArmed (int track, bool armed)
{
    const auto it = std::find (armedInserts.begin(), armedInserts.end(), track);
    const auto wasArmed = it != armedInserts.end();

    if (armed == wasArmed || track <= 0)
        return;

    if (armed)
        armedInserts.push_back (track);
    else
        armedInserts.erase (it);

    if (track < maxMixerTracks)
        liveInput.armed[(std::size_t) track].store (armed);

    if (armed && ! audioArmed)
        setAudioRecordingArmed (true);
    else if (! armed && armedInserts.empty() && audioArmed)
        setAudioRecordingArmed (false);

    sendChangeMessage();
}

bool AudioEngine::isInsertArmed (int track) const noexcept
{
    return std::find (armedInserts.begin(), armedInserts.end(), track) != armedInserts.end();
}
void AudioEngine::rewind() noexcept { transport.rewindRequested.store (true); }
void AudioEngine::seek (double ticks) noexcept { transport.seekRequest.store (juce::jmax (0.0, ticks)); }

void AudioEngine::copyScope (float* destination, int count) const noexcept
{
    count = juce::jmin (count, scopeSize);
    auto r = (scopeWrite.load (std::memory_order_acquire) - count + scopeSize) % scopeSize;

    for (int i = 0; i < count; ++i)
    {
        destination[i] = scope[(std::size_t) r].load (std::memory_order_relaxed);
        r = (r + 1) % scopeSize;
    }
}

void AudioEngine::previewFile (const juce::File& file)
{
    if (preview != nullptr)
        preview->play (file.existsAsFile() ? samples.load (file.getFullPathName()) : nullptr);
}

void AudioEngine::setInputScale (int scale, int root) noexcept
{
    inputScaleRoot.store (root);
    inputScale.store (scale);
}

int AudioEngine::snapInputKey (int key) const noexcept
{
    const auto scale = inputScale.load();
    return scale > 0 ? notetools::snapToScale (key, inputScaleRoot.load(), scale) : key;
}

void AudioEngine::previewSample (std::shared_ptr<const SampleData> sample, bool loop, float gain)
{
    if (preview != nullptr)
        preview->play (std::move (sample), loop, gain);
}

double AudioEngine::getPreviewPosition() const noexcept
{
    return preview != nullptr ? preview->getPlayPosition() : -1.0;
}

void AudioEngine::sendLiveNote (int channel, int key, float velocity, bool on, bool record)
{
    if (record) // played notes (the PC keyboard) follow the scale snap; previews don't
    {
        // the note-off stops the key its note-on started, even if the snap changed in between
        const auto requested = key;
        if (on)
        {
            key = snapInputKey (key);
            playedKeys.on (channel, requested, key);
            startOnInput();
        }
        else
            key = playedKeys.off (channel, requested, snapInputKey (key));
    }

    NoteEvent e;
    e.type = on ? NoteEvent::Type::on : NoteEvent::Type::off;
    e.channel = channel;
    e.key = key;
    e.velocity = velocity;
    liveEvents.push (e);

    if (record && transport.recording.load() && transport.playing.load())
        uiRecorded.push_back ({ estimatedPositionNow(), channel, key, velocity, on });
}

void AudioEngine::panic()
{
    NoteEvent e;
    e.type = NoteEvent::Type::choke;
    e.channel = -1; // every channel
    liveEvents.push (e);
}

double AudioEngine::estimatedPositionNow() const noexcept
{
    // the position only moves block by block; interpolate by the time since the last block
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
    const auto wantTake = audioArmed && recordOptions.recordAudio && transport.recording.load();
    const auto open = takeCount > 0;

    // the files are ready before Play: the very first block gets recorded
    if (wantTake && ! open && recordingError.isEmpty())
        startAudioTake();

    // playback stopped (or recording turned off) with something recorded: close the takes
    if (open && (! wantTake || (! transport.playing.load() && takeFrames.load() > 0)))
        finishAudioTake();
}

void AudioEngine::startAudioTake()
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return;

    const auto names = getInputChannelNames();
    const auto deviceChannels = (int) names.size();
    const auto active = device->getActiveInputChannels();

    if (deviceChannels == 0 || active.isZero())
    {
        recordingError = "no audio input is open";
        notify (InputIssue::noInputOpen);
        return;
    }

    // one take per armed insert that has an input source; without armed inserts (Settings' "Record input audio"),
    // the first two input channels into a new insert
    std::vector<Take> wanted;
    std::vector<int> withoutInput;

    for (const auto t : armedInserts)
    {
        if (t <= 0 || t >= (int) song.mixer.size() || (int) wanted.size() >= maxTakes)
            continue;
        const auto [first, count] = inputChannelsOf (song.mixer[(std::size_t) t], deviceChannels);
        if (count == 0)
            withoutInput.push_back (t);
        else
            wanted.push_back ({ t, first, count, {}, {} });
    }

    if (armedInserts.empty())
        wanted.push_back ({ 0, 0, juce::jmin (2, deviceChannels), {}, {} });

    if (wanted.empty())
    {
        recordingError = "the armed inserts have no audio input";
        notify (InputIssue::armedWithoutInput, withoutInput.empty() ? 0 : withoutInput.front());
        return;
    }

    if (! withoutInput.empty())
        notify (InputIssue::insertWithoutInput, withoutInput.front());

    const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Recordings");
    folder.createDirectory();
    takeSampleRate = device->getCurrentSampleRate();
    takeLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    juce::WavAudioFormat wav;

    for (auto& take : wanted)
    {
        auto name = "Take " + stamp;
        if (take.track > 0)
            name << " - " << (song.mixer[(std::size_t) take.track].name.empty() ? "Insert " + juce::String (take.track)
                                                                                 : juce::String::fromUTF8 (song.mixer[(std::size_t) take.track].name.c_str()));
        take.file = folder.getNonexistentChildFile (juce::File::createLegalFileName (name), ".wav");
        auto stream = take.file.createOutputStream();

        if (stream == nullptr)
        {
            recordingError = utf8 ("could not create ") + take.file.getFullPathName();
            continue;
        }

        auto* writer = wav.createWriterFor (stream.get(), takeSampleRate, (unsigned int) take.channels, 24, {}, 0);

        if (writer == nullptr)
        {
            stream.reset();
            take.file.deleteFile();
            recordingError = utf8 ("WAV does not accept this sample rate/channel count");
            continue;
        }

        stream.release(); // now owned by the writer
        take.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer, recordThread, 1 << 18);
    }

    if (recordingError.isNotEmpty())
        notify (InputIssue::failed, 0, recordingError);

    takeStartTick.store (-1.0);
    takeFrames.store (0);
    const juce::SpinLock::ScopedLockType lock (takeLock);
    takeCount = 0;

    for (auto& take : wanted)
        if (take.writer != nullptr)
            takes[(std::size_t) takeCount++] = std::move (take);
}

void AudioEngine::finishAudioTake()
{
    std::vector<Take> done;
    {
        const juce::SpinLock::ScopedLockType lock (takeLock);
        for (int k = 0; k < takeCount; ++k)
            done.push_back (std::move (takes[(std::size_t) k]));
        takeCount = 0;
    }

    for (auto& take : done)
        take.writer.reset(); // drains the queue and closes the WAV

    const auto frames = takeFrames.exchange (0);
    const auto startTick = takeStartTick.exchange (-1.0);

    if (frames <= 0 || startTick < 0.0)
    {
        for (auto& take : done)
            take.file.deleteFile();
        return;
    }

    // what came into the interface corresponds to what was heard one latency earlier
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

    struct Made
    {
        std::string path, name;
        int track;
    };
    std::vector<Made> made;
    for (const auto& take : done)
        made.push_back ({ take.file.getFullPathName().toStdString(), take.file.getFileNameWithoutExtension().toStdString(), take.track });

    editSong ([=] (Song& s) {
        int playlistTrack = 0;
        for (const auto& clip : s.playlist)
            playlistTrack = std::max (playlistTrack, clip.track + 1);

        for (const auto& m : made) // each take on its own Playlist track, through the insert it was recorded on
        {
            ChannelDef c;
            c.name = m.name;
            c.instrument = InstrumentKind::audioClip;
            c.samplePath = m.path;
            c.mixerTrack = m.track > 0 && m.track < (int) s.mixer.size() ? m.track : s.nextFreeInsert();
            s.channels.push_back (c);

            Clip clip;
            clip.kind = ClipKind::audio;
            clip.channel = (int) s.channels.size() - 1;
            clip.track = playlistTrack++;
            clip.start = start;
            clip.length = length;
            clip.offset = offset;
            s.playlist.push_back (clip);
        }
    });
}

std::vector<std::string> AudioEngine::getInputChannelNames() const
{
    std::vector<std::string> names;

    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && ! device->getActiveInputChannels().isZero())
        for (const auto& n : device->getInputChannelNames())
            if ((int) names.size() < LiveInput::maxChannels)
                names.push_back (n.toStdString());

    return names;
}

bool AudioEngine::hasAudioInput() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr && ! device->getActiveInputChannels().isZero();
}

void AudioEngine::notify (InputIssue issue, int track, const juce::String& detail)
{
    recordingNotice = { issue, track, detail };
    sendChangeMessage();
}

AudioEngine::RecordingNotice AudioEngine::openAudioInput (const juce::String& deviceName)
{
    auto* type = deviceManager.getCurrentDeviceTypeObject();

    if (type == nullptr || deviceManager.getCurrentAudioDevice() == nullptr)
        return { InputIssue::noDevice, 0, {} };

    auto setup = deviceManager.getAudioDeviceSetup();
    int count = 0;

    if (type->hasSeparateInputsAndOutputs())
    {
        // Windows Audio / DirectSound: the input is a device of its own
        const auto inputs = type->getDeviceNames (true);
        auto name = deviceName.isNotEmpty() ? deviceName : setup.inputDeviceName;

        if (! inputs.contains (name))
        {
            const auto index = type->getDefaultDeviceIndex (true);
            name = juce::isPositiveAndBelow (index, inputs.size()) ? inputs[index] : juce::String();
        }

        if (name.isEmpty())
            return { InputIssue::noInputDevice, 0, {} };

        auto* current = deviceManager.getCurrentAudioDevice();
        if (name == setup.inputDeviceName && ! current->getActiveInputChannels().isZero())
            count = current->getInputChannelNames().size();
        else if (std::unique_ptr<juce::AudioIODevice> probe (type->createDevice ({}, name)); probe != nullptr)
            count = probe->getInputChannelNames().size();

        setup.inputDeviceName = name;
    }
    else
    {
        // ASIO: one device does both; its input channels come in pairs in the mixer
        count = deviceManager.getCurrentAudioDevice()->getInputChannelNames().size();
        setup.inputDeviceName = setup.outputDeviceName;
    }

    if (count <= 0)
        count = 2;

    count = juce::jmin (count, (int) LiveInput::maxChannels);
    juce::BigInteger wanted;
    wanted.setRange (0, count, true);

    if (! setup.useDefaultInputChannels && setup.inputChannels == wanted && ! deviceManager.getCurrentAudioDevice()->getActiveInputChannels().isZero())
        return {};

    setup.useDefaultInputChannels = false;
    setup.inputChannels = wanted;
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    sendChangeMessage();

    if (error.isNotEmpty())
        return { InputIssue::failed, 0, error };
    if (! hasAudioInput())
        return { InputIssue::noInputDevice, 0, {} };
    return {};
}

void AudioEngine::setInsertInput (int track, int channel, bool stereo)
{
    if (track <= 0 || track >= (int) song.mixer.size())
        return;

    editSong ([=] (Song& s) {
        s.mixer[(std::size_t) track].inputChannel = channel;
        s.mixer[(std::size_t) track].inputStereo = stereo;
    });

    recordingError = {};

    if (channel >= 0 && ! hasAudioInput())
        if (const auto result = openAudioInput(); result.issue != InputIssue::none)
            notify (result.issue, track, result.detail);
}

void AudioEngine::setInsertMonitor (int track, InputMonitor mode)
{
    if (track > 0 && track < (int) song.mixer.size())
        editSong ([=] (Song& s) { s.mixer[(std::size_t) track].monitor = mode; });
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m)
{
    // MIDI thread: queues and atomics only (and a short copy of the device settings)
    prefs::MidiInputPrefs device;
    auto velocityCurve = prefs::VelocityCurve::linear;
    {
        const juce::SpinLock::ScopedLockType lock (midiPrefsLock);
        device = midiPrefsShared.input (source != nullptr ? source->getName().toStdString() : std::string());
        velocityCurve = midiPrefsShared.velocity;
    }

    if (! device.enabled || (device.midiChannel != 0 && m.getChannel() != device.midiChannel))
        return;

    if (m.isController())
    {
        controllerQueue.push ({ m.getControllerNumber(), m.getChannel() - 1, m.getControllerValue() });
        return;
    }

    if (! m.isNoteOnOrOff())
        return;

    const auto on = m.isNoteOn();

    if (on)
        startOnInput();

    // a note-off goes to the channels and key its note-on reached (the selection or the scale snap may have
    // changed while the key was down); a repeated note-on first stops what the previous one started
    InputHeldNotes::Target targets[InputHeldNotes::maxTargets], stale[InputHeldNotes::maxTargets];
    auto count = 0;
    auto staleCount = 0;

    if (on)
    {
        const auto key = snapInputKey (m.getNoteNumber());
        const auto extra = juce::jlimit (0, maxLiveChannels, extraLiveCount.load());

        // the primary selected channel, then the other selected ones (or the device's own channel)
        if (device.target >= 0)
            targets[count++] = { device.target, key };
        else
            for (int i = -1; i < extra && count < InputHeldNotes::maxTargets; ++i)
                targets[count++] = { i < 0 ? liveChannel.load() : extraLiveChannels[(std::size_t) i].load(), key };

        staleCount = midiHeld.noteOn (m.getNoteNumber(), targets, count, stale);
    }
    else
        count = midiHeld.noteOff (m.getNoteNumber(), targets);

    auto push = [this] (const InputHeldNotes::Target& t, bool noteOn, float velocity) {
        NoteEvent e;
        e.type = noteOn ? NoteEvent::Type::on : NoteEvent::Type::off;
        e.channel = t.channel;
        e.key = t.key;
        e.velocity = velocity;
        midiEvents.push (e);

        if (transport.recording.load() && transport.playing.load())
            midiRecordQueue.push ({ estimatedPositionNow(), e.channel, e.key, e.velocity, noteOn });
    };

    for (int i = 0; i < staleCount; ++i)
        push (stale[i], false, 0.0f);

    for (int i = 0; i < count; ++i)
        push (targets[i], on, on ? prefs::mapVelocity (m.getFloatVelocity(), velocityCurve) : 0.0f);
}

void AudioEngine::enableAllMidiInputs()
{
    // (a device turned off in the MIDI settings is closed, so other programs can use it)
    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        const auto on = midiPrefs.input (device.name.toStdString()).enabled;
        if (deviceManager.isMidiInputDeviceEnabled (device.identifier) != on)
            deviceManager.setMidiInputDeviceEnabled (device.identifier, on);
    }
}

void AudioEngine::setMidiPrefs (const prefs::MidiPrefs& p, bool save)
{
    midiPrefs = p;
    {
        const juce::SpinLock::ScopedLockType lock (midiPrefsLock);
        midiPrefsShared = p;
    }
    if (! p.pickup)
        pickups.clear();
    if (prefetcher != nullptr) // (only with an audio device: the real app)
        enableAllMidiInputs();
    if (save)
        midiSettingsFile().replaceWithText (juce::String (p.toText()));
    sendChangeMessage();
}

void AudioEngine::setUndoLevels (int levels)
{
    maxUndoSteps = (std::size_t) prefs::clampUndoLevels (levels);
    while (undoStack.size() > maxUndoSteps)
        undoStack.pop_front();
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
    recordingError = {};

    if (armed)
    {
        // opens the input of the current driver (every channel, so each insert can pick its own)
        if (! hasAudioInput())
            if (const auto result = openAudioInput(); result.issue != InputIssue::none)
            {
                recordingError = "no audio input";
                notify (result.issue, 0, result.detail);
            }

        if (! recordThread.isThreadRunning())
            recordThread.startThread();
    }

    sendChangeMessage();
}

void AudioEngine::setRecording (bool shouldRecord)
{
    if (shouldRecord && ! transport.recording.load())
        recordingError = {}; // a new attempt
    transport.recording.store (shouldRecord);
    liveInput.recording.store (shouldRecord);
    updateRecording();
    sendChangeMessage();
}

void AudioEngine::updateRecording()
{
    const auto playing = transport.playing.load();
    const auto active = transport.recording.load() && playing;

    if (playing)
        lastPlayingTick = estimatedPositionNow();

    // the take starts when playing with recording on, or at the first recorded event
    // (which is only queued in that situation, even if this timer hasn't seen it yet)
    auto beginTake = [this] {
        if (recorder.isActive())
            return;

        const auto& pattern = song.patterns[(std::size_t) song.currentPattern];
        recorder.begin (pattern.effectiveLength (song.beatsPerBar));
        takeHasUndoStep = false;
        takeClearedChannels.clear();
        takeStartPosition = lastPlayingTick;
    };

    if (active)
        beginTake();

    RecordedEvent e;

    while (midiRecordQueue.pop (e))
    {
        if (! recordOptions.recordNotes) // recording filter: notes off
            continue;
        beginTake();
        recorder.add (song, e);
    }

    for (const auto& ui : uiRecorded)
    {
        if (! recordOptions.recordNotes)
            continue;
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

    // the whole take is a single undo step: overdub or replace, quantised to the record grid; in song mode a
    // clip of the pattern is placed where recording started if none plays there
    const auto options = recordOptions;
    const auto placeClipAt = (std::int64_t) takeStartPosition;
    editSong ([&] (Song& s) {
        commitTake (s.patterns[(std::size_t) s.currentPattern], notes, options.mode, options.quantise, takeClearedChannels);

        if (options.placeSongClip && s.mode == PlayMode::song && ! takeHasUndoStep)
            placePatternClipAt (s, s.currentPattern, placeClipAt);
    }, ! takeHasUndoStep);
    takeHasUndoStep = true;
}

void AudioEngine::setSong (Song newSong)
{
    undoStack.clear();
    redoStack.clear();
    song = std::move (newSong);
    selectedChannel = 0;
    selectedChannels = { 0 };
    modified = false;

    // arming isn't saved: a new project starts disarmed; its inserts' audio inputs open the device input
    for (const auto t : std::vector<int> (armedInserts))
        setInsertArmed (t, false);
    if (prefetcher != nullptr && ! hasAudioInput()
        && std::any_of (song.mixer.begin(), song.mixer.end(), [] (const MixerTrack& t) { return t.inputChannel >= 0; }))
        openAudioInput();

    publishSong();
}

void AudioEngine::editSong (const std::function<void (Song&)>& edit, bool undoable)
{
    if (undoable)
    {
        // Dozens of edits arrive during a drag; only the first one of the click saves the state.
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

namespace
{
const Song& historyState (const std::deque<Song>& undo, const Song& current, const std::deque<Song>& redo, int index)
{
    if (index < (int) undo.size())
        return undo[(std::size_t) index];
    if (index == (int) undo.size())
        return current;
    return redo[redo.size() - 1 - (std::size_t) (index - (int) undo.size() - 1)];
}
} // namespace

ChangeLabel AudioEngine::getHistoryLabel (int index) const
{
    if (index <= 0 || index >= getHistorySize())
        return { TR_NOOP ("Start"), {} };

    return describeChange (historyState (undoStack, song, redoStack, index - 1), historyState (undoStack, song, redoStack, index));
}

void AudioEngine::jumpToHistory (int index)
{
    index = juce::jlimit (0, getHistorySize() - 1, index);

    if (index == getHistoryPosition())
        return;

    // move through the stacks, then publish once
    while (getHistoryPosition() > index && ! undoStack.empty())
    {
        redoStack.push_back (std::move (song));
        song = std::move (undoStack.back());
        undoStack.pop_back();
    }

    while (getHistoryPosition() < index && ! redoStack.empty())
    {
        undoStack.push_back (std::move (song));
        song = std::move (redoStack.back());
        redoStack.pop_back();
    }

    modified = true;
    lastUndoPress = {};
    publishSong();
}

void AudioEngine::setSelectedChannel (int channel)
{
    channel = juce::jlimit (0, juce::jmax (0, (int) song.channels.size() - 1), channel);
    setChannelSelection ({ channel }, channel);
}

void AudioEngine::setChannelSelection (std::vector<int> channels, int primary)
{
    const auto count = (int) song.channels.size();
    std::sort (channels.begin(), channels.end());
    channels.erase (std::unique (channels.begin(), channels.end()), channels.end());
    channels.erase (std::remove_if (channels.begin(), channels.end(), [count] (int c) { return c < 0 || c >= count; }), channels.end());
    primary = juce::jlimit (0, juce::jmax (0, count - 1), primary);

    if (std::find (channels.begin(), channels.end(), primary) == channels.end())
        channels.insert (std::lower_bound (channels.begin(), channels.end(), primary), primary);

    if (channels == selectedChannels && primary == selectedChannel)
    {
        publishSelection();
        return;
    }

    selectedChannels = std::move (channels);
    selectedChannel = primary;
    publishSelection();
    sendChangeMessage();
}

void AudioEngine::publishSelection()
{
    liveChannel.store (selectedChannel);
    int extra = 0;

    for (auto c : selectedChannels)
        if (c != selectedChannel && extra < maxLiveChannels)
            extraLiveChannels[(std::size_t) extra++].store (c);

    extraLiveCount.store (extra);
}

bool AudioEngine::importFlpFile (const juce::File& file, juce::String& error, const std::map<std::string, std::string>& relocations, bool asEdit)
{
    juce::MemoryBlock data;

    if (! file.loadFileAsData (data))
    {
        error = utf8 ("could not read the file");
        return false;
    }

    try
    {
        const auto project = flp::parseProject ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() });

        FlpImportOptions options;
        options.roots = findFlStudioRoots();
        options.projectFolder = file.getParentDirectory().getFullPathName().toStdString();
        options.fileExists = [] (const std::string& path) { return platform::localFile (path).existsAsFile(); };
        options.readFile = [] (const std::string& path) {
            juce::MemoryBlock data;
            platform::localFile (path).loadFileAsData (data);
            const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
            return std::vector<std::uint8_t> (bytes, bytes + data.getSize());
        };
        options.sampleFrames = [this] (const std::string& path) -> std::int64_t {
            const auto sample = samples.load (juce::String::fromUTF8 (path.c_str()));
            return sample != nullptr ? (std::int64_t) sample->frames() : 0;
        };
        // WAV that only exists inside the .flp (Slicex): goes to %APPDATA%\LYNX\flp-embedded, keyed by content
        options.saveEmbedded = [] (const std::vector<std::uint8_t>& wav, const std::string& name) -> std::string {
            const auto folder = PluginHost::settingsFolder().getChildFile ("flp-embedded");
            folder.createDirectory();
            const auto hash = juce::String::toHexString ((juce::int64) juce::DefaultHashFunctions::generateHash (
                juce::String::createStringFromData (wav.data(), (int) juce::jmin<std::size_t> (wav.size(), 65536)) + juce::String ((juce::int64) wav.size()),
                0x7FFFFFFF));
            const auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (name.c_str())) + "-" + hash + ".wav");

            if (! file.existsAsFile() && ! file.replaceWithData (wav.data(), wav.size()))
                return {};

            return file.getFullPathName().toStdString();
        };

        importReport = {};
        options.report = &importReport;
        options.relocations = relocations;
        auto imported = importFlp (project, options);

        // the project's third-party plugins, to say which channel/insert used each one that is missing
        struct Use
        {
            std::string name, user;
            bool instrument;
        };
        std::vector<Use> uses;
        for (const auto& c : imported.channels)
            if (c.plugin.foreign.format != ForeignPlugin::Format::none)
                uses.push_back ({ c.plugin.foreign.name, c.name, true });
        for (std::size_t t = 0; t < imported.mixer.size(); ++t)
            for (const auto& fx : imported.mixer[t].effects)
                if (fx.kind == EffectKind::plugin && fx.plugin.foreign.format != ForeignPlugin::Format::none)
                    uses.push_back ({ fx.plugin.foreign.name, imported.mixer[t].name.empty() ? (t == 0 ? std::string ("Master") : "Insert " + std::to_string (t))
                                                                                            : imported.mixer[t].name,
                                      false });

        juce::StringArray missing;
        juce::StringArray matched;
        juce::StringArray withoutState;
        juce::StringArray substituted;
        plugins.adoptFlPlugins (imported, &missing, &matched, &withoutState, &substituted);
        missingPlugins = missing;
        matchedPlugins = matched;
        pluginsWithoutState = withoutState;
        missingPluginDetails.clear();
        for (const auto& name : missing)
        {
            MissingPlugin m;
            m.name = name.toStdString();
            for (const auto& u : uses)
                if (u.name == m.name)
                {
                    if (std::find (m.usedBy.begin(), m.usedBy.end(), u.user) == m.usedBy.end())
                        m.usedBy.push_back (u.user);
                    m.replacement = u.instrument ? "Built-in synth (a preset chosen by the channel name)" : "Nothing: the slot was left out of the mixer";
                }
            missingPluginDetails.push_back (std::move (m));
        }
        // missing, played by a LYNX substitute that keeps the original for when it is installed
        for (std::size_t t = 0; t < imported.mixer.size(); ++t)
            for (const auto& fx : imported.mixer[t].effects)
                if (fx.isSubstitute())
                {
                    const auto name = substituteOriginalName (fx);
                    auto it = std::find_if (missingPluginDetails.begin(), missingPluginDetails.end(),
                                            [&] (const MissingPlugin& m) { return m.name == name && ! m.replacementArg.empty(); });
                    if (it == missingPluginDetails.end())
                        it = missingPluginDetails.insert (missingPluginDetails.end(),
                                                          MissingPlugin { name, {}, TR_NOOP ("LYNX substitute: {0} (the original plugin is used again once it is installed)"),
                                                                          effectInfo (fx.kind).name });
                    const auto user = imported.mixer[t].name.empty() ? (t == 0 ? std::string ("Master") : "Insert " + std::to_string (t)) : imported.mixer[t].name;
                    if (std::find (it->usedBy.begin(), it->usedBy.end(), user) == it->usedBy.end())
                        it->usedBy.push_back (user);
                }
        // found, but the project's settings are in the other format (VST2 <-> VST3): they play their default sound
        for (const auto& line : withoutState)
            missingPluginDetails.push_back ({ line.toStdString(), {}, TR_NOOP ("Its default settings: the project saved them for the other format (VST2/VST3)") });

        plugins.clearLoadFailures();
        if (asEdit)
            editSong ([&imported] (Song& s) { s = std::move (imported); });
        else
        {
            stop();
            rewind();
            setSong (std::move (imported));
        }

        // installed but failed to load (the rack was just built): silence / bypass, said as loudly as a missing one
        for (const auto& line : plugins.getLoadFailures())
            missingPluginDetails.push_back ({ line.toStdString(), {}, TR_NOOP ("Silence: the plugin is installed but did not load") });
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
    {
        const auto count = (int) song.channels.size();
        selectedChannels.erase (std::remove_if (selectedChannels.begin(), selectedChannels.end(), [count] (int c) { return c >= count; }),
                                selectedChannels.end());
        if (std::find (selectedChannels.begin(), selectedChannels.end(), selectedChannel) == selectedChannels.end())
            selectedChannels.insert (std::lower_bound (selectedChannels.begin(), selectedChannels.end(), selectedChannel), selectedChannel);
    }
    publishSelection();
    transport.bpm.store (song.bpm);
    transport.beatsPerBar.store (song.beatsPerBar);

    {
        // time-signature table for the metronome: the start (beatsPerBar/4) and each signature change
        transport.signatureVersion.fetch_add (1, std::memory_order_acq_rel); // odd: writing
        int count = 0;
        transport.signatureTick[0].store (0, std::memory_order_relaxed);
        transport.signatureLength[0].store ((std::int64_t) ppq * juce::jmax (1, song.beatsPerBar), std::memory_order_relaxed);
        ++count;

        for (const auto& m : song.markers())
            if (m.isSignature() && count < Transport::maxSignatures)
            {
                const auto length = juce::jmax<std::int64_t> (1, (std::int64_t) ppq * 4 * m.numerator / juce::jmax (1, m.denominator));
                const auto slot = m.tick <= 0 ? 0 : count++; // at position 0 it replaces the default
                transport.signatureTick[(std::size_t) slot].store (juce::jmax<std::int64_t> (0, m.tick), std::memory_order_relaxed);
                transport.signatureLength[(std::size_t) slot].store (length, std::memory_order_relaxed);
            }

        transport.signatureCount.store (count, std::memory_order_relaxed);
        transport.signatureVersion.fetch_add (1, std::memory_order_acq_rel); // even: ready
    }
    transport.songMode.store (song.mode == PlayMode::song);
    transport.loopStart.store ((double) song.loopStart);
    transport.loopEnd.store ((double) song.loopEnd);

    RackState::Factories factories;
    factories.samples = [this] (const std::string& path) { return samples.load (juce::String::fromUTF8 (path.c_str())); };
    factories.stretchedSamples = [this] (const std::string& path, double ratio) {
        return samples.loadStretched (juce::String::fromUTF8 (path.c_str()), ratio);
    };
    factories.pluginInstruments = [this] (const PluginRef& ref, double sr) { return plugins.makeInstrument (ref, sr); };
    factories.pluginEffects = [this] (const PluginRef& ref, double sr) { return plugins.makeEffect (ref, sr); };

    auto state = RackState::build (song, &lastPublishedShape, transport.sampleRate.load(), factories);

    // what the next build needs to reuse instruments and effects
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

void AudioEngine::learnMidiController (const AutomationTarget& target)
{
    learning = target;
    sendChangeMessage();
}

void AudioEngine::applyControllers()
{
    ControllerEvent e;
    std::map<std::pair<int, int>, int> latest; // (cc, channel) -> latest value this frame

    while (controllerQueue.pop (e))
    {
        if (learning.kind != AutomationTargetKind::none)
        {
            // MIDI learn: replace the old link for the same target with the new one
            editSong ([t = learning, e] (Song& s) {
                s.midiLinks.erase (std::remove_if (s.midiLinks.begin(), s.midiLinks.end(), [&] (const MidiLink& l) { return l.target == t; }),
                                   s.midiLinks.end());
                s.midiLinks.push_back ({ e.cc, e.channel, t });
            });
            learning = {};
        }

        latest[{ e.cc, e.channel }] = e.value;
    }

    if (latest.empty() || song.midiLinks.empty())
        return;

    // one snapshot per frame, outside the undo history (like turning the knob)
    std::vector<std::pair<AutomationTarget, float>> changes;

    for (const auto& [key, value] : latest)
        for (const auto& link : song.midiLinks)
            if (link.cc == key.first && (link.channel < 0 || link.channel == key.second))
            {
                const auto v = value / 127.0f;
                const auto& t = link.target;
                // takeover: only parameters the song holds (a plugin's own parameters have no value to compare here)
                const auto songHeld = t.kind != AutomationTargetKind::channelParam && t.kind != AutomationTargetKind::effectParam;
                if (midiPrefs.pickup && songHeld)
                {
                    auto& state = pickups[{ key.first, key.second, (int) t.kind, t.index, t.slot, t.param }];
                    if (! prefs::pickupUpdate (state, song.targetValue (t), v))
                        continue;
                }
                changes.push_back ({ t, v });
            }

    std::vector<std::pair<AutomationTarget, float>> pluginChanges;

    editSong ([&] (Song& s) {
        for (const auto& [target, v] : changes)
            if (! s.setTargetValue (target, v))
                pluginChanges.push_back ({ target, v });
    }, false);

    // plugin parameters live in the plugin
    for (const auto& [target, v] : pluginChanges)
    {
        juce::AudioPluginInstance* plugin = nullptr;

        if (target.kind == AutomationTargetKind::channelParam)
            if (auto* p = dynamic_cast<PluginInstrument*> (getChannelInstrument (target.index).get()))
                plugin = &p->getInstance();

        if (target.kind == AutomationTargetKind::effectParam)
            if (auto* p = dynamic_cast<PluginEffect*> (getEffect (target.index, target.slot).get()))
                plugin = &p->getInstance();

        if (plugin != nullptr && target.param >= 0 && target.param < plugin->getParameters().size())
            plugin->getParameters()[target.param]->setValueNotifyingHost (v);
    }
}

void AudioEngine::timerCallback()
{
    updateRealtimeStats();
    applyControllers();
    updateRecording();
    rackExchange.collectGarbage();

    // Plugins are prepared at a fixed rate; if the device changed rate, recreate them.
    // A plugin whose latency changed (preset change, lookahead) requests a new PDC plan.
    const auto rateChanged = std::abs (transport.sampleRate.load() - lastPublishedShape.sampleRate) > 0.5;
    const auto latencies = lastPublishedShape.currentLatencies();
    if (rateChanged || latencies != lastPublishedShape.latencies)
    {
        // (diagnostics: which unit's latency moved)
        if (! rateChanged)
            for (std::size_t i = 0; i < latencies.size() && i < lastPublishedShape.latencies.size(); ++i)
                if (latencies[i] != lastPublishedShape.latencies[i])
                {
                    lastLatencyChange = juce::String ((int) i) + ": " + juce::String (lastPublishedShape.latencies[i]) + " -> " + juce::String (latencies[i]);
                    break;
                }
        ++latencyRepublishes;
        publishSong();
    }
}

void AudioEngine::chooseSafeBufferSize()
{
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;
    std::vector<int> sizes;
    for (auto b : device->getAvailableBufferSizes())
        sizes.push_back (b);
    const auto current = device->getCurrentBufferSizeSamples();
    const auto target = prefs::safeBufferSize (sizes, current, device->getCurrentSampleRate());
    if (target > 0 && target != current)
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = target;
        deviceManager.setAudioDeviceSetup (setup, true);
    }
}

juce::File AudioEngine::glitchLogFile() { return PluginHost::settingsFolder().getChildFile ("audio-glitches.log"); }

void AudioEngine::updateRealtimeStats()
{
    // the message thread's pulse: this 30 Hz timer arriving late means the UI is saturated
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (lastUiPulseMs > 0.0)
            uiPulse.maxGapMs = juce::jmax (uiPulse.maxGapMs, now - lastUiPulseMs);
        lastUiPulseMs = now;
        ++uiPulse.calls;
    }

    // every ~2 s: the share of real time each channel / insert effect took in that window
    if (rackProcessor != nullptr && juce::Time::getMillisecondCounterHiRes() - lastUnitsWindowMs >= 2000.0)
    {
        lastUnitsWindowMs = juce::Time::getMillisecondCounterHiRes();
        auto& renderer = rackProcessor->getRenderer();
        const auto& units = renderer.unitTimes();
        const auto processed = units.processedSamples.load (std::memory_order_relaxed);
        const auto rate = juce::jmax (1.0, transport.sampleRate.load());
        const auto audioNs = (double) (processed - std::min (processed, lastProcessedSamples)) / rate * 1.0e9;
        lastProcessedSamples = processed;
        lastChannelNs.resize (units.channelNs.size(), 0);
        lastEffectNs.resize (units.effectNs.size(), 0);
        std::vector<UnitLoad> loads;

        auto take = [&] (const std::atomic<std::uint64_t>& total, std::uint64_t& last, std::atomic<std::uint64_t>& longest, auto&& name) {
            const auto now = total.load (std::memory_order_relaxed);
            const auto delta = now - std::min (now, last);
            last = now;
            const auto longestNs = const_cast<std::atomic<std::uint64_t>&> (longest).exchange (0, std::memory_order_relaxed);
            if (delta > 0 && audioNs > 0.0)
                loads.push_back ({ name(), (double) delta / audioNs, (double) longestNs * 1.0e-6 });
        };
        auto& mutableUnits = const_cast<RackRenderer::UnitTimes&> (units);
        for (std::size_t c = 0; c < units.channelNs.size(); ++c)
            take (units.channelNs[c], lastChannelNs[c], mutableUnits.channelMaxNs[c], [&] {
                return c < song.channels.size() ? juce::String::fromUTF8 (song.channels[c].name.c_str()) : "Channel " + juce::String ((int) c + 1);
            });
        for (std::size_t k = 0; k < units.effectNs.size(); ++k)
            take (units.effectNs[k], lastEffectNs[k], mutableUnits.effectMaxNs[k], [&] {
                const auto t = k / (std::size_t) maxEffectSlots, slot = k % (std::size_t) maxEffectSlots;
                juce::String effect;
                if (t < song.mixer.size() && slot < song.mixer[t].effects.size())
                {
                    const auto& fx = song.mixer[t].effects[slot];
                    effect = juce::String::fromUTF8 (fx.plugin.name.empty() ? effectInfo (fx.kind).name : fx.plugin.name.c_str());
                }
                const auto track = t == 0 ? juce::String ("Master") : (t < song.mixer.size() && ! song.mixer[t].name.empty() ? juce::String::fromUTF8 (song.mixer[t].name.c_str())
                                                                                                               : "Insert " + juce::String ((int) t));
                return track + " / " + juce::String ((int) slot + 1) + " " + effect;
            });
        std::sort (loads.begin(), loads.end(), [] (const UnitLoad& a, const UnitLoad& b) { return a.load > b.load; });
        if (loads.size() > 8)
            loads.resize (8);
        if (audioNs > 0.0)
            heaviestUnits = std::move (loads);
    }

    // a line in the glitch log when callbacks ran over their deadline or the device called late
    const auto stats = rtStats.snapshot();
    if ((stats.overBudget > loggedOver || stats.lateCalls > loggedLate) && juce::Time::getMillisecondCounterHiRes() - lastLogMs > 2000.0)
    {
        const auto file = glitchLogFile();
        if (file.getSize() > 1024 * 1024)
            file.moveFileTo (file.withFileExtension ("old.log"));
        const auto bar = transport.positionTicks.load() / (double) (ppq * juce::jmax (1, song.beatsPerBar)) + 1.0;
        file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "  over deadline +" + juce::String ((juce::int64) (stats.overBudget - loggedOver))
                             + ", late calls +" + juce::String ((juce::int64) (stats.lateCalls - loggedLate)) + ", at bar " + juce::String (bar, 2)
                             + (transport.playing.load() ? "" : " (stopped)") + "\n" + describeRealtime() + "\n",
                         false, false, "\r\n");
        loggedOver = stats.overBudget;
        loggedLate = stats.lateCalls;
        lastLogMs = juce::Time::getMillisecondCounterHiRes();
    }
}

juce::String AudioEngine::describeRealtime (bool withUnits) const
{
    juce::String text;
    if (auto* device = const_cast<juce::AudioDeviceManager&> (deviceManager).getCurrentAudioDevice())
    {
        const auto rate = device->getCurrentSampleRate();
        const auto buffer = device->getCurrentBufferSizeSamples();
        text << "  device: " << device->getTypeName() << " / " << device->getName() << ", " << (int) rate << " Hz, buffer " << buffer
             << " samples (deadline " << juce::String (prefs::bufferLatencyMs (buffer, rate), 2) << " ms), output latency "
             << device->getOutputLatencyInSamples() << " samples, driver dropouts " << device->getXRunCount() << "\n";
    }
    else
        text << "  device: none\n";
    const auto s = rtStats.snapshot();
    text << "  callbacks " << (juce::int64) s.callbacks << ": over deadline " << (juce::int64) s.overBudget << ", 80-100% " << (juce::int64) s.nearBudget
         << ", late device calls " << (juce::int64) s.lateCalls << ", average load " << juce::String (s.averageLoad * 100.0, 1) << "%, max "
         << juce::String (s.maxLoad * 100.0, 0) << "%, workers " << (workers != nullptr ? workers->workers() : 0) << "\n";
    if (latencyRepublishes > 0)
        text << "  latency changes re-planned PDC " << latencyRepublishes << " times (last: unit " << lastLatencyChange << ")\n";
    if (withUnits && heaviestUnits.empty())
        text << "  heaviest: not measured yet\n";
    if (withUnits && ! heaviestUnits.empty())
    {
        text << "  heaviest:";
        for (std::size_t i = 0; i < heaviestUnits.size() && i < 5; ++i)
            text << (i > 0 ? ";" : "") << " " << heaviestUnits[i].name << " " << juce::String (heaviestUnits[i].load * 100.0, 1) << "% (peak "
                 << juce::String (heaviestUnits[i].longestMs, 1) << " ms)";
        text << "\n";
    }
    return text;
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
    // Changes only the saved state (recreates nothing and doesn't enter the undo history).
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
    // real-time health: how long this callback took against its deadline (the buffer's duration)
    const auto callbackStart = juce::Time::getMillisecondCounterHiRes();
    struct Measure
    {
        ~Measure()
        {
            const auto rate = engine.transport.sampleRate.load (std::memory_order_relaxed);
            if (rate > 0.0)
                engine.rtStats.record (start, juce::Time::getMillisecondCounterHiRes() - start, 1000.0 * samples / rate);
        }
        AudioEngine& engine;
        double start;
        int samples;
    } measure { *this, callbackStart, numSamples };

    if (transport.rewindRequested.exchange (false))
        transport.positionTicks.store (0.0);

    if (const auto target = transport.seekRequest.exchange (-1.0); target >= 0.0)
        transport.positionTicks.store (target);

    // With the loop region on, the block is split at the loop end and the position jumps back to
    // its start (the nodes see a jump and release their notes, as in FL).
    // the audio editor's input recording (nothing happens unless it chose the input)
    meters.capture.push (AudioCapture::input, numInputChannels > 0 ? inputChannelData[0] : nullptr, numInputChannels > 1 ? inputChannelData[1] : nullptr,
                         numSamples);

    std::array<const float*, 64> inputs {};
    std::array<float*, 64> outputs {};
    numInputChannels = juce::jmin (numInputChannels, (int) inputs.size());
    numOutputChannels = juce::jmin (numOutputChannels, (int) outputs.size());

    // count-in: the metronome counts the pre-roll down, then playback starts
    if (const auto countIn = transport.countIn.load (std::memory_order_relaxed); countIn > 0.0 && ! transport.playing.load (std::memory_order_relaxed))
    {
        const auto left = countIn - numSamples * transport.ticksPerSample();
        transport.countIn.store (juce::jmax (0.0, left), std::memory_order_relaxed);

        if (left <= 0.0)
            transport.playing.store (true, std::memory_order_relaxed);
    }

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
            // before the loop, play normally until reaching it; past the end, jump back to the start
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

        // the block's input by device channel, for the inserts (monitoring, meters) and the takes
        liveInput.numChannels = 0;
        for (int c = 0; c < LiveInput::maxChannels; ++c)
        {
            const auto slot = inputSlot[(std::size_t) c];
            liveInput.channels[(std::size_t) c] = slot >= 0 && slot < numInputChannels ? inputs[(std::size_t) slot] : nullptr;
            if (liveInput.channels[(std::size_t) c] != nullptr)
                liveInput.numChannels = c + 1;
        }

        // audio recording: this block's input goes to disk (without waiting on a lock)
        if (playing && transport.recording.load (std::memory_order_relaxed))
        {
            const juce::SpinLock::ScopedTryLockType lock (takeLock);

            if (lock.isLocked() && takeCount > 0)
            {
                auto wrote = false;

                for (int k = 0; k < takeCount; ++k)
                {
                    auto& take = takes[(std::size_t) k];
                    const std::array<const float*, 2> data { liveInput.channel (take.first, 0), liveInput.channel (take.first + take.channels - 1, 0) };

                    if (data[0] == nullptr || data[1] == nullptr || take.writer == nullptr)
                        continue;

                    wrote = take.writer->write (data.data(), n) || wrote;
                }

                if (wrote)
                {
                    if (takeStartTick.load (std::memory_order_relaxed) < 0.0)
                        takeStartTick.store (transport.positionTicks.load (std::memory_order_relaxed));
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

        // oscilloscope: average of both sides into the ring buffer
        if (numOutputChannels > 0 && outputs[0] != nullptr)
        {
            auto w = scopeWrite.load (std::memory_order_relaxed);
            const auto* right = numOutputChannels > 1 && outputs[1] != nullptr ? outputs[1] : outputs[0];

            for (int i = 0; i < n; ++i)
            {
                scope[(std::size_t) w].store (0.5f * (outputs[0][i] + right[i]), std::memory_order_relaxed);
                w = (w + 1) % scopeSize;
            }

            scopeWrite.store (w, std::memory_order_release);
        }

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
    rtStats.deviceRestarted();
    transport.sampleRate.store (device->getCurrentSampleRate());

    // the callback gets the open input channels only, in order: map device channel -> callback index
    const auto active = device->getActiveInputChannels();
    int slot = 0;
    for (int c = 0; c < LiveInput::maxChannels; ++c)
        inputSlot[(std::size_t) c] = active[c] ? slot++ : -1;
    player.audioDeviceAboutToStart (device);
}

void AudioEngine::audioDeviceStopped()
{
    player.audioDeviceStopped();
}
} // namespace lynx
