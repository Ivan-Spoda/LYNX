#pragma once

#include <deque>
#include <map>
#include <tuple>
#include <functional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "core/HeldNotes.h"
#include "core/Preferences.h"
#include "core/LiveEventQueue.h"
#include "core/Recorder.h"
#include "core/WorkerPool.h"
#include "core/RackRenderer.h"
#include "core/RtStats.h"
#include "core/FlpImport.h"
#include "core/History.h"
#include "core/SnapshotExchange.h"
#include "core/Song.h"
#include "engine/PluginHost.h"
#include "engine/SampleLibrary.h"
#include "engine/StreamPrefetcher.h"
#include "engine/Transport.h"

namespace lynx
{
// Owns the audio device, the processing graph and the document (Song).
// Sits between the driver and the AudioProcessorPlayer to advance the transport
// after each block, so every node sees the same start position.
//
// The Song is touched only by the UI thread; each edit publishes an immutable
// snapshot (RackState) to the audio thread.
// Folders of the most recent FL Studio installation (empty if none).
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

    // Third-party plugins from the last imported .flp that were not found among the scanned ones.
    const juce::StringArray& getMissingPlugins() const noexcept { return missingPlugins; }
    // "plugin → file (how it was found)" for the plugins of the last imported .flp
    const juce::StringArray& getMatchedPlugins() const noexcept { return matchedPlugins; }
    // What the last imported .flp converted, approximated or left out.
    const FlpImportReport& getImportReport() const noexcept { return importReport; }
    // The last imported .flp's plugins that were not found, with what uses them and what LYNX put in their place.
    const std::vector<MissingPlugin>& getMissingPluginDetails() const noexcept { return missingPluginDetails; }

    // Live instances (to open the plugin window). nullptr if not a plugin.
    std::shared_ptr<Instrument> getChannelInstrument (int channel) const;
    std::shared_ptr<Effect> getEffect (int track, int slot) const;

    // Copies the plugins' current state into the Song (before saving).
    void capturePluginStates();

    void play() noexcept;
    void stop() noexcept; // also cancels a count-in or a wait for the first note

    // Recording options (docs/WORKFLOW_FL.md P2: recording set). UI thread.
    struct RecordOptions
    {
        int countInBars = 0;            // 0 = off
        bool waitForInput = false;      // playback starts at the first played note
        RecordMode mode = RecordMode::overdub;
        std::int64_t quantise = 0;      // record quantise in ticks (<= 1: as played); follows the global grid
        bool placeSongClip = true;      // song mode: a take puts a clip of the pattern where recording started
    };
    const RecordOptions& getRecordOptions() const noexcept { return recordOptions; }
    void setRecordOptions (const RecordOptions& options);

    // Play pressed: with recording on, honours wait-for-input and the count-in; otherwise play().
    void startPlayback();
    bool isWaitingForInput() const noexcept { return waitingForInput.load(); }
    bool isCountingIn() const noexcept { return transport.countIn.load() > 0.0; }

    // Per-insert record arm: an armed insert receives the audio takes (arming one turns audio recording on;
    // disarming the last one turns it off). Not saved with the project.
    void setInsertArmed (int track, bool armed);
    bool isInsertArmed (int track) const noexcept;
    const std::vector<int>& getArmedInserts() const noexcept { return armedInserts; }
    void rewind() noexcept;
    void seek (double ticks) noexcept;

    // Preview of an audio file (browser). An invalid/empty file stops the preview.
    void previewFile (const juce::File& file);
    // Plays a sound made in memory through the same preview output (the start-up sound). UI thread.
    void previewSample (std::shared_ptr<const SampleData> sample);

    // Note played live (PC keyboard, preview). UI thread.
    // `record` = goes into the recording if recording is on (Piano Roll preview doesn't).
    void sendLiveNote (int channel, int key, float velocity, bool on, bool record = false);

    // Scale snap for played notes (MIDI input and the PC keyboard): scale index into notetools::scales()
    // (0 = off) and its root (0..11). Any thread.
    void setInputScale (int scale, int root) noexcept;
    int snapInputKey (int key) const noexcept;

    // All notes off on every channel (double-click on Stop). UI thread.
    void panic();

    // Note recording (MIDI and PC keyboard) into the current pattern while playing. UI thread.
    void setRecording (bool shouldRecord);
    bool isRecording() const noexcept { return transport.recording.load(); }

    // Audio recording from the interface input: with this on, recording (Rec + Play) writes the
    // input to a WAV and, on stop, the take becomes a Playlist clip (latency-compensated).
    // Enabling it opens the device input (only then does Windows show the mic as in use).
    void setAudioRecordingArmed (bool armed);
    bool isAudioRecordingArmed() const noexcept { return audioArmed; }
    juce::String getLastRecordingError() const { return recordingError; }

    // MIDI learn: the next CC received becomes a controller linked to `target`.
    void learnMidiController (const AutomationTarget& target);
    bool isLearningMidi() const noexcept { return learning.kind != AutomationTargetKind::none; }

    // MIDI inputs: every device is enabled and plays the selected channels unless the MIDI settings say otherwise
    // (Settings › MIDI: device off, one MIDI channel only, a fixed Channel Rack channel; velocity curve; pickup).
    juce::StringArray getActiveMidiInputs() const;
    const prefs::MidiPrefs& getMidiPrefs() const noexcept { return midiPrefs; }
    // Applies them (and saves them in %APPDATA%\LYNX\midi-settings.txt when `save`).
    void setMidiPrefs (const prefs::MidiPrefs& prefs, bool save = true);

    // How many undo steps are kept (Settings › General).
    void setUndoLevels (int levels);
    int getUndoLevels() const noexcept { return (int) maxUndoSteps; }

    // Document (UI thread)
    const Song& getSong() const noexcept { return song; }
    void setSong (Song newSong); // new/opened project: clears the history

    // Edits the song and publishes it to audio. `undoable` edits go into the history;
    // all edits made while the same mouse click is held down
    // (dragging a note, turning a knob) become a single step.
    void editSong (const std::function<void (Song&)>& edit, bool undoable = true);

    // Changes since the last save/open (for the "*" in the title and the "save changes?" prompt).
    bool isModified() const noexcept { return modified; }
    void markSaved() noexcept { modified = false; sendChangeMessage(); }

    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    // Undo history as a list of states, oldest first: the undo steps, the current state, then the redo
    // steps. Each entry is named after what changed from the previous one (core/History.h).
    int getHistorySize() const noexcept { return (int) (undoStack.size() + 1 + redoStack.size()); }
    int getHistoryPosition() const noexcept { return (int) undoStack.size(); }
    ChangeLabel getHistoryLabel (int index) const; // index 0 = the oldest state kept
    void jumpToHistory (int index);

    // Channel selected in the Channel Rack (target of the Piano Roll and keyboard). UI thread.
    int getSelectedChannel() const noexcept { return selectedChannel; }
    void setSelectedChannel (int channel); // selects only this channel

    // Multi-selection in the Channel Rack (sorted). The selected channels all receive live MIDI input;
    // `primary` (one of them) is the Piano Roll / keyboard target above.
    const std::vector<int>& getSelectedChannels() const noexcept { return selectedChannels; }
    void setChannelSelection (std::vector<int> channels, int primary);

    // Imports a .flp, locating the FL installation to find the samples.
    // Returns false and fills `error` on failure. `relocations` (saved sample path → file to use) are the samples
    // the user located; with `asEdit` the result replaces the song as one undoable step instead of a new project.
    bool importFlpFile (const juce::File& file, juce::String& error, const std::map<std::string, std::string>& relocations = {},
                        bool asEdit = false);

    // Renders without an audio device. Writes to `output` if not nullptr.
    // After `seconds`, continues for up to `tailSeconds` while there is still sound.
    // `progress(0..1)` returns false to cancel. Returns the peak (used by the self-test),
    // or -1 if cancelled.
    struct RenderOptions
    {
        double seconds = 10.0;
        double sampleRate = 44100.0;
        juce::AudioFormatWriter* output = nullptr;
        std::function<bool (double)> progress; // 0..1; false cancels
        double tailSeconds = 0.0;
        int trimSamples = 0;  // PDC: discards this much from the start and renders the same amount extra at the end
        int ditherBits = 0;   // > 0: TPDF dither (with noise shaping unless ditherShaping is false) for this bit depth
        bool ditherShaping = true;
        // receives every rendered block after the PDC trim (left, right, frames), like `output` does
        std::function<void (const float*, const float*, int)> capture;
    };

    float renderOffline (const RenderOptions& options);

    // timings of the last renderOffline (diagnostics): the audio graph and the file writing
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

    // Time spent in each instrument and effect (the Plugin performance monitor); nullptr without the rack node.
    ProcessTimes* getProcessTimes() noexcept;

    // Latest output samples (mono), for the oscilloscope. UI thread.
    void copyScope (float* destination, int count) const noexcept;
    static constexpr int scopeSize = 2048;

    // Real-time health of the audio callback (REA round 87): callbacks over their deadline, late device calls, load.
    const RtStats& getRtStats() const noexcept { return rtStats; }
    RtStats& getRtStats() noexcept { return rtStats; }

    // What takes the most audio CPU: channels and insert effects, as a share of real time over the last ~2 s of
    // playback, heaviest first. UI thread.
    struct UnitLoad
    {
        juce::String name;
        double load = 0.0;      // CPU time / audio time (summed over the worker threads)
        double longestMs = 0.0; // the longest single call in the window
    };
    const std::vector<UnitLoad>& getHeaviestUnits() const noexcept { return heaviestUnits; }

    // How regularly the message thread runs the engine's 30 Hz timer (a saturated UI delays it). UI thread.
    struct UiPulse
    {
        int calls = 0;
        double maxGapMs = 0.0;
    };
    UiPulse takeUiPulse() noexcept { return std::exchange (uiPulse, {}); }

    // Device, buffer, deadline and the callback statistics in a few lines (CPU meter hint, glitch log, --rtstress).
    juce::String describeRealtime (bool withUnits = true) const;

    // %APPDATA%\LYNX\audio-glitches.log: a line each time callbacks run over their deadline (at most every 2 s).
    static juce::File glitchLogFile();

    // Total latency compensated by PDC (at the Master output), in samples.
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
    void saveAudioDeviceState();
    void updateRecording();
    void updateAudioRecording();
    void startAudioTake();
    void finishAudioTake();
    double estimatedPositionNow() const noexcept;

    void buildInitialGraph();
    void chooseSafeBufferSize();
    void updateRealtimeStats(); // timer: unit loads over a window, glitch log
    RtStats rtStats;
    std::vector<UnitLoad> heaviestUnits;
    std::vector<std::uint64_t> lastChannelNs, lastEffectNs;
    std::uint64_t lastProcessedSamples = 0;
    double lastUnitsWindowMs = 0.0, lastUiPulseMs = 0.0;
    UiPulse uiPulse;
    int latencyRepublishes = 0;   // PDC re-plans because a unit's latency changed (diagnostics)
    juce::String lastLatencyChange;
    std::uint64_t loggedOver = 0, loggedLate = 0;
    double lastLogMs = 0.0;
    class PreviewPlayer* preview = nullptr; // graph node (owned by the graph)
    void publishSong();

    std::unique_ptr<WorkerPool> workers; // extra renderer threads (before the graph, which uses them)
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    Transport transport;
    SampleLibrary samples;
    LiveEventQueue liveEvents;   // UI -> audio
    LiveEventQueue midiEvents;   // MIDI thread -> audio
    InputHeldNotes midiHeld;     // MIDI thread: what each held MIDI key started
    KeyLedger playedKeys;        // UI thread: the keys the typing piano started (after the scale snap)
    SpscQueue<RecordedEvent, 1024> midiRecordQueue; // MIDI thread -> UI (recording)

    struct ControllerEvent
    {
        int cc = 0, channel = 0, value = 0;
    };
    SpscQueue<ControllerEvent, 1024> controllerQueue; // MIDI thread -> UI (controllers)
    AutomationTarget learning;
    void applyControllers();
    std::vector<RecordedEvent> uiRecorded;           // PC keyboard -> recording
    NoteRecorder recorder;
    bool takeHasUndoStep = false;
    RecordOptions recordOptions;
    std::vector<int> takeClearedChannels; // replace mode: channels already cleared in this take
    double takeStartPosition = 0.0;
    std::atomic<bool> waitingForInput { false };
    void startOnInput() noexcept; // MIDI or UI thread: the first note while waiting starts playback
    std::vector<int> armedInserts;
    double lastPlayingTick = 0.0;
    std::atomic<int> inputScale { 0 }, inputScaleRoot { 0 };
    std::atomic<int> liveChannel { 0 };              // selected channel, read by the MIDI thread
    static constexpr int maxLiveChannels = 16;       // other selected channels that also get MIDI input
    std::array<std::atomic<int>, maxLiveChannels> extraLiveChannels {};
    std::atomic<int> extraLiveCount { 0 };
    void publishSelection();
    std::atomic<double> blockTimeMs { 0.0 };         // when positionTicks was last updated
    std::array<std::atomic<float>, scopeSize> scope {}; // ring buffer of the output (written by audio)
    std::atomic<int> scopeWrite { 0 };
    juce::MidiDeviceListConnection midiDevicesChanged;
    prefs::MidiPrefs midiPrefs;                      // UI thread
    juce::SpinLock midiPrefsLock;                    // guards midiPrefsShared (UI writes, MIDI thread reads)
    prefs::MidiPrefs midiPrefsShared;
    std::map<std::tuple<int, int, int, int, int, int>, prefs::PickupState> pickups; // (cc, channel, target) -> takeover
    struct DeviceStateSaver;
    std::unique_ptr<DeviceStateSaver> deviceStateSaver;

    // audio recording
    bool audioArmed = false;
    juce::String recordingError;
    juce::TimeSliceThread recordThread { "LYNX recording" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> takeWriter;
    juce::SpinLock takeLock;                       // the audio thread only tries (never waits)
    juce::File takeFile;
    int takeChannels = 0;
    std::atomic<double> takeStartTick { -1.0 };    // position of the first recorded sample
    std::atomic<juce::int64> takeFrames { 0 };
    double takeSampleRate = 44100.0;
    int takeLatency = 0;                           // input + output, in samples
    MeterBank meters;
    PluginHost plugins;
    juce::StringArray missingPlugins, matchedPlugins;
    FlpImportReport importReport;
    std::vector<MissingPlugin> missingPluginDetails;
    RenderStats lastRenderStats;
    class ChannelRackProcessor* rackProcessor = nullptr; // graph node (owner: the graph)

    Song song;
    int selectedChannel = 0;
    std::vector<int> selectedChannels { 0 };
    bool modified = false;
    std::unique_ptr<StreamPrefetcher> prefetcher; // only with an audio device (playing live)

    std::size_t maxUndoSteps = prefs::defaultUndoLevels;
    std::deque<Song> undoStack, redoStack;
    juce::Time lastUndoPress; // mouse click that already produced an undo step
    SnapshotExchange<RackState> rackExchange;
    RackState lastPublishedShape; // channels + instruments of the last snapshot, to reuse voices

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
} // namespace lynx
