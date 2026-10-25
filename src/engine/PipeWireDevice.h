#pragma once
// LYNX's own PipeWire client (round 161): an audio device type that talks to the PipeWire daemon directly, with
// libpipewire-0.3.so.0 loaded at run time (no link-time dependency: without PipeWire the type is simply not offered).
//
// Playback and capture are PipeWire streams named "LYNX" (pw-link / qpwgraph show LYNX:output_FL, LYNX:input_FL…)
// processed on PipeWire's real-time data thread, at the quantum asked for with node.latency. The "Default" devices
// follow the system's default output / input (WirePlumber moves the streams when it changes or the device goes away);
// a named device pins the stream to that sink / source. The device list follows hot-plugging.
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>

namespace lynx
{
inline constexpr const char* pipeWireTypeName = "PipeWire";

// The PipeWire device type, or nullptr when libpipewire is missing or no PipeWire daemon answers (Linux only).
std::unique_ptr<juce::AudioIODeviceType> makePipeWireDeviceType();

// What the PipeWire client found, for diagnostics (--audiodevices, the Settings page): library version, daemon.
juce::String describePipeWire();
} // namespace lynx
