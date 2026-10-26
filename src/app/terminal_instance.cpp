// SPDX-License-Identifier: MIT

#include <app/terminal_instance.hpp>

#include <utility>

namespace consola {

TerminalInstance::TerminalInstance(TerminalId id, SessionOptions options,
                                   const TerminalStyle &style)
    : id_(id), options_(std::move(options)), emulator_(style)
{}

void TerminalInstance::attach(std::unique_ptr<TerminalSession> session, int master_fd, int exit_fd,
                              std::int64_t process_id)
{
    session_ = std::move(session);
    master_fd_ = master_fd;
    exit_fd_ = exit_fd;
    process_id_ = process_id;
    state_ = TerminalState::running;
}

void TerminalInstance::prepare_relaunch(SessionOptions options)
{
    options_ = std::move(options);
    state_ = TerminalState::starting;
    eof_ = false;
    closing_ = false;
    process_id_ = -1;
}

std::unique_ptr<TerminalSession> TerminalInstance::release_session() noexcept
{
    master_fd_ = -1;
    exit_fd_ = -1;
    process_id_ = -1;
    state_ = TerminalState::closed;
    return std::move(session_);
}

} // namespace consola
