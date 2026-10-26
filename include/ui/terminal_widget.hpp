// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <FL/Fl_Widget.H>

#include <app/emulator.hpp>
#include <app/terminal_style.hpp>
#include <ui/url_detector.hpp>

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
    ~TerminalWidget() override;

    void draw() override;
    void resize(int x, int y, int w, int h) override;
    int handle(int event) override;

    // Ring the bell according to this pane's BellMode (DIP 2.8a, 6.2).
    // Audible uses FLTK's beep; visual flashes the pane briefly.
    void ring_bell();
    [[nodiscard]] bool bell_flash_active() const noexcept
    {
        return bell_flash_active_;
    }

    // URLs detected in the most recent paint (DIP 6.3). Detection is
    // presentation-only; the terminal model is unchanged.
    [[nodiscard]] const std::vector<UrlSpan> &url_spans() const noexcept
    {
        return url_spans_;
    }

    // Apply a new style (font/DPI). Forces a full repaint (DIP 2.5). Palette
    // changes for existing content are out of scope (DIP 2.3); the emulator
    // palette is fixed at construction, so this only affects font metrics.
    void set_style(TerminalStyle style);

    // Copy the current selection to the clipboard / selection buffer (DIP 2.8).
    void copy_selection();
    // Request the clipboard; FLTK delivers it as an FL_PASTE event.
    void paste_clipboard();

    // The raw pasted text, before terminal encoding. Each broadcast destination
    // encodes it with its own VTE so bracketed-paste modes stay independent
    // (DIP 2.8, 6.1).
    using PasteTextCallback = void (*)(void *context, std::string_view text);
    void set_paste_text_callback(PasteTextCallback paste_text_callback, void *context);

    // Notified with the new grid size when the widget is resized.
    using ResizeCallback = void (*)(void *context, int columns, int rows);
    void set_resize_callback(ResizeCallback resize_callback, void *context);

    // Notified when the pane gains focus (click or FLTK focus), for pane focus
    // tracking (DIP 3.5).
    using FocusCallback = void (*)(void *context);
    void set_focus_callback(FocusCallback focus_callback, void *context);

    // Consulted for each key press before the terminal consumes it; return true
    // to consume the key as an application command (DIP 3.6).
    using KeyCallback = bool (*)(void *context, int key, int state);
    void set_key_callback(KeyCallback key_callback, void *context);

    // Translated key events for this press. When set, the view fans them out to
    // the broadcast destinations; otherwise the widget encodes locally (DIP 6.1).
    using KeyInputCallback = void (*)(void *context, std::span<const KeyEvent> events);
    void set_key_input_callback(KeyInputCallback key_input_callback, void *context);

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
    [[nodiscard]] int scrollbar_width() const;
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
    static void bell_timeout(void *context);
    void begin_url_scan();
    void scan_url_cell(int column, int row, const Cell &cell);
    void flush_url_row();
    void finish_url_scan();
    [[nodiscard]] bool url_at(int column, int row) const noexcept;

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
    PasteTextCallback paste_text_callback_ = nullptr;
    void *paste_text_context_ = nullptr;

    // Resize notification (DIP 2.11).
    ResizeCallback resize_callback_ = nullptr;
    void *resize_context_ = nullptr;

    // Focus notification (DIP 3.5).
    FocusCallback focus_callback_ = nullptr;
    void *focus_context_ = nullptr;

    // Application command shortcut hook (DIP 3.6).
    KeyCallback key_callback_ = nullptr;
    void *key_context_ = nullptr;

    // Translated key fan-out hook (DIP 6.1).
    KeyInputCallback key_input_callback_ = nullptr;
    void *key_input_context_ = nullptr;

    // Bell flash and audible cooldown (DIP 6.2).
    bool bell_flash_active_ = false;
    std::chrono::steady_clock::time_point last_audible_bell_;

    // URL detection state (DIP 6.3); cells are fed from the collect pass.
    std::vector<UrlSpan> url_spans_;
    bool url_scan_active_ = false;
    int url_row_ = -1;
    std::string url_text_;
    std::vector<int> url_columns_;
    std::vector<int> url_widths_;
};

} // namespace consola
