#pragma once

// Calls into code LYNX does not control (hosted VST3 / CLAP / VST2 plugins) behind a structured-exception guard, so a
// plugin that crashes (access violation, division by zero, a C++ exception escaping it) is caught at the call instead
// of taking the whole app down. On Windows this is SEH (__try / __except in CrashGuard.cpp, the only unit built with
// -fms-extensions); elsewhere the call is made directly.
//
// A caught crash leaves the plugin in an unknown state: the caller must never call that instance again (PluginEffect /
// PluginInstrument switch to bypass / silence and report it, engine/PluginHost.h).

#include <cstdint>
#include <string>
#include <type_traits>

namespace lynx::crashguard
{
// Runs fn (context); returns 0 when it returned normally, otherwise the exception code (Windows' NTSTATUS, e.g.
// 0xC0000005 = access violation).
std::uint32_t call (void (*fn) (void*), void* context) noexcept;

template <typename Fn>
std::uint32_t run (Fn&& fn) noexcept
{
    return call ([] (void* c) { (*static_cast<std::remove_reference_t<Fn>*> (c)) (); }, &fn);
}

// Child processes that load plugins (scan, sandbox): a plugin crashing there must not pop Windows' "stopped working"
// dialog on the user's screen (the parent notices the exit code). Also covers fail-fast exits (0xC0000409).
void quietCrashReports() noexcept;

// Ends the process at once with `code`, without running DLL detach handlers or static destructors: some plugins
// (a Waves shell, round 111) crash in their own teardown at exit (DllMain / atexit calling into the GPU driver).
// Only after every file the process writes is closed.
[[noreturn]] void exitNow (int code) noexcept;

// Session breadcrumb (round 111). A fail-fast (0xC0000409, e.g. a GPU driver aborting inside a plugin's editor) ends
// the process without running any crash handler, so nothing tells what happened. While the UI runs, a small file
// (%APPDATA%\LYNX\sessions\session-<pid>.txt) holds a header and the last few steps (plugin being loaded, editor being
// opened, project being opened...). A clean exit removes it; the next start finds the file of a session whose process
// is gone and reports it (Main.cpp). Steps are written synchronously (the file survives the process dying right after);
// never call note() from the audio thread.
void beginSession (const std::string& fileUtf8, const std::string& header) noexcept;
void note (const std::string& step) noexcept; // no-op without a session
void endSession() noexcept;                   // removes the file
std::uint32_t processId() noexcept;
std::uint64_t processStartTime() noexcept; // Windows: creation FILETIME; 0 elsewhere
// The process `pid` still runs and (where known) started at `startTime`: not a reused id.
bool processRunning (std::uint32_t pid, std::uint64_t startTime) noexcept;

// Short English description of an exception code ("access violation", ...), or "exception".
const char* describe (std::uint32_t code) noexcept;
} // namespace lynx::crashguard
