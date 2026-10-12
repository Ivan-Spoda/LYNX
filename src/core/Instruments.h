#pragma once

// Instrumentos internos sintetizados (sem samples). Servem para a DAW fazer som
// antes de existir hosting de plugins e para tocar projetos importados.
// Tudo com capacidade fixa: noteOn/render nunca alocam.

#include <array>
#include <cstdint>
#include <memory>

#include "core/Song.h"

namespace daw
{
class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual void prepare (double sampleRate) noexcept = 0;
    virtual void noteOn (int key, float velocity) noexcept = 0;
    virtual void noteOff (int key) noexcept = 0;
    virtual void allNotesOff() noexcept = 0;

    // Soma (não sobrescreve) n samples em left/right, já aplicando os ganhos.
    virtual void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept = 0;
};

std::unique_ptr<Instrument> makeInstrument (InstrumentKind kind);

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
} // namespace daw
