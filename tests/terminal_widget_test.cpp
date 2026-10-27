// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include <app/emulator.hpp>
#include <ui/fonts.hpp>
#include <ui/terminal_widget.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::TerminalEmulator;
using consola::TerminalWidget;

void set_event(TerminalWidget &widget, int column, int row, int clicks)
{
    const consola::FontMetrics &metrics = widget.metrics();
    Fl::e_x =
        widget.x() + static_cast<int>((static_cast<double>(column) + 0.5) * metrics.cell_width);
    Fl::e_y = widget.y() + (row * metrics.cell_height) + (metrics.cell_height / 2);
    Fl::e_keysym = FL_Button + FL_LEFT_MOUSE;
    Fl::e_state = 0;
    Fl::event_clicks(clicks);
}

void press(TerminalWidget &widget, int column, int row)
{
    set_event(widget, column, row, 0);
    widget.handle(FL_PUSH);
}

void drag_to(TerminalWidget &widget, int column, int row)
{
    set_event(widget, column, row, 0);
    widget.handle(FL_DRAG);
}

void release(TerminalWidget &widget, int column, int row)
{
    set_event(widget, column, row, 0);
    widget.handle(FL_RELEASE);
}

void click(TerminalWidget &widget, int column, int row)
{
    press(widget, column, row);
    release(widget, column, row);
}

void drag(TerminalWidget &widget, int from_column, int from_row, int to_column, int to_row)
{
    press(widget, from_column, from_row);
    drag_to(widget, to_column, to_row);
    release(widget, to_column, to_row);
}

} // namespace

int main()
{
    consola::register_application_fonts();

    Fl_Window window(400, 200, "terminal_widget_test");
    TerminalEmulator emulator;
    emulator.resize(40, 10);
    const std::string text = "hello world\r\nsecond line";
    emulator.feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte *>(text.data()), text.size()));

    TerminalWidget widget(0, 0, 400, 200, emulator);
    window.resizable(widget);
    window.end();
    window.show();
    Fl::wait(0.2);

    // A click with no active selection selects the pressed cell.
    click(widget, 1, 0);
    CHECK(emulator.has_selection());
    CHECK(emulator.selection_copy() == "e");

    // A static click while a selection is active clears it instead of
    // selecting the new cell.
    click(widget, 3, 0);
    CHECK(!emulator.has_selection());

    // Click-and-drag selects from the press cell to the release cell.
    drag(widget, 0, 0, 4, 0);
    CHECK(emulator.selection_copy() == "hello");

    // Dragging elsewhere while a selection is active replaces it.
    drag(widget, 6, 0, 10, 0);
    CHECK(emulator.selection_copy() == "world");

    // A static click elsewhere clears the active selection.
    click(widget, 0, 1);
    CHECK(!emulator.has_selection());

    // With no selection, a click selects again.
    click(widget, 0, 1);
    CHECK(emulator.has_selection());
    CHECK(emulator.selection_copy() == "s");

    Fl::event_clicks(0);

    if (g_failures != 0) {
        std::fprintf(stderr, "%d widget check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
