// SPDX-License-Identifier: MIT

#include <ui/terminal_widget.hpp>

#include <FL/fl_draw.H>

#include <app/emulator.hpp>

namespace consola {

TerminalWidget::TerminalWidget(int x, int y, int w, int h, TerminalEmulator &emulator)
    : Fl_Widget(x, y, w, h, nullptr), emulator_(emulator)
{
    box(FL_NO_BOX);
}

void TerminalWidget::draw()
{
    // Cell rendering, cursor and scrollbar land in S2-CS2..S2-CS3.
    fl_color(FL_BLACK);
    fl_rectf(x(), y(), w(), h());
}

int TerminalWidget::handle(int event)
{
    static_cast<void>(event);
    return 0;
}

} // namespace consola
