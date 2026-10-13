#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Effects.h"
#include "core/Instruments.h"

namespace lynx
{
// Adaptadores: um plugin JUCE visto pelo núcleo como Instrument/Effect.
// As notas viram MIDI no início do próximo trecho renderizado; como o núcleo
// renderiza em trechos que começam exatamente em cada evento, o tempo é preciso.
class PluginInstrument final : public Instrument
{
public:
    PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize);
    ~PluginInstrument() override;

    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }

    void prepare (double) noexcept override {} // preparado na criação (fora da thread de áudio)
    void noteOn (int key, float velocity) noexcept override;
    void noteOff (int key) noexcept override;
    void allNotesOff() noexcept override;
    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override;
    void setParameter (int index, float value) noexcept override;
    int latencySamples() const noexcept override { return plugin->getLatencySamples(); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int blockSize;
};

class PluginEffect final : public Effect
{
public:
    PluginEffect (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize);
    ~PluginEffect() override;

    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }

    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override;
    void setParameter (int index, float value) noexcept override;
    int latencySamples() const noexcept override { return plugin->getLatencySamples(); }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    int blockSize;
};

// Formatos de plugin, lista de plugins conhecidos (salva em AppData) e criação de instâncias.
// Thread da UI.
class PluginHost
{
public:
    // persistent = lê e grava a lista em %APPDATA%\LYNX (false nos testes).
    explicit PluginHost (bool persistent = true);

    juce::KnownPluginList& getKnownPlugins() noexcept { return known; }
    juce::AudioPluginFormatManager& getFormats() noexcept { return formats; }

    static juce::File settingsFolder();          // %APPDATA%\LYNX
    static juce::FileSearchPath defaultSearchPath(); // pastas padrão de VST3

    // Varre as pastas atrás de plugins. `progress(0..1, arquivo)` retorna false para cancelar.
    // Cada arquivo é examinado num processo separado (LYNX.exe --scan-one): um plugin que
    // trave ou feche o processo não derruba o LYNX; ele vai para a lista negra e é pulado
    // nas próximas buscas.
    int scan (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress);

    // Modo filho da busca: descreve os plugins de um arquivo em `output` (XML). Retorna o nº achado.
    static int scanOneFile (const juce::String& formatName, const juce::String& file, const juce::File& output);

    static constexpr int scanTimeoutMs = 30000;

    // Adiciona um arquivo específico (ex.: um .vst3 fora das pastas padrão).
    int addPluginFile (const juce::File& file);

    std::unique_ptr<juce::PluginDescription> find (const juce::String& id) const;

    // Plugins de terceiros de um .flp importado (ForeignPlugin): acha o mesmo plugin entre os
    // escaneados (VST3 pelo class ID, CLAP pelo id, e pelo nome como plano B, também para os VST2
    // que o LYNX não hospeda) e converte o estado VST3 para o formato do JUCE. Canais achados viram
    // canais de plugin; slots de efeito não achados saem. Retorna quantos foram achados.
    int adoptFlPlugins (Song& song, juce::StringArray* missing = nullptr) const;
    const juce::PluginDescription* findForeign (const ForeignPlugin& plugin, bool instrument) const;

    std::shared_ptr<Instrument> makeInstrument (const PluginRef& ref, double sampleRate, juce::String* error = nullptr);
    std::shared_ptr<Effect> makeEffect (const PluginRef& ref, double sampleRate, juce::String* error = nullptr);

    static constexpr int maxBlockSize = 4096;

private:
    std::unique_ptr<juce::AudioPluginInstance> instantiate (const PluginRef& ref, double sampleRate, juce::String* error);
    void save();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    bool persistent = true;
};

// Estado atual do plugin em base64 (para salvar no projeto).
std::string capturePluginState (juce::AudioPluginInstance& plugin);
} // namespace lynx
