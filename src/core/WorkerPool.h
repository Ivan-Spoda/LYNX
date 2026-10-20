#pragma once

// Thread pool for the audio thread to split a block's work (channels and
// mixer tracks), like FL does with the mixer layers (docs/ANALISE_FL_STUDIO.md §2.2).
// parallelFor neither allocates nor blocks on a mutex: workers take indices from an atomic counter; the calling
// thread also works and spin-waits for the last index.
//
// Waking the workers (REA round 87): a worker that just finished spins for a short while (the next parallelFor of
// the same block — the next mixer layer, the next chunk — comes microseconds later), then sleeps on an OS
// semaphore (a Win32 semaphore on Windows, a libdispatch one on macOS, a futex-backed POSIX one on Linux). std::counting_semaphore is NOT used:
// the Windows libc++ build has no platform wait and falls back to Sleep() polling with growing intervals, so an
// idle worker woke 1–8 ms late — later than a whole audio block — and the audio thread ended up doing all the
// work alone.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
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

    // How long a worker keeps polling for new work after finishing before it sleeps (microseconds).
    static constexpr int spinMicros = 250;

    // Diagnostics: how many indices the workers (not the calling thread) ran, and how many times a worker
    // was woken from sleep. Relaxed counters.
    std::uint64_t indicesRunByWorkers() const noexcept { return byWorkers.load (std::memory_order_relaxed); }
    std::uint64_t wakeUps() const noexcept { return wokenUp.load (std::memory_order_relaxed); }

private:
    using Call = void (*) (void*, int);

    void run (int count, void* context, Call call) noexcept;
    bool work (bool isWorker) noexcept;
    bool hasWork() const noexcept;
    void workerLoop (std::function<void()> onStart);

    struct Semaphore; // OS semaphore (WorkerPool.cpp)
    std::unique_ptr<Semaphore> wake;
    std::vector<std::thread> threads;
    std::atomic<bool> quit { false };
    std::atomic<int> sleeping { 0 }; // workers blocked (or about to block) on `wake`

    // current work (published before `ticket`). The ticket packs generation (bits 40+), count (bits 20…39) and the
    // next index (bits 0…19), so an index taken by a late worker always carries its own round's count: a worker
    // that stalls between taking an index and running it can never run an index of a later round (no ABA)
    void* context = nullptr;
    Call call = nullptr;
    static constexpr int indexBits = 20;
    std::atomic<std::uint64_t> ticket { 0 };
    std::uint64_t generation = 0; // caller thread only
    std::atomic<int> done { 0 };
    std::atomic<std::uint64_t> byWorkers { 0 }, wokenUp { 0 };
};
} // namespace lynx
