#pragma once
// Real-time scheduling for LYNX's audio threads on Linux (round 161), without root:
//  1. SCHED_FIFO directly when RLIMIT_RTPRIO allows it (the "audio" group / limits.conf, as JACK set-ups do);
//  2. else RealtimeKit (org.freedesktop.RealtimeKit1 on the system bus, what PipeWire and PulseAudio use);
//  3. else the desktop portal's Realtime interface (Flatpak / sandboxes).
// libdbus-1 is loaded at run time. Threads already running real-time (PipeWire's or JACK's own audio thread) are left
// as they are. Elsewhere (Windows, macOS) everything here does nothing.
#include <juce_core/juce_core.h>

namespace lynx::rt
{
enum class Status
{
    unknown,         // no audio thread seen yet
    alreadyRealtime, // the driver's thread runs real-time already (PipeWire, JACK)
    direct,          // SCHED_FIFO through RLIMIT_RTPRIO
    rtkit,           // granted by RealtimeKit
    portal,          // granted by the desktop portal
    denied           // none of them: the audio thread runs at normal priority
};

// From an audio callback (deviceThread) or a worker thread: remembers the calling thread so the message thread can
// promote it.
// Cheap (once per thread) and real-time safe.
void noteAudioThread (bool deviceThread = true) noexcept;

// Message thread (a timer): promotes the threads noted since the last call. Returns true when something changed.
bool promotePending();

// The outcome for the device's audio thread (the first thread noted after resetForNewDevice()).
Status status();

// A new audio device opened: its callback thread will be noted again.
void resetForNewDevice();

// "rtkit (priority 20)"-style detail for the Settings page.
juce::String describe();

// The one-time hint when real-time was denied: true the first time it is asked after a denial in this installation
// (`flagFile` remembers it).
bool shouldShowHint (const juce::File& flagFile);
} // namespace lynx::rt
