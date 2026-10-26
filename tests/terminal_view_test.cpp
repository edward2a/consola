// SPDX-License-Identifier: MIT

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include <app/session.hpp>
#include <app/terminal_manager.hpp>
#include <layout/layout_geometry.hpp>
#include <ui/terminal_view.hpp>

namespace {

int g_failures = 0;

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (!(__VA_ARGS__)) {                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__);            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

using consola::FocusDirection;
using consola::Orientation;
using consola::SessionOptions;
using consola::TerminalId;
using consola::TerminalManager;
using consola::TerminalView;

template <typename T> const T &required(const std::optional<T> &value, int line)
{
    static const T fallback{};
    if (!value) {
        std::fprintf(stderr, "FAIL line %d: expected a value\n", line);
        ++g_failures;
        return fallback;
    }
    return *value;
}

#define REQUIRED(value) required((value), __LINE__)

bool wait_for(const std::function<bool()> &predicate, int timeout_ms, TerminalView &view)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        view.pump();
        Fl::check();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    view.pump();
    Fl::check();
    return predicate();
}

} // namespace

int main()
{
    Fl_Window window(500, 300, "terminal_view_test");

    TerminalManager manager(nullptr, nullptr, std::chrono::milliseconds{200});
    manager.start();

    TerminalView view(0, 0, 500, 300, manager);
    window.resizable(view);
    window.end();
    window.show();

    SessionOptions options;
    options.executable = "/bin/sh";
    options.arguments = {"-c", "sleep 5"};
    const auto root = view.start(options);
    CHECK(root.has_value());

    const bool root_running = wait_for(
        [&] { return manager.find(REQUIRED(root))->state() == consola::TerminalState::running; },
        5000, view);
    CHECK(root_running);
    CHECK(manager.focused() == root);

    // Forced undersizing keeps the PTY grid positive (DIP 3.3, 2.11).
    window.size(1, 1);
    Fl::check();
    view.pump();
    CHECK(manager.find(REQUIRED(root))->emulator().columns() >= 1);
    CHECK(manager.find(REQUIRED(root))->emulator().rows() >= 1);
    window.size(500, 300);
    Fl::check();
    view.pump();
    CHECK(manager.find(REQUIRED(root))->emulator().columns() > 1);

    const auto second = view.split_focused(Orientation::vertical);
    CHECK(second.has_value());
    CHECK(manager.tab().terminal_count() == 2);
    const bool both_running = wait_for(
        [&] {
            return manager.find(REQUIRED(root))->state() == consola::TerminalState::running &&
                   manager.find(REQUIRED(second))->state() == consola::TerminalState::running;
        },
        5000, view);
    CHECK(both_running);
    CHECK(manager.focused() == second);

    // Broadcast targets (DIP 6.1): the source alone when off; every
    // input-enabled pane in the active tab (source included once) when on.
    view.set_broadcast(false);
    const auto single = view.broadcast_targets(REQUIRED(root));
    CHECK(single.size() == 1);
    CHECK(single.front() == REQUIRED(root));
    view.set_broadcast(true);
    const auto targets = view.broadcast_targets(REQUIRED(root));
    CHECK(targets.size() == 2);
    CHECK(std::ranges::find(targets, REQUIRED(root)) != targets.end());
    CHECK(std::ranges::find(targets, REQUIRED(second)) != targets.end());
    CHECK(view.execute(
        consola::CommandRequest{consola::Command::ToggleBroadcast, FocusDirection::left}));
    CHECK(!view.broadcast());

    // The split placed the new pane to the right; focus left returns to root.
    CHECK(view.focus_direction(FocusDirection::left));
    CHECK(manager.focused() == root);
    CHECK(view.focus_direction(FocusDirection::right));
    CHECK(manager.focused() == second);

    // Pane zoom/maximize (DIP 6.1): only the focused pane is visible at the full
    // content rectangle; the tree is unchanged.
    const consola::SplitNode *split_before =
        manager.tab().layout().split(manager.tab().layout().root());
    CHECK(split_before != nullptr);
    const consola::Orientation orientation_before =
        split_before != nullptr ? split_before->orientation : Orientation::horizontal;
    const float ratio_before = split_before != nullptr ? split_before->ratio : 0.5F;
    const auto before_zoom = manager.tab().layout().terminals();
    view.toggle_zoom();
    CHECK(view.zoomed());
    CHECK(view.widget(REQUIRED(root))->visible() == 0);
    CHECK(view.widget(REQUIRED(second))->visible() != 0);
    CHECK(view.widget(REQUIRED(second))->x() == view.x());
    CHECK(view.widget(REQUIRED(second))->y() == view.y());
    CHECK(view.widget(REQUIRED(second))->w() == view.w());
    CHECK(view.widget(REQUIRED(second))->h() == view.h());
    CHECK(manager.tab().layout().terminals() == before_zoom);
    const consola::SplitNode *split_during =
        manager.tab().layout().split(manager.tab().layout().root());
    CHECK(split_during != nullptr);
    if (split_during != nullptr) {
        CHECK(split_during->orientation == orientation_before);
        CHECK(split_during->ratio == ratio_before);
    }
    // A focus change clears zoom and shows both panes again.
    view.focus(REQUIRED(root));
    CHECK(!view.zoomed());
    CHECK(manager.focused() == root);
    CHECK(view.widget(REQUIRED(root))->visible() != 0);
    CHECK(view.widget(REQUIRED(second))->visible() != 0);
    view.focus(REQUIRED(second));

    // Pane movement (DIP 6.1): no neighbor at an edge, then swap with the left.
    CHECK(!view.move_focused(FocusDirection::right));
    CHECK(view.move_focused(FocusDirection::left));
    const auto moved = manager.tab().layout().terminals();
    CHECK(moved.size() == 2);
    CHECK(moved[0] == REQUIRED(second));
    CHECK(moved[1] == REQUIRED(root));
    CHECK(manager.focused() == second);
    CHECK(view.move_focused(FocusDirection::right)); // swap back
    const auto restored = manager.tab().layout().terminals();
    CHECK(restored[0] == REQUIRED(root));
    CHECK(restored[1] == REQUIRED(second));
    view.focus(REQUIRED(root));
    CHECK(!view.move_focused(FocusDirection::left));
    view.focus(REQUIRED(second));

    // Command dispatch (DIP 3.6).
    CHECK(
        view.execute(consola::CommandRequest{consola::Command::ResizePane, FocusDirection::left}));
    CHECK(view.execute(consola::CommandRequest{consola::Command::FocusLeft, FocusDirection::left}));
    CHECK(manager.focused() == root);
    CHECK(
        view.execute(consola::CommandRequest{consola::Command::FocusRight, FocusDirection::right}));
    CHECK(manager.focused() == second);

    view.equalize();
    const consola::SplitNode *root_split =
        manager.tab().layout().split(manager.tab().layout().root());
    CHECK(root_split != nullptr);
    CHECK(root_split->ratio == 0.5F);

    // Zoom is cleared when the zoomed pane closes.
    view.toggle_zoom();
    CHECK(view.zoomed());
    CHECK(view.close_focused());
    const bool collapsed =
        wait_for([&] { return manager.tab().terminal_count() == 1; }, 5000, view);
    CHECK(collapsed);
    CHECK(!view.zoomed());
    CHECK(manager.tab().layout().contains(REQUIRED(root)));
    CHECK(!manager.tab().layout().contains(REQUIRED(second)));

    // A split that cannot fit the current content area is refused before
    // launching (DIP 3.3).
    window.size(10, 10);
    Fl::check();
    CHECK(!view.split_focused(Orientation::vertical).has_value());
    CHECK(manager.tab().terminal_count() == 1);

    // Switching the active tab relayouts the view (DIP 4.2): the new tab's
    // widget is shown while the previous tab's is hidden, and switching back
    // reverses it.
    window.size(500, 300);
    Fl::check();
    view.pump();
    const consola::TabId first_tab = manager.tabs().active_id();
    const consola::TabId second_tab = manager.create_tab();
    SessionOptions tab_options;
    tab_options.executable = "/bin/sh";
    tab_options.arguments = {"-c", "sleep 5"};
    const auto tab_root =
        manager.create_root_terminal(second_tab, consola::TerminalOptions{.session = tab_options});
    CHECK(tab_root.has_value());
    view.pump();
    CHECK(view.widget(REQUIRED(root)) != nullptr);
    CHECK(view.widget(REQUIRED(tab_root)) != nullptr);
    CHECK(view.widget(REQUIRED(root))->visible() == 0);
    CHECK(view.widget(REQUIRED(tab_root))->visible() != 0);

    // Tab movement (DIP 6.1): move the active (second) tab left and back.
    const auto tab_order = manager.tabs().ids();
    CHECK(tab_order.size() == 2);
    CHECK(tab_order[0] == first_tab);
    CHECK(tab_order[1] == second_tab);
    CHECK(view.move_tab(-1));
    const auto moved_tabs = manager.tabs().ids();
    CHECK(moved_tabs[0] == second_tab);
    CHECK(moved_tabs[1] == first_tab);
    CHECK(manager.tabs().active_id() == second_tab);
    CHECK(view.move_tab(1));
    CHECK(manager.tabs().ids()[0] == first_tab);
    CHECK(!view.move_tab(1)); // already last

    // Zoom is cleared when the active tab changes (the zoomed pane leaves the
    // active tab).
    view.toggle_zoom();
    CHECK(view.zoomed());
    CHECK(manager.select_tab(first_tab));
    view.pump();
    CHECK(!view.zoomed());
    CHECK(!view.move_tab(-1)); // already first
    CHECK(view.widget(REQUIRED(root))->visible() != 0);
    CHECK(view.widget(REQUIRED(tab_root))->visible() == 0);

    CHECK(manager.close_tab(second_tab));
    const bool tab_removed =
        wait_for([&] { return manager.find(REQUIRED(tab_root)) == nullptr; }, 8000, view);
    CHECK(tab_removed);
    CHECK(manager.tabs().count() == 1);

    // View-level tab commands (DIP 4.2).
    CHECK(view.execute(consola::CommandRequest{consola::Command::NewTab, FocusDirection::left}));
    CHECK(manager.tabs().count() == 2);
    const consola::TabId new_active = manager.tabs().active_id();
    CHECK(manager.tabs().find(new_active)->terminal_count() == 1);
    CHECK(manager.tabs().active_id() != first_tab);

    CHECK(
        view.execute(consola::CommandRequest{consola::Command::PreviousTab, FocusDirection::left}));
    CHECK(manager.tabs().active_id() == first_tab);
    CHECK(view.execute(consola::CommandRequest{consola::Command::NextTab, FocusDirection::left}));
    CHECK(manager.tabs().active_id() == new_active);

    CHECK(view.execute(consola::CommandRequest{consola::Command::CloseTab, FocusDirection::left}));
    const bool tab_closed = wait_for([&] { return manager.tabs().count() == 1; }, 8000, view);
    CHECK(tab_closed);
    CHECK(manager.terminal_count() == 1);

    // Switching tabs restores the window title from the newly active terminal:
    // a title update from an inactive tab must not leak, but selecting that tab
    // must refresh the label.
    const consola::TabId titled_tab = manager.tabs().active_id();
    const consola::TabId other_tab = manager.create_tab();
    const auto other_root =
        manager.create_root_terminal(other_tab, consola::TerminalOptions{.session = options});
    CHECK(other_root.has_value());
    view.pump();
    const std::string before(window.label());
    const char *osc = "\x1b]0;hello-title\x07";
    manager.find(REQUIRED(root))
        ->emulator()
        .feed(
            std::span<const std::byte>(reinterpret_cast<const std::byte *>(osc), std::strlen(osc)));
    view.pump();
    CHECK(std::string(window.label()) == before);
    CHECK(view.select_tab(titled_tab));
    CHECK(std::string(window.label()) == "hello-title");

    CHECK(manager.close_tab(other_tab));
    const bool other_closed =
        wait_for([&] { return manager.find(REQUIRED(other_root)) == nullptr; }, 8000, view);
    CHECK(other_closed);

    // A background pane's title must not retitle the window; focusing that pane
    // does. The split pane becomes focused, so root is background.
    const auto split_pane = view.split_focused(Orientation::vertical);
    CHECK(split_pane.has_value());
    const std::string focused_label(window.label());
    const char *root_osc = "\x1b]0;root-title\x07";
    manager.find(REQUIRED(root))
        ->emulator()
        .feed(std::span<const std::byte>(reinterpret_cast<const std::byte *>(root_osc),
                                         std::strlen(root_osc)));
    view.pump();
    CHECK(std::string(window.label()) == focused_label);
    view.focus(REQUIRED(root));
    CHECK(std::string(window.label()) == "root-title");

    // A pane whose process exits closes itself, and its tab closes once empty
    // while other tabs remain (DIP 4.4).
    const consola::TabId short_tab = manager.create_tab();
    SessionOptions short_options;
    short_options.executable = "/bin/sh";
    short_options.arguments = {"-c", "printf bye"};
    const auto short_root =
        manager.create_root_terminal(short_tab, consola::TerminalOptions{.session = short_options});
    CHECK(short_root.has_value());
    const bool auto_closed =
        wait_for([&] { return manager.find(REQUIRED(short_root)) == nullptr; }, 8000, view);
    CHECK(auto_closed);
    CHECK(manager.tabs().find(short_tab) == nullptr);
    CHECK(manager.tabs().count() == 1);

    manager.shutdown();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d terminal view check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
