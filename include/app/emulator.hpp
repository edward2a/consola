// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <app/key_event.hpp>
#include <app/mouse_event.hpp>
#include <app/terminal_style.hpp>

namespace consola {

struct TerminalEmulatorImpl;

// libtsm's native content age (uint_fast32_t); kept at native width so age
// comparisons do not wrap early (DIP 2.5).
using ContentAge = std::uint_fast32_t;

struct CellAttributes {
    Rgb foreground;
    Rgb background;
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
    ContentAge age = 0;
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

// Result of capturing an encoded paste (DIP 2.8, 1.7). ok is false when the
// source is oversized, the encoded result exceeds the limit, or the capture is
// empty (tsm_vte_paste() fails silently).
struct PasteCapture {
    bool ok = false;
    std::string bytes;
};

class TerminalEmulator {
public:
    using OutputCallback = void (*)(void *context, std::span<const std::byte> bytes);
    using CellCallback = void (*)(void *context, int column, int row, const Cell &cell);

    explicit TerminalEmulator(const TerminalStyle &style = default_terminal_style());
    ~TerminalEmulator();
    TerminalEmulator(const TerminalEmulator &) = delete;
    TerminalEmulator &operator=(const TerminalEmulator &) = delete;
    TerminalEmulator(TerminalEmulator &&) = delete;
    TerminalEmulator &operator=(TerminalEmulator &&) = delete;

    void feed(std::span<const std::byte> data);
    void resize(int cols, int rows);

    // Encode a key with libtsm and deliver the bytes through the output
    // callback (DIP 2.6). Returns true if the key produced output.
    bool handle_keyboard(const KeyEvent &event);

    // Terminal mouse reporting (DIP 2.7). mouse_tracking() reports the current
    // mode; handle_mouse() encodes a mouse event with libtsm.
    [[nodiscard]] MouseTracking mouse_tracking() const;
    bool handle_mouse(const MouseEvent &event);

    // Selection (DIP 2.7): libtsm owns the selection; callers pass screen
    // (viewport) coordinates. select_word() computes word boundaries from
    // word_chars.
    void selection_start(int column, int row);
    void selection_target(int column, int row);
    void selection_reset();
    [[nodiscard]] bool has_selection() const;
    [[nodiscard]] std::string selection_copy() const;
    void select_word(int column, int row, std::string_view word_chars);

    // Encode text once through tsm_vte_paste() into a bounded capture (DIP 2.8,
    // 1.7). No bytes are sent; the caller transports the encoded transaction.
    // Rejects a source larger than max_bytes and an encoded result over the
    // limit. text is NUL-terminated to libtsm, so embedded NULs truncate.
    PasteCapture capture_paste(std::string_view text, std::size_t max_bytes);

    // Invoked synchronously during feed(); the span is valid only for the
    // duration of the call, and the callback must not throw.
    void set_output_callback(OutputCallback callback, void *context);

    // Traverse the visible screen via tsm_screen_draw(); the callback receives
    // our Cell type. Returns the content age of the completed paint (DIP 2.2).
    // The callback runs inside libtsm's C callback path and must not throw.
    ContentAge draw(CellCallback callback, void *context);

    [[nodiscard]] int columns() const;
    [[nodiscard]] int rows() const;
    [[nodiscard]] CursorState cursor() const;
    [[nodiscard]] int scrollback_line_count() const;
    [[nodiscard]] int scrollback_line_pos() const;
    [[nodiscard]] bool alternate_screen() const;

    // Scrollback control (DIP 2.9). Positive delta scrolls toward older lines.
    void set_max_scrollback(int lines);
    void scroll_lines(int delta);
    void scroll_page(int pages);
    void scroll_to_top();
    void scroll_to_bottom();

    // Window title from OSC 0/2 (DIP 2.12) and working directory from OSC 7.
    using TitleCallback = void (*)(void *context, std::string_view title);
    [[nodiscard]] const std::string &title() const;
    void set_title_callback(TitleCallback callback, void *context);

    // Working directory reported by OSC 7 (DIP 6.1). Empty until a valid
    // file:// URI arrives; percent-escapes are decoded and the URI's host is
    // ignored (the path is what matters to the terminal).
    using WorkingDirectoryCallback = void (*)(void *context, std::string_view path);
    [[nodiscard]] const std::string &working_directory() const;
    void set_working_directory_callback(WorkingDirectoryCallback callback, void *context);

    // Bell from the VTE (DIP 2.8a, 6.2). Invoked synchronously during feed();
    // the callback must not throw and must not touch the emulator.
    using BellCallback = void (*)(void *context);
    void set_bell_callback(BellCallback callback, void *context);

    // Raw libtsm cursor position (not clamped); prefer cursor().
    [[nodiscard]] int cursor_x_raw() const;
    [[nodiscard]] int cursor_y_raw() const;

private:
    std::unique_ptr<TerminalEmulatorImpl> impl_;
};

} // namespace consola
