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

    // Selection and double-click word selection.
    emulator.selection_reset();
    emulator.selection_start(0, 0);
    emulator.selection_target(4, 0);
    if (emulator.selection_copy() != "hello") {
        return EXIT_FAILURE;
    }
    emulator.select_word(1, 1, "-A-Za-z0-9,./?%&#:_=+@~");
    if (emulator.selection_copy() != "world") {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
