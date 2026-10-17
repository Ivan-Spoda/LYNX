#pragma once

// One value handed from a non-real-time thread to the audio thread without locks or allocation on the audio side:
// the writer fills the slot only while it is empty, the audio thread copies it out and empties it.

#include <atomic>

namespace lynx
{
template <typename T>
class StateHandoff
{
public:
    // writer (not the audio thread): false while the previous value has not been taken yet
    bool offer (const T& value)
    {
        if (full.load (std::memory_order_acquire))
            return false;
        slot = value;
        full.store (true, std::memory_order_release);
        return true;
    }

    // audio thread: the offered value, if any
    bool take (T& out) noexcept
    {
        if (! full.load (std::memory_order_acquire))
            return false;
        out = slot;
        full.store (false, std::memory_order_release);
        return true;
    }

private:
    T slot {};
    std::atomic<bool> full { false };
};
} // namespace lynx
