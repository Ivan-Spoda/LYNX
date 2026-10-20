#pragma once

// Opening a project, measured and reported (round 112, docs/analise/REA_rodada112.md).
//
// Opening one of the user's vocal projects sat at 0 % for a long time with nothing said about why. This module is the
// bookkeeping behind both the answer and the fix:
//  - phase timing: every stage of an open (parse, import, sample probing and decoding, plugin database, plugin scan,
//    plugin instantiation, rack build, PDC...) adds its time here; the --export report and %APPDATA%\LYNX\load.log
//    print the table, so a slow open on someone else's PC says what dominated;
//  - progress: the stages map onto one bar that only moves forward, with a line saying what is loading now
//    ("Loading Auto-Tune Pro... 3/12"); the UI polls it from a timer while the work runs elsewhere or in steps;
//  - missing roots: a sample folder on a drive that is not there (an unplugged external drive, a disconnected network
//    share) is probed once per root, not once per file.
// No JUCE here (core, tested by tests/LoadTraceTests112.inc). Thread-safe: phases may be timed on worker threads.

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace lynx::loadtrace
{
struct Phase
{
    std::string name;
    double ms = 0.0;
    int count = 0;
};

// Starts a new trace (forgets the previous one); `what` names it in the report (the project file).
void begin (const std::string& what);
// Ends the trace: the total time is fixed and the progress reads 1.
void end();
bool active();

// Adds `ms` (and `count` events) to a phase. Phases keep the order in which they first appeared.
void add (const std::string& phase, double ms, int count = 1);

// Times a block into a phase: { loadtrace::Scope s ("rack_build"); ... }
class Scope
{
public:
    explicit Scope (std::string phaseName);
    ~Scope();
    Scope (const Scope&) = delete;
    Scope& operator= (const Scope&) = delete;

private:
    std::string phase;
    std::chrono::steady_clock::time_point started;
};

// Milliseconds since `start`.
double msSince (std::chrono::steady_clock::time_point start);

// --- progress ------------------------------------------------------------------------------------------------------
// The open is split into stages, each owning a slice [from, to) of the bar. `stage` enters one (the bar jumps to its
// start, never back) with its English status text and the template its items use ("Loading {0}... {1}/{2}");
// `step` reports item `index` of `count` inside it (the bar moves to from + (to-from)*index/count); a non-empty
// `item` becomes the status (template filled with item, index, count).
struct Status
{
    std::string text;      // English; the UI translates it (and the template) through its tables
    std::string itemTemplate;
    std::string item;      // empty: just `text`
    int index = 0, count = 0;
};
void stage (double from, double to, const std::string& text, const std::string& itemTemplate = {});
// `shown` is the number the status prints (default `index`; an item being started shows index + 1)
void step (int index, int count, const std::string& item, int shown = -1);
double progress();       // 0..1, never decreases during one trace
Status status();
// The status in English with the template filled ("Loading Auto-Tune Pro... 3/12"); `tr` translates text/template.
std::string describe (const Status& s, const std::function<std::string (const std::string&)>& tr = {});
std::string fill (const std::string& pattern, const std::string& item, int index, int count);

// --- report --------------------------------------------------------------------------------------------------------
std::vector<Phase> phases();
double totalMs();        // since begin (until end, once ended)
std::string name();
// "load_total_ms=..\nload_<phase>_ms=..\nload_<phase>_count=..\n" for the --export report
std::string reportLines();
// A human-readable table, slowest phases first, for load.log
std::string table();

// --- missing roots -------------------------------------------------------------------------------------------------
// The root of an absolute path: "K:\" for a drive, "\\server\share\" for a network path; empty otherwise.
std::string pathRoot (const std::string& path);

// Remembers, for one open, which roots exist. `rootExists` is asked once per root (the app's one has a time limit:
// a dead network share must not stall the open); a path under a missing root is answered "no" without touching it.
class RootProbe
{
public:
    explicit RootProbe (std::function<bool (const std::string& root)> rootExists);
    // false when the path's root is known to be missing; true otherwise (the caller then checks the file itself)
    bool rootAvailable (const std::string& path);
    int probes() const noexcept { return probeCount; }
    int skipped() const noexcept { return skippedCount; }
    std::vector<std::string> missingRoots() const;

private:
    std::function<bool (const std::string&)> exists;
    std::map<std::string, bool> roots;
    mutable std::mutex lock;
    int probeCount = 0, skippedCount = 0;
};
} // namespace lynx::loadtrace
