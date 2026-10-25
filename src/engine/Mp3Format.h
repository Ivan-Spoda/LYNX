#pragma once

// MP3 (and MPEG-1/2 Layer I/II) audio format for JUCE, read-only, through core/Mp3Decoder (minimp3; round 157b). It
// replaces JUCE's MP3AudioFormat (JUCE_USE_MP3AUDIOFORMAT=0), which glitched on many real files and got lengths
// wrong. The whole file is decoded on open, with FL Studio's timing (core/Mp3Decoder.h).

#include <juce_audio_formats/juce_audio_formats.h>

namespace lynx
{
class Mp3AudioFormat final : public juce::AudioFormat
{
public:
    Mp3AudioFormat();

    static constexpr const char* formatName = "MP3 file (LYNX)";

    juce::Array<int> getPossibleSampleRates() override { return { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }; }
    juce::Array<int> getPossibleBitDepths() override { return {}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }
    bool isCompressed() override { return true; }

    juce::AudioFormatReader* createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails) override;
    juce::AudioFormatWriter* createWriterFor (juce::OutputStream*, double, unsigned int, int, const juce::StringPairArray&, int) override
    {
        return nullptr; // read-only (export encodes with core/Mp3Encoder)
    }
    using juce::AudioFormat::createWriterFor;
};

// Registers LYNX's MP3 reader first, then JUCE's basic formats: on Windows/macOS JUCE also lists Windows Media /
// Core Audio, which open .mp3 files too, and the manager uses the first format that opens a file.
void registerAudioFormats (juce::AudioFormatManager& formats);
} // namespace lynx
