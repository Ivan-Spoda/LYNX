#pragma once

#include <map>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/Instruments.h"

namespace lynx
{
// Decodes audio files into SampleData, cached by path.
// UI thread (decoding allocates and does I/O).
class SampleLibrary
{
public:
    SampleLibrary();

    std::shared_ptr<const SampleData> load (const juce::String& path);

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

    juce::AudioFormatManager formats;
    std::map<juce::String, std::weak_ptr<const SampleData>> cache;
    std::map<juce::String, std::shared_ptr<const SampleData>> stretchedCache; // keeps the latest one per path
};
// Brings in from disk the range [from, to) (seconds) of a mapped sample. Any thread except the audio thread.
void prefetchSample (const SampleData& data, double fromSeconds, double toSeconds);
} // namespace lynx
