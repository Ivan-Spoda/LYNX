// Only part of the Zig/MinGW cross build (scripts/build-windows-zig.sh).
// JUCE 8 uses __uuidof on a ComSmartPtr; MSVC resolves it via the pointee type,
// but MinGW looks for a __mingw_uuidof specialization for the smart pointer itself.

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
