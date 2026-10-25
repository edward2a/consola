// SPDX-License-Identifier: MIT

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <span>
#include <string>
#include <thread>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include <app/emulator.hpp>
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

using consola::KeyEvent;
using consola::Orientation;
using consola::SessionOptions;
using consola::TerminalId;
using consola::TerminalManager;
using consola::TerminalView;

struct ScreenText {
    std::string text;
};

void collect_cell(void *context, int column, int row, const consola::Cell &cell)
{
    static_cast<void>(column);
    static_cast<void>(row);
    auto *screen = static_cast<ScreenText *>(context);
    if (cell.width == 0) {
        return;
    }
    for (const std::uint32_t codepoint : cell.text) {
        if (codepoint >= 0x20 && codepoint < 0x7F) {
            screen->text.push_back(static_cast<char>(codepoint));
        }
    }
}

int count_char(consola::TerminalEmulator &emulator, char character)
{
    ScreenText screen;
    emulator.draw(&collect_cell, &screen);
    return static_cast<int>(std::count(screen.text.begin(), screen.text.end(), character));
}

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
    Fl_Window window(600, 400, "broadcast_view_test");

    TerminalManager manager(nullptr, nullptr, std::chrono::milliseconds{200});
    manager.start();

    TerminalView view(0, 0, 600, 400, manager);
    window.resizable(view);
    window.end();
    window.show();

    SessionOptions options;
    options.executable = "/bin/sh";
    options.arguments = {"-c", "cat"};
    const auto root = view.start(options);
    CHECK(root.has_value());
    if (!root.has_value()) {
        return EXIT_FAILURE;
    }
    CHECK(wait_for([&] { return manager.find(*root)->state() == consola::TerminalState::running; },
                   5000, view));

    const auto second = view.split_focused(Orientation::vertical);
    CHECK(second.has_value());
    if (!second.has_value()) {
        return EXIT_FAILURE;
    }
    CHECK(wait_for(
        [&] {
            return manager.find(*root)->state() == consola::TerminalState::running &&
                   manager.find(*second)->state() == consola::TerminalState::running;
        },
        5000, view));

    // Broadcast: one 'x' reaches both panes, exactly once each (the focused
    // pane is not delivered twice).
    view.set_broadcast(true);
    KeyEvent x;
    x.keysym = 'x';
    x.unicode = 'x';
    view.dispatch_key_input(*root, std::span<const KeyEvent>(&x, 1));
    CHECK(wait_for(
        [&] {
            return count_char(manager.emulator(*root), 'x') >= 1 &&
                   count_char(manager.emulator(*second), 'x') >= 1;
        },
        3000, view));
    CHECK(count_char(manager.emulator(*root), 'x') == 1);
    CHECK(count_char(manager.emulator(*second), 'x') == 1);

    // Broadcast off: 'y' reaches only the source pane.
    view.set_broadcast(false);
    KeyEvent y;
    y.keysym = 'y';
    y.unicode = 'y';
    view.dispatch_key_input(*root, std::span<const KeyEvent>(&y, 1));
    CHECK(wait_for([&] { return count_char(manager.emulator(*root), 'y') >= 1; }, 3000, view));
    CHECK(count_char(manager.emulator(*second), 'y') == 0);

    view.close_tab(manager.tabs().active_id());
    manager.shutdown();
    window.hide();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("broadcast_view_test: ok\n");
    return EXIT_SUCCESS;
}
