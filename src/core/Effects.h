#pragma once

// Efeitos internos para os slots do mixer. Escritos do zero, sem dependências.
// Os buffers são alocados no construtor já para 192 kHz, então prepare() e
// process() nunca alocam e podem rodar na thread de áudio.

#include <array>
#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// Descrição de um parâmetro para a interface (todos os valores são 0..1 no modelo).
struct EffectParamInfo
{
    const char* name;
    float defaultValue;
};

struct EffectInfo
{
    const char* name;
    std::vector<EffectParamInfo> params;
};

const EffectInfo& effectInfo (EffectKind kind);

// Texto do valor de um parâmetro já convertido para a unidade real (dB, ms, %...).
std::string formatEffectParam (EffectKind kind, int param, float value01, double bpm);

EffectSlot makeEffectSlot (EffectKind kind);

class Effect
{
public:
    virtual ~Effect() = default;

    virtual void prepare (double sampleRate) noexcept = 0;
    virtual void reset() noexcept = 0;

    // Processa no lugar. Os parâmetros chegam a cada bloco (podem mudar a qualquer momento).
    virtual void process (float* left, float* right, int n, const EffectSlot& slot, double bpm) noexcept = 0;
};

std::unique_ptr<Effect> makeEffect (EffectKind kind);
} // namespace lynx
