// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

namespace consola {

// Bounded single-producer/single-consumer ring (DIP 1.7). Capacity is the
// number of usable slots; the backing array holds Capacity + 1 so that full
// and empty are distinguishable. Only one thread may be inside the producer
// methods and only one (different) thread inside the consumer methods.
//
// The producer fills the slot returned by prepare_push() and then publishes it
// with commit_push(); the consumer observes it through front()/pop(). The
// slot contents are only read by the consumer after the release/acquire pair,
// so no additional locking is needed.
template <typename T, std::size_t Capacity> class SpscRing {
public:
    static_assert(Capacity > 0, "SpscRing needs at least one slot");

    // Producer. Returns a slot to fill, or nullptr if full.
    [[nodiscard]] T *prepare_push() noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) % slots_.size();
        if (next == tail_.load(std::memory_order_acquire)) {
            return nullptr;
        }
        return &slots_[head];
    }

    void commit_push() noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        head_.store((head + 1) % slots_.size(), std::memory_order_release);
    }

    // Consumer. Returns the oldest published slot, or nullptr if empty.
    [[nodiscard]] const T *front() const noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return nullptr;
        }
        return &slots_[tail];
    }

    [[nodiscard]] T *front_mut() noexcept
    {
        return const_cast<T *>(std::as_const(*this).front());
    }

    void pop() noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        tail_.store((tail + 1) % slots_.size(), std::memory_order_release);
    }

    // Consumer. Moves the oldest slot into out. Returns false if empty.
    [[nodiscard]] bool try_pop(T &out) noexcept
    {
        T *slot = front_mut();
        if (slot == nullptr) {
            return false;
        }
        out = std::move(*slot);
        pop();
        return true;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return front() == nullptr;
    }

    [[nodiscard]] bool full() const noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        return (head + 1) % slots_.size() == tail_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::size_t> head_{0};
    std::array<T, Capacity + 1> slots_{};
    std::atomic<std::size_t> tail_{0};
};

} // namespace consola
