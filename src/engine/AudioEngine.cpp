#include "engine/AudioEngine.h"
#include "engine/FlStudio.h"

#include <map>

#include "engine/Text.h"

#include "core/FlpImport.h"
#include "core/Instruments.h"
#include "core/Dither.h"
#include "core/SoftClip.h"
#include "engine/ChannelRackProcessor.h"
#include "engine/Metronome.h"
#include "core/NoteTools.h"
#include "engine/PreviewPlayer.h"
#include "flp/Flp.h"

#if JUCE_WINDOWS
 #include <windows.h>
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
// Audio workers with real-time priority (MMCSS "Pro Audio" on Windows, like the driver).
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
    // diagnostics: LYNX_WORKERS=n fixes the number of extra threads
    if (const auto forced = juce::SystemStats::getEnvironmentVariable ("LYNX_WORKERS", {}); forced.isNotEmpty())
        return juce::jlimit (0, 31, forced.getIntValue());

    // the audio thread works too; leave one core free for the UI
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

        // 0 inputs, 2 outputs, no saved state yet, use the default device.
        deviceManager.initialise (0, 2, nullptr, true);
        deviceManager.addAudioCallback (this);

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
        takeWriter = nullptr; // closes the WAV of the take in progress (it stays in the recordings folder)
    }

    recordThread.stopThread (2000);
    prefetcher = nullptr;
    midiDevicesChanged = {};
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
        if (output != nullptr && done + n > trim)
        {
            const auto skip = (int) juce::jmax<std::int64_t> (0, trim - done);
            const auto beforeWrite = juce::Time::getMillisecondCounterHiRes();
            output->writeFromAudioSampleBuffer (view, skip, n - skip);
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

void AudioEngine::previewSample (std::shared_ptr<const SampleData> sample)
{
    if (preview != nullptr)
        preview->play (std::move (sample));
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
    const auto wantTake = audioArmed && transport.recording.load();

    // the file is ready before Play: the very first block gets recorded
    if (wantTake && takeWriter == nullptr && recordingError.isEmpty())
        startAudioTake();

    // playback stopped (or recording turned off) with something recorded: close the take
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
        recordingError = utf8 ("no active audio input");
        return;
    }

    const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Recordings");
    folder.createDirectory();
    takeFile = folder.getNonexistentChildFile ("Take " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav");
    takeSampleRate = device->getCurrentSampleRate();
    takeLatency = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();

    juce::WavAudioFormat wav;
    auto stream = takeFile.createOutputStream();

    if (stream == nullptr)
    {
        recordingError = utf8 ("could not create ") + takeFile.getFullPathName();
        return;
    }

    auto* writer = wav.createWriterFor (stream.get(), takeSampleRate, (unsigned int) takeChannels, 24, {}, 0);

    if (writer == nullptr)
    {
        stream.reset();
        takeFile.deleteFile();
        recordingError = utf8 ("WAV does not accept this sample rate/channel count");
        return;
    }

    stream.release(); // now owned by the writer
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
    done.reset(); // drains the queue and closes the WAV

    const auto frames = takeFrames.exchange (0);
    const auto startTick = takeStartTick.exchange (-1.0);

    if (frames <= 0 || startTick < 0.0)
    {
        takeFile.deleteFile();
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

    const auto path = takeFile.getFullPathName().toStdString();
    const auto name = takeFile.getFileNameWithoutExtension().toStdString();
    const auto armedTrack = armedInserts.empty() ? 0 : armedInserts.front(); // the take plays through the armed insert

    editSong ([=] (Song& s) {
        ChannelDef c;
        c.name = name;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = path;
        c.mixerTrack = armedTrack > 0 && armedTrack < (int) s.mixer.size() ? armedTrack : s.nextFreeInsert();
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
    // MIDI thread: queues and atomics only
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

        // the primary selected channel, then the other selected ones
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
        push (targets[i], on, on ? m.getFloatVelocity() : 0.0f);
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
        // opens the default input (stereo if possible) on the same device/driver type
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
        options.fileExists = [] (const std::string& path) { return juce::File (juce::String::fromUTF8 (path.c_str())).existsAsFile(); };
        options.readFile = [] (const std::string& path) {
            juce::MemoryBlock data;
            juce::File (juce::String::fromUTF8 (path.c_str())).loadFileAsData (data);
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
        plugins.adoptFlPlugins (imported, &missing, &matched);
        missingPlugins = missing;
        matchedPlugins = matched;
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

        if (asEdit)
        {
            editSong ([&imported] (Song& s) { s = std::move (imported); });
            return true;
        }

        stop();
        rewind();
        setSong (std::move (imported));
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
                changes.push_back ({ link.target, value / 127.0f });

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
    applyControllers();
    updateRecording();
    rackExchange.collectGarbage();

    // Plugins are prepared at a fixed rate; if the device changed rate, recreate them.
    // A plugin whose latency changed (preset change, lookahead) requests a new PDC plan.
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
    if (transport.rewindRequested.exchange (false))
        transport.positionTicks.store (0.0);

    if (const auto target = transport.seekRequest.exchange (-1.0); target >= 0.0)
        transport.positionTicks.store (target);

    // With the loop region on, the block is split at the loop end and the position jumps back to
    // its start (the nodes see a jump and release their notes, as in FL).
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

        // audio recording: this block's input goes to disk (without waiting on a lock)
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
    transport.sampleRate.store (device->getCurrentSampleRate());
    player.audioDeviceAboutToStart (device);
}

void AudioEngine::audioDeviceStopped()
{
    player.audioDeviceStopped();
}
} // namespace lynx
