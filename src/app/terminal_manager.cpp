// SPDX-License-Identifier: MIT

#include <app/terminal_manager.hpp>

#include <cmath>
#include <memory>
#include <utility>

#include <app/log.hpp>

namespace consola {

TerminalManager::TerminalManager(WakeCallback wake, void *wake_context,
                                 std::chrono::milliseconds kill_timeout)
    : worker_(wake, wake_context), loop_(make_linux_io_loop(wake, wake_context, kill_timeout))
{}

TerminalManager::~TerminalManager()
{
    worker_.stop();
    StartupResult result;
    while (worker_.try_pop(result)) {
        if (result.session != nullptr) {
            result.session->terminate();
        }
    }
    for (auto &entry : instances_) {
        if (entry.second->session() != nullptr) {
            entry.second->session()->terminate();
        }
    }
    if (loop_) {
        loop_->stop();
    }
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
    if (tab_.terminal_count() != 0) {
        return std::nullopt;
    }
    const TerminalId id = allocate_id();
    auto instance = std::make_unique<TerminalInstance>(id, options.session);
    if (!submit(*instance)) {
        return std::nullopt;
    }
    instances_.emplace(id, instance.get());
    tab_.add_root(std::move(instance));
    return id;
}

std::optional<TerminalId> TerminalManager::split_terminal(TerminalId target,
                                                          Orientation orientation,
                                                          const TerminalOptions &options,
                                                          float ratio)
{
    if (!tab_.layout().find_terminal(target).has_value() || !std::isfinite(ratio) ||
        ratio <= 0.0F || ratio >= 1.0F) {
        return std::nullopt;
    }
    const TerminalId id = allocate_id();
    auto instance = std::make_unique<TerminalInstance>(id, options.session);
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
    LoopEvent event;
    while (loop_->try_pop_event(event)) {
        // Registration/removal/shutdown acknowledgements are handled in S3-CS4.
    }
    return dirty;
}

void TerminalManager::consume_results()
{
    StartupResult result;
    while (worker_.try_pop(result)) {
        const auto it = instances_.find(result.id);
        if (it == instances_.end()) {
            // Result for a terminal no longer tracked; S3-CS4 adds the cleanup
            // barrier, this is a best-effort retire for now.
            if (result.session != nullptr) {
                result.session->terminate();
            }
            continue;
        }

        TerminalInstance *instance = it->second;
        if (result.session == nullptr) {
            instance->set_state(TerminalState::exited);
            launch_failed_ = true;
            continue;
        }

        instance->attach(std::move(result.session), result.master_fd, result.exit_notification_fd,
                         result.process_id);
        if (!loop_->add(*instance->session(), instance->master_fd(),
                        instance->exit_notification_fd(), instance->process_id(), instance->id(),
                        instance->options().drain_output_on_close, instance->inbound(),
                        instance->outbound())) {
            log(LogLevel::Error, "session registration failed");
            auto session = instance->release_session();
            session->terminate();
            instance->set_state(TerminalState::exited);
            launch_failed_ = true;
        }
    }
}

bool TerminalManager::consume_inbound()
{
    bool dirty = false;
    for (auto &entry : instances_) {
        TerminalInstance *instance = entry.second;
        InboundMessage message;
        while (instance->inbound().try_pop(message)) {
            if (message.kind == InboundMessage::Kind::data) {
                instance->emulator().feed(message.bytes);
                dirty = true;
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

} // namespace consola
