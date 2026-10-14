// SPDX-License-Identifier: MIT

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
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

    // The split placed the new pane to the right; focus left returns to root.
    CHECK(view.focus_direction(FocusDirection::left));
    CHECK(manager.focused() == root);
    CHECK(view.focus_direction(FocusDirection::right));
    CHECK(manager.focused() == second);

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

    CHECK(view.close_focused());
    const bool collapsed =
        wait_for([&] { return manager.tab().terminal_count() == 1; }, 5000, view);
    CHECK(collapsed);
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
        manager.create_root_terminal(second_tab, consola::TerminalOptions{tab_options});
    CHECK(tab_root.has_value());
    view.pump();
    CHECK(view.widget(REQUIRED(root)) != nullptr);
    CHECK(view.widget(REQUIRED(tab_root)) != nullptr);
    CHECK(view.widget(REQUIRED(root))->visible() == 0);
    CHECK(view.widget(REQUIRED(tab_root))->visible() != 0);

    CHECK(manager.select_tab(first_tab));
    view.pump();
    CHECK(view.widget(REQUIRED(root))->visible() != 0);
    CHECK(view.widget(REQUIRED(tab_root))->visible() == 0);

    CHECK(manager.close_tab(second_tab));
    const bool tab_removed =
        wait_for([&] { return manager.find(REQUIRED(tab_root)) == nullptr; }, 8000, view);
    CHECK(tab_removed);
    CHECK(manager.tabs().count() == 1);

    manager.shutdown();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d terminal view check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
