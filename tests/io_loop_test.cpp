// SPDX-License-Identifier: MIT

#include <app/emulator.hpp>
#include <app/message.hpp>
#include <app/session.hpp>
#include <app/session_queue.hpp>
#include <platform/io_loop.hpp>
#include <platform/startup.hpp>

#include <cerrno>
#include <chrono>
#include <csignal>
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
    std::unique_ptr<consola::IoLoop> loop =
        consola::make_linux_io_loop(nullptr, nullptr, std::chrono::milliseconds{300});
    consola::StartupResult result;
    bool drain_output_on_close = true;

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
                         result.process_id, id, drain_output_on_close, inbound, outbound);
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

    harness.loop->terminate(id);
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

    harness.loop->terminate(id);
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

bool test_resume_finishes_post_exit_drain()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{5};
    // The background shell keeps the PTY slave open after the direct child
    // exits, so there is no POLLHUP: post-exit completion must come from the
    // resume-triggered drain, not from FD readiness.
    if (!harness.launch(
            id, "/bin/sh",
            {"-c", "sh -c 'sleep 3' & dd if=/dev/zero bs=1024 count=2000 2>/dev/null"})) {
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
    if (!wait_for([&] { return harness.inbound.data_full(); }, 5000)) {
        std::printf("inbound queue never filled\n");
        return false;
    }

    const auto start = Clock::now();
    const Drain drained = drain_session(harness, id, 10000, true);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
    if (!drained.exited) {
        std::printf("session did not complete (bytes=%zu)\n", drained.bytes);
        return false;
    }
    if (drained.bytes != std::size_t{1024} * 2000U) {
        std::printf("byte mismatch: got %zu\n", drained.bytes);
        return false;
    }
    if (elapsed >= std::chrono::milliseconds(2000)) {
        std::printf("completion waited for the descendant (%lld ms)\n",
                    static_cast<long long>(elapsed.count()));
        return false;
    }

    harness.loop->terminate(id); // clean up the background descendant
    if (!harness.loop->remove(id)) {
        return false;
    }
    wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event);
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
    harness.loop->stop();
    return true;
}

bool test_terminate_escalates_sigkill()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{6};
    // The shell ignores SIGHUP and the ignored disposition is inherited across
    // exec, so only the SIGKILL escalation can end the session. Wait for the
    // trap to be installed before terminating, or the initial SIGHUP could
    // kill the shell first.
    if (!harness.launch(id, "/bin/sh", {"-c", "trap '' HUP; printf ready; sleep 10"})) {
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
    if (!wait_for(
            [&] {
                consola::InboundMessage message;
                while (harness.inbound.try_pop(message)) {
                    if (message.kind == consola::InboundMessage::Kind::data) {
                        const std::string text(reinterpret_cast<const char *>(message.bytes.data()),
                                               message.bytes.size());
                        if (text.find("ready") != std::string::npos) {
                            return true;
                        }
                    }
                }
                return false;
            },
            5000)) {
        std::printf("shell never became ready\n");
        return false;
    }
    if (!harness.loop->terminate(id)) {
        return false;
    }

    const Drain drained = drain_session(harness, id, 5000, true);
    if (!drained.exited) {
        std::printf("SIGKILL escalation did not complete\n");
        return false;
    }
    if (drained.status != 128 + SIGKILL) {
        std::printf("unexpected escalation status %d\n", drained.status);
        return false;
    }

    harness.loop->remove(id);
    wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event);
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
    harness.loop->stop();
    return true;
}

bool test_close_discards_output()
{
    Harness harness;
    harness.drain_output_on_close = false;
    harness.loop->start();

    const consola::TerminalId id{7};
    if (!harness.launch(id, "/bin/sh", {"-c", "dd if=/dev/zero bs=1024 count=4000 2>/dev/null"})) {
        std::printf("launch failed\n");
        return false;
    }
    // Queue add and terminate together so the discard policy is set before the
    // loop services the first read.
    if (!harness.add(id) || !harness.loop->terminate(id)) {
        std::printf("add/terminate failed\n");
        return false;
    }

    const Drain drained = drain_session(harness, id, 10000, true);
    const std::size_t expected = std::size_t{1024} * 4000U;
    if (!drained.exited) {
        std::printf("discarding session did not exit\n");
        return false;
    }
    if (drained.bytes >= expected) {
        std::printf("close policy did not discard output (%zu bytes)\n", drained.bytes);
        return false;
    }

    consola::LoopEvent event;
    harness.loop->remove(id);
    wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event);
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
    harness.loop->stop();
    return true;
}

bool test_remove_before_exited_preserves_output()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{8};
    if (!harness.launch(id, "/bin/sh", {"-c", "dd if=/dev/zero bs=1024 count=2000 2>/dev/null"})) {
        std::printf("launch failed\n");
        return false;
    }
    if (!harness.add(id)) {
        return false;
    }
    // Request removal before the child has exited or its output has drained;
    // the acknowledgement must wait for `exited`.
    if (!harness.loop->remove(id)) {
        return false;
    }
    if (!wait_for([&] { return harness.inbound.data_full(); }, 5000)) {
        std::printf("inbound queue never filled\n");
        return false;
    }

    const Drain drained = drain_session(harness, id, 30000, true);
    if (!drained.exited || drained.bytes != std::size_t{1024} * 2000U) {
        std::printf("early remove truncated output (bytes=%zu)\n", drained.bytes);
        return false;
    }

    consola::LoopEvent event;
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event)) {
        std::printf("early remove was not acknowledged\n");
        return false;
    }
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
    harness.loop->stop();
    return true;
}

bool test_shell_exit_foreground_sigkill()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{9};
    // The shell exits immediately while a background job in the same group
    // ignores SIGHUP; only the deadline SIGKILL can remove the group.
    if (!harness.launch(id, "/bin/sh", {"-c", "sh -c 'trap \"\" HUP; sleep 10' & exit 0"})) {
        std::printf("launch failed\n");
        return false;
    }
    if (!harness.add(id)) {
        return false;
    }
    consola::LoopEvent event;
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::added, 5000, event)) {
        return false;
    }

    const Drain drained = drain_session(harness, id, 5000, true);
    if (!drained.exited) {
        std::printf("shell did not exit\n");
        return false;
    }
    if (!harness.loop->terminate(id)) {
        return false;
    }

    const auto pgid = static_cast<pid_t>(harness.result.process_id);
    bool group_gone = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (::kill(-pgid, 0) != 0 && errno == ESRCH) {
            group_gone = true;
            break;
        }
        sleep_ms(20);
    }
    if (!group_gone) {
        std::printf("foreground group survived SIGKILL escalation\n");
        return false;
    }

    harness.loop->remove(id);
    wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event);
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
    harness.loop->stop();
    return true;
}

bool test_outbound_credits_release()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{10};
    if (!harness.launch(id, "/bin/cat", {})) {
        std::printf("launch failed\n");
        return false;
    }
    if (!harness.add(id)) {
        return false;
    }
    consola::LoopEvent event;
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::added, 5000, event)) {
        return false;
    }

    const std::vector<std::byte> payload(1000, std::byte{'x'});
    if (!harness.outbound.try_push(payload)) {
        std::printf("outbound push failed\n");
        return false;
    }
    if (harness.outbound.pending_bytes() != payload.size()) {
        return false;
    }
    if (!harness.loop->write_pending(id)) {
        return false;
    }
    // Credits return only as the loop writes the bytes.
    if (!wait_for([&] { return harness.outbound.pending_bytes() == 0; }, 5000)) {
        std::printf("outbound credits did not return (pending=%zu)\n",
                    harness.outbound.pending_bytes());
        return false;
    }

    harness.loop->terminate(id);
    const Drain drained = drain_session(harness, id, 5000, true);
    if (!drained.exited) {
        return false;
    }
    harness.loop->remove(id);
    wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event);
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
    harness.loop->stop();
    return true;
}

bool test_output_reaches_emulator()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{11};
    if (!harness.launch(id, "/bin/sh", {"-c", "printf abc"})) {
        std::printf("launch failed\n");
        return false;
    }
    if (!harness.add(id)) {
        return false;
    }
    consola::LoopEvent event;
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::added, 5000, event)) {
        return false;
    }

    // Stage 1 exit criterion: PTY output reaches libtsm.
    consola::TerminalEmulator emulator;
    bool exited = false;
    const auto deadline = Clock::now() + std::chrono::milliseconds(5000);
    while (!exited && Clock::now() < deadline) {
        consola::InboundMessage message;
        bool popped = false;
        while (harness.inbound.try_pop(message)) {
            popped = true;
            if (message.kind == consola::InboundMessage::Kind::data) {
                emulator.feed(message.bytes);
            } else if (message.kind == consola::InboundMessage::Kind::exited) {
                exited = true;
            }
        }
        if (!exited && !popped) {
            harness.loop->resume(id);
        }
        sleep_ms(1);
    }
    if (!exited) {
        std::printf("emulator session did not exit\n");
        return false;
    }
    if (emulator.cursor_x_raw() != 3) {
        std::printf("libtsm cursor at %d, expected 3\n", emulator.cursor_x_raw());
        return false;
    }

    harness.loop->remove(id);
    wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event);
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
    harness.loop->stop();
    return true;
}

bool test_fail_session_terminates()
{
    Harness harness;
    harness.loop->start();

    const consola::TerminalId id{12};
    if (!harness.launch(id, "/bin/cat", {})) {
        std::printf("launch failed\n");
        return false;
    }
    if (!harness.add(id)) {
        return false;
    }
    consola::LoopEvent event;
    if (!wait_event(*harness.loop, consola::LoopEvent::Kind::added, 5000, event)) {
        return false;
    }

    // A required-reply overflow fails the session through termination/reaping.
    if (!harness.loop->fail_session(id, consola::SessionError::reply_overflow)) {
        return false;
    }
    const Drain drained = drain_session(harness, id, 5000, true);
    if (!drained.exited) {
        std::printf("failed session did not exit\n");
        return false;
    }

    harness.loop->remove(id);
    wait_event(*harness.loop, consola::LoopEvent::Kind::removed, 5000, event);
    harness.loop->shutdown();
    wait_event(*harness.loop, consola::LoopEvent::Kind::shutdown_done, 5000, event);
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
    if (!test_resume_finishes_post_exit_drain()) {
        std::printf("test_resume_finishes_post_exit_drain failed\n");
        return 1;
    }
    if (!test_terminate_escalates_sigkill()) {
        std::printf("test_terminate_escalates_sigkill failed\n");
        return 1;
    }
    if (!test_close_discards_output()) {
        std::printf("test_close_discards_output failed\n");
        return 1;
    }
    if (!test_remove_before_exited_preserves_output()) {
        std::printf("test_remove_before_exited_preserves_output failed\n");
        return 1;
    }
    if (!test_shell_exit_foreground_sigkill()) {
        std::printf("test_shell_exit_foreground_sigkill failed\n");
        return 1;
    }
    if (!test_outbound_credits_release()) {
        std::printf("test_outbound_credits_release failed\n");
        return 1;
    }
    if (!test_output_reaches_emulator()) {
        std::printf("test_output_reaches_emulator failed\n");
        return 1;
    }
    if (!test_fail_session_terminates()) {
        std::printf("test_fail_session_terminates failed\n");
        return 1;
    }
    std::printf("io_loop_test ok\n");
    return 0;
}
