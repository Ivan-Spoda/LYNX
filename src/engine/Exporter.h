#pragma once

#include <functional>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/Song.h"

namespace lynx
{
class AudioEngine;

struct ExportSettings
{
    enum class Format
    {
        wav16,
        wav24,
        wav32Float,
        flac24,
        ogg
    };

    Format format = Format::wav24;
    double sampleRate = 44100.0;
    bool wholeSong = true;     // false = só a pattern atual
    double tailSeconds = 6.0;  // máximo de cauda; para antes quando o som acaba
};

// Extensão (com ponto) do formato.
juce::String exportExtension (ExportSettings::Format format);

// Exporta uma cópia da música com um engine próprio, sem mexer no que está tocando.
// O construtor cria os instrumentos e plugins (chamar na thread da UI); run() pode ir
// numa thread de fundo.
class ExportJob
{
public:
    ExportJob (const Song& song, const ExportSettings& settings, const juce::File& output);
    ~ExportJob();

    // progress(0..1) retorna false para cancelar. Em erro ou cancelamento apaga o arquivo.
    bool run (const std::function<bool (double)>& progress, juce::String& error);

    double getMusicSeconds() const noexcept { return musicSeconds; }
    float getPeak() const noexcept { return peak; }
    AudioEngine& getEngine() noexcept { return *engine; }

private:
    std::unique_ptr<AudioEngine> engine;
    ExportSettings settings;
    juce::File output;
    double musicSeconds = 0.0;
    float peak = 0.0f;
};
} // namespace lynx
