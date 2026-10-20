#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Effects.h"
#include "core/FlPluginDb.h"
#include "core/PluginSubstitutes.h"
#include "core/Instruments.h"

namespace lynx
{
// FL's "Notify about rendering mode" (wrapper option, on by default): tells the plugin an export is running
// (VST3 kOffline process mode, VST2 offline process level) so it can use its render quality. Re-prepares the plugin
// when the mode changes; call only while it is not processing (offline renders, before/after the render loop).
void setPluginRenderMode (juce::AudioPluginInstance& plugin, bool offline, int blockSize);

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
    void setRenderMode (bool offline) { setPluginRenderMode (*plugin, offline, blockSize); }

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
    void setRenderMode (bool offline) { setPluginRenderMode (*plugin, offline, blockSize); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int blockSize;
};

// Plugin formats, known-plugin list (saved in AppData) and instance creation.
// UI thread, except the scan functions (any thread; one scan at a time) and the background scan.
class PluginHost
{
public:
    // persistent = read and write the list in %APPDATA%\LYNX (false in tests).
    explicit PluginHost (bool persistent = true);
    ~PluginHost();

    juce::KnownPluginList& getKnownPlugins() noexcept { return known; }
    juce::AudioPluginFormatManager& getFormats() noexcept { return formats; }

    static juce::File settingsFolder();          // %APPDATA%\LYNX
    static juce::FileSearchPath defaultSearchPath(); // default VST3 folders

    // The folders LYNX scans: the user's list (setup wizard / %APPDATA%\LYNX\plugin-folders.txt), else suggestedFolders().
    juce::FileSearchPath pluginFolders() const;
    void setPluginFolders (const juce::FileSearchPath& folders);
    // The standard VST3/CLAP folders plus the folders FL Studio uses on this PC (its configured VST folders and
    // the folders of the plugins in its plugin database), existing ones only.
    juce::FileSearchPath suggestedFolders();

    // FL Studio's plugin database on this PC (read once, on first use; empty without FL Studio). Thread-safe.
    const FlPluginDb& flPluginDatabase();
    int flPluginDatabaseFiles() { flPluginDatabase(); return flDbFiles; }

    // Scans the folders for plugins. `progress(0..1, file)` returns false to cancel.
    // Each file is examined in a separate process (LYNX.exe --scan-one): a plugin that
    // hangs or crashes the process doesn't take LYNX down; it is blacklisted and skipped
    // in later scans. Incremental: files already listed and unchanged since are skipped.
    int scan (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress);
    // The same for a given list of plugin files (targeted scan).
    int scanFiles (const juce::StringArray& files, const std::function<bool (float, const juce::String&)>& progress);

    // Background scan of pluginFolders() (or `paths`): never blocks the caller; poll backgroundScan() for progress.
    struct ScanState
    {
        bool running = false;
        float progress = 0.0f;
        juce::String file;  // being examined
        int found = 0;      // new plugins of the last (or current) background scan
        int finished = 0;   // how many background scans finished since start (changes when one ends)
    };
    void startBackgroundScan (const juce::FileSearchPath& paths);
    void startBackgroundScan() { startBackgroundScan (pluginFolders()); }
    ScanState backgroundScan() const;
    void cancelBackgroundScan();

    // Background scan of these files only (Plugin manager: "Try again", "Add plugin file").
    void startBackgroundScanOfFiles (const juce::StringArray& files);

    // Plugin manager: forgets a plugin file (its plugins and its failed-scan mark, so a later scan examines it
    // again), or everything (before a full rescan). UI thread, not while a scan runs.
    void forgetFile (const juce::String& file);
    void forgetAll();

    // Favourite plugins (identifier strings), listed first by the plugin menus and the quick-add palette.
    // Saved in %APPDATA%\LYNX\plugin-favourites.txt. UI thread.
    const std::set<std::string>& favourites() const noexcept { return favouriteIds; }
    void setFavourite (const std::string& id, bool favourite);

    // Scan child mode: describes a file's plugins into `output` (XML). Returns the number found.
    static int scanOneFile (const juce::String& formatName, const juce::String& file, const juce::File& output);

    // (the parent waits core/PluginScanPolicy.h's time for each file; the child first reports a VST3's class count)

    // Adds a specific file (e.g. a .vst3 outside the default folders).
    int addPluginFile (const juce::File& file);

    std::unique_ptr<juce::PluginDescription> find (const juce::String& id) const;

    // Third-party plugins from an imported .flp (ForeignPlugin): finds the same plugin among the
    // scanned ones (VST3 by class ID, CLAP by id; then the file FL Studio's plugin database lists for it; then
    // VST2 by its ID; by name as a fallback) and carries the state over (VST3: JUCE's format; VST2: FL's layout as is).
    // Matched channels become plugin channels; unmatched effect slots are dropped. Returns how many were matched.
    // `matched` gets one "name → file (how)" line per plugin found.
    // `withoutState` gets the plugins found whose saved settings couldn't be carried over (VST2 <-> VST3 swap).
    // Effect slots whose plugin is missing become their LYNX substitute (core/PluginSubstitutes.h) when
    // pluginSubstitutesEnabled(); `substituted` gets one "name -> LYNX effect" line each (they are not in `missing`).
    int adoptFlPlugins (Song& song, juce::StringArray* missing = nullptr, juce::StringArray* matched = nullptr,
                        juce::StringArray* withoutState = nullptr, juce::StringArray* substituted = nullptr);

    // A .lynx song just loaded: substitute slots whose original plugin is now installed go back to it (always: the
    // original is preferred); plugin effect slots whose plugin isn't installed get their LYNX substitute (when
    // pluginSubstitutesEnabled()). Returns how many slots changed; `restored` / `substituted` count each kind.
    int applySubstitutes (Song& song, int* restored = nullptr, int* substituted = nullptr);

    // What a substitute is matched on for a saved reference: its .flp record when it has one, else the format, name
    // and (decoded) state of the LYNX reference.
    static PluginIdentity identityOf (const PluginRef& ref);
    std::optional<juce::PluginDescription> findForeign (const ForeignPlugin& plugin, bool instrument, juce::String* how = nullptr);

    // Plugin files worth a targeted scan so a project's plugins can be found: for each plugin not known yet, the
    // file FL's database or the project names, and same-named VST3/CLAP files in pluginFolders(). Cheap (no loading).
    juce::StringArray filesToScanFor (const std::vector<FlpPluginUse>& uses);

    // Plugins of the song that are known but failed to load ("name: reason"; they play silence / are bypassed).
    // Kept until clearLoadFailures (a new project). Any thread.
    juce::StringArray getLoadFailures() const;
    void clearLoadFailures();

    std::shared_ptr<Instrument> makeInstrument (const PluginRef& ref, double sampleRate, juce::String* error = nullptr);
    std::shared_ptr<Effect> makeEffect (const PluginRef& ref, double sampleRate, juce::String* error = nullptr);

    static constexpr int maxBlockSize = 4096;

private:
    std::unique_ptr<juce::AudioPluginInstance> instantiate (const PluginRef& ref, double sampleRate, juce::String* error);
    void save();
    bool scanOne (juce::AudioPluginFormat& format, const juce::String& file);

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    bool persistent = true;

    std::mutex scanLock, saveLock, flDbLock;
    std::atomic<bool> stopScan { false };
    FlPluginDb flDb;
    int flDbFiles = 0;
    bool flDbLoaded = false;

    std::set<std::string> favouriteIds;

    class BackgroundScan;
    std::unique_ptr<BackgroundScan> background;
    mutable std::mutex failuresLock;
    juce::StringArray loadFailures;
    mutable std::mutex stateLock;
    ScanState state;
};

// Settings → "Substitute missing plugins with LYNX equivalents" (default on; %APPDATA%\LYNX\substitutes.txt).
bool pluginSubstitutesEnabled();
void setPluginSubstitutesEnabled (bool enabled);

// Stereo layout (if the plugin accepts it), rate and block size; calls prepareToPlay.
void preparePlugin (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize);

// Current plugin state as base64 (for saving in the project).
std::string capturePluginState (juce::AudioPluginInstance& plugin);
} // namespace lynx
