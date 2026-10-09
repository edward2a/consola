// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <memory>
#include <span>

namespace consola {

struct TerminalEmulatorImpl;

class TerminalEmulator {
public:
    using OutputCallback = void (*)(void *context, std::span<const std::byte> bytes);

    TerminalEmulator();
    ~TerminalEmulator();
    TerminalEmulator(const TerminalEmulator &) = delete;
    TerminalEmulator &operator=(const TerminalEmulator &) = delete;
    TerminalEmulator(TerminalEmulator &&) = delete;
    TerminalEmulator &operator=(TerminalEmulator &&) = delete;

    void feed(std::span<const std::byte> data);
    void resize(int cols, int rows);

    // Invoked synchronously during feed(); the span is valid only for the
    // duration of the call, and the callback must not throw.
    void set_output_callback(OutputCallback callback, void *context);

    [[nodiscard]] int columns() const;
    [[nodiscard]] int rows() const;
    [[nodiscard]] int cursor_x_raw() const;
    [[nodiscard]] int cursor_y_raw() const;

private:
    std::unique_ptr<TerminalEmulatorImpl> impl_;
};

} // namespace consola
