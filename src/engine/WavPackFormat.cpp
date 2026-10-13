#include "engine/WavPackFormat.h"

#include "core/WavPack.h"

namespace lynx
{
namespace
{
class WavPackReader final : public juce::AudioFormatReader
{
public:
    explicit WavPackReader (DecodedAudio&& decoded)
        : juce::AudioFormatReader (nullptr, "WavPack"),
          audio (std::move (decoded))
    {
        sampleRate = audio.sampleRate;
        bitsPerSample = 32;
        usesFloatingPointData = true;
        numChannels = (unsigned int) audio.channels.size();
        lengthInSamples = audio.channels.empty() ? 0 : (juce::int64) audio.channels.front().size();
    }

    bool readSamples (int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile,
                      int numSamples) override
    {
        for (int c = 0; c < numDestChannels; ++c)
        {
            if (destChannels[c] == nullptr)
                continue;
            auto* dest = reinterpret_cast<float*> (destChannels[c]) + startOffsetInDestBuffer;
            const auto& source = audio.channels[(std::size_t) juce::jmin (c, (int) audio.channels.size() - 1)];
            for (int i = 0; i < numSamples; ++i)
            {
                const auto index = startSampleInFile + i;
                dest[i] = index >= 0 && index < (juce::int64) source.size() ? source[(std::size_t) index] : 0.0f;
            }
        }
        return true;
    }

private:
    DecodedAudio audio;
};
} // namespace

WavPackAudioFormat::WavPackAudioFormat()
    : juce::AudioFormat ("WavPack", ".wv")
{
}

juce::AudioFormatReader* WavPackAudioFormat::createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails)
{
    std::unique_ptr<juce::InputStream> owned (source);
    juce::MemoryBlock bytes;
    if (source != nullptr)
        source->readIntoMemoryBlock (bytes);

    DecodedAudio audio;
    if (source == nullptr || ! decodeWavPack (static_cast<const std::uint8_t*> (bytes.getData()), bytes.getSize(), audio))
    {
        if (! deleteStreamIfOpeningFails && source != nullptr)
        {
            source->setPosition (0); // the caller will try another format with the same stream
            owned.release();
        }
        return nullptr;
    }
    return new WavPackReader (std::move (audio)); // the stream has been fully read: it can go
}
} // namespace lynx
