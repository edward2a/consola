// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <FL/Fl_Widget.H>

#include <app/emulator.hpp>
#include <app/terminal_style.hpp>

namespace consola {

struct FontMetrics {
    // Fractional advance of the monospace face; grid origins are rounded from
    // this so fractional advances do not accumulate error (DIP 2.3, 2.4).
    double cell_width = 0.0;
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
    void resize(int x, int y, int w, int h) override;
    int handle(int event) override;

    // Apply a new style (font/DPI). Forces a full repaint (DIP 2.5). Palette
    // changes for existing content are out of scope (DIP 2.3); the emulator
    // palette is fixed at construction, so this only affects font metrics.
    void set_style(TerminalStyle style);

    // Copy the current selection to the clipboard / selection buffer (DIP 2.8).
    void copy_selection();
    // Request the clipboard and deliver an encoded paste through the callback.
    void paste_clipboard();

    using PasteCallback = void (*)(void *context, std::string_view bytes);
    void set_paste_callback(PasteCallback paste_callback, void *context);

    [[nodiscard]] TerminalEmulator &emulator() noexcept
    {
        return emulator_;
    }
    [[nodiscard]] const FontMetrics &metrics() const noexcept
    {
        return metrics_;
    }

private:
    struct Rect {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        bool valid = false;

        constexpr Rect() = default;
        constexpr Rect(int left, int top, int right, int bottom, bool is_valid)
            : x0(left), y0(top), x1(right), y1(bottom), valid(is_valid)
        {}
    };

    void apply_font_family() const;
    void update_metrics();
    [[nodiscard]] Rect cell_rect(int column, int row, int width) const;
    [[nodiscard]] static Rect union_rect(Rect a, Rect b);
    [[nodiscard]] static bool intersects(Rect a, Rect b);
    [[nodiscard]] static bool inside(Rect a, Rect b);
    void paint_cell(int column, int row, const Cell &cell);
    void collect_cell(int column, int row, const Cell &cell);
    void draw_cursor();
    void draw_scrollbar();
    void event_cell(int &column, int &row) const;
    int handle_push();
    int handle_drag();
    int handle_release();
    int handle_wheel();
    static void paint_callback(void *context, int column, int row, const Cell &cell);
    static void collect_callback(void *context, int column, int row, const Cell &cell);

    TerminalEmulator &emulator_;
    TerminalStyle style_;
    FontMetrics metrics_;

    // Dirty-region state (DIP 2.5).
    ContentAge last_painted_age_ = 0;
    bool full_redraw_requested_ = true;
    int last_scrollback_pos_ = 0;
    int last_cursor_column_ = -1;
    int last_cursor_row_ = -1;
    bool last_cursor_visible_ = false;

    // Per-paint state.
    bool full_ = false;
    Rect paint_region_;
    Rect content_region_;
    bool cursor_repaint_ = false;
    int cursor_column_ = 0;
    int cursor_row_ = 0;
    bool cursor_visible_ = false;
    CursorStyle cursor_style_ = CursorStyle::default_style;
    std::string cursor_text_;
    CellAttributes cursor_attributes_;
    Rgb cursor_foreground_;
    Rgb cursor_background_;
    std::vector<char> utf8_scratch_;

    // Mouse/selection state (DIP 2.7).
    bool selecting_ = false;
    bool word_selection_ = false;
    int pressed_button_ = -1;

    // Clipboard (DIP 2.8).
    PasteCallback paste_callback_ = nullptr;
    void *paste_context_ = nullptr;
};

} // namespace consola
