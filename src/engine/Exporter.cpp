#include "engine/Exporter.h"

#include "engine/AudioEngine.h"
#include "engine/Text.h"

namespace lynx
{
juce::String exportExtension (ExportSettings::Format format)
{
    switch (format)
    {
        case ExportSettings::Format::flac24: return ".flac";
        case ExportSettings::Format::ogg: return ".ogg";
        default: return ".wav";
    }
}

ExportJob::ExportJob (const Song& song, const ExportSettings& s, const juce::File& file)
    : engine (std::make_unique<AudioEngine> (false)),
      settings (s),
      output (file)
{
    // duração integrando a automação de tempo; a cauda no andamento do fim da música
    std::int64_t musicTicks = 0;
    makeExportSong (song, settings.wholeSong, 0, musicTicks);
    const auto ticksPerSecond = song.tempoAt ((double) musicTicks) * ppq / 60.0;
    auto copy = makeExportSong (song, settings.wholeSong, (std::int64_t) (settings.tailSeconds * ticksPerSecond), musicTicks);
    musicSeconds = copy.secondsForTicks (musicTicks);

    // a taxa antes do setSong: instrumentos e plugins já nascem preparados nela
    engine->getTransport().sampleRate.store (settings.sampleRate);
    engine->setSong (std::move (copy));
}

ExportJob::~ExportJob() = default;

bool ExportJob::run (const std::function<bool (double)>& progress, juce::String& error)
{
    std::unique_ptr<juce::AudioFormat> format;
    int bits = 24, quality = 0;

    switch (settings.format)
    {
        case ExportSettings::Format::wav16: format = std::make_unique<juce::WavAudioFormat>(); bits = 16; break;
        case ExportSettings::Format::wav24: format = std::make_unique<juce::WavAudioFormat>(); bits = 24; break;
        case ExportSettings::Format::wav32Float: format = std::make_unique<juce::WavAudioFormat>(); bits = 32; break;
        case ExportSettings::Format::flac24: format = std::make_unique<juce::FlacAudioFormat>(); bits = 24; break;
        case ExportSettings::Format::ogg:
            format = std::make_unique<juce::OggVorbisAudioFormat>();
            bits = 16;
            quality = juce::jmax (0, format->getQualityOptions().size() - 3); // ~ 256 kbps
            break;
    }

    output.deleteFile();
    auto stream = output.createOutputStream();

    if (stream == nullptr)
    {
        error = utf8 ("não foi possível criar ") + output.getFullPathName();
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        format->createWriterFor (stream.get(), settings.sampleRate, 2, bits, {}, quality));

    if (writer == nullptr)
    {
        error = utf8 ("o formato não aceita estas configurações");
        return false;
    }

    stream.release(); // agora pertence ao writer

    AudioEngine::RenderOptions render;
    render.seconds = musicSeconds;
    render.sampleRate = settings.sampleRate;
    render.output = writer.get();
    render.progress = progress;
    render.tailSeconds = settings.tailSeconds;
    render.trimSamples = engine->getLatencySamples();
    render.ditherBits = settings.format == ExportSettings::Format::wav16 ? 16 : 0; // OGG codifica em float
    peak = engine->renderOffline (render);
    writer.reset(); // fecha o arquivo (o cabeçalho do WAV é finalizado aqui)

    if (peak < 0.0f)
    {
        output.deleteFile();
        error = "cancelado";
        return false;
    }

    return true;
}
} // namespace lynx
