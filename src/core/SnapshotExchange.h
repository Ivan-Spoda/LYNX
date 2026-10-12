#pragma once

// Entrega objetos imutáveis da UI para a thread de áudio sem locks nem
// alocação/liberação na thread de áudio.
//
//  - UI:    publish(novo)   → coloca na caixa de entrada (substitui um ainda não lido)
//           collectGarbage() → libera os objetos que o áudio devolveu
//  - Áudio: acquire()       → pega o mais recente, devolve o anterior para a UI liberar
//
// Só uma thread de UI e uma thread de áudio podem usar a mesma instância.

#include <array>
#include <atomic>
#include <memory>

namespace lynx
{
template <typename T>
class SnapshotExchange
{
public:
    SnapshotExchange() = default;
    SnapshotExchange (const SnapshotExchange&) = delete;
    SnapshotExchange& operator= (const SnapshotExchange&) = delete;

    ~SnapshotExchange()
    {
        delete incoming.exchange (nullptr);
        delete active;
        collectGarbage();
    }

    // UI
    void publish (std::unique_ptr<const T> next)
    {
        collectGarbage();
        delete incoming.exchange (next.release(), std::memory_order_acq_rel); // um não lido é simplesmente trocado
    }

    // UI
    void collectGarbage()
    {
        for (auto& slot : retired)
            delete slot.exchange (nullptr, std::memory_order_acq_rel);
    }

    // Áudio: retorna o snapshot atual (pode ser nullptr antes do primeiro publish).
    const T* acquire() noexcept
    {
        if (incoming.load (std::memory_order_relaxed) == nullptr)
            return active;

        // Precisa de uma vaga para devolver o anterior; sem vaga, continua com o atual
        // e tenta de novo no próximo bloco.
        std::atomic<const T*>* freeSlot = nullptr;

        if (active != nullptr)
        {
            for (auto& slot : retired)
                if (slot.load (std::memory_order_acquire) == nullptr)
                {
                    freeSlot = &slot;
                    break;
                }

            if (freeSlot == nullptr)
                return active;
        }

        auto* next = incoming.exchange (nullptr, std::memory_order_acq_rel);

        if (next == nullptr)
            return active;

        if (freeSlot != nullptr)
            freeSlot->store (active, std::memory_order_release);

        active = next;
        return active;
    }

private:
    std::atomic<const T*> incoming { nullptr };
    std::array<std::atomic<const T*>, 8> retired {};
    const T* active = nullptr; // só a thread de áudio toca
};
} // namespace lynx
