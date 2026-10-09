// SPDX-License-Identifier: MIT

#pragma once

#include <string>

#include <FL/Fl_Widget.H>

#include <app/emulator.hpp>
#include <app/terminal_style.hpp>

namespace consola {

struct FontMetrics {
    int cell_width = 0;
    int cell_height = 0;
    int ascent = 0;
};

// Presentation-only FLTK widget for one terminal pane (DIP 2.1). It owns
// presentation state and must not own the PTY or the terminal model.
class TerminalWidget : public Fl_Widget {
public:
    TerminalWidget(int x, int y, int w, int h, TerminalEmulator &emulator,
                   TerminalStyle style = default_terminal_style());

    void draw() override;
    int handle(int event) override;

    [[nodiscard]] TerminalEmulator &emulator() noexcept
    {
        return emulator_;
    }
    [[nodiscard]] const FontMetrics &metrics() const noexcept
    {
        return metrics_;
    }

private:
    void update_metrics();
    void draw_cell(int column, int row, const Cell &cell);
    void draw_cursor();
    static void cell_callback(void *context, int column, int row, const Cell &cell);

    TerminalEmulator &emulator_;
    TerminalStyle style_;
    FontMetrics metrics_;
    int cursor_column_ = 0;
    int cursor_row_ = 0;
    bool cursor_visible_ = false;
    CursorStyle cursor_style_ = CursorStyle::default_style;
    std::string cursor_text_;
    Rgb cursor_foreground_;
    Rgb cursor_background_;
};

} // namespace consola
