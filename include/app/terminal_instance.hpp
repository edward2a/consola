// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>

#include <app/emulator.hpp>
#include <app/session.hpp>
#include <app/session_queue.hpp>
#include <app/terminal_style.hpp>

namespace consola {

// Lifecycle state for one terminal (DIP 1.6, 3.0). Input is disabled until a
// successful launch is attached.
enum class TerminalState : std::uint8_t {
    created,
    starting,
    running,
    stopping,
    exited,
    closed,
};

// One terminal pane's model: its emulator, its session queues and, once
// launched, its backend (DIP 4.1). A Tab owns it; the layout refers to it only
// by TerminalId.
class TerminalInstance {
public:
    TerminalInstance(TerminalId id, SessionOptions options,
                     const TerminalStyle &style = default_terminal_style());

    TerminalInstance(const TerminalInstance &) = delete;
    TerminalInstance &operator=(const TerminalInstance &) = delete;
    TerminalInstance(TerminalInstance &&) = delete;
    TerminalInstance &operator=(TerminalInstance &&) = delete;

    [[nodiscard]] TerminalId id() const noexcept
    {
        return id_;
    }
    [[nodiscard]] TerminalState state() const noexcept
    {
        return state_;
    }
    void set_state(TerminalState state) noexcept
    {
        state_ = state;
    }

    // Input is accepted only while the backend is attached and running and the
    // session has not reached EOF.
    [[nodiscard]] bool input_enabled() const noexcept
    {
        return state_ == TerminalState::running && !eof_;
    }

    // The I/O loop reported terminal EOF; no further input is accepted.
    void mark_eof() noexcept
    {
        eof_ = true;
    }
    [[nodiscard]] bool eof() const noexcept
    {
        return eof_;
    }

    [[nodiscard]] const SessionOptions &options() const noexcept
    {
        return options_;
    }

    [[nodiscard]] TerminalEmulator &emulator() noexcept
    {
        return emulator_;
    }
    [[nodiscard]] const TerminalEmulator &emulator() const noexcept
    {
        return emulator_;
    }

    [[nodiscard]] SessionInboundQueue &inbound() noexcept
    {
        return inbound_;
    }
    [[nodiscard]] SessionOutboundQueue &outbound() noexcept
    {
        return outbound_;
    }

    // Backend accessors are valid only after a successful launch is attached.
    [[nodiscard]] TerminalSession *session() noexcept
    {
        return session_.get();
    }
    [[nodiscard]] int master_fd() const noexcept
    {
        return master_fd_;
    }
    [[nodiscard]] int exit_notification_fd() const noexcept
    {
        return exit_fd_;
    }
    [[nodiscard]] std::int64_t process_id() const noexcept
    {
        return process_id_;
    }

    // Take ownership of a launched backend and enter the running state.
    // Precondition: state() == TerminalState::starting.
    void attach(std::unique_ptr<TerminalSession> session, int master_fd, int exit_fd,
                std::int64_t process_id);

    // Give up the backend (for cleanup after the loop has retired its poll
    // entries) and disable input. Returns null when no backend is attached.
    std::unique_ptr<TerminalSession> release_session() noexcept;

private:
    TerminalId id_;
    SessionOptions options_;
    TerminalState state_ = TerminalState::created;
    bool eof_ = false;
    TerminalEmulator emulator_;
    SessionInboundQueue inbound_;
    SessionOutboundQueue outbound_{std::size_t{1} << 20};
    std::unique_ptr<TerminalSession> session_;
    int master_fd_ = -1;
    int exit_fd_ = -1;
    std::int64_t process_id_ = -1;
};

} // namespace consola
