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
        loop_->start();
        started_ = true;
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
    if (!started_ || !accepting_ || tab_.terminal_count() != 0) {
        return std::nullopt;
    }
    const TerminalId id = allocate_id();
    auto instance = std::make_unique<TerminalInstance>(id, options.session);
    instance->set_state(TerminalState::starting);
    if (!submit(*instance)) {
        return std::nullopt;
    }
    instances_.emplace(id, instance.get());
    if (tab_.add_root(std::move(instance)) == nullptr) {
        worker_.cancel(id);
        instances_.erase(id);
        return std::nullopt;
    }
    return id;
}

std::optional<TerminalId> TerminalManager::split_terminal(TerminalId target,
                                                          Orientation orientation,
                                                          const TerminalOptions &options,
                                                          float ratio)
{
    if (!started_ || !accepting_ || !tab_.layout().find_terminal(target).has_value() ||
        !std::isfinite(ratio) || ratio <= 0.0F || ratio >= 1.0F) {
        return std::nullopt;
    }
    const TerminalId id = allocate_id();
    auto instance = std::make_unique<TerminalInstance>(id, options.session);
    instance->set_state(TerminalState::starting);
    if (!submit(*instance)) {
        return std::nullopt;
    }
    const std::optional<NodeId> leaf =
        tab_.split_terminal(target, orientation, std::move(instance), ratio);
    if (!leaf) {
        worker_.cancel(id);
        return std::nullopt;
    }
    instances_.emplace(id, tab_.find(id));
    return id;
}

bool TerminalManager::pump()
{
    consume_results();
    const bool dirty = consume_inbound();
    process_closing();
    drain_events();
    return dirty;
}

void TerminalManager::consume_results()
{
    StartupResult result;
    while (worker_.try_pop(result)) {
        const auto it = instances_.find(result.id);
        if (it == instances_.end()) {
            // Result for a terminal no longer tracked; best-effort retire.
            if (result.session != nullptr) {
                result.session->terminate();
            }
            continue;
        }

        TerminalInstance *instance = it->second;
        if (result.session == nullptr) {
            instance->set_state(TerminalState::exited);
            if (result.error != SessionError::cancelled) {
                launch_failed_ = true;
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
            instance->set_state(TerminalState::exited);
            launch_failed_ = true;
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
        InboundMessage message;
        while (instance->inbound().try_pop(message)) {
            if (message.kind == InboundMessage::Kind::data) {
                // Explicit-close discard policy: consume queued data without
                // feeding it or triggering a redraw (DIP 1.6).
                if (!discard) {
                    instance->emulator().feed(message.bytes);
                    dirty = true;
                }
            } else if (message.kind == InboundMessage::Kind::eof) {
                instance->mark_eof();
            } else if (message.kind == InboundMessage::Kind::exited) {
                instance->set_state(TerminalState::exited);
            }
        }
        if (instance->state() == TerminalState::running) {
            loop_->resume(entry.first);
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
    tab_.remove_terminal(id);
    instances_.erase(it);
    terminate_sent_.erase(id);
    remove_requested_.erase(id);
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
            instance->release_session();
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
        // The loop has stopped (unrecoverable poll failure or an external
        // stop), so no I/O can touch the instances; release them. This is the
        // defined failure path rather than a timeout.
        log(LogLevel::Error, "I/O loop stopped before shutdown completed");
        std::vector<TerminalId> remaining;
        remaining.reserve(instances_.size());
        for (const auto &entry : instances_) {
            remaining.push_back(entry.first);
        }
        for (const TerminalId id : remaining) {
            finalize(id);
        }
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

void TerminalManager::send(TerminalId id, std::span<const std::byte> bytes)
{
    const auto it = instances_.find(id);
    if (it == instances_.end() || bytes.empty()) {
        return;
    }
    TerminalInstance *instance = it->second;
    if (!instance->input_enabled()) {
        return;
    }
    if (instance->outbound().try_push(bytes)) {
        loop_->write_pending(id);
    }
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
    tab_.layout().focus(id);
}

std::optional<TerminalId> TerminalManager::focused() const noexcept
{
    return tab_.layout().focused();
}

bool TerminalManager::set_split_ratio(NodeId split, float ratio) noexcept
{
    return tab_.layout().set_split_ratio(split, ratio);
}

void TerminalManager::equalize() noexcept
{
    tab_.layout().equalize();
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
