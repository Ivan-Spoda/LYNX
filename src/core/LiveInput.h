#pragma once

// External audio input through the mixer (REA_rodada88): the sources an insert can take its input from, when the
// input is monitored, and the block of input the audio thread hands to the renderer.

#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "core/Meters.h"
#include "core/Song.h"

namespace lynx
{
// One choice of an insert's "Audio input source": a stereo pair of device input channels, or one channel.
struct InputSource
{
    int channel = -1; // first device input channel (0-based)
    bool stereo = false;
    std::string name; // "In 1 - In 2" style, from the device's channel names
};

// The choices for a device whose input channels are called `channelNames` (in device order): the stereo pairs
// (1-2, 3-4, ...) first, then every channel alone, as FL lists them. A single-channel device gives one mono entry.
inline std::vector<InputSource> inputSources (const std::vector<std::string>& channelNames)
{
    std::vector<InputSource> out;
    const auto count = (int) channelNames.size();

    for (int c = 0; c + 1 < count; c += 2)
        out.push_back ({ c, true, channelNames[(std::size_t) c] + " - " + channelNames[(std::size_t) c + 1] });

    for (int c = 0; c < count; ++c)
        out.push_back ({ c, false, channelNames[(std::size_t) c] });

    return out;
}

// The name of an insert's input (empty when it has none). A channel the device doesn't have (another device
// is open now) still gets a name, numbered from 1.
inline std::string inputSourceName (int channel, bool stereo, const std::vector<std::string>& channelNames)
{
    if (channel < 0)
        return {};

    auto nameOf = [&] (int c) {
        return c < (int) channelNames.size() ? channelNames[(std::size_t) c] : "In " + std::to_string (c + 1);
    };

    return stereo ? nameOf (channel) + " - " + nameOf (channel + 1) : nameOf (channel);
}

// Whether an insert hears its external input now.
inline bool inputMonitored (InputMonitor mode, bool armed, bool playing, bool recording) noexcept
{
    switch (mode)
    {
        case InputMonitor::off: return false;
        case InputMonitor::whenArmed: return armed;
        case InputMonitor::automatic: return armed && (recording || ! playing);
        case InputMonitor::on: return true;
    }

    return false;
}

// Device input channels an insert's source uses: { first, count } (count 0 = none or not available).
inline std::pair<int, int> inputChannelsOf (const MixerTrack& track, int deviceChannels) noexcept
{
    if (track.inputChannel < 0 || track.inputChannel >= deviceChannels)
        return { 0, 0 };

    return { track.inputChannel, track.inputStereo && track.inputChannel + 1 < deviceChannels ? 2 : 1 };
}

// Shared between the audio callback (writes the block's input pointers, then calls the renderer on the same
// thread), the renderer (reads them) and the UI (arm switches, input meters).
struct LiveInput
{
    static constexpr int maxChannels = 64;

    // audio thread, valid only during one render call
    std::array<const float*, maxChannels> channels {};
    int numChannels = 0;

    // UI -> audio
    std::array<std::atomic<bool>, maxMixerTracks> armed {};
    std::atomic<bool> recording { false };

    // audio -> UI: peak of each insert's input source, monitored or not
    MeterBank meters;

    const float* channel (int c, int offset) const noexcept
    {
        return c >= 0 && c < numChannels && channels[(std::size_t) c] != nullptr ? channels[(std::size_t) c] + offset : nullptr;
    }
};
} // namespace lynx
