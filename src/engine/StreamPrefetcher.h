#pragma once

#include <juce_core/juce_core.h>

#include "core/Song.h"
#include "engine/SampleLibrary.h"
#include "engine/Transport.h"

namespace lynx
{
// Thread that reads ahead from disk the part of mapped samples (the long ones, see
// SampleLibrary) that will play in the next few seconds, so the audio thread almost never
// waits for the disk. Clips come from the UI (update), plus the transport position.
class StreamPrefetcher final : private juce::Thread
{
public:
    explicit StreamPrefetcher (const Transport& transport);
    ~StreamPrefetcher() override;

    // UI thread: audio clips of the current song that use mapped samples.
    void update (const Song& song, SampleLibrary& samples);

    static constexpr double lookaheadSeconds = 4.0;

private:
    struct Entry
    {
        std::shared_ptr<const SampleData> sample;
        double start = 0.0, end = 0.0; // ticks in the song
        double offsetSeconds = 0.0;    // where in the sample playback starts
    };

    void run() override;

    const Transport& transport;
    juce::CriticalSection lock;
    std::vector<Entry> entries;
    double songLength = 1.0;
    double bpm = 120.0;
    bool songMode = false;
};
} // namespace lynx
