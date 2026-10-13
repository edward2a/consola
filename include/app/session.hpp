// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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

    // Threading (DIP 1.7): read()/write() are called only by the I/O loop
    // thread; resize() is called by the UI thread and is safe against the loop
    // because TIOCSWINSZ is thread-safe and the FD outlives the session.
    // start() is called only by the startup worker while the backend is
    // unregistered. terminate()/running() belong to the backend's owner: in
    // the application the I/O loop owns termination (the UI posts the loop's
    // terminate command), so direct calls must not race the loop.
    virtual SessionError start(const SessionOptions &options) = 0;
    virtual IoResult read(std::span<std::byte> buffer) = 0;
    virtual IoResult write(std::span<const std::byte> buffer) = 0;
    virtual SessionError resize(int cols, int rows) = 0;
    virtual void terminate() = 0;
    // Backend I/O/stop state, not process liveness. A naturally exited child
    // still reports running() until terminate() is called; the I/O loop uses
    // waitpid() as the authoritative exit signal (DIP 1.4, 1.6).
    [[nodiscard]] virtual bool running() const = 0;
};

} // namespace consola

namespace std {

template <> struct hash<consola::TerminalId> {
    std::size_t operator()(const consola::TerminalId &id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

} // namespace std
