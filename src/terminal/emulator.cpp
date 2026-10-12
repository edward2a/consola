// SPDX-License-Identifier: MIT

#include <app/emulator.hpp>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

#include <libtsm.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <app/log.hpp>

namespace consola {

struct TerminalEmulatorImpl {
    tsm_screen *screen = nullptr;
    tsm_vte *vte = nullptr;
    TerminalEmulator::OutputCallback output_callback = nullptr;
    void *output_context = nullptr;
};

namespace {

void log_callback([[maybe_unused]] void *data, [[maybe_unused]] const char *file,
                  [[maybe_unused]] int line, [[maybe_unused]] const char *func,
                  [[maybe_unused]] const char *subs, unsigned int severity, const char *format,
                  va_list args)
{
    std::array<char, 512> buffer{};
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    log(log_level_from_libtsm_severity(static_cast<int>(severity)), buffer.data());
}

void vte_write_callback([[maybe_unused]] tsm_vte *vte, const char *u8, std::size_t len, void *data)
{
    auto *impl = static_cast<TerminalEmulatorImpl *>(data);
    if (impl->output_callback != nullptr) {
        impl->output_callback(
            impl->output_context,
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(u8), len));
    }
}

CellAttributes to_attributes(const tsm_screen_attr &attr)
{
    CellAttributes out;
    out.foreground = Rgb{attr.fr, attr.fg, attr.fb};
    out.background = Rgb{attr.br, attr.bg, attr.bb};
    out.bold = attr.bold != 0;
    out.italic = attr.italic != 0;
    out.underline = attr.underline != 0;
    out.inverse = attr.inverse != 0;
    out.blink = attr.blink != 0;
    out.dim = attr.dim != 0;
    return out;
}

CursorStyle to_cursor_style(tsm_screen_cursor_style style)
{
    switch (style) {
    case TSM_SCREEN_CURSOR_BLOCK_BLINK:
        return CursorStyle::block_blink;
    case TSM_SCREEN_CURSOR_BLOCK_STEADY:
        return CursorStyle::block_steady;
    case TSM_SCREEN_CURSOR_UNDERLINE_BLINK:
        return CursorStyle::underline_blink;
    case TSM_SCREEN_CURSOR_UNDERLINE_STEADY:
        return CursorStyle::underline_steady;
    case TSM_SCREEN_CURSOR_VBAR_BLINK:
        return CursorStyle::vbar_blink;
    case TSM_SCREEN_CURSOR_VBAR_STEADY:
        return CursorStyle::vbar_steady;
    case TSM_SCREEN_CURSOR_DEFAULT:
    default:
        return CursorStyle::default_style;
    }
}

struct DrawBridge {
    TerminalEmulator::CellCallback callback;
    void *context;
};

void apply_palette(tsm_vte *vte, const TerminalStyle &style)
{
    std::array<std::array<uint8_t, 3>, TSM_COLOR_NUM> palette{};
    for (std::size_t i = 0; i < style.ansi.size(); ++i) {
        palette[i] = {style.ansi[i].r, style.ansi[i].g, style.ansi[i].b};
    }
    palette[TSM_COLOR_FOREGROUND] = {style.foreground.r, style.foreground.g, style.foreground.b};
    palette[TSM_COLOR_BACKGROUND] = {style.background.r, style.background.g, style.background.b};
    // libtsm requires uint8_t(*)[3]; std::array has the same contiguous layout.
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    if (tsm_vte_set_custom_palette(vte, reinterpret_cast<uint8_t (*)[3]>(palette.data())) < 0) {
        log(LogLevel::Warning, "failed to apply custom palette");
    }
}

int draw_trampoline([[maybe_unused]] tsm_screen *screen, [[maybe_unused]] uint64_t id,
                    const uint32_t *ch, size_t len, unsigned int width, unsigned int posx,
                    unsigned int posy, const tsm_screen_attr *attr, tsm_age_t age, void *data)
{
    auto *bridge = static_cast<DrawBridge *>(data);
    Cell cell;
    cell.text = std::span<const std::uint32_t>(ch, len);
    cell.attributes = to_attributes(*attr);
    cell.width = static_cast<std::uint8_t>(width);
    cell.age = static_cast<ContentAge>(age);
    bridge->callback(bridge->context, static_cast<int>(posx), static_cast<int>(posy), cell);
    return 0;
}

} // namespace

TerminalEmulator::TerminalEmulator(const TerminalStyle &style)
    : impl_(std::make_unique<TerminalEmulatorImpl>())
{
    if (tsm_screen_new(&impl_->screen, &log_callback, nullptr) < 0) {
        throw std::runtime_error("failed to create libtsm screen");
    }
    if (tsm_vte_new(&impl_->vte, impl_->screen, &vte_write_callback, impl_.get(), &log_callback,
                    nullptr) < 0) {
        tsm_screen_unref(impl_->screen);
        impl_->screen = nullptr;
        throw std::runtime_error("failed to create libtsm vte");
    }
    // Apply the palette before any output is fed (DIP 2.3).
    apply_palette(impl_->vte, style);
    // Backspace sends DEL (0x7f), like xterm/VTE (DIP 2.6).
    tsm_vte_set_backspace_sends_delete(impl_->vte, true);
}

TerminalEmulator::~TerminalEmulator()
{
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->vte != nullptr) {
        tsm_vte_unref(impl_->vte);
    }
    if (impl_->screen != nullptr) {
        tsm_screen_unref(impl_->screen);
    }
}

void TerminalEmulator::feed(std::span<const std::byte> data)
{
    if (data.empty()) {
        return;
    }
    tsm_vte_input(impl_->vte, reinterpret_cast<const char *>(data.data()), data.size());
}

void TerminalEmulator::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0) {
        return;
    }
    tsm_screen_resize(impl_->screen, static_cast<unsigned int>(cols),
                      static_cast<unsigned int>(rows));
}

bool TerminalEmulator::handle_keyboard(const KeyEvent &event)
{
    unsigned int mods = 0;
    if ((event.modifiers & KeyModifierShift) != 0) {
        mods |= TSM_SHIFT_MASK;
    }
    if ((event.modifiers & KeyModifierCtrl) != 0) {
        mods |= TSM_CONTROL_MASK;
    }
    if ((event.modifiers & KeyModifierAlt) != 0) {
        mods |= TSM_ALT_MASK;
    }
    if ((event.modifiers & KeyModifierMeta) != 0) {
        mods |= TSM_LOGO_MASK;
    }
    const std::uint32_t keysym = event.keysym == kKeysymNone ? XKB_KEY_NoSymbol : event.keysym;
    const std::uint32_t ascii = event.ascii == kKeysymNone ? XKB_KEY_NoSymbol : event.ascii;
    const std::uint32_t unicode = event.unicode == kUnicodeNone ? TSM_VTE_INVALID : event.unicode;
    return tsm_vte_handle_keyboard(impl_->vte, keysym, ascii, mods, unicode);
}

MouseTracking TerminalEmulator::mouse_tracking() const
{
    if (tsm_vte_get_mouse_mode(impl_->vte) == 0) {
        return MouseTracking::disabled;
    }
    if (tsm_vte_get_mouse_event(impl_->vte) == TSM_MOUSE_TRACK_ANY) {
        return MouseTracking::any;
    }
    return MouseTracking::click;
}

bool TerminalEmulator::handle_mouse(const MouseEvent &event)
{
    unsigned int button = TSM_MOUSE_BUTTON_LEFT;
    switch (event.button) {
    case MouseButton::left:
        button = TSM_MOUSE_BUTTON_LEFT;
        break;
    case MouseButton::middle:
        button = TSM_MOUSE_BUTTON_MIDDLE;
        break;
    case MouseButton::right:
        button = TSM_MOUSE_BUTTON_RIGHT;
        break;
    case MouseButton::wheel_up:
        button = TSM_MOUSE_BUTTON_WHEEL_UP;
        break;
    case MouseButton::wheel_down:
        button = TSM_MOUSE_BUTTON_WHEEL_DOWN;
        break;
    }

    unsigned int flags = 0;
    if ((event.modifiers & MouseModifierShift) != 0) {
        flags |= TSM_MOUSE_MODIFIER_SHIFT;
    }
    if ((event.modifiers & MouseModifierCtrl) != 0) {
        flags |= TSM_MOUSE_MODIFIER_CTRL;
    }
    if ((event.modifiers & (MouseModifierAlt | MouseModifierMeta)) != 0) {
        flags |= TSM_MOUSE_MODIFIER_META;
    }

    unsigned int action = 0;
    if ((event.action & MouseActionPressed) != 0) {
        action |= TSM_MOUSE_EVENT_PRESSED;
    }
    if ((event.action & MouseActionReleased) != 0) {
        action |= TSM_MOUSE_EVENT_RELEASED;
    }
    if ((event.action & MouseActionMoved) != 0) {
        action |= TSM_MOUSE_EVENT_MOVED;
    }

    return tsm_vte_handle_mouse(impl_->vte, static_cast<unsigned int>(event.cell_x),
                                static_cast<unsigned int>(event.cell_y),
                                static_cast<unsigned int>(event.pixel_x),
                                static_cast<unsigned int>(event.pixel_y), button, action,
                                static_cast<unsigned char>(flags));
}

void TerminalEmulator::selection_start(int column, int row)
{
    tsm_screen_selection_start(impl_->screen, static_cast<unsigned int>(column),
                               static_cast<unsigned int>(row));
}

void TerminalEmulator::selection_target(int column, int row)
{
    tsm_screen_selection_target(impl_->screen, static_cast<unsigned int>(column),
                                static_cast<unsigned int>(row));
}

void TerminalEmulator::selection_reset()
{
    tsm_screen_selection_reset(impl_->screen);
}

std::string TerminalEmulator::selection_copy() const
{
    char *out = nullptr;
    const int length = tsm_screen_selection_copy(impl_->screen, &out);
    if (length < 0 || out == nullptr) {
        if (out != nullptr) {
            std::free(out);
        }
        return {};
    }
    std::string text(out, static_cast<std::size_t>(length));
    std::free(out);
    return text;
}

void TerminalEmulator::select_word(int column, int row, std::string_view word_chars)
{
    const int columns = this->columns();
    if (columns <= 0) {
        selection_reset();
        return;
    }
    // draw2() is used only to read cell codepoints for word boundaries; it has
    // no age side effects and is not used for rendering (DIP 2.2, 2.7).
    const struct tsm_screen_cell *cells = tsm_screen_draw2(impl_->screen);
    if (cells == nullptr) {
        selection_reset();
        return;
    }
    const auto cell_at = [&](int index) -> std::uint32_t {
        if (index < 0 || index >= columns) {
            return 0;
        }
        return cells[(static_cast<std::size_t>(row) * static_cast<std::size_t>(columns)) +
                     static_cast<std::size_t>(index)]
            .ch;
    };
    const auto in_word_chars = [&](char character) {
        // word_chars is a Terminator-style class: literal characters plus
        // `a-z` ranges.
        for (std::size_t i = 0; i < word_chars.size(); ++i) {
            if (i + 2 < word_chars.size() && word_chars[i + 1] == '-') {
                if (character >= word_chars[i] && character <= word_chars[i + 2]) {
                    return true;
                }
                i += 2;
            } else if (word_chars[i] == character) {
                return true;
            }
        }
        return false;
    };
    const auto is_word = [&](int index) {
        const std::uint32_t codepoint = cell_at(index);
        if (codepoint == 0 || codepoint > 0x7f) {
            return false;
        }
        return in_word_chars(static_cast<char>(codepoint));
    };
    if (!is_word(column)) {
        selection_reset();
        return;
    }
    int start = column;
    while (is_word(start - 1)) {
        --start;
    }
    int end = column;
    while (is_word(end + 1)) {
        ++end;
    }
    selection_start(start, row);
    selection_target(end, row);
}

void TerminalEmulator::set_output_callback(OutputCallback callback, void *context)
{
    impl_->output_callback = callback;
    impl_->output_context = context;
}

ContentAge TerminalEmulator::draw(CellCallback callback, void *context)
{
    if (callback == nullptr) {
        return 0;
    }
    DrawBridge bridge{.callback = callback, .context = context};
    return static_cast<ContentAge>(tsm_screen_draw(impl_->screen, &draw_trampoline, &bridge));
}

int TerminalEmulator::columns() const
{
    return static_cast<int>(tsm_screen_get_width(impl_->screen));
}

int TerminalEmulator::rows() const
{
    return static_cast<int>(tsm_screen_get_height(impl_->screen));
}

CursorState TerminalEmulator::cursor() const
{
    CursorState state;
    state.x = static_cast<int>(tsm_screen_get_cursor_x(impl_->screen));
    state.y = static_cast<int>(tsm_screen_get_cursor_y(impl_->screen));
    state.style = to_cursor_style(tsm_screen_get_cursor_style(impl_->screen));
    state.blink_rate_ms = tsm_screen_get_cursor_blinking_rate_ms(impl_->screen);
    state.hidden = (tsm_screen_get_flags(impl_->screen) & TSM_SCREEN_HIDE_CURSOR) != 0;
    return state;
}

int TerminalEmulator::scrollback_line_count() const
{
    return static_cast<int>(tsm_screen_sb_get_line_count(impl_->screen));
}

int TerminalEmulator::scrollback_line_pos() const
{
    return static_cast<int>(tsm_screen_sb_get_line_pos(impl_->screen));
}

int TerminalEmulator::cursor_x_raw() const
{
    return static_cast<int>(tsm_screen_get_cursor_x(impl_->screen));
}

int TerminalEmulator::cursor_y_raw() const
{
    return static_cast<int>(tsm_screen_get_cursor_y(impl_->screen));
}

} // namespace consola
