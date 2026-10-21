#pragma once

// Plugin modules started ahead of time, off the message thread (round 121, docs/analise/REA_rodada121.md).
//
// Opening the user's vocal projects with the Waves trials installed took 11–16 s and froze the window: the FIRST
// call into a Waves shell's plugin factory (counting its ~600 classes) waits for the vendor's background service and
// took 9.8 s with that service not running yet (0.7–0.9 s when it is), all inside the first plugin created — on the
// message thread. Measured black-box, from the public VST3 entry points only (tmp probe, §2 of the round doc).
//
// The fix: as soon as an open knows which modules it needs, the slow ones are loaded and their factory listed on a
// thread of their own (what a plugin scanner does too); the factory is kept, so the instances created later on the
// message thread find it ready (and later opens too, for the rest of the session). The progress panel waits for
// them without blocking, says which one it waits for and offers to continue without it.
//
// This file is the bookkeeping (no JUCE, tested by tests/LoadTests121.inc); the platform part (loading the module)
// is a function handed in by engine/PluginHost.

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lynx::modulewarm
{
// Modules worth starting ahead: VST3 Waves shells ("WaveShell1-VST3 17.1_x64.vst3", any version, also inside a
// bundle folder). Other plugins load in well under a second, and starting them on another thread is not something
// every vendor supports (JUCE-made plugins take the thread that loads them for their message thread).
bool worthStartingAhead (const std::string& formatName, const std::string& file);

enum class State
{
    none,    // never asked
    warming, // its thread is running
    ready,   // loaded, factory kept
    failed,  // could not load (the normal creation reports why)
    skipped  // the user chose to continue without it (this open only)
};

struct Waiting
{
    std::string file; // the module waited for longest (empty: none)
    double seconds = 0.0;
    int modules = 0;  // how many are still starting
};

class Registry
{
public:
    // `load(file)` loads the module and lists its factory; true = ready. It runs on a thread of its own and may take
    // long; `clock()` returns seconds (steady). Both injectable for tests.
    using Loader = std::function<bool (const std::string& file)>;
    explicit Registry (Loader load, std::function<double()> clock = {}, bool threaded = true);
    ~Registry();

    // Starts `file` once per session (a module that failed is tried again on a later open). Returns its state.
    State start (const std::string& file);
    State state (const std::string& file) const;
    // The modules of this open still starting (skipped ones excluded).
    Waiting waiting() const;
    // "Continue without it": the modules still starting are skipped until the next `newOpen`.
    void skipWaiting();
    bool skipped (const std::string& file) const;
    // A new open: forgets skips; failed modules may be tried again.
    void newOpen();
    // How long until a wait is called slow in the panel (and the skip button shows).
    static constexpr double slowSeconds = 8.0;

private:
    struct Entry
    {
        State state = State::none;
        double started = 0.0;
    };
    Loader load;
    std::function<double()> clock;
    bool threaded;
    mutable std::mutex lock;
    std::shared_ptr<std::map<std::string, Entry>> entries;
    std::shared_ptr<std::mutex> entriesLock;
};

// The status line of the panel while it waits: "Starting {0}..." then, once slow, the longer message (English
// template keys, translated by the UI; {0} = the module's display name, {1} = seconds).
std::string waitTemplate (const Waiting& w);
// "WaveShell1-VST3 17.1_x64.vst3" -> "WaveShell1-VST3 17.1" (path, extension and the _x64 suffix dropped)
std::string displayName (const std::string& file);
} // namespace lynx::modulewarm
