#pragma once

// Thread pool for the audio thread to split a block's work (channels and
// mixer tracks), like FL does with the mixer layers (docs/ANALISE_FL_STUDIO.md §2.2).
// parallelFor neither allocates nor blocks on a mutex: workers wake via semaphore and take
// indices from an atomic counter; the calling thread also works and spin-waits.

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
    // `threads` workers besides the calling thread. `onThreadStart` runs on each worker
    // (e.g. raising the priority); may be empty.
    explicit WorkerPool (int threads, std::function<void()> onThreadStart = {});
    ~WorkerPool();

    WorkerPool (const WorkerPool&) = delete;
    WorkerPool& operator= (const WorkerPool&) = delete;

    int workers() const noexcept { return (int) threads.size(); }

    // Calls fn(i) for i in [0, count), in parallel. Returns when all have finished.
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

    // current work (published before `next` goes back to 0)
    void* context = nullptr;
    Call call = nullptr;
    std::atomic<int> count { 0 };
    std::atomic<int> next { 0 };
    std::atomic<int> done { 0 };
};
} // namespace lynx
