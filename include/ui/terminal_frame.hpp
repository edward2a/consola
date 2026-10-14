// SPDX-License-Identifier: MIT

#pragma once

#include <FL/Fl_Group.H>

#include <app/terminal_manager.hpp>
#include <ui/tab_bar.hpp>
#include <ui/terminal_view.hpp>

namespace consola {

// Window content (DIP 4.2): a tab bar above the active tab's terminal view. The
// frame owns both and lays them out on resize; the window's resizable is the
// frame. UI-thread only.
class TerminalFrame : public Fl_Group {
public:
    TerminalFrame(int x, int y, int w, int h, TerminalManager &manager);
    ~TerminalFrame() override = default;

    TerminalFrame(const TerminalFrame &) = delete;
    TerminalFrame &operator=(const TerminalFrame &) = delete;
    TerminalFrame(TerminalFrame &&) = delete;
    TerminalFrame &operator=(TerminalFrame &&) = delete;

    [[nodiscard]] TerminalView &view() noexcept
    {
        return *view_;
    }
    [[nodiscard]] TerminalTabBar &tab_bar() noexcept
    {
        return *tab_bar_;
    }

    void resize(int x, int y, int w, int h) override;

private:
    TerminalTabBar *tab_bar_ = nullptr;
    TerminalView *view_ = nullptr;
};

} // namespace consola
