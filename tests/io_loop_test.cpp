// SPDX-License-Identifier: MIT

#include <app/message.hpp>
#include <app/session.hpp>
#include <app/session_queue.hpp>
#include <platform/io_loop.hpp>
#include <platform/startup.hpp>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

void sleep_ms(int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

bool wait_for(const std::function<bool()> &predicate, int timeout_ms)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (Clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        sleep_ms(1);
    }
    return predicate();
}

struct Harness {
    consola::SessionOutboundQueue outbound{1U << 20};
    consola::SessionInboundQueue inbound;
    consola::StartupWorker worker{nullptr, nullptr};
    std::unique_ptr<consola::IoLoop> loop = consola::make_linux_io_loop(nullptr, nullptr);
    consola::StartupResult result;

    ~Harness()
    {
        if (loop) {
            loop->stop();
        }
    }

    bool launch(consola::TerminalId id, const std::string &executable,
                std::vector<std::string> arguments)
    {
        consola::LaunchRequest request;
        request.id = id;
        request.options.executable = executable;
        request.options.arguments = std::move(arguments);
        if (!worker.submit(std::move(request))) {
            return false;
        }
        return wait_for([&] { return worker.try_pop(result); }, 5000);
    }

    bool add(consola::TerminalId id)
    {
        if (result.session == nullptr) {
            return false;
        }
        return loop->add(*result.session, result.master_fd, result.exit_notification_fd,
                         result.process_id, id, inbound, outbound);
    }
};

bool wait_event(consola::IoLoop &loop, consola::LoopEvent::Kind kind, int timeout_ms,
                consola::LoopEvent &out)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (Clock::now() < deadline) {
        consola::LoopEvent event;
        while (loop.try_pop_event(event)) {
            if (event.kind == kind) {
                out = event;
                return true;
            }
        }
        sleep_ms(1);
    }
    return false;
}

struct Drain {
    std::string data;
    std::size_t bytes = 0;
    bool eof = false;
    bool eof_before_exit = false;
    bool exited = false;
    int status = 0;
};

Drain drain_session(Harness &harness, consola::TerminalId id, int timeout_ms, bool auto_resume)
{
    Drain result;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (Clock::now() < deadline) {
        consola::InboundMessage message;
        bool popped = false;
        while (harness.inbound.try_pop(message)) {
            popped = true;
            switch (message.kind) {
            case consola::InboundMessage::Kind::data:
                result.bytes += message.bytes.size();
                result.data.append(reinterpret_cast<const char *>(message.bytes.data()),
                                   message.bytes.size());
                break;
            case consola::InboundMessage::Kind::eof:
                result.eof = true;
                break;
            case consola::InboundMessage::Kind::exited:
                result.exited = true;
                result.status = message.exit_status;
                result.eof_before_exit = result.eof;
                break;
            case consola::InboundMessage::Kind::paste_finished:
                break;
            }
        }
        if (result.exited) {
            break;
        }
        if (auto_resume && !popped) {
            harness.loop->resume(id);
        }
        sleep_ms(1);
    }
    return result;
}

bool test_shell_exit_and_order()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{1};
    if (!harness.launch(id, "/bin/sh", {"-c", "printf hello"})) {
        std::printf("launch failed\n");
        return false;
    }
    if (!harness.add(id)) {
        std::printf("add failed\n");
        return false;
    }

    consola::LoopEvent event;
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::added, 5000, event)) {
        std::printf("no added ack\n");
        return false;
    }

    const Drain drained = drain_session(harness, id, 5000, true);
    if (!drained.exited || drained.data.find("hello") == std::string::npos) {
        std::printf("unexpected output/exited: '%s' exited=%d\n", drained.data.c_str(),
                    drained.exited ? 1 : 0);
        return false;
    }
    if (!drained.eof || !drained.eof_before_exit) {
        std::printf("eof not delivered before exited\n");
        return false;
    }
    if (drained.status != 0) {
        std::printf("unexpected exit status %d\n", drained.status);
        return false;
    }

    if (!harness.loop->remove(id)) {
        std::printf("remove rejected\n");
        return false;
    }
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event)) {
        std::printf("no removed ack\n");
        return false;
    }
    if (!harness.loop->shutdown()) {
        std::printf("shutdown rejected\n");
        return false;
    }
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event) ||
        event.error != consola::SessionError::none) {
        std::printf("shutdown not acknowledged\n");
        return false;
    }
    harness.loop->stop();
    return true;
}

bool test_write_roundtrip()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{2};
    if (!harness.launch(id, "/bin/cat", {})) {
        std::printf("launch failed\n");
        return false;
    }
    if (!harness.add(id)) {
        std::printf("add failed\n");
        return false;
    }
    consola::LoopEvent event;
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::added, 5000, event)) {
        return false;
    }

    const std::string payload = "ping\n";
    const auto *bytes = reinterpret_cast<const std::byte *>(payload.data());
    if (!harness.outbound.try_push(std::span<const std::byte>(bytes, payload.size()))) {
        std::printf("outbound push failed\n");
        return false;
    }
    if (!harness.loop->write_pending(id)) {
        std::printf("write_pending rejected\n");
        return false;
    }

    if (!wait_for(
            [&] {
                consola::InboundMessage message;
                while (harness.inbound.try_pop(message)) {
                    if (message.kind == consola::InboundMessage::Kind::data) {
                        const std::string text(reinterpret_cast<const char *>(message.bytes.data()),
                                               message.bytes.size());
                        if (text.find("ping") != std::string::npos) {
                            return true;
                        }
                    }
                }
                return false;
            },
            5000)) {
        std::printf("echo not received\n");
        return false;
    }

    harness.result.session->terminate();
    const Drain drained = drain_session(harness, id, 5000, true);
    if (!drained.exited) {
        std::printf("cat did not exit\n");
        return false;
    }

    if (!harness.loop->remove(id)) {
        return false;
    }
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event)) {
        return false;
    }
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
    harness.loop->stop();
    return true;
}

bool test_backpressure_resume()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{3};
    if (!harness.launch(id, "/bin/sh", {"-c", "dd if=/dev/zero bs=1024 count=2000 2>/dev/null"})) {
        std::printf("launch failed\n");
        return false;
    }
    if (!harness.add(id)) {
        std::printf("add failed\n");
        return false;
    }
    consola::LoopEvent event;
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::added, 5000, event)) {
        return false;
    }

    // Let the loop outrun the UI until its inbound queue is full, which
    // suspends the master poll entry (DIP 1.4).
    if (!wait_for([&] { return harness.inbound.data_full(); }, 5000)) {
        std::printf("inbound queue never filled\n");
        return false;
    }

    const Drain drained = drain_session(harness, id, 30000, true);
    const std::size_t expected = std::size_t{1024} * 2000U;
    if (!drained.exited) {
        std::printf("dd did not exit (bytes=%zu)\n", drained.bytes);
        return false;
    }
    if (drained.bytes != expected) {
        std::printf("byte mismatch: got %zu expected %zu\n", drained.bytes, expected);
        return false;
    }
    if (!drained.eof_before_exit) {
        std::printf("eof not before exited\n");
        return false;
    }

    if (!harness.loop->remove(id)) {
        return false;
    }
    wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event);
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
    harness.loop->stop();
    return true;
}

bool test_shutdown_rejected_with_session()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{4};
    if (!harness.launch(id, "/bin/cat", {})) {
        return false;
    }
    if (!harness.add(id)) {
        return false;
    }
    consola::LoopEvent event;
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::added, 5000, event)) {
        return false;
    }

    if (!harness.loop->shutdown()) {
        return false;
    }
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event) ||
        event.error == consola::SessionError::none) {
        std::printf("premature shutdown was not rejected\n");
        return false;
    }

    harness.result.session->terminate();
    const Drain drained = drain_session(harness, id, 5000, true);
    if (!drained.exited) {
        std::printf("session did not exit after terminate\n");
        return false;
    }
    if (!harness.loop->remove(id)) {
        return false;
    }
    wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event);

    if (!harness.loop->shutdown()) {
        return false;
    }
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event) ||
        event.error != consola::SessionError::none) {
        std::printf("shutdown after removal not accepted\n");
        return false;
    }
    harness.loop->stop();
    return true;
}

} // namespace

int main()
{
    if (!test_shell_exit_and_order()) {
        std::printf("test_shell_exit_and_order failed\n");
        return 1;
    }
    if (!test_write_roundtrip()) {
        std::printf("test_write_roundtrip failed\n");
        return 1;
    }
    if (!test_backpressure_resume()) {
        std::printf("test_backpressure_resume failed\n");
        return 1;
    }
    if (!test_shutdown_rejected_with_session()) {
        std::printf("test_shutdown_rejected_with_session failed\n");
        return 1;
    }
    std::printf("io_loop_test ok\n");
    return 0;
}
