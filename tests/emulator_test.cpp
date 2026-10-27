// SPDX-License-Identifier: MIT

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <app/emulator.hpp>

namespace {

std::span<const std::byte> as_bytes(const char *text)
{
    return {reinterpret_cast<const std::byte *>(text), std::strlen(text)};
}

std::string g_output;

int g_bells = 0;

void count_bell(void *context)
{
    static_cast<void>(context);
    ++g_bells;
}

void collect_output(void *context, std::span<const std::byte> bytes)
{
    static_cast<void>(context);
    g_output.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

struct DrawnCell {
    std::vector<std::uint32_t> text;
    std::uint8_t width = 0;
};

struct DrawGrid {
    int columns = 0;
    std::vector<DrawnCell> cells;
};

void collect_cell(void *context, int column, int row, const consola::Cell &cell)
{
    auto *grid = static_cast<DrawGrid *>(context);
    if (column < 0 || row < 0) {
        return;
    }
    const auto index = (static_cast<std::size_t>(row) * static_cast<std::size_t>(grid->columns)) +
                       static_cast<std::size_t>(column);
    if (index >= grid->cells.size()) {
        return;
    }
    grid->cells[index].text.assign(cell.text.begin(), cell.text.end());
    grid->cells[index].width = cell.width;
}

} // namespace

int main()
{
    consola::TerminalEmulator emulator;

    if (emulator.columns() != 80 || emulator.rows() != 24) {
        return EXIT_FAILURE;
    }

    emulator.resize(100, 30);
    if (emulator.columns() != 100 || emulator.rows() != 30) {
        return EXIT_FAILURE;
    }

    emulator.resize(0, 10);
    emulator.resize(-1, 10);
    if (emulator.columns() != 100 || emulator.rows() != 30) {
        return EXIT_FAILURE;
    }

    emulator.feed(as_bytes("hello"));
    if (emulator.cursor_x_raw() != 5 || emulator.cursor_y_raw() != 0) {
        return EXIT_FAILURE;
    }

    emulator.feed(as_bytes("\r\nworld"));
    if (emulator.cursor_x_raw() != 5 || emulator.cursor_y_raw() != 1) {
        return EXIT_FAILURE;
    }

    emulator.set_output_callback(&collect_output, nullptr);
    emulator.feed(as_bytes("\x1b[c"));
    if (g_output.empty()) {
        return EXIT_FAILURE;
    }

    const consola::CursorState cursor = emulator.cursor();
    if (cursor.x != 5 || cursor.y != 1 || cursor.hidden) {
        return EXIT_FAILURE;
    }

    DrawGrid grid;
    grid.columns = emulator.columns();
    grid.cells.resize(static_cast<std::size_t>(grid.columns) *
                      static_cast<std::size_t>(emulator.rows()));
    const std::uint32_t age = emulator.draw(&collect_cell, &grid);
    static_cast<void>(age);

    const auto cell_text = [&](int column, int row) {
        const auto index =
            (static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns)) +
            static_cast<std::size_t>(column);
        std::string text;
        for (const std::uint32_t codepoint : grid.cells[index].text) {
            text.push_back(static_cast<char>(codepoint));
        }
        return text;
    };
    const auto cell_width = [&](int column, int row) {
        const auto index =
            (static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns)) +
            static_cast<std::size_t>(column);
        return grid.cells[index].width;
    };
    if (cell_text(0, 0) != "h" || cell_text(4, 0) != "o" || cell_text(0, 1) != "w") {
        return EXIT_FAILURE;
    }
    if (cell_width(0, 0) != 1 || cell_width(4, 0) != 1 || cell_width(0, 1) != 1) {
        return EXIT_FAILURE;
    }

    // Keyboard encoding goes through libtsm (DIP 2.6).
    g_output.clear();
    consola::KeyEvent ctrl_c;
    ctrl_c.keysym = 'c';
    ctrl_c.ascii = 'c';
    ctrl_c.modifiers = consola::KeyModifierCtrl;
    if (!emulator.handle_keyboard(ctrl_c) || g_output != std::string(1, '\x03')) {
        return EXIT_FAILURE;
    }

    g_output.clear();
    consola::KeyEvent text;
    text.keysym = 'a';
    text.unicode = 'a';
    if (!emulator.handle_keyboard(text) || g_output != "a") {
        return EXIT_FAILURE;
    }

    g_output.clear();
    consola::KeyEvent left;
    left.keysym = 0xff51; // XKB_KEY_Left
    if (!emulator.handle_keyboard(left) || g_output != "\x1b[D") {
        return EXIT_FAILURE;
    }

    g_output.clear();
    consola::KeyEvent backspace;
    backspace.keysym = 0xff08; // XKB_KEY_BackSpace
    if (!emulator.handle_keyboard(backspace) || g_output != std::string(1, '\x7f')) {
        return EXIT_FAILURE;
    }

    // U+0000 is distinct from "no composed text".
    g_output.clear();
    consola::KeyEvent nul;
    nul.unicode = 0;
    if (!emulator.handle_keyboard(nul) || g_output != std::string(1, '\0')) {
        return EXIT_FAILURE;
    }

    // Mouse reporting (VT200): enable, then a left press at (1,2).
    g_output.clear();
    emulator.feed(as_bytes("\x1b[?1000h"));
    if (emulator.mouse_tracking() == consola::MouseTracking::disabled) {
        return EXIT_FAILURE;
    }
    consola::MouseEvent press;
    press.cell_x = 1;
    press.cell_y = 2;
    press.button = consola::MouseButton::left;
    press.action = consola::MouseActionPressed;
    if (!emulator.handle_mouse(press)) {
        return EXIT_FAILURE;
    }
    if (g_output.size() != 6 || !g_output.starts_with("\x1b[M") || g_output[3] != ' ' ||
        g_output[4] != '"' || g_output[5] != '#') {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b[?1000l"));

    // A held-button drag move in SGR mode encodes button + 32.
    g_output.clear();
    emulator.feed(as_bytes("\x1b[?1002h\x1b[?1006h"));
    consola::MouseEvent drag;
    drag.cell_x = 1;
    drag.cell_y = 2;
    drag.button = consola::MouseButton::left;
    drag.action = consola::MouseActionMoved;
    drag.dragging = true;
    if (!emulator.handle_mouse(drag) || g_output != "\x1b[<32;2;3M") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b[?1006l\x1b[?1002l"));

    // Selection and double-click word selection.
    emulator.selection_reset();
    if (emulator.has_selection()) {
        return EXIT_FAILURE;
    }
    emulator.selection_start(0, 0);
    emulator.selection_target(4, 0);
    if (!emulator.has_selection() || emulator.selection_copy() != "hello") {
        return EXIT_FAILURE;
    }
    emulator.select_word(1, 1, "-A-Za-z0-9,./?%&#:_=+@~");
    if (!emulator.has_selection() || emulator.selection_copy() != "world") {
        return EXIT_FAILURE;
    }
    emulator.selection_reset();
    if (emulator.has_selection()) {
        return EXIT_FAILURE;
    }

    // Paste capture: bracketed mode wraps the text; unbracketed does not.
    emulator.feed(as_bytes("\x1b[?2004h"));
    const consola::PasteCapture bracketed = emulator.capture_paste("hi", 1024);
    if (!bracketed.ok || bracketed.bytes != "\x1b[200~hi\x1b[201~") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b[?2004l"));
    const consola::PasteCapture plain = emulator.capture_paste("hi", 1024);
    if (!plain.ok || plain.bytes != "hi") {
        return EXIT_FAILURE;
    }
    if (emulator.capture_paste("hi", 1).ok) {
        return EXIT_FAILURE;
    }
    if (emulator.capture_paste("", 1024).ok) {
        return EXIT_FAILURE;
    }
    // A bracketed-paste closing marker in the payload is neutralised.
    emulator.feed(as_bytes("\x1b[?2004h"));
    const consola::PasteCapture injected = emulator.capture_paste("a\x1b[201~b", 1024);
    if (!injected.ok || injected.bytes != "\x1b[200~ab\x1b[201~") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b[?2004l"));

    // Scrollback and title (DIP 2.9, 2.12).
    emulator.set_max_scrollback(100);
    for (int i = 0; i < 40; ++i) {
        emulator.feed(as_bytes("line\r\n"));
    }
    const int scroll_count = emulator.scrollback_line_count();
    if (scroll_count <= 0) {
        return EXIT_FAILURE;
    }
    // libtsm: live/bottom is pos == count, top is pos == 0.
    emulator.scroll_to_top();
    if (emulator.scrollback_line_pos() != 0) {
        return EXIT_FAILURE;
    }
    emulator.scroll_to_bottom();
    if (emulator.scrollback_line_pos() != scroll_count) {
        return EXIT_FAILURE;
    }

    emulator.feed(as_bytes("\x1b]0;hello\x07"));
    if (emulator.title() != "hello") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b]2;world\x1b\\"));
    if (emulator.title() != "world") {
        return EXIT_FAILURE;
    }

    // OSC 7 working-directory reporting (DIP 6.1): file:// URIs with
    // percent-escapes are decoded; non-file and path-less URIs are ignored.
    if (!emulator.working_directory().empty()) {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b]7;file://host/tmp/some%20dir\x07"));
    if (emulator.working_directory() != "/tmp/some dir") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b]7;file:///srv/data\x1b\\"));
    if (emulator.working_directory() != "/srv/data") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b]7;http://host/tmp\x07"));
    if (emulator.working_directory() != "/srv/data") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b]7;file://host\x07"));
    if (emulator.working_directory() != "/srv/data") {
        return EXIT_FAILURE;
    }
    // Malformed escapes stay verbatim; query/fragment text is not a path and
    // trailing suffixes are stripped.
    emulator.feed(as_bytes("\x1b]7;file:///tmp/%zz\x07"));
    if (emulator.working_directory() != "/tmp/%zz") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b]7;file://host?query/path\x07"));
    if (emulator.working_directory() != "/tmp/%zz") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b]7;file://host/dir?x\x07"));
    if (emulator.working_directory() != "/dir") {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b]7;file://host/dir2#frag\x07"));
    if (emulator.working_directory() != "/dir2") {
        return EXIT_FAILURE;
    }

    // The VTE bell callback (DIP 2.8a, 6.2).
    emulator.set_bell_callback(&count_bell, nullptr);
    emulator.feed(as_bytes("\a"));
    if (g_bells != 1) {
        return EXIT_FAILURE;
    }

    emulator.feed(as_bytes("\x1b[?1049h"));
    if (!emulator.alternate_screen()) {
        return EXIT_FAILURE;
    }
    emulator.feed(as_bytes("\x1b[?1049l"));
    if (emulator.alternate_screen()) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
