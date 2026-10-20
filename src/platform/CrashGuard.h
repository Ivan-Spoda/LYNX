#pragma once

// Calls into code LYNX does not control (hosted VST3 / CLAP / VST2 plugins) behind a structured-exception guard, so a
// plugin that crashes (access violation, division by zero, a C++ exception escaping it) is caught at the call instead
// of taking the whole app down. On Windows this is SEH (__try / __except in CrashGuard.cpp, the only unit built with
// -fms-extensions); elsewhere the call is made directly.
//
// A caught crash leaves the plugin in an unknown state: the caller must never call that instance again (PluginEffect /
// PluginInstrument switch to bypass / silence and report it, engine/PluginHost.h).

#include <cstdint>
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

// Short English description of an exception code ("access violation", ...), or "exception".
const char* describe (std::uint32_t code) noexcept;
} // namespace lynx::crashguard
