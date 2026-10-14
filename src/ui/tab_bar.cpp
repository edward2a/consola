// SPDX-License-Identifier: MIT

#include <ui/tab_bar.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <FL/Fl.H>
#include <FL/fl_draw.H>

namespace consola {
namespace {

constexpr int kMaxTabWidth = 160;
constexpr int kCloseWidth = 18;

} // namespace

TerminalTabBar::TerminalTabBar(int x, int y, int w, int h, TerminalManager &manager)
    : Fl_Group(x, y, w, h), manager_(manager)
{
    box(FL_NO_BOX);
    end();
}

void TerminalTabBar::set_select_callback(ActionCallback action, void *context)
{
    select_callback_ = action;
    select_context_ = context;
}

void TerminalTabBar::set_close_callback(ActionCallback action, void *context)
{
    close_callback_ = action;
    close_context_ = context;
}

void TerminalTabBar::sync()
{
    const std::size_t count = manager_.tabs().count();
    const TabId active = manager_.tabs().active_id();
    if (count != last_count_ || !last_active_.has_value() || *last_active_ != active) {
        last_count_ = count;
        last_active_ = active;
        redraw();
    }
}

int TerminalTabBar::tab_width() const
{
    const std::size_t count = manager_.tabs().count();
    if (count == 0) {
        return w();
    }
    return std::max(1, std::min(kMaxTabWidth, w() / static_cast<int>(count)));
}

std::optional<TabId> TerminalTabBar::tab_at(int local_x) const
{
    if (local_x < 0) {
        return std::nullopt;
    }
    const int width = tab_width();
    const auto index = static_cast<std::size_t>(local_x / width);
    const std::vector<TabId> ids = manager_.tabs().ids();
    if (index >= ids.size()) {
        return std::nullopt;
    }
    return ids[index];
}

bool TerminalTabBar::close_at(int local_x) const
{
    const int width = tab_width();
    if (width <= 0) {
        return false;
    }
    return (local_x % width) >= width - kCloseWidth;
}

void TerminalTabBar::activate(int local_x, int local_y)
{
    (void)local_y;
    const std::optional<TabId> tab = tab_at(local_x);
    if (!tab) {
        return;
    }
    if (close_at(local_x)) {
        if (close_callback_ != nullptr) {
            close_callback_(close_context_, *tab);
        }
    } else if (select_callback_ != nullptr) {
        select_callback_(select_context_, *tab);
    }
}

void TerminalTabBar::draw()
{
    fl_push_clip(x(), y(), w(), h());
    fl_color(FL_BACKGROUND_COLOR);
    fl_rectf(x(), y(), w(), h());

    const std::vector<TabId> ids = manager_.tabs().ids();
    const TabId active = manager_.tabs().active_id();
    const int width = tab_width();
    fl_font(FL_HELVETICA, 12);
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const int tx = x() + (static_cast<int>(i) * width);
        const bool is_active = ids[i] == active;
        fl_color(is_active ? FL_SELECTION_COLOR : FL_BACKGROUND2_COLOR);
        fl_rectf(tx + 1, y() + 1, width - 2, h() - 2);
        fl_color(FL_DARK3);
        fl_rect(tx, y(), width, h());

        const Tab *tab = manager_.tabs().find(ids[i]);
        const std::string label = (tab != nullptr && !tab->title().empty())
                                      ? tab->title()
                                      : "Tab " + std::to_string(i + 1);
        fl_color(is_active ? FL_WHITE : FL_FOREGROUND_COLOR);
        fl_draw(label.c_str(), tx + 8, y(), width - kCloseWidth - 12, h(),
                FL_ALIGN_LEFT | FL_ALIGN_CLIP);
        fl_draw("x", tx + width - kCloseWidth, y(), kCloseWidth - 4, h(), FL_ALIGN_CENTER);
    }
    fl_pop_clip();
}

int TerminalTabBar::handle(int event)
{
    switch (event) {
    case FL_PUSH:
        activate(Fl::event_x() - x(), Fl::event_y() - y());
        return 1;
    default:
        break;
    }
    return Fl_Group::handle(event);
}

} // namespace consola
