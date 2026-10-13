#include "core/WorkerPool.h"

#include "core/Denormals.h"

namespace lynx
{
WorkerPool::WorkerPool (int n, std::function<void()> onThreadStart)
{
    next.store (1 << 30); // nothing to take until the first parallelFor

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
    while (true)
    {
        const auto i = next.fetch_add (1, std::memory_order_acq_rel);

        if (i >= count.load (std::memory_order_acquire))
            return;

        call (context, i);
        done.fetch_add (1, std::memory_order_acq_rel);
    }
}

void WorkerPool::run (int n, void* ctx, Call fn) noexcept
{
    if (n <= 0)
        return;

    if (n == 1 || threads.empty())
    {
        for (int i = 0; i < n; ++i)
            fn (ctx, i);
        return;
    }

    context = ctx;
    call = fn;
    done.store (0, std::memory_order_relaxed);
    count.store (n, std::memory_order_release);
    next.store (0, std::memory_order_release); // from here on the workers may take indices

    wake.release ((std::ptrdiff_t) std::min<int> ((int) threads.size(), n - 1));
    work();

    while (done.load (std::memory_order_acquire) < n)
        std::this_thread::yield();

    next.store (1 << 30, std::memory_order_release); // late workers take nothing
}
} // namespace lynx
