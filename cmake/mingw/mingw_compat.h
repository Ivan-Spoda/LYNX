#pragma once
// Declarações que o JUCE 8 usa e os headers MinGW-w64 (Zig) não trazem.
// Usado só no build cruzado com Zig; o MSVC tem tudo no Windows SDK.

#ifdef __cplusplus
// UIAutomationCore.h (Windows SDK)
enum CaretPosition
{
    CaretPosition_Unknown = 0,
    CaretPosition_EndOfLine = 1,
    CaretPosition_BeginningOfLine = 2
};
#endif
