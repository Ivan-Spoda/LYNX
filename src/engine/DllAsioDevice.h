#pragma once
// An ASIO driver loaded straight from its DLL (no registry): LYNX's own lynx-asio.dll, or any ASIO driver DLL for a
// comparison. Hidden test mode `--asiodll <path>[|{clsid}]` (REA round 97); the device type is named
// "ASIO driver file".
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace lynx
{
// The type name the device manager lists for it.
inline constexpr const char* dllAsioTypeName = "ASIO driver file";

// nullptr when not on Windows. `spec` = "C:\\path\\driver.dll" or "C:\\path\\driver.dll|{CLSID}"; without a CLSID
// LYNX ASIO's own class id is used.
std::unique_ptr<juce::AudioIODeviceType> makeDllAsioDeviceType (const juce::String& spec);
} // namespace lynx
