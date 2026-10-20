#include "platform/CrashGuard.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mutex>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
#else
 #include <signal.h>
 #include <unistd.h>
#endif

namespace lynx::crashguard
{
#if defined(_WIN32) && (defined(_MSC_VER) || defined(__clang__))
namespace
{
// Breakpoints and single steps belong to a debugger; everything else is the plugin's crash.
int filter (unsigned long code) noexcept
{
    return code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

std::uint32_t call (void (*fn) (void*), void* context) noexcept
{
    // no C++ objects in this frame (SEH and C++ unwinding must not share one function)
    __try
    {
        fn (context);
        return 0;
    }
    __except (filter (GetExceptionCode()))
    {
        const auto code = (std::uint32_t) GetExceptionCode();
        return code != 0 ? code : 1u;
    }
}
#else
std::uint32_t call (void (*fn) (void*), void* context) noexcept
{
    fn (context);
    return 0;
}
#endif

void quietCrashReports() noexcept
{
#if defined(_WIN32)
    SetErrorMode (GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // WerSetFlags (kernel32, Vista+): no Windows Error Reporting UI for this process
    using WerSetFlagsFn = long (WINAPI*) (unsigned long);
    if (auto* kernel = GetModuleHandleW (L"kernel32.dll"))
        if (auto* set = reinterpret_cast<WerSetFlagsFn> (reinterpret_cast<void*> (GetProcAddress (kernel, "WerSetFlags"))))
            set (0x00000004 /* WER_FAULT_REPORTING_NO_UI */);
#endif
}

void exitNow (int code) noexcept
{
#if defined(_WIN32)
    TerminateProcess (GetCurrentProcess(), (UINT) code);
#endif
    std::_Exit (code);
}

namespace
{
std::mutex sessionLock;
std::string sessionFile, sessionHeader;
std::deque<std::string> sessionSteps;
constexpr std::size_t maxSteps = 10;

std::FILE* openUtf8 (const std::string& path, const char* mode)
{
#if defined(_WIN32)
    const auto n = MultiByteToWideChar (CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return nullptr;
    std::wstring wide ((std::size_t) n, L'\0');
    MultiByteToWideChar (CP_UTF8, 0, path.c_str(), -1, wide.data(), n);
    std::wstring wmode;
    for (const char* m = mode; *m != 0; ++m)
        wmode += (wchar_t) *m;
    return _wfopen (wide.c_str(), wmode.c_str());
#else
    return std::fopen (path.c_str(), mode);
#endif
}

void writeSession() // sessionLock held
{
    if (sessionFile.empty())
        return;
    std::string text = sessionHeader;
    for (const auto& s : sessionSteps)
        text += "step=" + s + "\n";
    if (auto* f = openUtf8 (sessionFile, "wb"))
    {
        std::fwrite (text.data(), 1, text.size(), f);
        std::fclose (f);
    }
}
} // namespace

void beginSession (const std::string& fileUtf8, const std::string& header) noexcept
{
    try
    {
        const std::lock_guard<std::mutex> lock (sessionLock);
        sessionFile = fileUtf8;
        sessionHeader = header;
        sessionSteps.clear();
        writeSession();
    }
    catch (...) {}
}

void note (const std::string& step) noexcept
{
    try
    {
        const std::lock_guard<std::mutex> lock (sessionLock);
        if (sessionFile.empty())
            return;
        char stamp[32] = {};
        const auto now = std::time (nullptr);
        if (const auto* t = std::localtime (&now))
            std::strftime (stamp, sizeof (stamp), "%H:%M:%S ", t);
        std::string line = stamp + step;
        for (auto& c : line)
            if (c == '\n' || c == '\r')
                c = ' ';
        sessionSteps.push_back (std::move (line));
        while (sessionSteps.size() > maxSteps)
            sessionSteps.pop_front();
        writeSession();
    }
    catch (...) {}
}

void endSession() noexcept
{
    try
    {
        const std::lock_guard<std::mutex> lock (sessionLock);
        if (sessionFile.empty())
            return;
#if defined(_WIN32)
        const auto n = MultiByteToWideChar (CP_UTF8, 0, sessionFile.c_str(), -1, nullptr, 0);
        std::wstring wide ((std::size_t) (n > 0 ? n : 1), L'\0');
        MultiByteToWideChar (CP_UTF8, 0, sessionFile.c_str(), -1, wide.data(), n);
        DeleteFileW (wide.c_str());
#else
        std::remove (sessionFile.c_str());
#endif
        sessionFile.clear();
    }
    catch (...) {}
}

std::uint32_t processId() noexcept
{
#if defined(_WIN32)
    return (std::uint32_t) GetCurrentProcessId();
#else
    return (std::uint32_t) getpid();
#endif
}

#if defined(_WIN32)
namespace
{
std::uint64_t creationTime (HANDLE process) noexcept
{
    FILETIME created {}, exited {}, kernel {}, user {};
    if (! GetProcessTimes (process, &created, &exited, &kernel, &user))
        return 0;
    return ((std::uint64_t) created.dwHighDateTime << 32) | created.dwLowDateTime;
}
} // namespace
#endif

std::uint64_t processStartTime() noexcept
{
#if defined(_WIN32)
    return creationTime (GetCurrentProcess());
#else
    return 0;
#endif
}

bool processRunning (std::uint32_t pid, std::uint64_t startTime) noexcept
{
    if (pid == 0)
        return false;
#if defined(_WIN32)
    auto* process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
    if (process == nullptr)
        return false;
    DWORD exitCode = 0;
    const auto running = GetExitCodeProcess (process, &exitCode) && exitCode == STILL_ACTIVE
                         && (startTime == 0 || creationTime (process) == startTime);
    CloseHandle (process);
    return running;
#else
    (void) startTime;
    return kill ((pid_t) pid, 0) == 0;
#endif
}

const char* describe (std::uint32_t code) noexcept
{
    switch (code)
    {
        case 0xC0000005u: return "access violation";
        case 0xC000001Du: return "illegal instruction";
        case 0xC0000094u: return "integer division by zero";
        case 0xC00000FDu: return "stack overflow";
        case 0xC000008Eu: case 0xC0000090u: case 0xC0000091u: case 0xC0000093u: return "floating-point exception";
        case 0xC0000409u: return "buffer overrun";
        case 0xE06D7363u: case 0x20474343u: return "C++ exception";
        default: return "exception";
    }
}
} // namespace lynx::crashguard
