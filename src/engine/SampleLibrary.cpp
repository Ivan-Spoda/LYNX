#include "engine/SampleLibrary.h"

#include "core/TimeStretch.h"
#include "engine/WavPackFormat.h"

namespace lynx
{
namespace
{
// FL stores many factory samples as WAV whose "data" is an Ogg Vorbis stream
// (format tag 0x674F/0x6750/0x6751, "Og"). JUCE's WAV reader doesn't understand this.
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
    formats.registerFormat (new WavPackAudioFormat(), false); // .wv files from FL packs
}

juce::String SampleLibrary::wildcard() const
{
    return formats.getWildcardForAllFormats();
}

std::unique_ptr<juce::AudioFormatReader> SampleLibrary::createReader (const juce::File& file)
{
    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (file)))
        return reader;

    // Misleading extension (e.g. an Ogg saved as .wav): try every format by content.
    if (auto* reader = formats.createReaderFor (file.createInputStream()))
        return std::unique_ptr<juce::AudioFormatReader> (reader);

    if (auto ogg = extractOggFromWav (file))
    {
        juce::OggVorbisAudioFormat vorbis;

        // The reader takes ownership of the stream; on failure, JUCE itself deletes the stream.
        if (auto* reader = vorbis.createReaderFor (new juce::MemoryInputStream (std::move (*ogg)), true))
            return std::unique_ptr<juce::AudioFormatReader> (reader);
    }

    return nullptr;
}

std::shared_ptr<const SampleData> SampleLibrary::loadStretched (const juce::String& path, double ratio)
{
    const auto key = path + "|" + juce::String (ratio, 4);

    if (auto it = stretchedCache.find (key); it != stretchedCache.end())
        return it->second;

    auto original = load (path);

    if (original == nullptr)
        return nullptr;

    std::shared_ptr<const SampleData> stretched = timeStretch (*original, ratio);

    // only one length per file is kept (changing the BPM replaces the stretched one)
    for (auto it = stretchedCache.begin(); it != stretchedCache.end();)
        it = it->first.startsWith (path + "|") ? stretchedCache.erase (it) : std::next (it);

    stretchedCache[key] = stretched;
    return stretched;
}

juce::File SampleLibrary::cacheFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LYNX-cache");
    folder.createDirectory();
    return folder;
}

namespace
{
// One cache file per channel: 32-byte header + contiguous float32.
constexpr char cacheMagic[8] = { 'L', 'Y', 'N', 'X', 'P', 'C', 'M', '1' };
constexpr int cacheHeader = 32;

struct Mapped
{
    std::unique_ptr<juce::MemoryMappedFile> left, right;
};

bool writeHeader (juce::OutputStream& out, double sampleRate, juce::int64 frames)
{
    out.write (cacheMagic, 8);
    out.writeDouble (sampleRate);
    out.writeInt64 (frames);
    out.writeInt64 (0);
    return out.getPosition() == cacheHeader;
}

std::unique_ptr<juce::MemoryMappedFile> mapChannel (const juce::File& file, double sampleRate, juce::int64 frames)
{
    if (file.getSize() != cacheHeader + frames * (juce::int64) sizeof (float))
        return nullptr;

    auto mapped = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readOnly);
    const auto* d = static_cast<const char*> (mapped->getData());

    if (d == nullptr || (juce::int64) mapped->getSize() != file.getSize() || std::memcmp (d, cacheMagic, 8) != 0)
        return nullptr;

    double rate = 0.0;
    juce::int64 count = 0;
    std::memcpy (&rate, d + 8, 8);
    std::memcpy (&count, d + 16, 8);
    return rate == sampleRate && count == frames ? std::move (mapped) : nullptr;
}

// Reads one sample per page so the OS brings the range in from disk now (off the audio thread).
float touch (const float* data, std::size_t from, std::size_t to)
{
    float sum = 0.0f;
    for (auto i = from; i < to; i += 1024)
        sum += data[i];
    return sum;
}
} // namespace

std::shared_ptr<const SampleData> SampleLibrary::loadStreamed (const juce::File& file, juce::AudioFormatReader& reader)
{
    const auto frames = reader.lengthInSamples;
    const auto channels = juce::jmin (2, (int) reader.numChannels);
    const auto key = juce::String::toHexString ((file.getFullPathName() + "|" + juce::String (file.getSize()) + "|"
                                                 + juce::String (file.getLastModificationTime().toMilliseconds()))
                                                    .hashCode64());
    const auto leftFile = cacheFolder().getChildFile (key + ".L.pcm");
    const auto rightFile = cacheFolder().getChildFile (key + ".R.pcm");

    auto mapped = std::make_shared<Mapped>();
    mapped->left = mapChannel (leftFile, reader.sampleRate, frames);
    mapped->right = channels > 1 ? mapChannel (rightFile, reader.sampleRate, frames) : nullptr;

    if (mapped->left == nullptr || (channels > 1 && mapped->right == nullptr))
    {
        // decode once, in chunks, straight into the cache files
        leftFile.deleteFile();
        rightFile.deleteFile();
        {
            juce::FileOutputStream outL (leftFile);
            std::unique_ptr<juce::FileOutputStream> outR;

            if (channels > 1)
                outR = std::make_unique<juce::FileOutputStream> (rightFile);

            if (outL.failedToOpen() || (outR != nullptr && outR->failedToOpen()) || ! writeHeader (outL, reader.sampleRate, frames)
                || (outR != nullptr && ! writeHeader (*outR, reader.sampleRate, frames)))
                return nullptr;

            constexpr int block = 1 << 16;
            juce::AudioBuffer<float> buffer (channels, block);

            for (juce::int64 done = 0; done < frames; done += block)
            {
                const auto n = (int) juce::jmin<juce::int64> (block, frames - done);
                reader.read (&buffer, 0, n, done, true, channels > 1);
                outL.write (buffer.getReadPointer (0), (size_t) n * sizeof (float));

                if (outR != nullptr)
                    outR->write (buffer.getReadPointer (1), (size_t) n * sizeof (float));
            }

            outL.flush();
            if (outR != nullptr)
                outR->flush();
        }

        mapped->left = mapChannel (leftFile, reader.sampleRate, frames);
        mapped->right = channels > 1 ? mapChannel (rightFile, reader.sampleRate, frames) : nullptr;

        if (mapped->left == nullptr || (channels > 1 && mapped->right == nullptr))
            return nullptr;
    }

    auto data = std::make_shared<SampleData>();
    data->sampleRate = reader.sampleRate;
    data->mappedFrames = (std::size_t) frames;
    data->mappedLeft = reinterpret_cast<const float*> (static_cast<const char*> (mapped->left->getData()) + cacheHeader);
    data->mappedRight = mapped->right != nullptr
                            ? reinterpret_cast<const float*> (static_cast<const char*> (mapped->right->getData()) + cacheHeader)
                            : nullptr;
    data->mapping = mapped;

    // the first seconds already in memory: the clip start doesn't wait for the disk
    const auto head = juce::jmin ((std::size_t) frames, (std::size_t) (reader.sampleRate * 3.0));
    volatile float sink = touch (data->leftData(), 0, head) + touch (data->rightData(), 0, head);
    juce::ignoreUnused (sink);
    return data;
}

void prefetchSample (const SampleData& data, double fromSeconds, double toSeconds)
{
    if (! data.isMapped())
        return;

    const auto from = (std::size_t) juce::jlimit (0.0, (double) data.frames(), fromSeconds * data.sampleRate);
    const auto to = (std::size_t) juce::jlimit (0.0, (double) data.frames(), toSeconds * data.sampleRate);
    volatile float sink = touch (data.leftData(), from, to) + (data.isStereo() ? touch (data.rightData(), from, to) : 0.0f);
    juce::ignoreUnused (sink);
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

    if ((double) reader->lengthInSamples > reader->sampleRate * streamAboveSeconds)
        if (auto streamed = loadStreamed (file, *reader))
        {
            cache[path] = streamed;
            return streamed;
        }

    // In memory: 10-minute safety limit (long ones go through the disk cache).
    const auto frames = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    const auto channels = juce::jmin (2, (int) reader->numChannels);

    juce::AudioBuffer<float> buffer (channels, frames);
    reader->read (&buffer, 0, frames, 0, true, channels > 1);

    auto data = std::make_shared<SampleData>();
    data->sampleRate = reader->sampleRate;
    data->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
    // the file's own loop (WAV smpl, first loop; JUCE gives the end frame inclusive)
    {
        const auto& meta = reader->metadataValues;
        if (meta.getValue ("NumSampleLoops", "0").getIntValue() > 0)
        {
            const auto start = meta.getValue ("Loop0Start", "-1").getLargeIntValue();
            const auto end = meta.getValue ("Loop0End", "-1").getLargeIntValue() + 1;
            if (start >= 0 && end > start && end <= frames)
                data->loopStart = start, data->loopEnd = end;
        }
    }

    if (channels > 1)
        data->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + frames);

    cache[path] = data;
    return data;
}
} // namespace lynx
