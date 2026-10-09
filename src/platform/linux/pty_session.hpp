// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <span>

#include <app/session.hpp>

namespace consola {

class LinuxPtySession final : public TerminalSession {
public:
    explicit LinuxPtySession(int cancel_fd) noexcept;
    ~LinuxPtySession() override;

    LinuxPtySession(const LinuxPtySession &) = delete;
    LinuxPtySession &operator=(const LinuxPtySession &) = delete;
    LinuxPtySession(LinuxPtySession &&) = delete;
    LinuxPtySession &operator=(LinuxPtySession &&) = delete;

    SessionError start(const SessionOptions &options) override;
    IoResult read(std::span<std::byte> buffer) override;
    IoResult write(std::span<const std::byte> buffer) override;
    SessionError resize(int cols, int rows) override;
    void terminate() override;
    [[nodiscard]] bool running() const override;

    [[nodiscard]] int master_fd() const noexcept;
    [[nodiscard]] int exit_notification_fd() const noexcept;
    [[nodiscard]] std::int64_t process_id() const noexcept;

private:
    void close_descriptors() noexcept;

    int cancel_fd_ = -1;
    int master_fd_ = -1;
    int exit_fd_ = -1;
    std::int64_t child_pid_ = -1;
    bool started_ = false;
    bool stopping_ = false;
};

} // namespace consola
