#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lynx
{
// Hosting de plugins CLAP (o JUCE 8 só traz VST3/AU/LV2). Implementado como mais um
// juce::AudioPluginFormat: a busca, a lista de plugins, a janela do plugin, o estado salvo,
// a latência (PDC) e a automação de parâmetros funcionam pelo mesmo caminho do VST3.
// Escrito a partir dos headers públicos do CLAP 1.2 (licença MIT, em third_party/clap).
class ClapPluginFormat final : public juce::AudioPluginFormat
{
public:
    static constexpr const char* formatName = "CLAP";

    juce::String getName() const override { return formatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override;
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& directories, bool recursive, bool allowAsync) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

private:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate, int initialBufferSize,
                               PluginCreationCallback) override;
};

// Adiciona os formatos do LYNX (os do JUCE + CLAP) a um gerenciador.
void addPluginFormats (juce::AudioPluginFormatManager& manager);
} // namespace lynx
