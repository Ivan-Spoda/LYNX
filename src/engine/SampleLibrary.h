#pragma once

#include <map>
#include <memory>
#include <mutex>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/Instruments.h"

namespace lynx
{
// Decodes audio files into SampleData, cached by path (one cache for the whole process).
// Any thread except the audio thread.
class SampleLibrary
{
public:
    SampleLibrary();

    std::shared_ptr<const SampleData> load (const juce::String& path);
    // The file changed on disk (the audio editor saved over it): the next load() reads it again.
    void forget (const juce::String& path);

    // The sample time-stretched without changing pitch (WSOLA), cached by (path, ratio).
    std::shared_ptr<const SampleData> loadStretched (const juce::String& path, double ratio);

    // Above this, audio goes to a memory-mapped disk cache instead of staying entirely
    // in RAM ("disk streaming"). The cache is reused across sessions.
    static constexpr double streamAboveSeconds = 30.0;
    static juce::File cacheFolder();

    // Filter for FileChooser ("*.wav;*.flac;...")
    juce::String wildcard() const;

private:
    std::unique_ptr<juce::AudioFormatReader> createReader (const juce::File& file);
    std::shared_ptr<const SampleData> loadStreamed (const juce::File& file, juce::AudioFormatReader& reader);
    std::shared_ptr<const SampleData> decode (const juce::String& path);

    juce::AudioFormatManager formats;
    std::map<juce::String, std::shared_ptr<const SampleData>> stretchedCache; // keeps the latest one per path
    std::mutex stretchLock; // (an open prepares stretched copies on worker threads)
};
// Brings in from disk the range [from, to) (seconds) of a mapped sample. Any thread except the audio thread.
void prefetchSample (const SampleData& data, double fromSeconds, double toSeconds);
} // namespace lynx
