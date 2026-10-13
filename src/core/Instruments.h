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
};

// Áudio de um sample já decodificado (carregado fora da thread de áudio).
struct SampleData
{
    std::vector<float> left, right; // right vazio = mono
    double sampleRate = 44100.0;

    std::size_t frames() const noexcept { return left.size(); }
};

// `sample` só é usado pelo sampler; sem sample o sampler fica mudo.
std::unique_ptr<Instrument> makeInstrument (InstrumentKind kind, std::shared_ptr<const SampleData> sample = nullptr);

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
