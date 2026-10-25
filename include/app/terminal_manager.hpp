// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <app/session.hpp>
#include <app/tab.hpp>
#include <app/tab_manager.hpp>
#include <app/terminal_instance.hpp>
#include <app/terminal_style.hpp>
#include <platform/io_loop.hpp>
#include <platform/startup.hpp>

namespace consola {

// Options for creating a terminal through the factory (DIP 4.3). The style is
// resolved from configuration at the wiring boundary and applied to both the
// emulator (palette, scrollback, cursor) and the widget.
struct TerminalOptions {
    SessionOptions session;
    TerminalStyle style = default_terminal_style();
};

// Owns the application-wide startup worker and I/O loop and is the single
// asynchronous terminal factory (DIP 3.0, 4.3). It owns the TabManager, so
// terminal instances outlive the loop they are registered with and are
// destroyed only after the manager has terminated them and stopped the loop.
// UI-thread only. The create_* methods queue a launch and return a Starting
// instance owned by a tab; pump() consumes launch results, loop events and
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

    // Tab ownership (DIP 4.1, 4.2). Tabs are exposed read-only so callers cannot
    // remove instances or mutate layouts behind the manager's registry; tab
    // mutations go through the manager methods below.
    [[nodiscard]] const TabManager &tabs() const noexcept
    {
        return tabs_;
    }

    // The active tab (always valid: TabManager keeps at least one).
    [[nodiscard]] const Tab &tab() const noexcept
    {
        return tabs_.active();
    }
    [[nodiscard]] const Tab *active_tab() const noexcept
    {
        return &tabs_.active();
    }

    // Number of live terminals across every tab.
    [[nodiscard]] std::size_t terminal_count() const noexcept
    {
        return instances_.size();
    }

    // Append a new empty tab and make it active (DIP 4.2).
    TabId create_tab();

    // Select a tab, or move the selection with wraparound. next/previous return
    // the new active id, or nullopt when there is only one tab.
    bool select_tab(TabId tab);
    std::optional<TabId> select_next_tab();
    std::optional<TabId> select_previous_tab();

    // Reorder a tab to `index` (clamped). Returns false for an unknown id.
    bool move_tab(TabId tab, std::size_t index);

    // Set a tab's presentation label (DIP 4.2 rename). Returns false for an
    // unknown id. This is the only tab mutation not tied to terminal lifecycle.
    bool set_tab_title(TabId tab, std::string title);

    // The tab that owns a live terminal, or nullopt for an unknown id.
    [[nodiscard]] std::optional<TabId> tab_of(TerminalId id) const noexcept;

    // Orderly teardown (DIP 4.5). begin_shutdown() rejects new terminals,
    // resolves pending launches (registering a late success and closing it,
    // finalizing a failed/cancelled launch) and marks every pane closing; the
    // caller then keeps pumping until shutdown_complete(), after which
    // finish_shutdown() stops the loop. This keeps the UI event loop alive
    // through reaping and removal acknowledgements.
    void begin_shutdown();
    [[nodiscard]] bool shutdown_complete() const noexcept;
    void finish_shutdown();

    // Synchronous convenience used by tests and the destructor: begins shutdown
    // and pumps until every child is reaped and every removal acknowledged. It
    // never releases a registered session before its acknowledgement.
    void shutdown();

    // Create a terminal as the active tab's root. Returns its id, or nullopt
    // when the tab is not empty, the manager is shutting down, or the launch
    // could not be queued.
    std::optional<TerminalId> create_root_terminal(const TerminalOptions &options);

    // Create a terminal as `tab`'s root. Returns nullopt for an unknown or
    // non-empty tab, when shutting down, or when the launch could not be queued.
    std::optional<TerminalId> create_root_terminal(TabId tab, const TerminalOptions &options);

    // Create a terminal and split `target`'s leaf, placing it in the second
    // half. Returns its id, or nullopt when the target/ratio is invalid, the
    // manager is shutting down, or the launch could not be queued.
    std::optional<TerminalId> split_terminal(TerminalId target, Orientation orientation,
                                             const TerminalOptions &options, float ratio = 0.5F);

    // Begin closing a tab (DIP 4.2): every terminal in it is closed through the
    // normal pane-close path, and the tab is removed once it is empty. Returns
    // false for an unknown tab.
    bool close_tab(TabId tab);

    // Consume launch results, loop events and inbound data for all live
    // terminals. Returns true when any emulator was fed (a redraw is needed).
    bool pump();

    // Route encoded terminal output (keys/paste) to a running session. Returns
    // false when the pane is unknown, not input-enabled, or its outbound queue
    // is full (DIP 1.7); callers may report a rejected broadcast destination.
    bool send(TerminalId id, std::span<const std::byte> bytes);

    // Begin closing a pane (DIP 4.4): a Starting pane cancels its launch and is
    // retained until the result is consumed; a running pane is terminated
    // through the loop. The pane's instance and layout node are removed once the
    // loop acknowledges removal, driven by pump(). Returns false for an unknown
    // id. Idempotent while a close is in progress.
    bool close_terminal(TerminalId id);

    void resize(TerminalId id, int columns, int rows);

    // Pane focus and layout operations forwarded to the tab's layout tree
    // (DIP 3.5). focus() ignores ids that are not present.
    void focus(TerminalId id) noexcept;
    [[nodiscard]] std::optional<TerminalId> focused() const noexcept;
    bool set_split_ratio(NodeId split, float ratio) noexcept;
    // Swap two terminals in the active tab's layout (pane movement, DIP 6.1).
    bool swap_terminals(TerminalId first, TerminalId second) noexcept;
    void equalize() noexcept;

    [[nodiscard]] TerminalInstance *find(TerminalId id) noexcept;

    // Ids of all live terminals, for presentation code that must iterate them
    // without gaining structural or lifecycle access to the instances.
    [[nodiscard]] std::vector<TerminalId> terminal_ids() const;

    // The emulator of a live terminal. Precondition: the id exists (obtain it
    // from terminal_ids() or find()).
    [[nodiscard]] TerminalEmulator &emulator(TerminalId id) noexcept;

    [[nodiscard]] bool launch_failed() const noexcept
    {
        return launch_failed_;
    }

private:
    [[nodiscard]] TerminalId allocate_id() noexcept;
    bool submit(TerminalInstance &instance);
    void consume_results();
    bool consume_inbound();
    void process_closing();
    void drain_events();
    void finalize(TerminalId id);
    void fail_remaining();

    StartupWorker worker_;
    std::unique_ptr<IoLoop> loop_;
    std::unordered_map<TerminalId, TerminalInstance *> instances_;
    std::unordered_map<TerminalId, TabId> instance_tab_;
    std::unordered_set<TerminalId> terminate_sent_;
    std::unordered_set<TerminalId> remove_requested_;
    std::unordered_set<TabId> closing_tabs_;
    TabManager tabs_;
    std::uint64_t next_id_ = 1;
    bool started_ = false;
    bool accepting_ = true;
    bool shutdown_started_ = false;
    bool shutdown_finished_ = false;
    bool launch_failed_ = false;
};

} // namespace consola
