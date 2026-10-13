#pragma once

// Formato de áudio WavPack (.wv) para o JUCE: só leitura, decodifica o arquivo inteiro na abertura
// (os .wv dos pacotes do FL são samples curtos). Usa core/WavPack (biblioteca oficial, BSD).

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
        return nullptr; // só leitura
    }
    using juce::AudioFormat::createWriterFor;
};
} // namespace lynx
