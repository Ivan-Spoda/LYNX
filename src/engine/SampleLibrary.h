#pragma once

#include <map>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/Instruments.h"

namespace lynx
{
// Decodifica arquivos de áudio para SampleData, com cache por caminho.
// Thread da UI (decodificar aloca e faz I/O).
class SampleLibrary
{
public:
    SampleLibrary();

    std::shared_ptr<const SampleData> load (const juce::String& path);

    // Acima disto o áudio vai para um cache em disco mapeado na memória, em vez de ficar todo
    // na RAM (o "streaming do disco"). O cache é reaproveitado entre sessões.
    static constexpr double streamAboveSeconds = 30.0;
    static juce::File cacheFolder();

    // Filtro para FileChooser ("*.wav;*.flac;...")
    juce::String wildcard() const;

private:
    std::unique_ptr<juce::AudioFormatReader> createReader (const juce::File& file);
    std::shared_ptr<const SampleData> loadStreamed (const juce::File& file, juce::AudioFormatReader& reader);

    juce::AudioFormatManager formats;
    std::map<juce::String, std::weak_ptr<const SampleData>> cache;
};
// Traz do disco o trecho [from, to) (segundos) de um sample mapeado. Thread qualquer, menos a de áudio.
void prefetchSample (const SampleData& data, double fromSeconds, double toSeconds);
} // namespace lynx
