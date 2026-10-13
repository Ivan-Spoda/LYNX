#pragma once

// Instrumentos internos: bateria e synth sintetizados, e um sampler. Servem para a
// DAW fazer som antes de existir hosting de plugins e para tocar projetos importados.
// Tudo com capacidade fixa: noteOn/render nunca alocam.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual void prepare (double sampleRate) noexcept = 0;
    virtual void noteOn (int key, float velocity) noexcept = 0;
    // Começa a nota já `startSeconds` adiante (só faz sentido para samples; os outros ignoram).
    virtual void noteOnAt (int key, float velocity, double /*startSeconds*/) noexcept { noteOn (key, velocity); }
    virtual void noteOff (int key) noexcept = 0;
    virtual void allNotesOff() noexcept = 0;

    // Soma (não sobrescreve) n samples em left/right, já aplicando os ganhos.
    virtual void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept = 0;
    // Parâmetro automatizado (plugins). 0..1. Thread de áudio.
    virtual void setParameter (int /*index*/, float /*value*/) noexcept {}
    // Latência que o instrumento introduz (plugins), em samples. Para o PDC.
    virtual int latencySamples() const noexcept { return 0; }
    // Nota "slide": as notas soando deslizam até `key` em `samples`, sem reatacar. Quem não suporta ignora.
    virtual void slideTo (int /*key*/, float /*cents*/, float /*samples*/) noexcept {}
    // Clip de áudio: fades e posição no clip da próxima nota (antes do noteOn), em samples.
    virtual void setNextClipFades (float /*elapsed*/, float /*remaining*/, float /*fadeIn*/, float /*fadeOut*/) noexcept {}
    // Pan, afinação fina e "porta" (inverte o porta do canal) da próxima nota (chamado logo antes
    // do noteOn). Quem não suporta ignora.
    virtual void setNextNoteExpression (float /*pan*/, float /*cents*/, bool /*porta*/ = false) noexcept {}
    // Ajustes do canal (sampler, synth). Thread da UI, a cada snapshot; o instrumento guarda de
    // forma que a thread de áudio leia sem esperar.
    virtual void configure (const ChannelDef&, double /*bpm*/) noexcept {}
    // Posição da música (ticks) no começo do bloco, para os LFOs globais. Thread de áudio.
    virtual void setSongPosition (double /*ticks*/) noexcept {}
};

// Áudio de um sample já decodificado (carregado fora da thread de áudio). Samples curtos
// ficam nos vetores; os longos ficam num arquivo de cache mapeado na memória (o sistema lê
// do disco sob demanda) e usam os ponteiros `mapped*`.
struct SampleData
{
    std::vector<float> left, right; // right vazio = mono
    double sampleRate = 44100.0;

    const float* mappedLeft = nullptr;
    const float* mappedRight = nullptr; // nullptr = mono
    std::size_t mappedFrames = 0;
    std::shared_ptr<const void> mapping; // dono do mapeamento

    bool isMapped() const noexcept { return mappedLeft != nullptr; }
    std::size_t frames() const noexcept { return isMapped() ? mappedFrames : left.size(); }
    const float* leftData() const noexcept { return isMapped() ? mappedLeft : left.data(); }
    const float* rightData() const noexcept
    {
        if (isMapped())
            return mappedRight != nullptr ? mappedRight : mappedLeft;
        return right.empty() ? left.data() : right.data();
    }
    bool isStereo() const noexcept { return isMapped() ? mappedRight != nullptr : ! right.empty(); }
};

// `sample` só é usado pelo sampler; sem sample o sampler fica mudo.
std::unique_ptr<Instrument> makeInstrument (InstrumentKind kind, std::shared_ptr<const SampleData> sample = nullptr);

// Sampler multi-sample: cada zona com o sample já carregado (nullptr = arquivo que faltou).
struct LoadedZone
{
    SampleZone zone;
    std::shared_ptr<const SampleData> sample;
};
std::unique_ptr<Instrument> makeZonedSampler (std::vector<LoadedZone> zones);

// Ruído branco barato e determinístico (xorshift), igual em toda plataforma.
struct Noise
{
    std::uint32_t state = 0x12345678u;

    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
    }
};
} // namespace lynx
