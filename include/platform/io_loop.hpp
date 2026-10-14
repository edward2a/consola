// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include <app/message.hpp>
#include <app/session.hpp>
#include <app/session_queue.hpp>

namespace consola {

// Loop -> UI acknowledgements (DIP 1.4). Session output and lifecycle events
// travel through each session's SessionInboundQueue; this channel only carries
// registration/removal handshakes.
struct LoopEvent {
    enum class Kind : std::uint8_t { added, removed, shutdown_done };

    Kind kind = Kind::added;
    TerminalId id;
    SessionError error = SessionError::none;
};

// Narrow, platform-neutral I/O loop interface (DIP 1.4). One application-owned
// loop thread polls every registered session's master FD and child-exit
// notification. The UI thread never blocks on PTY I/O. All methods are
// thread-safe and non-blocking; the UI consumes events with try_pop_event()
// after clearing its WakeupFlag.
class IoLoop {
public:
    virtual ~IoLoop() = default;

    // Start the loop thread. Returns false when the loop is unavailable (its
    // wake FD or SIGCHLD self-pipe could not be created), in which case no
    // commands are processed.
    virtual bool start() = 0;
    virtual void stop() = 0;

    // True while the loop thread is alive. Becomes false if the loop stops,
    // including after an unrecoverable poll error, so a shutdown waiter can
    // detect that no further acknowledgements will arrive.
    [[nodiscard]] virtual bool running() const = 0;

    // Register a session and its queues. The referenced objects must outlive
    // the registration until remove() is acknowledged. master_fd,
    // exit_fd (may be -1) and process_id come from StartupResult.
    // drain_output_on_close selects the explicit-close output policy (DIP 1.6):
    // when false, terminate() discards newly read output and skips the
    // post-exit drain.
    virtual bool add(TerminalSession &session, int master_fd, int exit_fd, std::int64_t process_id,
                     TerminalId id, bool drain_output_on_close, SessionInboundQueue &inbound,
                     SessionOutboundQueue &outbound) = 0;

    // Begin normal terminal-close semantics (DIP 1.6): capture the shell and
    // foreground process groups, send SIGHUP, then SIGKILL at the kill
    // deadline. Returns immediately; repeated calls do not restart the
    // deadline. Completion is reported through the normal reaping/`exited`
    // path.
    virtual bool terminate(TerminalId id) = 0;

    // Fail a session through the normal termination/reaping path (DIP 1.7),
    // e.g. when a required automatic reply cannot fit the outbound budget. The
    // caller is responsible for the visible failure indication.
    virtual bool fail_session(TerminalId id, SessionError error) = 0;

    // Unregister a session. Acknowledged through LoopEvent::removed once its
    // `exited` event has been published, any pending termination signal
    // sequence has completed and its poll entries are retired; the UI may then
    // release the backend. Each session has one outstanding remove request: a
    // remove for an unknown id is acknowledged as a no-op.
    virtual bool remove(TerminalId id) = 0;

    // The session has outgoing bytes; monitor POLLOUT until drained.
    virtual bool write_pending(TerminalId id) = 0;

    // The UI made room in the inbound queue; restore the master poll entry.
    virtual bool resume(TerminalId id) = 0;

    // Stop the loop. Rejected while sessions are still registered.
    virtual bool shutdown() = 0;

    // UI: non-blocking drain of the acknowledgement channel.
    virtual bool try_pop_event(LoopEvent &out) = 0;
};

std::unique_ptr<IoLoop>
make_linux_io_loop(WakeCallback wake, void *wake_context,
                   std::chrono::milliseconds kill_timeout = std::chrono::milliseconds{2000});

} // namespace consola
