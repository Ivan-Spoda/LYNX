#include "engine/Exporter.h"

#include "core/Dither.h"
#include "core/Mp3Encoder.h"
#include "engine/AudioEngine.h"
#include "engine/Text.h"

namespace lynx
{
using namespace exportopts;

juce::String exportExtension (ExportSettings::Format format)
{
    switch (format)
    {
        case ExportSettings::Format::flac24: return ".flac";
        case ExportSettings::Format::ogg: return ".ogg";
        default: return ".wav";
    }
}

Options ExportSettings::toOptions() const
{
    Options o;
    switch (format)
    {
        case Format::wav16: o.format = exportopts::Format::wav; o.wavBits = 16; break;
        case Format::wav24: o.format = exportopts::Format::wav; o.wavBits = 24; break;
        case Format::wav32Float: o.format = exportopts::Format::wav; o.wavBits = 32; break;
        case Format::flac24: o.format = exportopts::Format::flac; o.flacBits = 24; o.flacLevel = 0; break;
        case Format::ogg: o.format = exportopts::Format::ogg; o.oggKbps = 256; break;
    }
    o.dither = format == Format::wav16 ? DitherMode::shaped : DitherMode::none; // as LYNX always did
    o.sampleRate = sampleRate;
    o.range = wholeSong ? Range::song : Range::pattern;
    o.tail = TailMode::leave;
    o.maxTailSeconds = tailSeconds;
    return o;
}

namespace
{
// tail length in ticks for `seconds` at the tempo where the music ends
std::int64_t tailTicksFor (const Song& song, const Options& o, std::int64_t& musicTicks, std::int64_t& loopTicks)
{
    makeRenderSong (song, o, 0, musicTicks, loopTicks);
    const auto seconds = o.tail == TailMode::cut ? 0.0 : o.maxTailSeconds;
    return (std::int64_t) (seconds * song.tempoAt ((double) musicTicks) * ppq / 60.0);
}

double tailSecondsFor (const Options& o)
{
    return o.tail == TailMode::cut ? 0.0 : o.maxTailSeconds;
}

int oggQualityIndex (juce::AudioFormat& format, int kbps)
{
    const auto options = format.getQualityOptions();
    for (int i = 0; i < options.size(); ++i)
        if (options[i].getIntValue() == kbps)
            return i;
    return juce::jmax (0, options.size() - 3);
}
} // namespace

double exportMusicSeconds (const Song& song, const Options& options)
{
    std::int64_t musicTicks = 0, loopTicks = 0;
    const auto copy = makeRenderSong (song, options, 0, musicTicks, loopTicks);
    return copy.secondsForTicks (musicTicks);
}

ExportJob::ExportJob (const Song& song, const ExportSettings& s, const juce::File& file)
    : ExportJob (song, s.toOptions(), file)
{
}

ExportJob::ExportJob (const Song& song, const Options& o, const juce::File& file)
    : engine (std::make_unique<AudioEngine> (false)),
      options (clamped (o)),
      output (file),
      title (song.info.title),
      artist (song.info.author)
{
    // duration integrating tempo automation; the tail at the tempo of the song's end
    std::int64_t musicTicks = 0, loopTicks = 0;
    const auto tailTicks = tailTicksFor (song, options, musicTicks, loopTicks);
    auto copy = makeRenderSong (song, options, tailTicks, musicTicks, loopTicks);
    musicSeconds = copy.secondsForTicks (musicTicks);
    loopSeconds = copy.secondsForTicks (loopTicks);

    // rate before setSong: instruments and plugins are created already prepared for it
    engine->getTransport().sampleRate.store (options.sampleRate);
    engine->setSong (std::move (copy));
}

ExportJob::~ExportJob() = default;

bool ExportJob::run (const std::function<bool (double)>& progress, juce::String& error)
{
    AudioEngine::RenderOptions render;
    render.seconds = musicSeconds;
    render.sampleRate = options.sampleRate;
    render.progress = progress;
    render.tailSeconds = tailSecondsFor (options);
    render.trimPdc = options.trimPdc; // the latency as reported after the plugins' warm-up
    render.hqAllPlugins = options.hqAllPlugins;
    render.pluginWarmUp = options.pluginWarmUp;

    // streaming straight into the file when nothing needs the whole render first (the classic WAV/FLAC/OGG export)
    const auto streaming = options.format != exportopts::Format::mp3 && options.channels == Channels::stereo && ! options.normalize
                           && options.tail == TailMode::leave && formatsOf (options).size() == 1;

    if (! streaming)
    {
        std::vector<float> left, right;
        const auto expected = (std::size_t) ((musicSeconds + render.tailSeconds) * options.sampleRate) + 4096;
        left.reserve (expected);
        right.reserve (expected);
        render.keepOvers = true; // normalising comes after; each file's writer / dither clamps what still goes over
        render.capture = [&left, &right] (const float* l, const float* r, int n) {
            left.insert (left.end(), l, l + n);
            right.insert (right.end(), r, r + n);
        };
        peak = engine->renderOffline (render);

        if (peak < 0.0f)
        {
            error = "cancelled";
            return false;
        }

        return writeBuffered (left, right, error);
    }

    std::unique_ptr<juce::AudioFormat> format;
    int quality = 0;
    const auto bits = fileBits (options);

    if (options.format == exportopts::Format::flac)
    {
        format = std::make_unique<juce::FlacAudioFormat>();
        quality = options.flacLevel;
    }
    else if (options.format == exportopts::Format::ogg)
    {
        format = std::make_unique<juce::OggVorbisAudioFormat>();
        quality = oggQualityIndex (*format, options.oggKbps);
    }
    else
        format = std::make_unique<juce::WavAudioFormat>();

    output.deleteFile();
    auto stream = output.createOutputStream();

    if (stream == nullptr)
    {
        error = utf8 ("could not create ") + output.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (format->createWriterFor (stream.get(), options.sampleRate, 2, bits, {}, quality));

    if (writer == nullptr)
    {
        error = utf8 ("the format does not accept these settings");
        return false;
    }

    stream.release(); // now owned by the writer
    render.output = writer.get();
    render.ditherBits = usesDither (options) ? bits : 0;
    render.keepOvers = options.format == exportopts::Format::wav && bits == 32; // 32-bit float WAV keeps overs, as FL
    render.ditherShaping = options.dither == DitherMode::shaped;
    peak = engine->renderOffline (render);
    writer.reset(); // closes the file (the WAV header is finalized here)

    if (peak < 0.0f)
    {
        output.deleteFile();
        error = "cancelled";
        return false;
    }

    return true;
}

bool ExportJob::writeBuffered (std::vector<float>& left, std::vector<float>& right, juce::String& error)
{
    // tail, then normalising (the peak of what is kept), then mono; dither per file (each format has its own bits)
    applyTail (left, right, options.tail == TailMode::leave ? 0 : (std::int64_t) std::llround (musicSeconds * options.sampleRate), options.tail);

    const auto frames = (int) left.size();
    if (options.normalize)
    {
        float p = 0.0f;
        for (int i = 0; i < frames; ++i)
            p = juce::jmax (p, std::abs (left[(std::size_t) i]), std::abs (right[(std::size_t) i]));
        const auto gain = normalizeGain (p);
        juce::FloatVectorOperations::multiply (left.data(), gain, frames);
        juce::FloatVectorOperations::multiply (right.data(), gain, frames);
    }

    const auto channels = channelCount (options.channels);
    toChannels (left.data(), right.data(), frames, options.channels);

    // FL writes every ticked format from the one render (REA round 139): the first file is `output`, the others the
    // same name with their extension
    peak = 0.0f;
    for (const auto f : formatsOf (options))
    {
        auto o = options;
        o.format = f;
        const auto file = f == options.format ? output : output.withFileExtension (extension (f));
        std::vector<float> dl, dr;
        float* l = left.data();
        float* r = right.data();
        if (usesDither (o))
        {
            dl = left;
            dr = right;
            l = dl.data();
            r = dr.data();
            Dither dither (fileBits (o), o.dither == DitherMode::shaped);
            constexpr int block = 512; // the same block order as a streamed render
            for (int done = 0; done < frames; done += block)
            {
                const auto n = juce::jmin (block, frames - done);
                dither.process (l + done, n, 0);
                if (channels == 2)
                    dither.process (r + done, n, 1);
            }
        }

        for (int i = 0; i < frames; ++i)
            peak = juce::jmax (peak, std::abs (l[i]), channels == 2 ? std::abs (r[i]) : 0.0f);

        if (! writeFile (o, file, l, r, frames, channels, error))
            return false;
    }
    return true;
}

bool ExportJob::writeFile (const Options& o, const juce::File& file, float* left, float* right, int frames, int channels, juce::String& error)
{
    file.deleteFile();

    if (o.format == exportopts::Format::mp3)
    {
        Mp3Encoder encoder;
        std::string message;
        if (! encoder.open ((int) o.sampleRate, (int) mp3SampleRate (o.sampleRate), channels, o.mp3Kbps, message, title, artist))
        {
            error = utf8 (message.c_str());
            return false;
        }

        std::vector<std::uint8_t> bytes;
        bytes.reserve ((std::size_t) (frames / o.sampleRate * o.mp3Kbps * 125.0) + 65536);
        constexpr int block = 8192;
        for (int done = 0; done < frames; done += block)
            encoder.encode (left + done, right + done, juce::jmin (block, frames - done), bytes);
        encoder.finish (bytes);

        if (! file.replaceWithData (bytes.data(), bytes.size()))
        {
            error = utf8 ("could not create ") + file.getFullPathName();
            return false;
        }
        return true;
    }

    std::unique_ptr<juce::AudioFormat> format;
    int quality = 0;
    if (o.format == exportopts::Format::flac)
    {
        format = std::make_unique<juce::FlacAudioFormat>();
        quality = o.flacLevel;
    }
    else if (o.format == exportopts::Format::ogg)
    {
        format = std::make_unique<juce::OggVorbisAudioFormat>();
        quality = oggQualityIndex (*format, o.oggKbps);
    }
    else
        format = std::make_unique<juce::WavAudioFormat>();

    auto stream = file.createOutputStream();
    if (stream == nullptr)
    {
        error = utf8 ("could not create ") + file.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        format->createWriterFor (stream.get(), o.sampleRate, (unsigned int) channels, fileBits (o), {}, quality));
    if (writer == nullptr)
    {
        error = utf8 ("the format does not accept these settings");
        return false;
    }
    stream.release();

    float* data[2] = { left, right };
    juce::AudioBuffer<float> view (data, channels, frames);
    const auto ok = writer->writeFromAudioSampleBuffer (view, 0, frames);
    writer.reset();

    if (! ok)
    {
        file.deleteFile();
        error = utf8 ("could not write ") + file.getFullPathName();
        return false;
    }
    return true;
}
} // namespace lynx
