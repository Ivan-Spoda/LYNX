#pragma once

// WavPack (.wv) audio format for JUCE: read-only, decodes the whole file on open
// (the .wv files in FL packs are short samples). Uses core/WavPack (official library, BSD).

#include <juce_audio_formats/juce_audio_formats.h>

namespace lynx
{
class WavPackAudioFormat final : public juce::AudioFormat
{
public:
    WavPackAudioFormat();

    juce::Array<int> getPossibleSampleRates() override { return {}; }
    juce::Array<int> getPossibleBitDepths() override { return {}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }

    juce::AudioFormatReader* createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails) override;
    juce::AudioFormatWriter* createWriterFor (juce::OutputStream*, double, unsigned int, int, const juce::StringPairArray&, int) override
    {
        return nullptr; // read-only
    }
    using juce::AudioFormat::createWriterFor;
};
} // namespace lynx
