// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>

#include <app/session.hpp>
#include <app/tab.hpp>
#include <app/terminal_instance.hpp>
#include <platform/io_loop.hpp>
#include <platform/startup.hpp>

namespace consola {

// Options for creating a terminal through the factory (DIP 4.3).
struct TerminalOptions {
    SessionOptions session;
};

// Owns the application-wide startup worker and I/O loop and is the single
// asynchronous terminal factory (DIP 3.0, 4.3). It also owns the single Stage 3
// tab, so terminal instances outlive the loop they are registered with and are
// destroyed only after the manager has terminated them and stopped the loop.
// UI-thread only. The create_* methods queue a launch and return a Starting
// instance owned by the tab; pump() consumes launch results, loop events and
// inbound data. Close and shutdown barriers are added in S3-CS4.
class TerminalManager {
public:
    TerminalManager(WakeCallback wake = nullptr, void *wake_context = nullptr,
                    std::chrono::milliseconds kill_timeout = std::chrono::milliseconds{2000});
    ~TerminalManager();

    TerminalManager(const TerminalManager &) = delete;
    TerminalManager &operator=(const TerminalManager &) = delete;
    TerminalManager(TerminalManager &&) = delete;
    TerminalManager &operator=(TerminalManager &&) = delete;

    void start();

    [[nodiscard]] Tab &tab() noexcept
    {
        return tab_;
    }
    [[nodiscard]] const Tab &tab() const noexcept
    {
        return tab_;
    }

    // Create a terminal as the tab's root. Returns its id, or nullopt when the
    // tab is not empty or the launch could not be queued.
    std::optional<TerminalId> create_root_terminal(const TerminalOptions &options);

    // Create a terminal and split `target`'s leaf, placing it in the second
    // half. Returns its id, or nullopt when the target/ratio is invalid or the
    // launch could not be queued.
    std::optional<TerminalId> split_terminal(TerminalId target, Orientation orientation,
                                             const TerminalOptions &options, float ratio = 0.5F);

    // Consume launch results, loop events and inbound data for all live
    // terminals. Returns true when any emulator was fed (a redraw is needed).
    bool pump();

    // Route encoded terminal output (keys/paste) to a running session.
    void send(TerminalId id, std::span<const std::byte> bytes);

    void resize(TerminalId id, int columns, int rows);

    [[nodiscard]] TerminalInstance *find(TerminalId id) noexcept;

    [[nodiscard]] bool launch_failed() const noexcept
    {
        return launch_failed_;
    }

private:
    [[nodiscard]] TerminalId allocate_id() noexcept;
    bool submit(TerminalInstance &instance);
    void consume_results();
    bool consume_inbound();

    StartupWorker worker_;
    std::unique_ptr<IoLoop> loop_;
    std::unordered_map<TerminalId, TerminalInstance *> instances_;
    Tab tab_;
    std::uint64_t next_id_ = 1;
    bool started_ = false;
    bool launch_failed_ = false;
};

} // namespace consola
