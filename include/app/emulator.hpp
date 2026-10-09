// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace consola {

struct TerminalEmulatorImpl;

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

struct CellAttributes {
    Rgb foreground{};
    Rgb background{};
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool inverse = false;
    bool blink = false;
    bool dim = false;
};

// One screen cell as presented to the renderer (DIP 2.2). libtsm types never
// leave the emulator.
struct Cell {
    // The cell's symbol as one or more codepoints. Valid only for the duration
    // of the draw callback. Empty text with width > 0 is a blank cell (paint
    // the background only).
    std::span<const std::uint32_t> text;
    CellAttributes attributes{};
    // 1 or 2 for a glyph, or 0 for the trailing half of a wide glyph (skip it).
    std::uint8_t width = 0;
    std::uint32_t age = 0;
};

enum class CursorStyle : std::uint8_t {
    default_style = 0,
    block_blink,
    block_steady,
    underline_blink,
    underline_steady,
    vbar_blink,
    vbar_steady,
};

struct CursorState {
    // Raw libtsm position: x may equal columns after writing the last cell.
    // The renderer clamps it (DIP 2.2, 2.4).
    int x = 0;
    int y = 0;
    CursorStyle style = CursorStyle::default_style;
    std::uint32_t blink_rate_ms = 0;
    bool hidden = false;
};

class TerminalEmulator {
public:
    using OutputCallback = void (*)(void *context, std::span<const std::byte> bytes);
    using CellCallback = void (*)(void *context, int column, int row, const Cell &cell);

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

    // Traverse the visible screen via tsm_screen_draw(); the callback receives
    // our Cell type. Returns the content age of the completed paint (DIP 2.2).
    std::uint32_t draw(CellCallback callback, void *context);

    [[nodiscard]] int columns() const;
    [[nodiscard]] int rows() const;
    [[nodiscard]] CursorState cursor() const;
    [[nodiscard]] int scrollback_line_count() const;
    [[nodiscard]] int scrollback_line_pos() const;

    // Raw libtsm cursor position (not clamped); prefer cursor().
    [[nodiscard]] int cursor_x_raw() const;
    [[nodiscard]] int cursor_y_raw() const;

private:
    std::unique_ptr<TerminalEmulatorImpl> impl_;
};

} // namespace consola
