#pragma once

// Pool de threads para a thread de áudio dividir o trabalho de um bloco (canais e
// trilhas do mixer), como o FL faz com as camadas do mixer (docs/ANALISE_FL_STUDIO.md §2.2).
// parallelFor não aloca nem trava em mutex: os workers acordam por semáforo e pegam
// índices num contador atômico; a thread chamadora também trabalha e espera girando.

#include <atomic>
#include <functional>
#include <semaphore>
#include <thread>
#include <vector>

namespace lynx
{
class WorkerPool
{
public:
    // `threads` workers além da thread chamadora. `onThreadStart` roda em cada worker
    // (ex.: subir a prioridade); pode ser vazio.
    explicit WorkerPool (int threads, std::function<void()> onThreadStart = {});
    ~WorkerPool();

    WorkerPool (const WorkerPool&) = delete;
    WorkerPool& operator= (const WorkerPool&) = delete;

    int workers() const noexcept { return (int) threads.size(); }

    // Chama fn(i) para i em [0, count), em paralelo. Volta quando todos terminarem.
    template <typename Fn>
    void parallelFor (int count, Fn& fn) noexcept
    {
        run (count, &fn, [] (void* f, int i) { (*static_cast<Fn*> (f)) (i); });
    }

private:
    using Call = void (*) (void*, int);

    void run (int count, void* context, Call call) noexcept;
    void work() noexcept;
    void workerLoop (std::function<void()> onStart);

    std::vector<std::thread> threads;
    std::counting_semaphore<> wake { 0 };
    std::atomic<bool> quit { false };

    // trabalho atual (publicado antes de `next` voltar a 0)
    void* context = nullptr;
    Call call = nullptr;
    std::atomic<int> count { 0 };
    std::atomic<int> next { 0 };
    std::atomic<int> done { 0 };
};
} // namespace lynx
