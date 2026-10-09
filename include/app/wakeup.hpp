// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>

#include <app/session.hpp>

namespace consola {

// Coalesces loop -> UI wakeups behind one atomic flag (DIP 1.7). The loop
// calls notify() after producing events; the wake callback runs only when
// notify() flips the flag from false to true, so FLTK's fixed awake queue
// cannot overflow. The UI clears the flag *before* draining every queue, so a
// notification that lands during the drain sets the flag again and wakes once
// more; no wakeup is lost and several notifications collapse into one redraw.
class WakeupFlag {
public:
    explicit WakeupFlag(WakeCallback wake = nullptr, void *context = nullptr) noexcept
        : wake_(wake), context_(context)
    {}

    void notify() noexcept
    {
        if (!pending_.exchange(true, std::memory_order_acq_rel) && wake_ != nullptr) {
            wake_(context_);
        }
    }

    // UI: clear first, then drain every queue.
    void clear() noexcept
    {
        pending_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool pending() const noexcept
    {
        return pending_.load(std::memory_order_acquire);
    }

private:
    WakeCallback wake_ = nullptr;
    void *context_ = nullptr;
    std::atomic<bool> pending_{false};
};

} // namespace consola
