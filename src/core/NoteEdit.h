#pragma once

// Operações de edição de notas usadas pelo Piano Roll (seleção, copiar/colar,
// quantizar...). Trabalham com índices em Pattern::notes. Sem JUCE, testáveis.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/Song.h"

namespace lynx::noteedit
{
// Ordena itens com campo `start` (notas, clips) e atualiza `tracked` (índices) para as novas posições.
template <typename T>
void sortByStartTracking (std::vector<T>& items, std::vector<int>& tracked)
{
    std::vector<int> order (items.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = (int) i;

    std::stable_sort (order.begin(), order.end(),
                      [&] (int a, int b) { return items[(std::size_t) a].start < items[(std::size_t) b].start; });

    std::vector<int> newIndex (order.size());
    std::vector<T> sorted;
    sorted.reserve (order.size());

    for (std::size_t j = 0; j < order.size(); ++j)
    {
        newIndex[(std::size_t) order[j]] = (int) j;
        sorted.push_back (items[(std::size_t) order[j]]);
    }

    items = std::move (sorted);

    for (auto& i : tracked)
        if (i >= 0 && i < (int) newIndex.size())
            i = newIndex[(std::size_t) i];
}

// Ordena as notas por início e atualiza `tracked` (índices) para as novas posições.
void sortTracking (Pattern& pattern, std::vector<int>& tracked);

// Remove as notas dos índices dados (em qualquer ordem, repetidos ignorados).
void remove (Pattern& pattern, std::vector<int> indices);

// Cópias das notas dos índices dados, em ordem de início.
std::vector<Note> copy (const Pattern& pattern, const std::vector<int>& indices);

// Acrescenta `notes` deslocadas de `offset` ticks; channel >= 0 força o canal.
// Retorna os índices das notas novas (o vetor não é reordenado).
std::vector<int> paste (Pattern& pattern, const std::vector<Note>& notes, std::int64_t offset, int channel = -1);

// Início da primeira nota e fim da última (0, 0 se vazio).
void span (const std::vector<Note>& notes, std::int64_t& start, std::int64_t& end);

// Duplica as notas logo depois delas mesmas (como o Ctrl+B do FL): o deslocamento é o
// tamanho do trecho arredondado para cima em múltiplos de `grid`. Retorna os índices novos.
std::vector<int> duplicate (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// Arredonda o início das notas para o múltiplo de `grid` mais próximo (duração mantida).
void quantize (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// Move as notas por (deltaTicks, deltaKeys), limitando para nenhuma sair de [0, ...) e [0, 127].
// Retorna o deslocamento aplicado de fato.
void move (Pattern& pattern, const std::vector<int>& indices, std::int64_t& deltaTicks, int& deltaKeys);
} // namespace lynx::noteedit
