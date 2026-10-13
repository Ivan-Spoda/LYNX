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

// EQ paramétrico de 7 bandas. Parâmetros: banda b usa [4b .. 4b+3] = tipo, frequência, ganho, Q;
// [28] = saída; [29 + b] = inclinação dos cortes (1 a 4 biquads: 12/24/36/48 dB por oitava).
// Nas prateleiras o "Q" é a inclinação S do cookbook (1 = padrão).
namespace peq
{
constexpr int bands = 7;
constexpr int outputParam = 28;
constexpr int slopeParam = 29;

enum class BandType { off, peak, lowShelf, highShelf, lowCut, highCut, bandPass, notch };
constexpr int bandTypes = 8;

inline int param (int band, int field) noexcept { return band * 4 + field; } // field: 0 tipo, 1 freq, 2 ganho, 3 Q
BandType type (float v) noexcept;
float typeValue (BandType t) noexcept;
double hz (float v) noexcept;      // 20 Hz .. 20 kHz (log)
float hzValue (double hz) noexcept;
double gainDb (float v) noexcept;  // ±18 dB
float gainValue (double db) noexcept;
double q (float v) noexcept;       // 0,1 .. 18 (log)
float qValue (double q) noexcept;
int slopeStages (float v) noexcept; // 1 a 4 biquads (12/24/36/48 dB/oit)
float slopeValue (int stages) noexcept;
const char* typeName (BandType t) noexcept;

// Resposta do EQ inteiro (todas as bandas e a saída) em dB, para o gráfico do editor.
double responseDb (const EffectParams& params, double sampleRate, double hz);
} // namespace peq

class Effect
{
public:
    virtual ~Effect() = default;

    virtual void prepare (double sampleRate) noexcept = 0;
    virtual void reset() noexcept = 0;

    // Processa no lugar. Os parâmetros chegam a cada bloco (podem mudar a qualquer momento).
    virtual void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept = 0;
    // Parâmetro automatizado de plugin (os internos recebem os parâmetros em process()). 0..1.
    virtual void setParameter (int /*index*/, float /*value*/) noexcept {}
    // Posição da música no começo do bloco, em ticks (efeitos sincronizados ao compasso).
    virtual void setSongPosition (double /*ticks*/) noexcept {}
    // Latência que o efeito introduz (plugins), em samples. Para o PDC.
    virtual int latencySamples() const noexcept { return 0; }
};

std::unique_ptr<Effect> makeEffect (EffectKind kind);
} // namespace lynx
