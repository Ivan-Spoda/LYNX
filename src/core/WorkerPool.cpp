#include "core/WorkerPool.h"

#include "core/Denormals.h"

namespace lynx
{
WorkerPool::WorkerPool (int n, std::function<void()> onThreadStart)
{
    // ticket 0: count 0, nothing to take until the first parallelFor

    for (int i = 0; i < n; ++i)
        threads.emplace_back ([this, onThreadStart] { workerLoop (onThreadStart); });
}

WorkerPool::~WorkerPool()
{
    quit.store (true);
    wake.release ((std::ptrdiff_t) threads.size());

    for (auto& t : threads)
        t.join();
}

void WorkerPool::workerLoop (std::function<void()> onStart)
{
    const ScopedFlushDenormals noDenormals;

    if (onStart)
        onStart();

    while (true)
    {
        wake.acquire();

        if (quit.load())
            return;

        work();
    }
}

void WorkerPool::work() noexcept
{
    constexpr auto mask = (std::uint64_t (1) << indexBits) - 1;
    while (true)
    {
        const auto t = ticket.fetch_add (1, std::memory_order_acq_rel);
        const auto i = t & mask, n = (t >> indexBits) & mask;

        if (i >= n)
            return;

        call (context, (int) i);
        done.fetch_add (1, std::memory_order_acq_rel);
    }
}

void WorkerPool::run (int n, void* ctx, Call fn) noexcept
{
    if (n <= 0)
        return;

    if (n == 1 || threads.empty() || n >= (1 << indexBits))
    {
        for (int i = 0; i < n; ++i)
            fn (ctx, i);
        return;
    }

    context = ctx;
    call = fn;
    done.store (0, std::memory_order_relaxed);
    ++generation;
    const auto base = generation << (2 * indexBits);
    ticket.store (base | ((std::uint64_t) n << indexBits), std::memory_order_release); // from here on the workers may take indices

    wake.release ((std::ptrdiff_t) std::min<int> ((int) threads.size(), n - 1));
    work();

    while (done.load (std::memory_order_acquire) < n)
        std::this_thread::yield();

    ticket.store (base, std::memory_order_release); // count 0: late workers take nothing
}
} // namespace lynx
