#include "engine/SampleLibrary.h"

namespace daw
{
namespace
{
// O FL guarda muitos samples de fábrica como WAV cujo "data" é um stream Ogg Vorbis
// (format tag 0x674F/0x6750/0x6751, "Og"). O leitor de WAV do JUCE não entende isso.
std::unique_ptr<juce::MemoryBlock> extractOggFromWav (const juce::File& file)
{
    juce::MemoryBlock bytes;

    if (! file.loadFileAsData (bytes) || bytes.getSize() < 12)
        return nullptr;

    const auto* d = static_cast<const juce::uint8*> (bytes.getData());

    if (std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
        return nullptr;

    bool isOgg = false;

    for (size_t pos = 12; pos + 8 <= bytes.getSize();)
    {
        const auto size = (size_t) juce::ByteOrder::littleEndianInt (d + pos + 4);
        const auto body = pos + 8;

        if (std::memcmp (d + pos, "fmt ", 4) == 0 && body + 2 <= bytes.getSize())
        {
            const auto tag = juce::ByteOrder::littleEndianShort (d + body);
            isOgg = tag == 0x674F || tag == 0x6750 || tag == 0x6751;
        }
        else if (std::memcmp (d + pos, "data", 4) == 0 && isOgg)
        {
            const auto available = juce::jmin (size, bytes.getSize() - body);
            return std::make_unique<juce::MemoryBlock> (d + body, available);
        }

        pos = body + size + (size & 1);
    }

    return nullptr;
}
} // namespace

SampleLibrary::SampleLibrary()
{
    formats.registerBasicFormats();
}

juce::String SampleLibrary::wildcard() const
{
    return formats.getWildcardForAllFormats();
}

std::unique_ptr<juce::AudioFormatReader> SampleLibrary::createReader (const juce::File& file)
{
    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)))
        return reader;

    // Extensão mentirosa (ex.: um Ogg salvo como .wav): testa todos os formatos pelo conteúdo.
    if (auto* reader = formats.createReaderFor (file.createInputStream()))
        return std::unique_ptr<juce::AudioFormatReader> (reader);

    if (auto ogg = extractOggFromWav (file))
    {
        juce::OggVorbisAudioFormat vorbis;

        // O reader fica dono do stream; se falhar, o próprio JUCE apaga o stream.
        if (auto* reader = vorbis.createReaderFor (new juce::MemoryInputStream (std::move (*ogg)), true))
            return std::unique_ptr<juce::AudioFormatReader> (reader);
    }

    return nullptr;
}

std::shared_ptr<const SampleData> SampleLibrary::load (const juce::String& path)
{
    if (auto cached = cache[path].lock())
        return cached;

    const juce::File file (path);

    if (! file.existsAsFile())
        return nullptr;

    auto reader = createReader (file);

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return nullptr;

    // Limite de segurança: 10 minutos. Samples maiores vão precisar de streaming do disco.
    const auto frames = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    const auto channels = juce::jmin (2, (int) reader->numChannels);

    juce::AudioBuffer<float> buffer (channels, frames);
    reader->read (&buffer, 0, frames, 0, true, channels > 1);

    auto data = std::make_shared<SampleData>();
    data->sampleRate = reader->sampleRate;
    data->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);

    if (channels > 1)
        data->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);

    cache[path] = data;
    return data;
}
} // namespace daw
