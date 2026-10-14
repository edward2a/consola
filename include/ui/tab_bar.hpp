// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <FL/Fl_Group.H>

#include <app/terminal_manager.hpp>

namespace consola {

// FLTK tab strip (DIP 4.2): one box per tab, active tab highlighted, a close
// glyph on each. It is a pure view: selecting or closing a tab fires a callback
// so the owner (the application) can route it through TerminalView. UI-thread
// only.
class TerminalTabBar : public Fl_Group {
public:
    using ActionCallback = void (*)(void *context, TabId tab);

    TerminalTabBar(int x, int y, int w, int h, TerminalManager &manager);
    ~TerminalTabBar() override = default;

    TerminalTabBar(const TerminalTabBar &) = delete;
    TerminalTabBar &operator=(const TerminalTabBar &) = delete;
    TerminalTabBar(TerminalTabBar &&) = delete;
    TerminalTabBar &operator=(TerminalTabBar &&) = delete;

    void set_select_callback(ActionCallback action, void *context);
    void set_close_callback(ActionCallback action, void *context);

    // Request a redraw when the tab set or active selection changes. Cheap to
    // call every frame.
    void sync();

    // Hit testing in bar-local coordinates (DIP 4.2).
    [[nodiscard]] std::optional<TabId> tab_at(int local_x) const;
    [[nodiscard]] bool close_at(int local_x) const;
    // Fire the select/close callback for a click at bar-local (x, y).
    void activate(int local_x, int local_y);

    void draw() override;
    int handle(int event) override;

private:
    [[nodiscard]] int tab_width() const;

    TerminalManager &manager_;
    ActionCallback select_callback_ = nullptr;
    void *select_context_ = nullptr;
    ActionCallback close_callback_ = nullptr;
    void *close_context_ = nullptr;
    std::optional<TabId> last_active_;
    std::vector<TabId> last_ids_;
    std::vector<std::string> last_titles_;
};

} // namespace consola
