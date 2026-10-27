// SPDX-License-Identifier: MIT

#include <app/terminal_manager.hpp>

#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <app/log.hpp>

namespace consola {

TerminalManager::TerminalManager(WakeCallback wake, void *wake_context,
                                 std::chrono::milliseconds kill_timeout)
    : worker_(wake, wake_context), loop_(make_linux_io_loop(wake, wake_context, kill_timeout))
{}

TerminalManager::~TerminalManager()
{
    shutdown();
}

void TerminalManager::start()
{
    if (!started_) {
        started_ = loop_->start();
    }
}

TerminalId TerminalManager::allocate_id() noexcept
{
    return TerminalId{next_id_++};
}

bool TerminalManager::submit(TerminalInstance &instance)
{
    LaunchRequest request;
    request.id = instance.id();
    request.options = instance.options();
    return worker_.submit(std::move(request));
}

std::optional<TerminalId> TerminalManager::create_root_terminal(const TerminalOptions &options)
{
    return create_root_terminal(tabs_.active_id(), options);
}

std::optional<TerminalId> TerminalManager::create_root_terminal(TabId tab_id,
                                                                const TerminalOptions &options)
{
    Tab *tab = tabs_.find(tab_id);
    if (!started_ || !accepting_ || tab == nullptr || tab->terminal_count() != 0) {
        return std::nullopt;
    }
    const TerminalId id = allocate_id();
    auto instance = std::make_unique<TerminalInstance>(id, options.session, options.style);
    instance->set_state(TerminalState::starting);
    if (!submit(*instance)) {
        return std::nullopt;
    }
    instances_.emplace(id, instance.get());
    instance_tab_.emplace(id, tab_id);
    if (tab->add_root(std::move(instance)) == nullptr) {
        worker_.cancel(id);
        instances_.erase(id);
        instance_tab_.erase(id);
        return std::nullopt;
    }
    return id;
}

std::optional<TerminalId> TerminalManager::split_terminal(TerminalId target,
                                                          Orientation orientation,
                                                          const TerminalOptions &options,
                                                          float ratio)
{
    const std::optional<TabId> tab_id = tab_of(target);
    if (!tab_id) {
        return std::nullopt;
    }
    Tab *tab = tabs_.find(*tab_id);
    if (!started_ || !accepting_ || tab == nullptr ||
        !tab->layout().find_terminal(target).has_value() || !std::isfinite(ratio) ||
        ratio <= 0.0F || ratio >= 1.0F) {
        return std::nullopt;
    }
    const TerminalId id = allocate_id();
    auto instance = std::make_unique<TerminalInstance>(id, options.session, options.style);
    instance->set_state(TerminalState::starting);
    if (!submit(*instance)) {
        return std::nullopt;
    }
    const std::optional<NodeId> leaf =
        tab->split_terminal(target, orientation, std::move(instance), ratio);
    if (!leaf) {
        worker_.cancel(id);
        return std::nullopt;
    }
    instances_.emplace(id, tab->find(id));
    instance_tab_.emplace(id, *tab_id);
    return id;
}

bool TerminalManager::relaunch(TerminalId id, const TerminalOptions &options)
{
    if (!accepting_) {
        return false;
    }
    const auto it = instances_.find(id);
    if (it == instances_.end()) {
        return false;
    }
    TerminalInstance *instance = it->second;
    // Only a launch that never produced a process may be retried; a pane that
    // ran and then exited is handled by the normal close path.
    if (instance->state() != TerminalState::exited || instance->closing() ||
        instance->process_id() >= 0) {
        return false;
    }
    instance->prepare_relaunch(options.session);
    LaunchRequest request;
    request.id = id;
    request.options = options.session;
    if (!worker_.submit(std::move(request))) {
        instance->set_state(TerminalState::exited);
        return false;
    }
    return true;
}

TabId TerminalManager::create_tab()
{
    return tabs_.create();
}

bool TerminalManager::select_tab(TabId tab)
{
    return tabs_.select(tab);
}

std::optional<TabId> TerminalManager::select_next_tab()
{
    return tabs_.select_next();
}

std::optional<TabId> TerminalManager::select_previous_tab()
{
    return tabs_.select_previous();
}

bool TerminalManager::move_tab(TabId tab, std::size_t index)
{
    return tabs_.move(tab, index);
}

bool TerminalManager::set_tab_title(TabId tab, std::string title)
{
    Tab *found = tabs_.find(tab);
    if (found == nullptr) {
        return false;
    }
    found->set_title(std::move(title));
    return true;
}

std::optional<TabId> TerminalManager::tab_of(TerminalId id) const noexcept
{
    const auto it = instance_tab_.find(id);
    if (it == instance_tab_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool TerminalManager::close_tab(TabId tab_id)
{
    Tab *tab = tabs_.find(tab_id);
    if (tab == nullptr) {
        return false;
    }
    if (tab->terminal_count() == 0) {
        tabs_.remove(tab_id);
        return true;
    }
    // Close every pane; finalize() removes the tab once the last one is gone.
    closing_tabs_.insert(tab_id);
    const std::vector<TerminalId> ids = tab->layout().terminals();
    for (const TerminalId id : ids) {
        close_terminal(id);
    }
    return true;
}

bool TerminalManager::pump()
{
    consume_results();
    const bool dirty = consume_inbound();
    process_closing();
    drain_events();
    // If the loop thread has stopped while sessions are still registered (an
    // unrecoverable poll failure), no further acknowledgements will arrive.
    if (started_ && !instances_.empty() && !loop_->running()) {
        fail_remaining();
    }
    return dirty;
}

void TerminalManager::fail_remaining()
{
    if (instances_.empty()) {
        return;
    }
    log(LogLevel::Error, "I/O loop stopped; cleaning up remaining sessions");
    // Reject new terminals; the loop cannot serve them.
    accepting_ = false;
    // Join the loop so nothing else touches the sessions, then terminate and
    // reap any child the loop did not reap before releasing it.
    loop_->stop();
    std::vector<TerminalId> remaining;
    remaining.reserve(instances_.size());
    for (const auto &entry : instances_) {
        remaining.push_back(entry.first);
    }
    for (const TerminalId id : remaining) {
        TerminalInstance *instance = find(id);
        if (instance != nullptr && instance->session() != nullptr) {
            instance->session()->terminate();
            instance->session()->reap();
        }
        finalize(id);
    }
}

void TerminalManager::consume_results()
{
    StartupResult result;
    while (worker_.try_pop(result)) {
        const auto it = instances_.find(result.id);
        if (it == instances_.end()) {
            // Result for a terminal no longer tracked: it was never registered
            // with the loop, so terminate and reap it directly.
            if (result.session != nullptr) {
                result.session->terminate();
                result.session->reap();
            }
            continue;
        }

        TerminalInstance *instance = it->second;
        if (result.session == nullptr) {
            instance->set_state(TerminalState::exited);
            if (result.error != SessionError::cancelled) {
                failed_launches_.insert(result.id);
            }
            if (instance->closing()) {
                finalize(result.id);
            }
            continue;
        }

        // A late launch success on a closing pane is registered and then closed
        // through the normal steps (DIP 4.4).
        instance->attach(std::move(result.session), result.master_fd, result.exit_notification_fd,
                         result.process_id);
        failed_launches_.erase(result.id);
        // The emulator may have been resized before the backend attached; keep
        // the PTY grid in step (DIP 2.11).
        if (instance->emulator().columns() > 0 && instance->emulator().rows() > 0) {
            instance->session()->resize(instance->emulator().columns(),
                                        instance->emulator().rows());
        }
        if (loop_->add(*instance->session(), instance->master_fd(),
                       instance->exit_notification_fd(), instance->process_id(), instance->id(),
                       instance->options().drain_output_on_close, instance->inbound(),
                       instance->outbound())) {
            if (instance->closing()) {
                instance->set_state(TerminalState::stopping);
            }
        } else {
            log(LogLevel::Error, "session registration failed");
            auto session = instance->release_session();
            session->terminate();
            session->reap();
            instance->set_state(TerminalState::exited);
            failed_launches_.insert(result.id);
            if (instance->closing()) {
                finalize(result.id);
            }
        }
    }
}

bool TerminalManager::consume_inbound()
{
    bool dirty = false;
    for (auto &entry : instances_) {
        TerminalInstance *instance = entry.second;
        const bool discard = instance->closing() && !instance->options().drain_output_on_close;
        bool consumed = false;
        SessionInboundQueue::View view;
        while (instance->inbound().try_peek(view)) {
            consumed = true;
            if (view.kind == InboundMessage::Kind::data) {
                // Explicit-close discard policy: consume queued data without
                // feeding it or triggering a redraw (DIP 1.6).
                if (!discard) {
                    instance->emulator().feed(view.bytes);
                    dirty = true;
                }
            } else if (view.kind == InboundMessage::Kind::eof) {
                instance->mark_eof();
            } else if (view.kind == InboundMessage::Kind::exited) {
                instance->set_state(TerminalState::exited);
            }
            instance->inbound().pop();
        }
        // Wake the loop only when the UI actually consumed something and the
        // queue has room. The loop can observe a full queue and suspend just
        // after the UI drained it; consuming in this pump is the signal that
        // this race may have happened. Idle pumps consume nothing, so they do
        // not turn the idle poll into a timer. A resume can be rejected while
        // the command ring is full, so retry it until the loop accepts it.
        if (instance->state() == TerminalState::running && consumed &&
            !instance->inbound().data_full()) {
            resume_pending_.insert(entry.first);
        }
    }
    for (auto it = resume_pending_.begin(); it != resume_pending_.end();) {
        if (loop_->resume(*it)) {
            it = resume_pending_.erase(it);
        } else {
            ++it;
        }
    }
    return dirty;
}

void TerminalManager::drain_events()
{
    LoopEvent event;
    while (loop_->try_pop_event(event)) {
        if (event.kind == LoopEvent::Kind::removed) {
            finalize(event.id);
        }
    }
}

void TerminalManager::process_closing()
{
    for (auto &entry : instances_) {
        TerminalInstance *instance = entry.second;
        if (!instance->closing() || instance->session() == nullptr) {
            continue;
        }
        // Send terminate once (it is idempotent in the loop); retry until the
        // bounded command queue accepts it.
        if (!terminate_sent_.contains(entry.first)) {
            if (!loop_->terminate(entry.first)) {
                continue;
            }
            terminate_sent_.insert(entry.first);
        }
        // Request removal only after terminate was accepted and the child has
        // exited, so the acknowledgement covers the termination sequence.
        if (instance->state() == TerminalState::exited &&
            !remove_requested_.contains(entry.first)) {
            if (loop_->remove(entry.first)) {
                remove_requested_.insert(entry.first);
            }
        }
    }
}

void TerminalManager::finalize(TerminalId id)
{
    const auto it = instances_.find(id);
    if (it == instances_.end()) {
        return;
    }
    // The loop has retired this session's poll entries, so the backend can be
    // released and the instance destroyed.
    it->second->release_session();
    TabId tab_id{};
    const auto tab_it = instance_tab_.find(id);
    if (tab_it != instance_tab_.end()) {
        tab_id = tab_it->second;
        if (Tab *tab = tabs_.find(tab_id); tab != nullptr) {
            tab->remove_terminal(id);
        }
        instance_tab_.erase(tab_it);
    }
    instances_.erase(it);
    terminate_sent_.erase(id);
    remove_requested_.erase(id);
    resume_pending_.erase(id);
    // A tab being closed is removed once its last terminal is finalized.
    if (tab_id.value != 0 && closing_tabs_.contains(tab_id)) {
        Tab *tab = tabs_.find(tab_id);
        if (tab != nullptr && tab->terminal_count() == 0) {
            closing_tabs_.erase(tab_id);
            tabs_.remove(tab_id);
        }
    }
}

void TerminalManager::begin_shutdown()
{
    if (shutdown_started_) {
        return;
    }
    shutdown_started_ = true;
    accepting_ = false;

    // Every pane is closing for the purpose of removal.
    for (auto &entry : instances_) {
        entry.second->set_closing(true);
    }

    // Resolve pending launches: a late success is registered and closed, a
    // failed/cancelled launch completes without a registration (DIP 4.5).
    worker_.stop();
    StartupResult result;
    while (worker_.try_pop(result)) {
        const auto it = instances_.find(result.id);
        if (it == instances_.end()) {
            if (result.session != nullptr) {
                result.session->terminate();
                result.session->reap();
            }
            continue;
        }
        TerminalInstance *instance = it->second;
        if (result.session == nullptr) {
            instance->set_state(TerminalState::exited);
            finalize(result.id);
            continue;
        }
        instance->attach(std::move(result.session), result.master_fd, result.exit_notification_fd,
                         result.process_id);
        if (loop_->add(*instance->session(), instance->master_fd(),
                       instance->exit_notification_fd(), instance->process_id(), instance->id(),
                       instance->options().drain_output_on_close, instance->inbound(),
                       instance->outbound())) {
            instance->set_state(TerminalState::stopping);
        } else {
            // Registration failed: the session was never handed to the loop, so
            // terminate and reap it before releasing.
            auto session = instance->release_session();
            session->terminate();
            session->reap();
            finalize(result.id);
        }
    }

    // Instances whose launch failed or was cancelled have no loop registration
    // to retire; finalize them directly. Every remaining attached session is
    // terminated and removed by process_closing() while the caller pumps.
    std::vector<TerminalId> unregistered;
    for (const auto &entry : instances_) {
        if (entry.second->session() == nullptr) {
            unregistered.push_back(entry.first);
        }
    }
    for (const TerminalId id : unregistered) {
        finalize(id);
    }
}

bool TerminalManager::shutdown_complete() const noexcept
{
    return instances_.empty();
}

void TerminalManager::finish_shutdown()
{
    if (shutdown_finished_) {
        return;
    }
    shutdown_finished_ = true;
    loop_->shutdown();
    loop_->stop();
}

void TerminalManager::shutdown()
{
    begin_shutdown();
    // Wait for every child to be reaped and every removal acknowledged. The
    // loop escalates to SIGKILL, so this terminates for any killable child; a
    // registered session is never released before its acknowledgement.
    while (!instances_.empty() && loop_->running()) {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!instances_.empty()) {
        // The loop stopped before acknowledging (unrecoverable poll failure):
        // use the safe fallback that joins the loop and reaps un-reaped
        // children before releasing them.
        fail_remaining();
    }
    finish_shutdown();
}

bool TerminalManager::close_terminal(TerminalId id)
{
    const auto it = instances_.find(id);
    if (it == instances_.end()) {
        return false;
    }
    TerminalInstance *instance = it->second;
    if (instance->closing()) {
        return true;
    }
    instance->set_closing(true);

    if (instance->session() == nullptr) {
        if (instance->state() == TerminalState::starting) {
            // Cancel the launch; the instance is retained until the result is
            // consumed (DIP 4.4).
            worker_.cancel(id);
        } else {
            // Failed launch with no registration; nothing to retire.
            finalize(id);
        }
    }
    // Attached sessions are terminated and removed by process_closing(), which
    // retries bounded loop commands until they are accepted.
    return true;
}

bool TerminalManager::send(TerminalId id, std::span<const std::byte> bytes)
{
    const auto it = instances_.find(id);
    if (it == instances_.end() || bytes.empty()) {
        return false;
    }
    TerminalInstance *instance = it->second;
    if (!instance->input_enabled()) {
        return false;
    }
    if (!instance->outbound().try_push(bytes)) {
        return false;
    }
    loop_->write_pending(id);
    return true;
}

void TerminalManager::resize(TerminalId id, int columns, int rows)
{
    const auto it = instances_.find(id);
    if (it == instances_.end()) {
        return;
    }
    TerminalInstance *instance = it->second;
    // Send a resize only when the resulting grid changes (DIP 2.11).
    if (columns == instance->emulator().columns() && rows == instance->emulator().rows()) {
        return;
    }
    instance->emulator().resize(columns, rows);
    if (instance->session() != nullptr) {
        instance->session()->resize(columns, rows);
    }
}

TerminalInstance *TerminalManager::find(TerminalId id) noexcept
{
    const auto it = instances_.find(id);
    return it == instances_.end() ? nullptr : it->second;
}

void TerminalManager::focus(TerminalId id) noexcept
{
    const std::optional<TabId> tab_id = tab_of(id);
    if (!tab_id) {
        return;
    }
    tabs_.select(*tab_id);
    if (Tab *tab = tabs_.find(*tab_id); tab != nullptr) {
        tab->layout().focus(id);
    }
}

std::optional<TerminalId> TerminalManager::focused() const noexcept
{
    return tabs_.active().layout().focused();
}

bool TerminalManager::set_split_ratio(NodeId split, float ratio) noexcept
{
    return tabs_.active().layout().set_split_ratio(split, ratio);
}

bool TerminalManager::swap_terminals(TerminalId first, TerminalId second) noexcept
{
    return tabs_.active().layout().swap_terminals(first, second);
}

void TerminalManager::equalize() noexcept
{
    tabs_.active().layout().equalize();
}

std::vector<TerminalId> TerminalManager::terminal_ids() const
{
    std::vector<TerminalId> out;
    out.reserve(instances_.size());
    for (const auto &entry : instances_) {
        out.push_back(entry.first);
    }
    return out;
}

TerminalEmulator &TerminalManager::emulator(TerminalId id) noexcept
{
    return instances_.at(id)->emulator();
}

} // namespace consola
