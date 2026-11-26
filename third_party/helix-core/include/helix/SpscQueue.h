// Fixed-capacity single-producer / single-consumer queue. Lock free, no allocation after construction.
// Used to hand loaded wavetables to the audio thread and to hand retired ones back for deletion.
#pragma once
#include <atomic>
#include <cstddef>
#include <utility>

namespace helix {

template <typename T, size_t Capacity>
class SpscQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    bool push(T&& v)
    {
        const size_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= Capacity) return false;
        slots_[w & (Capacity - 1)] = std::move(v);
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out)
    {
        const size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        out = std::move(slots_[r & (Capacity - 1)]);
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    bool full() const { return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire) >= Capacity; }
    bool empty() const { return write_.load(std::memory_order_acquire) == read_.load(std::memory_order_acquire); }
private:
    T slots_[Capacity];
    std::atomic<size_t> write_{0};
    std::atomic<size_t> read_{0};
};

} // namespace helix
