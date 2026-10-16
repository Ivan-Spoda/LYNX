#pragma once

// Ties Song + instruments + scheduler together: given the transport state, renders
// a block of stereo audio with notes triggered at the exact sample.

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/Arpeggiator.h"
#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/Instruments.h"
#include "core/LiveEventQueue.h"
#include "core/Meters.h"
#include "core/Pdc.h"
#include "core/Scheduler.h"
#include "core/Song.h"

namespace lynx
{
// Immutable snapshot published by the UI. Instruments are shared between
// consecutive snapshots so voices are not cut off when the song is edited.
struct RackState
{
    Song song;
    std::vector<std::shared_ptr<Instrument>> instruments; // one per channel
    std::vector<std::vector<std::shared_ptr<Effect>>> effects; // [mixer track][slot]
    double sampleRate = 44100.0; // rate the new instruments were prepared with

    // Automation, preprocessed so the audio thread only does table lookups.
    // Indices into song.automations, or -1 = no automation.
    static constexpr int maxAutomations = 1024;
    std::vector<std::vector<int>> automationClips; // per automation: clips in the Playlist
    std::vector<int> channelVolumeAuto, channelPanAuto;
    std::vector<int> mixerVolumeAuto, mixerPanAuto;
    std::vector<std::vector<std::array<int, maxEffectParams>>> effectParamAuto; // [track][slot][param] (built-in)
    std::vector<std::vector<int>> effectMixAuto;                                 // [track][slot]: dry/wet
    std::vector<int> liveAutomations;                                            // automations with a live source (controllers)

    // pattern events (event editor): each lane uses an automation slot after the song's automations
    struct EventLane
    {
        int pattern = 0, lane = 0, slot = 0;
    };
    std::vector<EventLane> eventLanes;
    std::vector<std::vector<int>> patternClips; // per pattern: its clips in the Playlist, by start

    struct PluginParamBinding
    {
        int automation = 0;
        int channel = -1;       // channel instrument, or
        int track = 0, slot = 0; // effect in the slot
        int param = 0;
    };
    std::vector<PluginParamBinding> pluginParams;
    bool hasAutomation = false;
    ScheduleIndex schedule; // index to schedule the Playlist without scanning every note
    int tempoAutomation = -1; // tempo automation, if any

    // PDC: per-edge delays (nullptr = no delay) and the latencies used to build the plan.
    PdcPlan pdc;
    std::vector<std::shared_ptr<DelayLine>> channelDelays;
    std::vector<std::vector<std::shared_ptr<DelayLine>>> sendDelays; // [track][k] like PdcPlan::sendDelay
    std::vector<int> latencies; // channels, then slots of all tracks

    // Current latencies of instruments and effects, in the same layout as `latencies`.
    std::vector<int> currentLatencies() const;
    void buildPdc (const RackState* previous);

    // Arpeggiators of channels with arpeggio on (nullptr for the others); reused across snapshots.
    std::vector<std::shared_ptr<Arpeggiator>> arpeggiators;

    // Cut groups: channels each channel cuts when it plays (cutBy == its cutGroup).
    std::vector<std::vector<int>> cutTargets;
    bool hasCutGroups = false;

    // Mixer: insert processing order and which ones sound with the current solo.
    std::vector<int> mixerOrder;
    std::vector<char> mixerAudible;

    // "Pull" model: each track sums the (post-fader) outputs of the tracks that send to it.
    struct MixerInput
    {
        int source = 0;
        int delay = 0; // index into sendDelays[source]
        float level = 1.0f;
    };
    std::vector<std::vector<MixerInput>> mixerInputs; // [track]; [0] = Master
    std::vector<std::vector<int>> mixerLayers;        // inserts with no dependencies among them, in order
    bool parallel = false;                            // worth splitting across threads
    void buildMixerGraph();

    void buildAutomationTables();

    // Loads (or fetches from cache) a file's audio; nullptr on failure.
    using SampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path)>;

    // The app creates plugin instruments/effects (the core does not know JUCE). nullptr = failed
    // (missing plugin): the channel goes silent / the slot becomes bypass.
    using PluginInstrumentFactory = std::function<std::shared_ptr<Instrument> (const PluginRef&, double sampleRate)>;
    using PluginEffectFactory = std::function<std::shared_ptr<Effect> (const PluginRef&, double sampleRate)>;

    // Sample stretched without changing pitch (ratio = new / original duration); cached in the app.
    using StretchedSampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path, double ratio)>;

    struct Factories
    {
        SampleProvider samples;
        StretchedSampleProvider stretchedSamples;
        PluginInstrumentFactory pluginInstruments;
        PluginEffectFactory pluginEffects;
    };

    // Builds the next state reusing the instruments from `previous` when the
    // channel changed neither instrument nor sample, and the effects when the slot has the
    // same type. Runs on the UI thread (allocates).
    static std::unique_ptr<const RackState> build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const SampleProvider& samples = {});
    static std::unique_ptr<const RackState> build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const Factories& factories);
};

class WorkerPool;

class RackRenderer
{
public:
    // Extra threads for channels and mixer layers (nullptr = everything on the audio thread).
    void setWorkerPool (WorkerPool* pool) noexcept { workers = pool; }

    // Allocates the mixer buffers: call off the audio thread.
    // Blocks larger than maxBlockSize are processed in pieces.
    void prepare (double sampleRate, int maxBlockSize = 2048);

    // Audio thread. left/right are overwritten. `live` (optional) carries notes
    // played live, applied at the start of the block.
    void process (const RackState* state,
                  bool playing,
                  double startTick,
                  int numSamples,
                  float* left,
                  float* right,
                  LiveEventQueue* live = nullptr,
                  MeterBank* meters = nullptr,
                  LiveEventQueue* moreLive = nullptr) noexcept;

    static void applyStereoForTest (const MixerTrack& t, float* l, float* r, int n) noexcept { applyStereo (t, l, r, n); }

    // Tempo used in the last block (changes with tempo automation). The transport advances with it.
    double currentBpm() const noexcept { return bpm; }

    // Events of the last block (for tests and diagnostics).
    const EventBuffer& lastEvents() const noexcept { return events; }

private:
    void processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                       float* left, float* right, MeterBank* meters) noexcept;
    void renderChannel (const RackState& state, int channel, int n) noexcept;
    void processTrack (const RackState& state, int track, int n, MeterBank* meters) noexcept;
    void pullInputs (const RackState& state, int track, int n) noexcept;
    static void applyStereo (const MixerTrack& track, float* l, float* r, int n) noexcept;
    float* channelBuffer (int channel, int side) noexcept;
    float* trackScratch (int track, int side) noexcept;
    void runEffects (const RackState& state, int track, int n) noexcept;
    void setSidechain (const RackState& state, const EffectSlot& slot, Effect& effect, int track) noexcept;
    void updateAutomation (const RackState& state, double startTick) noexcept;
    void applyPluginParams (const RackState& state, int firstSlot) noexcept; // bindings from this automation slot on
    float automated (int automation, float base) const noexcept;
    float* trackBuffer (int track, int channel) noexcept;

    double sampleRate = 44100.0;
    double bpm = 120.0;
    bool wasPlaying = false;
    double expectedTick = -1.0; // where the next block should start, to detect jumps
    bool resumeAudio = false;
    const RackState* lastState = nullptr;
    EventBuffer events;
    std::vector<float> mixBuffers; // maxMixerTracks * 2 * blockCapacity
    std::array<float, RackState::maxAutomations> automationValues {}; // NaN = not evaluated yet
    std::array<float, maxMixerTracks * 2> lastTrackGain {}; // L/R gain of the previous block, for the ramp
    std::array<char, maxMixerTracks> trackActive {};        // the track output has sound in this block
    static constexpr int maxChannels = 1024;
    std::array<char, maxChannels> channelSilent {};          // channel skipped in this chunk (idle, no notes)
    static constexpr int chunkCapacity = 512;               // chunks processed at once
    std::vector<float> channelBuffers;                      // maxChannels * 2 * chunkCapacity
    std::vector<float> trackScratchBuffers;                 // maxMixerTracks * 2 * chunkCapacity
    double chunkStartTick = 0.0, chunkTps = 1.0;            // of the chunk being rendered (for the arpeggiator)
    WorkerPool* workers = nullptr;

    struct SmartDisableState
    {
        const Effect* effect = nullptr;
        int silentSamples = 0;
    };
    std::vector<SmartDisableState> smartDisable; // [track * maxEffectSlots + slot]

    struct TrackEqState
    {
        TrackEq settings;
        std::array<Biquad, 3> bands;
        bool running = false;
    };
    std::vector<TrackEqState> trackEqs; // built-in EQ of each track
    double chunkTick = 0.0;             // start of the block (for the global LFOs)
    bool chunkPlaying = false;          // the song plays during the block (effects that follow the transport)
    void applyTrackEq (const MixerTrack& track, int t, float* l, float* r, int n) noexcept;
    static constexpr float silenceLevel = 1.5849e-5f;  // -96 dBFS
    int blockCapacity = 0;
};
} // namespace lynx
