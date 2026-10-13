#pragma once

// Effector (o multiefeito de 12 algoritmos do FL Studio), reimplementado a partir do comportamento
// descrito em docs/analise/REA_rodada18.md §3 e REA_rodada19.md §1. Os 12 parâmetros ficam como no FL
// (valores crus 0..1): 0 tipo, 1 bypass, 2 wet mix, 3 X, 4 Y, 5 mod rate, 6 tempo reduce, 7 mod X,
// 8 mod Y, 9 forma do LFO, 10 nível de entrada, 11 ganho de saída.

#include <memory>
#include <string>

#include "core/Effects.h"

namespace lynx::effector
{
constexpr int typeCount = 12;
enum Param { type, bypass, wetMix, paramX, paramY, modRate, tempoReduce, modX, modY, modShape, inputLevel, outputGain };

int typeOf (float v) noexcept;                // trunc(11v + 0,5)
const char* typeName (int type) noexcept;     // "Distort", "Lo-fi", …
double rateBeats (float modRate, float tempoReduce) noexcept; // período do LFO em tempos
std::string formatParam (int param, float value, double bpm);

std::unique_ptr<Effect> make();
} // namespace lynx::effector
