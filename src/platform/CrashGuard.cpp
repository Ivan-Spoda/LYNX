#include "platform/CrashGuard.h"

#include <cstdlib>

#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
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
