// SPDX-License-Identifier: MIT

#include <ui/terminal_frame.hpp>

#include <algorithm>

#include <FL/Fl.H>

namespace consola {
namespace {

constexpr int kBarHeight = 24;

} // namespace

TerminalFrame::TerminalFrame(int x, int y, int w, int h, TerminalManager &manager)
    : Fl_Group(x, y, w, h)
{
    box(FL_NO_BOX);
    begin();
    tab_bar_ = new TerminalTabBar(x, y, w, kBarHeight, manager);
    view_ = new TerminalView(x, y + kBarHeight, w, std::max(0, h - kBarHeight), manager);
    end();
    resizable(view_);
}

void TerminalFrame::resize(int x, int y, int w, int h)
{
    Fl_Group::resize(x, y, w, h);
    tab_bar_->resize(x, y, w, kBarHeight);
    view_->resize(x, y + kBarHeight, w, std::max(0, h - kBarHeight));
}

} // namespace consola
