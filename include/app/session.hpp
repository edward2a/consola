// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <app/error.hpp>

namespace consola {

struct TerminalId {
    std::uint64_t value = 0;

    friend bool operator==(TerminalId, TerminalId) = default;
};

using WakeCallback = void (*)(void *context);

struct SessionOptions {
    std::string executable;
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    std::string working_directory;
    int columns = 80;
    int rows = 24;
    std::string term = "xterm-256color";
    std::string colorterm = "truecolor";
    bool login_shell = false;
    bool drain_output_on_close = false;
};

class TerminalSession {
public:
    virtual ~TerminalSession() = default;

    virtual SessionError start(const SessionOptions &options) = 0;
    virtual IoResult read(std::span<std::byte> buffer) = 0;
    virtual IoResult write(std::span<const std::byte> buffer) = 0;
    virtual SessionError resize(int cols, int rows) = 0;
    virtual void terminate() = 0;
    [[nodiscard]] virtual bool running() const = 0;
};

} // namespace consola
