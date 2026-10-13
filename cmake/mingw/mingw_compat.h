#pragma once
// Declarations JUCE 8 uses that the MinGW-w64 (Zig) headers lack.
// Only used in the Zig cross build; MSVC has all of this in the Windows SDK.

#ifdef __cplusplus
// UIAutomationCore.h (Windows SDK)
enum CaretPosition
{
    CaretPosition_Unknown = 0,
    CaretPosition_EndOfLine = 1,
    CaretPosition_BeginningOfLine = 2
};
#endif
