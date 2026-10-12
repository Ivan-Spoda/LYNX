// Só entra no build cruzado com Zig/MinGW (scripts/build-windows-zig.sh).
// O JUCE 8 usa __uuidof num ComSmartPtr; o MSVC resolve isso pelo tipo apontado,
// mas o MinGW procura uma especialização de __mingw_uuidof para o próprio smart pointer.

#if defined(__MINGW32__)
 #include <windows.h>
 #include <dxgi.h>
 #define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
 #include <juce_core/juce_core.h>

template <>
const GUID& __mingw_uuidof<juce::ComSmartPtr<IDXGIDevice>>()
{
    return __mingw_uuidof<IDXGIDevice>();
}

template <>
const GUID& __mingw_uuidof<juce::ComSmartPtr<IDXGISurface>>()
{
    return __mingw_uuidof<IDXGISurface>();
}
#endif
