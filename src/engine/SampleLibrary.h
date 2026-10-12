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

    // Filtro para FileChooser ("*.wav;*.flac;...")
    juce::String wildcard() const;

private:
    std::unique_ptr<juce::AudioFormatReader> createReader (const juce::File& file);

    juce::AudioFormatManager formats;
    std::map<juce::String, std::weak_ptr<const SampleData>> cache;
};
} // namespace lynx
