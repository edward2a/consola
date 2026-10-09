// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>

#include <app/session.hpp>

namespace consola {

struct LaunchRequest {
    TerminalId id;
    SessionOptions options;
};

struct StartupResult {
    TerminalId id;
    SessionError error = SessionError::none;
    // On success, session owns the master and exit-notification descriptors;
    // master_fd and exit_notification_fd are borrowed views valid only while
    // session is alive. The owner must retire its poll entries before
    // destroying the session (DIP 1.4). process_id is the direct child.
    std::unique_ptr<TerminalSession> session;
    int master_fd = -1;
    int exit_notification_fd = -1;
    std::int64_t process_id = -1;
};

class StartupWorker {
public:
    StartupWorker(WakeCallback wake, void *wake_context);
    ~StartupWorker();

    StartupWorker(const StartupWorker &) = delete;
    StartupWorker &operator=(const StartupWorker &) = delete;
    StartupWorker(StartupWorker &&) = delete;
    StartupWorker &operator=(StartupWorker &&) = delete;

    // Enqueue a launch request. Returns false if the bounded request queue is
    // full or the worker is stopping. Every accepted request resolves exactly
    // once through try_pop().
    bool submit(LaunchRequest request);

    // Non-blocking. Returns true and fills out if a result is ready.
    bool try_pop(StartupResult &out);

    // Cancel a queued or in-flight request. The request still resolves exactly
    // once: SessionError::cancelled unless it had already launched successfully.
    void cancel(TerminalId id);

    // Cancel all outstanding requests, wake and join the worker. Results remain
    // retrievable through try_pop().
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace consola
