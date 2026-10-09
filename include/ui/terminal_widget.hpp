// SPDX-License-Identifier: MIT

#pragma once

#include <FL/Fl_Widget.H>

namespace consola {

class TerminalEmulator;

// Presentation-only FLTK widget for one terminal pane (DIP 2.1). It owns
// presentation state and must not own the PTY or the terminal model.
class TerminalWidget : public Fl_Widget {
public:
    TerminalWidget(int x, int y, int w, int h, TerminalEmulator &emulator);

    void draw() override;
    int handle(int event) override;

    [[nodiscard]] TerminalEmulator &emulator() noexcept
    {
        return emulator_;
    }

private:
    TerminalEmulator &emulator_;
};

} // namespace consola
