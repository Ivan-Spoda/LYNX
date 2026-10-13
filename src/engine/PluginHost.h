#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Effects.h"
#include "core/Instruments.h"

namespace lynx
{
// Adapters: a JUCE plugin seen by the core as an Instrument/Effect.
// Notes become MIDI at the start of the next rendered chunk; since the core
// renders in chunks that start exactly at each event, timing is sample-accurate.
class PluginInstrument final : public Instrument
{
public:
    PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize);
    ~PluginInstrument() override;

    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }

    void prepare (double) noexcept override {} // prepared at creation (off the audio thread)
    void noteOn (int key, float velocity) noexcept override;
    void noteOff (int key) noexcept override;
    void allNotesOff() noexcept override;
    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override;
    void setParameter (int index, float value) noexcept override;
    int latencySamples() const noexcept override { return plugin->getLatencySamples(); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int blockSize;
};

class PluginEffect final : public Effect
{
public:
    PluginEffect (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize);
    ~PluginEffect() override;

    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }

    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override;
    void setParameter (int index, float value) noexcept override;
    int latencySamples() const noexcept override { return plugin->getLatencySamples(); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int blockSize;
};

// Plugin formats, known-plugin list (saved in AppData) and instance creation.
// UI thread.
class PluginHost
{
public:
    // persistent = read and write the list in %APPDATA%\LYNX (false in tests).
    explicit PluginHost (bool persistent = true);

    juce::KnownPluginList& getKnownPlugins() noexcept { return known; }
    juce::AudioPluginFormatManager& getFormats() noexcept { return formats; }

    static juce::File settingsFolder();          // %APPDATA%\LYNX
    static juce::FileSearchPath defaultSearchPath(); // default VST3 folders

    // Scans the folders for plugins. `progress(0..1, file)` returns false to cancel.
    // Each file is examined in a separate process (LYNX.exe --scan-one): a plugin that
    // hangs or crashes the process doesn't take LYNX down; it is blacklisted and skipped
    // in later scans.
    int scan (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress);

    // Scan child mode: describes a file's plugins into `output` (XML). Returns the number found.
    static int scanOneFile (const juce::String& formatName, const juce::String& file, const juce::File& output);

    static constexpr int scanTimeoutMs = 30000;

    // Adds a specific file (e.g. a .vst3 outside the default folders).
    int addPluginFile (const juce::File& file);

    std::unique_ptr<juce::PluginDescription> find (const juce::String& id) const;

    // Third-party plugins from an imported .flp (ForeignPlugin): finds the same plugin among the
    // scanned ones (VST3 by class ID, CLAP by id, and by name as a fallback, also for the VST2s
    // LYNX doesn't host) and converts the VST3 state to JUCE's format. Matched channels become
    // plugin channels; unmatched effect slots are dropped. Returns how many were matched.
    int adoptFlPlugins (Song& song, juce::StringArray* missing = nullptr) const;
    const juce::PluginDescription* findForeign (const ForeignPlugin& plugin, bool instrument) const;

    std::shared_ptr<Instrument> makeInstrument (const PluginRef& ref, double sampleRate, juce::String* error = nullptr);
    std::shared_ptr<Effect> makeEffect (const PluginRef& ref, double sampleRate, juce::String* error = nullptr);

    static constexpr int maxBlockSize = 4096;

private:
    std::unique_ptr<juce::AudioPluginInstance> instantiate (const PluginRef& ref, double sampleRate, juce::String* error);
    void save();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    bool persistent = true;
};

// Stereo layout (if the plugin accepts it), rate and block size; calls prepareToPlay.
void preparePlugin (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize);

// Current plugin state as base64 (for saving in the project).
std::string capturePluginState (juce::AudioPluginInstance& plugin);
} // namespace lynx
