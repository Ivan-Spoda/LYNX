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

// The calling thread's stack as "module+0xoffset" lines, every frame (round 157a): JUCE's backtrace leaves out frames
// it has no symbol for — every frame of a release LYNX.exe — so a crash file could not say where LYNX was. With the
// build's map / PDB the offsets give the functions. Empty where not supported.
std::string moduleBacktrace (int skip = 0);
// The faulting thread's frames from a crash handler's exception pointers (Windows: EXCEPTION_POINTERS*, what JUCE's
// crash handler receives), with the exception code and address on top; "" where not supported
std::string exceptionBacktrace (void* exceptionPointers);
// "module+0xoffset" of one code address ("" when unknown)
std::string describeAddress (const void* address);

// UI hang watch (round 157a). A frozen message thread leaves no crash file: Windows shows "not responding" and the
// user closes LYNX. startHangWatch (on the message thread) starts a watcher thread; heartbeat() (the message thread,
// from a timer, several times a second) proves the UI is alive. When no heartbeat arrives for `stallMs`, the watcher
// samples the message thread's stack (Windows x64: suspended for a moment, unwound without symbols;
// Linux / macOS: SIGPROF to that thread, which records its own frames), writes it to
// `folderUtf8`/hang <time>.txt and adds a session step ("UI not responding ..."); a second sample follows if the stall
// lasts, and "UI responding again after N s" when it ends. stopHangWatch ends the thread (before quitting).
void startHangWatch (const std::string& folderUtf8, int stallMs = 2000) noexcept;
void heartbeat() noexcept;
void stopHangWatch() noexcept;
// --symbolize: the "LYNX.exe+0x…" frames of a crash / hang file named with this very build's symbols (LYNX.pdb of
// the same build next to LYNX.exe; CI keeps it as the "LYNX-symbols" artifact). Other lines unchanged.
std::string symbolizeOwnFrames (const std::string& text);
// Test: the last hang file written by this process ("" when none)
std::string lastHangFile();
} // namespace lynx::crashguard
