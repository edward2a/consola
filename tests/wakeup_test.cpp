// SPDX-License-Identifier: MIT

#include <app/wakeup.hpp>

#include <atomic>
#include <cstdio>
#include <thread>

namespace {

std::atomic<int> g_wake_count{0};

void count_wake(void *context)
{
    static_cast<void>(context);
    g_wake_count.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

int main()
{
    consola::WakeupFlag flag(&count_wake, nullptr);

    for (int i = 0; i < 1000; ++i) {
        flag.notify();
    }
    if (g_wake_count.load() != 1) {
        std::printf("coalescing failed: %d wakes\n", g_wake_count.load());
        return 1;
    }
    if (!flag.pending()) {
        std::printf("flag should be pending\n");
        return 1;
    }

    flag.clear();
    if (flag.pending()) {
        std::printf("clear did not reset the flag\n");
        return 1;
    }
    flag.notify();
    if (g_wake_count.load() != 2) {
        std::printf("wake lost after clear\n");
        return 1;
    }

    consola::WakeupFlag null_flag(nullptr, nullptr);
    null_flag.notify();
    null_flag.notify();
    if (!null_flag.pending()) {
        std::printf("null-callback flag not pending\n");
        return 1;
    }

    // Producer wakes continuously; the consumer clears and drains. The callback
    // must run at least once and never after the flag stays set without a clear.
    g_wake_count.store(0);
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            flag.notify();
        }
    });
    int observed = 0;
    for (int i = 0; i < 1000; ++i) {
        flag.clear();
        if (flag.pending()) {
            ++observed;
        }
        std::this_thread::yield();
    }
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    if (observed == 0 && g_wake_count.load() == 0) {
        std::printf("no wake observed under concurrency\n");
        return 1;
    }

    std::printf("wakeup_test ok\n");
    return 0;
}
