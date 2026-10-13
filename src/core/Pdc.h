#pragma once

// Compensação de latência de plugins (PDC), no modelo "por aresta" que o FL usa
// (docs/analise/REA_rodada2.md §1): em ordem de roteamento,
//   inAlign[T] = max(latência dos instrumentos que chegam em T, outLat[S] de cada S -> T)
//   outLat[T]  = inAlign[T] + soma das latências dos efeitos ligados de T
// e cada aresta (instrumento -> trilha, trilha -> trilha) atrasa o que falta para chegar
// em inAlign do destino. O caminho mais longo nunca é atrasado.

#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct PdcPlan
{
    std::vector<int> channelDelay;             // por canal, em samples
    std::vector<std::vector<int>> sendDelay;   // [trilha][k]: k = 0 Master, k = 1 + i para sends[i]
    std::vector<int> inAlign, outLat;          // por trilha
    int total = 0;                              // latência na saída do Master

    bool any() const noexcept;
};

// `channelLatency[c]` = latência do instrumento do canal; `slotLatency[t][s]` = do efeito do
// slot (0 se desligado). Vetores menores que o Song valem 0.
PdcPlan computePdc (const Song& song, const std::vector<int>& channelLatency, const std::vector<std::vector<int>>& slotLatency);

// Linha de atraso estéreo com atraso inteiro, alocada na criação. process() não aloca.
class DelayLine
{
public:
    explicit DelayLine (int delaySamples);

    int length() const noexcept { return delay; }
    void process (float* left, float* right, int n) noexcept; // no lugar

private:
    int delay;
    std::vector<float> buffer; // intercalado L R
    int position = 0;
};
} // namespace lynx
