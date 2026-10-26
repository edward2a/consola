// SPDX-License-Identifier: MIT

#include <app/emulator.hpp>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <optional>
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
    TerminalEmulator::TitleCallback title_callback = nullptr;
    void *title_context = nullptr;
    std::string title;
    TerminalEmulator::WorkingDirectoryCallback working_directory_callback = nullptr;
    void *working_directory_context = nullptr;
    std::string working_directory;
    TerminalEmulator::BellCallback bell_notify = nullptr;
    void *bell_notify_context = nullptr;
    // Active paste capture (DIP 2.8, 1.7); when set, write-callback output is
    // captured instead of forwarded.
    std::string *capture = nullptr;
    std::size_t capture_limit = 0;
    bool capture_overflow = false;
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
    if (impl->capture != nullptr) {
        if (impl->capture->size() + len > impl->capture_limit) {
            impl->capture_overflow = true;
            return;
        }
        // Never let an allocation failure cross the libtsm callback boundary.
        try {
            impl->capture->append(u8, len);
        } catch (...) {
            impl->capture_overflow = true;
        }
        return;
    }
    if (impl->output_callback != nullptr) {
        impl->output_callback(
            impl->output_context,
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(u8), len));
    }
}

int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// OSC 7 payload shape: file://HOST/PATH. Only the path is retained; invalid
// escapes are kept verbatim so a malformed report never drops the update.
std::optional<std::string> parse_osc7_path(std::string_view uri)
{
    constexpr std::string_view kScheme = "file://";
    if (!uri.starts_with(kScheme)) {
        return std::nullopt;
    }
    uri.remove_prefix(kScheme.size());
    const std::size_t slash = uri.find('/');
    const std::size_t terminator = uri.find_first_of("?#");
    if (slash == std::string_view::npos ||
        (terminator != std::string_view::npos && terminator < slash)) {
        return std::nullopt;
    }
    std::string_view encoded = uri.substr(slash);
    if (const std::size_t end = encoded.find_first_of("?#"); end != std::string_view::npos) {
        encoded = encoded.substr(0, end);
    }
    std::string path;
    path.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            const int high = hex_value(encoded[i + 1]);
            const int low = hex_value(encoded[i + 2]);
            if (high >= 0 && low >= 0) {
                path.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        path.push_back(encoded[i]);
    }
    if (path.empty()) {
        return std::nullopt;
    }
    return path;
}

void osc_callback([[maybe_unused]] tsm_vte *vte, const char *u8, std::size_t len, void *data)
{
    auto *impl = static_cast<TerminalEmulatorImpl *>(data);
    const std::string_view payload(u8, len);
    const std::size_t separator = payload.find(';');
    if (separator == std::string_view::npos) {
        return;
    }
    const std::string_view command = payload.substr(0, separator);
    if (command == "0" || command == "2") {
        impl->title.assign(payload.substr(separator + 1));
        if (impl->title_callback != nullptr) {
            impl->title_callback(impl->title_context, impl->title);
        }
        return;
    }
    if (command == "7") {
        const std::optional<std::string> path = parse_osc7_path(payload.substr(separator + 1));
        if (!path) {
            return;
        }
        impl->working_directory = *path;
        if (impl->working_directory_callback != nullptr) {
            impl->working_directory_callback(impl->working_directory_context,
                                             impl->working_directory);
        }
    }
}

void bell_callback([[maybe_unused]] tsm_vte *vte, void *data)
{
    auto *impl = static_cast<TerminalEmulatorImpl *>(data);
    if (impl->bell_notify != nullptr) {
        impl->bell_notify(impl->bell_notify_context);
    }
}

// Remove bracketed-paste framing markers from pasted text so a payload cannot
// end bracketed mode early (watchlist #8).
std::string sanitize_paste(std::string_view text)
{
    constexpr std::string_view kStart = "\x1b[200~";
    constexpr std::string_view kEnd = "\x1b[201~";
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const std::string_view rest = text.substr(i);
        if (rest.starts_with(kEnd)) {
            i += kEnd.size();
            continue;
        }
        if (rest.starts_with(kStart)) {
            i += kStart.size();
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
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

tsm_screen_cursor_style from_cursor_config(CursorShape shape, bool blink)
{
    switch (shape) {
    case CursorShape::underline:
        return blink ? TSM_SCREEN_CURSOR_UNDERLINE_BLINK : TSM_SCREEN_CURSOR_UNDERLINE_STEADY;
    case CursorShape::bar:
        return blink ? TSM_SCREEN_CURSOR_VBAR_BLINK : TSM_SCREEN_CURSOR_VBAR_STEADY;
    case CursorShape::block:
    default:
        return blink ? TSM_SCREEN_CURSOR_BLOCK_BLINK : TSM_SCREEN_CURSOR_BLOCK_STEADY;
    }
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
    // Backspace sends DEL (0x7f) or BS (0x08) per configuration (DIP 2.6).
    tsm_vte_set_backspace_sends_delete(impl_->vte, style.backspace_sends_delete);
    // Scrollback and the default cursor style come from the configuration
    // (DIP 2.9, watchlist #18).
    set_max_scrollback(style.scrollback);
    tsm_screen_set_cursor_style(impl_->screen,
                                from_cursor_config(style.cursor_shape, style.cursor_blink));
    // Window title (OSC 0/2) and the VTE bell (DIP 2.12, 2.8a).
    tsm_vte_set_osc_cb(impl_->vte, &osc_callback, impl_.get());
    tsm_vte_set_bell_cb(impl_->vte, &bell_callback, impl_.get());
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
    // libtsm encodes a button drag as button + 32; hover moves keep the low
    // code so it emits the motion marker (DIP 2.7).
    if (event.dragging) {
        button += 32;
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
    char *raw = nullptr;
    const int length = tsm_screen_selection_copy(impl_->screen, &raw);
    const std::unique_ptr<char, void (*)(void *)> out(raw, &std::free);
    if (length < 0 || raw == nullptr) {
        return {};
    }
    return {raw, static_cast<std::size_t>(length)};
}

void TerminalEmulator::select_word(int column, int row, std::string_view word_chars)
{
    const int columns = this->columns();
    if (columns <= 0 || row < 0 || row >= this->rows()) {
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
        // `a-z` ranges. The grammar is ASCII-only; a descending range such as
        // `z-a` matches nothing.
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

PasteCapture TerminalEmulator::capture_paste(std::string_view text, std::size_t max_bytes)
{
    if (text.size() > max_bytes) {
        return {}; // reject oversized source before capture
    }
    // Build the source and capture buffer before touching capture state, so a
    // failed allocation cannot leave a dangling capture pointer.
    std::string source = sanitize_paste(text);
    std::string encoded;
    encoded.reserve(source.size() + 12);

    impl_->capture = &encoded;
    impl_->capture_limit = max_bytes;
    impl_->capture_overflow = false;

    tsm_vte_paste(impl_->vte, source.c_str());

    const bool overflow = impl_->capture_overflow;
    impl_->capture = nullptr;
    impl_->capture_limit = 0;
    impl_->capture_overflow = false;
    if (overflow || encoded.empty()) {
        return {};
    }
    PasteCapture result;
    result.ok = true;
    result.bytes = std::move(encoded);
    return result;
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

bool TerminalEmulator::alternate_screen() const
{
    return (tsm_screen_get_flags(impl_->screen) & TSM_SCREEN_ALTERNATE) != 0;
}

void TerminalEmulator::set_max_scrollback(int lines)
{
    const int clamped = std::max(0, lines);
    tsm_screen_set_max_sb(impl_->screen, static_cast<unsigned int>(clamped));
}

void TerminalEmulator::scroll_lines(int delta)
{
    if (delta > 0) {
        tsm_screen_sb_up(impl_->screen, static_cast<unsigned int>(delta));
    } else if (delta < 0) {
        tsm_screen_sb_down(impl_->screen, static_cast<unsigned int>(-delta));
    }
}

void TerminalEmulator::scroll_page(int pages)
{
    if (pages > 0) {
        tsm_screen_sb_page_up(impl_->screen, static_cast<unsigned int>(pages));
    } else if (pages < 0) {
        tsm_screen_sb_page_down(impl_->screen, static_cast<unsigned int>(-pages));
    }
}

void TerminalEmulator::scroll_to_top()
{
    tsm_screen_sb_up(impl_->screen, static_cast<unsigned int>(scrollback_line_count()));
}

void TerminalEmulator::scroll_to_bottom()
{
    tsm_screen_sb_reset(impl_->screen);
}

const std::string &TerminalEmulator::title() const
{
    return impl_->title;
}

void TerminalEmulator::set_title_callback(TitleCallback callback, void *context)
{
    impl_->title_callback = callback;
    impl_->title_context = context;
}

const std::string &TerminalEmulator::working_directory() const
{
    return impl_->working_directory;
}

void TerminalEmulator::set_working_directory_callback(WorkingDirectoryCallback callback,
                                                      void *context)
{
    impl_->working_directory_callback = callback;
    impl_->working_directory_context = context;
}

void TerminalEmulator::set_bell_callback(BellCallback callback, void *context)
{
    impl_->bell_notify = callback;
    impl_->bell_notify_context = context;
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
